#ifdef __linux__

#include <kernel/thread/linux_scheduler.h>
#include <util/log.h>
#include <thread>
#include <sched.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace sce_kernel_thread {

// Function-local statics to avoid initialization order issues
bool& SimpleLinuxScheduler::get_enabled() {
    static bool enabled = false;
    return enabled;
}

int& SimpleLinuxScheduler::get_total_cores() {
    static int total_cores = 0;
    return total_cores;
}

std::vector<int>& SimpleLinuxScheduler::get_performance_cores() {
    static std::vector<int> performance_cores;
    return performance_cores;
}

std::vector<int>& SimpleLinuxScheduler::get_background_cores() {
    static std::vector<int> background_cores;
    return background_cores;
}

bool SimpleLinuxScheduler::initialize() {
    try {
        LOG_INFO("Initializing Linux scheduler");
        
        get_total_cores() = std::thread::hardware_concurrency();
        LOG_INFO("Detected {} CPU cores", get_total_cores());
        
        detect_cores();
        
        // DISABLED BY DEFAULT FOR SAFETY
        get_enabled() = false;
        LOG_INFO("Linux scheduler initialized but DISABLED - P-cores: {}, Background: {}", 
                 get_performance_cores().size(), get_background_cores().size());
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in scheduler init: {}", e.what());
        return false;
    }
}

void SimpleLinuxScheduler::shutdown() {
    get_enabled() = false;
    LOG_INFO("Linux scheduler disabled");
}

bool SimpleLinuxScheduler::is_enabled() {
    return get_enabled();
}

void SimpleLinuxScheduler::enable(bool enabled) {
    get_enabled() = enabled;
    LOG_INFO("Linux scheduler {}", enabled ? "ENABLED" : "DISABLED");
}

void SimpleLinuxScheduler::detect_cores() {
    auto& perf_cores = get_performance_cores();
    auto& bg_cores = get_background_cores();
    
    perf_cores.clear();
    bg_cores.clear();
    
    int total = get_total_cores();
    int mid = total / 2;
    if (mid == 0) mid = 1; // Ensure at least 1 performance core
    
    for (int i = 0; i < mid; ++i) {
        perf_cores.push_back(i);
    }
    
    for (int i = mid; i < total; ++i) {
        bg_cores.push_back(i);
    }
    
    LOG_DEBUG("Performance cores: 0-{}, Background cores: {}-{}", 
              mid-1, mid, total-1);
}

ThreadRole SimpleLinuxScheduler::classify_thread(const std::string& name) {
    if (name.empty()) return ThreadRole::Unknown;
    
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    if (lower_name.find("render") != std::string::npos ||
        lower_name.find("gxm") != std::string::npos ||
        lower_name.find("graphics") != std::string::npos) {
        return ThreadRole::Render;
    }
    
    if (lower_name.find("audio") != std::string::npos ||
        lower_name.find("sound") != std::string::npos) {
        return ThreadRole::Audio;
    }
    
    return ThreadRole::Background;
}

void SimpleLinuxScheduler::apply_affinity_hint(pthread_t thread, ThreadRole role) {
    if (!get_enabled()) {
        return;
    }
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    const std::vector<int>* target_cores = nullptr;
    
    switch (role) {
        case ThreadRole::Render:
        case ThreadRole::Audio:
            target_cores = &get_performance_cores();
            break;
        case ThreadRole::Background:
        default:
            target_cores = &get_background_cores();
            break;
    }
    
    for (int cpu_id : *target_cores) {
        if (cpu_id >= 0 && cpu_id < CPU_SETSIZE) {
            CPU_SET(cpu_id, &cpuset);
        }
    }
    
    int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (result != 0) {
        LOG_WARN("Failed to set thread affinity: {}", std::strerror(result));
    }
}

void SimpleLinuxScheduler::log_thread_info(const std::string& name, ThreadRole role) {
    const char* role_str = "Unknown";
    switch (role) {
        case ThreadRole::Render: role_str = "Render"; break;
        case ThreadRole::Audio: role_str = "Audio"; break;
        case ThreadRole::Background: role_str = "Background"; break;
        default: break;
    }
    
    LOG_INFO("Thread '{}' classified as {} and assigned scheduler hints", name, role_str);
}

} // namespace sce_kernel_thread

#endif // __linux__