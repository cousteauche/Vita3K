// vita3k/kernel/src/host_thread_scheduler_linux.cpp
#ifdef __linux__
#include <kernel/thread/host_thread_scheduler.h>
#include <util/log.h>
#include <thread>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

namespace sce_kernel_thread {

// Function-local statics to avoid initialization order issues
bool& HostThreadScheduler::get_enabled() {
    static bool enabled = false;
    return enabled;
}

TurboMode& HostThreadScheduler::get_turbo_mode_ref() {
    static TurboMode turbo_mode = TurboMode::Disabled;
    return turbo_mode;
}

int& HostThreadScheduler::get_total_cores() {
    static int total_cores = 0;
    return total_cores;
}

std::vector<int>& HostThreadScheduler::get_performance_cores() {
    static std::vector<int> performance_cores;
    return performance_cores;
}

std::vector<int>& HostThreadScheduler::get_efficiency_cores() {
    static std::vector<int> efficiency_cores;
    return efficiency_cores;
}

std::vector<int>& HostThreadScheduler::get_turbo_cores() {
    static std::vector<int> turbo_cores;
    return turbo_cores;
}

bool HostThreadScheduler::initialize() {
    try {
        LOG_INFO("Initializing Host Thread Scheduler (Linux)");
        
        get_total_cores() = std::thread::hardware_concurrency();
        LOG_INFO("Detected {} CPU cores", get_total_cores());
        
        detect_cores();
        apply_process_optimizations();
        
        get_enabled() = true;
        LOG_INFO("Host Thread Scheduler initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize Host Thread Scheduler: {}", e.what());
        return false;
    }
}

void HostThreadScheduler::detect_cores() {
    auto& perf_cores = get_performance_cores();
    auto& eff_cores = get_efficiency_cores();
    auto& turbo_cores = get_turbo_cores();
    
    perf_cores.clear();
    eff_cores.clear();
    turbo_cores.clear();
    
    int total = get_total_cores();
    
    // Intel 13700HX detection: 16 P-cores (0-15), 8 E-cores (16-23)
    if (total == 24) {
        // P-cores: 0-15, with turbo cores being 0-5
        for (int i = 0; i < 16; i++) {
            perf_cores.push_back(i);
            if (i < 6) {
                turbo_cores.push_back(i);
            }
        }
        // E-cores: 16-23
        for (int i = 16; i < 24; i++) {
            eff_cores.push_back(i);
        }
        LOG_INFO("Intel 13700HX detected - P-cores: 0-15 (turbo: 0-5), E-cores: 16-23");
    } else {
        // Fallback: treat all as performance cores
        for (int i = 0; i < total; i++) {
            perf_cores.push_back(i);
            if (i < total / 2) {
                turbo_cores.push_back(i);
            }
        }
        LOG_INFO("Generic CPU detected - {} cores, treating all as performance", total);
    }
}

void HostThreadScheduler::apply_affinity_hint_current_thread(ThreadRole role) {
    if (!get_enabled()) return;
    
    pthread_t current_thread = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    const auto& perf_cores = get_performance_cores();
    const auto& eff_cores = get_efficiency_cores();
    const auto& turbo_cores = get_turbo_cores();
    TurboMode turbo = get_turbo_mode_ref();
    
    switch (role) {
        case ThreadRole::MainRender:
            // MainRender gets turbo cores in aggressive mode, all P-cores otherwise
            if (turbo == TurboMode::Aggressive && !turbo_cores.empty()) {
                for (int core : turbo_cores) {
                    CPU_SET(core, &cpuset);
                }
            } else if (!perf_cores.empty()) {
                for (int core : perf_cores) {
                    CPU_SET(core, &cpuset);
                }
            }
            break;
            
        case ThreadRole::Audio:
            // Audio gets dedicated P-cores for low latency
            if (!perf_cores.empty()) {
                // Use first few P-cores for audio
                for (size_t i = 0; i < std::min(size_t(4), perf_cores.size()); i++) {
                    CPU_SET(perf_cores[i], &cpuset);
                }
            }
            break;
            
        case ThreadRole::Input:
            // Input gets some P-cores
            if (!perf_cores.empty()) {
                for (size_t i = 0; i < std::min(size_t(2), perf_cores.size()); i++) {
                    CPU_SET(perf_cores[i], &cpuset);
                }
            }
            break;
            
        case ThreadRole::Network:
        case ThreadRole::Background:
            // Background tasks go to E-cores when available
            if (!eff_cores.empty()) {
                for (int core : eff_cores) {
                    CPU_SET(core, &cpuset);
                }
            } else if (!perf_cores.empty()) {
                // Fallback to P-cores if no E-cores
                for (int core : perf_cores) {
                    CPU_SET(core, &cpuset);
                }
            }
            break;
            
        default:
            // Unknown role - allow all cores
            for (int i = 0; i < get_total_cores(); i++) {
                CPU_SET(i, &cpuset);
            }
            break;
    }
    
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        LOG_WARN("Failed to set CPU affinity for thread role {}", static_cast<int>(role));
    }
    
