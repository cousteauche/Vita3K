// vita3k/kernel/src/linux_scheduler.cpp
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

TurboMode& SimpleLinuxScheduler::get_turbo_mode_ref() {
    static TurboMode turbo_mode = TurboMode::Disabled;
    return turbo_mode;
}

int& SimpleLinuxScheduler::get_total_cores() {
    static int total_cores = 0;
    return total_cores;
}

std::vector<int>& SimpleLinuxScheduler::get_performance_cores() {
    static std::vector<int> performance_cores;
    return performance_cores;
}

std::vector<int>& SimpleLinuxScheduler::get_efficiency_cores() {
    static std::vector<int> efficiency_cores;
    return efficiency_cores;
}

std::vector<int>& SimpleLinuxScheduler::get_turbo_cores() {
    static std::vector<int> turbo_cores;
    return turbo_cores;
}

bool SimpleLinuxScheduler::initialize() {
    try {
        LOG_INFO("Initializing Linux Turbo Scheduler");
        
        get_total_cores() = std::thread::hardware_concurrency();
        LOG_INFO("Detected {} CPU cores", get_total_cores());
        
        detect_cores();
        
        // Start disabled for safety
        get_enabled() = false;
        get_turbo_mode_ref() = TurboMode::Disabled;
        
        LOG_INFO("Linux scheduler initialized - P-cores: {}, E-cores: {}, Turbo-cores: {}", 
                 get_performance_cores().size(), 
                 get_efficiency_cores().size(),
                 get_turbo_cores().size());
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in scheduler init: {}", e.what());
        return false;
    }
}

void SimpleLinuxScheduler::detect_cores() {
    auto& perf_cores = get_performance_cores();
    auto& eff_cores = get_efficiency_cores();
    auto& turbo_cores = get_turbo_cores();
    
    perf_cores.clear();
    eff_cores.clear();
    turbo_cores.clear();
    
    int total = get_total_cores();
    
    // For Intel 13700HX (24 threads): 16 P-cores + 8 E-cores
    // Assume P-cores are 0-15, E-cores are 16-23
    int p_core_count = std::min(16, total * 2 / 3);  // 2/3 for P-cores
    
    // Performance cores (P-cores with hyperthreading)
    for (int i = 0; i < p_core_count; ++i) {
        perf_cores.push_back(i);
    }
    
    // Efficiency cores
    for (int i = p_core_count; i < total; ++i) {
        eff_cores.push_back(i);
    }
    
    // Turbo cores: Best 4-6 P-cores for critical threads
    int turbo_count = std::min(6, p_core_count / 2);
    for (int i = 0; i < turbo_count; ++i) {
        turbo_cores.push_back(i);
    }
    
    LOG_INFO("Core detection: P-cores 0-{}, E-cores {}-{}, Turbo 0-{}", 
             p_core_count-1, p_core_count, total-1, turbo_count-1);
}

ThreadRole SimpleLinuxScheduler::classify_thread(const std::string& name) {
    if (name.empty()) return ThreadRole::Unknown;
    
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    // Main render threads - highest priority
    if (lower_name.find("render") != std::string::npos ||
        lower_name.find("gxm") != std::string::npos ||
        lower_name.find("graphics") != std::string::npos ||
        lower_name.find("gpu") != std::string::npos ||
        lower_name.find("opengl") != std::string::npos ||
        lower_name.find("vulkan") != std::string::npos) {
        return ThreadRole::MainRender;
    }
    
    // Audio threads - real-time priority
    if (lower_name.find("audio") != std::string::npos ||
        lower_name.find("sound") != std::string::npos ||
        lower_name.find("music") != std::string::npos ||
        lower_name.find("atrac") != std::string::npos) {
        return ThreadRole::Audio;
    }
    
    // Input threads - low latency required
    if (lower_name.find("input") != std::string::npos ||
        lower_name.find("ctrl") != std::string::npos ||
        lower_name.find("pad") != std::string::npos ||
        lower_name.find("touch") != std::string::npos) {
        return ThreadRole::Input;
    }
    
    // Network/IO threads
    if (lower_name.find("net") != std::string::npos ||
        lower_name.find("io") != std::string::npos ||
        lower_name.find("file") != std::string::npos ||
        lower_name.find("fios") != std::string::npos) {
        return ThreadRole::Network;
    }
    
    return ThreadRole::Background;
}

