#ifdef __linux__

#include <kernel/thread/linux_scheduler.h>
#include <util/log.h>
#include <thread>
#include <sched.h>
#include <algorithm>
#include <cctype>

namespace sce_kernel_thread {

// Static member definitions
bool SimpleLinuxScheduler::enabled_ = false;
int SimpleLinuxScheduler::total_cores_ = 0;
std::vector<int> SimpleLinuxScheduler::performance_cores_;
std::vector<int> SimpleLinuxScheduler::background_cores_;

bool SimpleLinuxScheduler::initialize() {
    LOG_INFO("Initializing simple Linux scheduler");
    
    total_cores_ = std::thread::hardware_concurrency();
    LOG_INFO("Detected {} CPU cores", total_cores_);
    
    // Simple detection: first half = performance, second half = background
    detect_cores();
    
    enabled_ = true;
    LOG_INFO("Linux scheduler enabled - P-cores: {}, Background: {}", 
             performance_cores_.size(), background_cores_.size());
    
    return true;
}

void SimpleLinuxScheduler::shutdown() {
    enabled_ = false;
    LOG_INFO("Linux scheduler disabled");
}

bool SimpleLinuxScheduler::is_enabled() {
    return enabled_;
}

void SimpleLinuxScheduler::detect_cores() {
    performance_cores_.clear();
    background_cores_.clear();
    
    // Simple split: first half for performance, second half for background
    int mid = total_cores_ / 2;
    if (mid == 0) mid = 1; // Ensure at least 1 performance core
    
    for (int i = 0; i < mid; ++i) {
        performance_cores_.push_back(i);
    }
    
    for (int i = mid; i < total_cores_; ++i) {
        background_cores_.push_back(i);
    }
    
    LOG_DEBUG("Performance cores: 0-{}, Background cores: {}-{}", 
              mid-1, mid, total_cores_-1);
}

ThreadRole SimpleLinuxScheduler::classify_thread(const std::string& name) {
    if (name.empty()) return ThreadRole::Unknown;
    
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    // Simple classification
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
    if (!enabled_) return;
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    std::vector<int> target_cores;
    
    switch (role) {
        case ThreadRole::Render:
        case ThreadRole::Audio:
            target_cores = performance_cores_;
            break;
        case ThreadRole::Background:
        default:
            target_cores = background_cores_;
            break;
    }
    
    // Set CPU affinity
    for (int cpu_id : target_cores) {
        if (cpu_id >= 0 && cpu_id < CPU_SETSIZE) {
            CPU_SET(cpu_id, &cpuset);
        }
    }
    
    int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (result != 0) {
        LOG_WARN("Failed to set thread affinity: {}", strerror(result));
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