    // Apply real-time scheduling for critical threads
    apply_platform_priority(role, turbo);
}

void HostThreadScheduler::apply_platform_priority(ThreadRole role, TurboMode turbo) {
    pthread_t current_thread = pthread_self();
    
    // Apply real-time scheduling for critical threads
    if (role == ThreadRole::Audio || role == ThreadRole::MainRender) {
        struct sched_param param;
        param.sched_priority = (role == ThreadRole::Audio) ? 50 : 40;
        
        if (pthread_setschedparam(current_thread, SCHED_FIFO, &param) != 0) {
            LOG_WARN("Failed to set real-time priority for thread role {}", static_cast<int>(role));
        }
    }
}

void HostThreadScheduler::apply_process_optimizations() {
    TurboMode turbo = get_turbo_mode_ref();
    
    if (turbo != TurboMode::Disabled) {
        // Set process priority
        int nice_value = (turbo == TurboMode::Aggressive) ? -10 : -5;
        if (nice(nice_value) == -1 && errno != 0) {
            LOG_WARN("Failed to set process nice value to {}: {}", nice_value, strerror(errno));
        }
    }
}

void HostThreadScheduler::detect_hardware_capabilities() {
    // Additional hardware detection can be added here
    LOG_INFO("Hardware capabilities detected - {} total cores", get_total_cores());
}

void HostThreadScheduler::set_turbo_mode(TurboMode mode) {
    TurboMode old_mode = get_turbo_mode_ref();
    get_turbo_mode_ref() = mode;
    
    const char* mode_str = (mode == TurboMode::Aggressive) ? "AGGRESSIVE" :
                          (mode == TurboMode::Balanced) ? "BALANCED" : "DISABLED";
    
    LOG_INFO("Turbo mode changed: {} -> {}", 
             static_cast<int>(old_mode), mode_str);
    
    if (get_enabled()) {
        apply_process_optimizations();
    }
}

TurboMode HostThreadScheduler::get_turbo_mode() {
    return get_turbo_mode_ref();
}

void HostThreadScheduler::shutdown() {
    if (get_enabled()) {
        LOG_INFO("Shutting down Host Thread Scheduler");
        get_enabled() = false;
    }
}

bool HostThreadScheduler::is_enabled() {
    return get_enabled();
}

void HostThreadScheduler::enable(bool enabled) {
    get_enabled() = enabled;
    LOG_INFO("Host Thread Scheduler {}", enabled ? "enabled" : "disabled");
}

void HostThreadScheduler::log_thread_info(const std::string& name, ThreadRole role) {
    const char* role_str = "Unknown";
    switch (role) {
        case ThreadRole::MainRender: role_str = "MainRender"; break;
        case ThreadRole::Audio: role_str = "Audio"; break;
        case ThreadRole::Input: role_str = "Input"; break;
        case ThreadRole::Network: role_str = "Network"; break;
        case ThreadRole::Background: role_str = "Background"; break;
        default: break;
    }
    
    LOG_INFO("Thread '{}' classified as {} and scheduled", name, role_str);
}

} // namespace sce_kernel_thread

#endif // __linux__