void SimpleLinuxScheduler::apply_affinity_hint(pthread_t thread, ThreadRole role) {
    if (!get_enabled()) return;
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    const std::vector<int>* target_cores = nullptr;
    int priority = 0;
    int policy = SCHED_OTHER;
    
    TurboMode turbo = get_turbo_mode_ref();
    
    switch (role) {
        case ThreadRole::MainRender:
            if (turbo != TurboMode::Disabled) {
                target_cores = &get_turbo_cores();  // Best cores for rendering
                priority = (turbo == TurboMode::Aggressive) ? -10 : -5;
                policy = SCHED_FIFO;  // Real-time scheduling for render
            } else {
                target_cores = &get_performance_cores();
            }
            break;
            
        case ThreadRole::Audio:
            target_cores = &get_turbo_cores();  // Audio always gets priority cores
            if (turbo != TurboMode::Disabled) {
                priority = (turbo == TurboMode::Aggressive) ? -15 : -8;
                policy = SCHED_FIFO;  // Audio needs real-time scheduling
            }
            break;
            
        case ThreadRole::Input:
            target_cores = &get_performance_cores();
            if (turbo == TurboMode::Aggressive) {
                priority = -5;
                policy = SCHED_FIFO;
            }
            break;
            
        case ThreadRole::Network:
            target_cores = &get_performance_cores();
            break;
            
        case ThreadRole::Background:
        default:
            target_cores = &get_efficiency_cores();
            if (turbo == TurboMode::Aggressive) {
                priority = 5;  // Lower priority for background
            }
            break;
    }
    
    // Set CPU affinity
    for (int cpu_id : *target_cores) {
        if (cpu_id >= 0 && cpu_id < CPU_SETSIZE) {
            CPU_SET(cpu_id, &cpuset);
        }
    }
    
    int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (result != 0) {
        LOG_WARN("Failed to set thread affinity: {}", std::strerror(result));
        return;
    }
    
    // Apply turbo optimizations
    if (turbo != TurboMode::Disabled) {
        apply_turbo_optimizations(thread, role);
    }
}

void SimpleLinuxScheduler::apply_turbo_optimizations(pthread_t thread, ThreadRole role) {
    TurboMode turbo = get_turbo_mode_ref();
    
    // Set scheduling priority and policy for critical threads
    if (role == ThreadRole::MainRender || role == ThreadRole::Audio || 
        (role == ThreadRole::Input && turbo == TurboMode::Aggressive)) {
        
        struct sched_param param;
        param.sched_priority = 0;  // Start with 0 for SCHED_FIFO
        
        int policy = SCHED_FIFO;
        int result = pthread_setschedparam(thread, policy, &param);
        if (result != 0) {
            // Fallback to nice values if real-time scheduling fails
            int nice_val = (role == ThreadRole::Audio) ? -10 :
                          (role == ThreadRole::MainRender) ? -5 : 0;
            
            // Note: pthread_setprio doesn't exist, we'd need to use setpriority
            // from the thread itself, so we'll skip this for now
            LOG_DEBUG("Real-time scheduling failed, using default priority");
        } else {
            LOG_DEBUG("Applied real-time scheduling to {} thread", 
                     (role == ThreadRole::Audio) ? "audio" : 
                     (role == ThreadRole::MainRender) ? "render" : "input");
        }
    }
}

void SimpleLinuxScheduler::set_turbo_mode(TurboMode mode) {
    get_turbo_mode_ref() = mode;
    const char* mode_str = (mode == TurboMode::Aggressive) ? "AGGRESSIVE" :
                          (mode == TurboMode::Balanced) ? "BALANCED" : "DISABLED";
    LOG_INFO("Turbo mode set to: {}", mode_str);
}

TurboMode SimpleLinuxScheduler::get_turbo_mode() {
    return get_turbo_mode_ref();
}

void SimpleLinuxScheduler::shutdown() {
    get_enabled() = false;
    get_turbo_mode_ref() = TurboMode::Disabled;
    LOG_INFO("Linux scheduler disabled");
}

bool SimpleLinuxScheduler::is_enabled() {
    return get_enabled();
}

void SimpleLinuxScheduler::enable(bool enabled) {
    get_enabled() = enabled;
    LOG_INFO("Linux scheduler {}", enabled ? "ENABLED" : "DISABLED");
}

void SimpleLinuxScheduler::log_thread_info(const std::string& name, ThreadRole role) {
    const char* role_str = "Unknown";
    switch (role) {
        case ThreadRole::MainRender: role_str = "MainRender"; break;
        case ThreadRole::Audio: role_str = "Audio"; break;
        case ThreadRole::Input: role_str = "Input"; break;
        case ThreadRole::Network: role_str = "Network"; break;
        case ThreadRole::Background: role_str = "Background"; break;
        default: break;
    }
    
    TurboMode turbo = get_turbo_mode_ref();
    const char* turbo_str = (turbo == TurboMode::Aggressive) ? " [TURBO-AGG]" :
                           (turbo == TurboMode::Balanced) ? " [TURBO-BAL]" : "";
    
    LOG_INFO("Thread '{}' classified as {}{} and assigned scheduler hints", 
             name, role_str, turbo_str);
}

} // namespace sce_kernel_thread

#endif // __linux__