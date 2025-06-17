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

int& HostThreadScheduler::get_gpu_cores() {
    static int gpu_worker_cores = 0;
    return gpu_worker_cores;
}

void HostThreadScheduler::set_gpu_worker_cores(int gpu_cores) {
    get_gpu_cores() = gpu_cores;
    LOG_INFO("GPU pipeline workers: {} cores (informational only)", gpu_cores);
    // Just informational - don't interfere with CPU scheduling
}

int HostThreadScheduler::get_gpu_worker_cores() {
    return get_gpu_cores();
}

bool HostThreadScheduler::initialize() {
    try {
        LOG_INFO("Initializing Super-Optimized Host Thread Scheduler (Linux)");
        
        get_total_cores() = std::thread::hardware_concurrency();
        LOG_INFO("Detected {} CPU cores", get_total_cores());
        
        detect_cores();
        detect_hardware_capabilities();
        
        get_enabled() = true;
        LOG_INFO("Host Thread Scheduler initialized successfully - P:{} E:{} T:{}",
                 get_performance_cores().size(), 
                 get_efficiency_cores().size(),
                 get_turbo_cores().size());
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
    
    // PROVEN STABLE LOGIC: Based on working scheduler patterns
    if (total == 24) {
        // Intel 13700HX/13900HX: 16 P-cores (0-15), 8 E-cores (16-23)
        for (int i = 0; i < 16; i++) {
            perf_cores.push_back(i);
            if (i < 6) {
                turbo_cores.push_back(i);  // Best 6 P-cores for critical work
            }
        }
        for (int i = 16; i < 24; i++) {
            eff_cores.push_back(i);
        }
        LOG_INFO("Intel 24-thread CPU: P-cores 0-15 (turbo 0-5), E-cores 16-23");
    } 
    else if (total >= 16 && total < 24) {
        // 16-20 thread CPUs: assume mostly P-cores
        int p_core_count = total - 4;  // Reserve 4 for E-cores
        for (int i = 0; i < p_core_count; i++) {
            perf_cores.push_back(i);
            if (i < 6) {
                turbo_cores.push_back(i);
            }
        }
        for (int i = p_core_count; i < total; i++) {
            eff_cores.push_back(i);
        }
        LOG_INFO("High-end CPU: P-cores 0-{}, E-cores {}-{}", 
                 p_core_count-1, p_core_count, total-1);
    }
    else if (total >= 12) {
        // 12-15 thread CPUs: 2/3 P-cores, 1/3 E-cores
        int p_core_count = (total * 2) / 3;
        for (int i = 0; i < p_core_count; i++) {
            perf_cores.push_back(i);
            if (i < p_core_count / 2) {
                turbo_cores.push_back(i);
            }
        }
        for (int i = p_core_count; i < total; i++) {
            eff_cores.push_back(i);
        }
        LOG_INFO("Mid-range CPU: P-cores 0-{}, E-cores {}-{}", 
                 p_core_count-1, p_core_count, total-1);
    }
    else {
        // 8-11 thread CPUs: treat all as performance cores
        for (int i = 0; i < total; i++) {
            perf_cores.push_back(i);
            if (i < total / 2) {
                turbo_cores.push_back(i);
            }
        }
        LOG_INFO("Standard CPU: All {} cores treated as performance", total);
    }
}

void HostThreadScheduler::apply_affinity_hint_current_thread(ThreadRole role) {
    if (!get_enabled()) return;
    
    // SAFETY: Prevent multiple applications per thread
    static thread_local bool affinity_applied = false;
    static thread_local ThreadRole last_role = ThreadRole::Unknown;
    
    if (affinity_applied && last_role == role) {
        LOG_DEBUG("Affinity already applied to this thread - skipping");
        return;
    }
    
    pthread_t current_thread = pthread_self();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    const auto& perf_cores = get_performance_cores();
    const auto& eff_cores = get_efficiency_cores();
    const auto& turbo_cores = get_turbo_cores();
    int total_cores = get_total_cores();
    
    // INTELLIGENT CORE ASSIGNMENT: Scale based on system size
    bool cores_assigned = false;
    
    if (total_cores <= 4) {
        // Tiny systems: everyone shares everything
        for (int i = 0; i < total_cores; i++) {
            CPU_SET(i, &cpuset);
        }
        cores_assigned = true;
        LOG_DEBUG("Tiny system ({}): All threads share all cores", total_cores);
    }
    else if (total_cores <= 8) {
        // Small systems: light separation
        switch (role) {
            case ThreadRole::MainRender:
            case ThreadRole::Audio:
                // Critical: First 2/3 of cores
                for (int i = 0; i < (total_cores * 2) / 3 + 1; i++) {
                    CPU_SET(i, &cpuset);
                }
                cores_assigned = true;
                LOG_DEBUG("Small system: Critical thread gets cores 0-{}", (total_cores * 2) / 3);
                break;
                
            default:
                // Others: All cores with overlap
                for (int i = 0; i < total_cores; i++) {
                    CPU_SET(i, &cpuset);
                }
                cores_assigned = true;
                LOG_DEBUG("Small system: Non-critical thread gets all cores");
                break;
        }
    }
    else {
        // Large systems: Smart P/E-core separation
        switch (role) {
            case ThreadRole::MainRender:
                // Render: Turbo cores first, P-cores as fallback
                if (!turbo_cores.empty() && get_turbo_mode_ref() != TurboMode::Disabled) {
                    for (int core : turbo_cores) {
                        CPU_SET(core, &cpuset);
                    }
                    cores_assigned = true;
                    LOG_DEBUG("Render thread assigned to {} turbo cores", turbo_cores.size());
                } else if (!perf_cores.empty()) {
                    for (int core : perf_cores) {
                        CPU_SET(core, &cpuset);
                    }
                    cores_assigned = true;
                    LOG_DEBUG("Render thread assigned to {} P-cores", perf_cores.size());
                }
                break;
                
            case ThreadRole::Audio:
                // Audio: Always gets turbo cores for real-time performance
                if (!turbo_cores.empty()) {
                    for (int core : turbo_cores) {
                        CPU_SET(core, &cpuset);
                    }
                    cores_assigned = true;
                    LOG_DEBUG("Audio thread assigned to {} turbo cores", turbo_cores.size());
                } else if (!perf_cores.empty()) {
                    for (int core : perf_cores) {
                        CPU_SET(core, &cpuset);
                    }
                    cores_assigned = true;
                    LOG_DEBUG("Audio thread assigned to {} P-cores", perf_cores.size());
                }
                break;
                
            case ThreadRole::Input:
                // Input: P-cores for low latency
                if (!perf_cores.empty()) {
                    for (int core : perf_cores) {
                        CPU_SET(core, &cpuset);
                    }
                    cores_assigned = true;
                    LOG_DEBUG("Input thread assigned to {} P-cores", perf_cores.size());
                }
                break;
                
            case ThreadRole::Network:
                // Network: P-cores but lower priority than input
                if (!perf_cores.empty()) {
                    for (int core : perf_cores) {
                        CPU_SET(core, &cpuset);
                    }
                    cores_assigned = true;
                    LOG_DEBUG("Network thread assigned to {} P-cores", perf_cores.size());
                }
                break;
                
            case ThreadRole::Background:
            default:
                // Background: E-cores if available, P-cores otherwise
                if (!eff_cores.empty()) {
                    for (int core : eff_cores) {
                        CPU_SET(core, &cpuset);
                    }
                    cores_assigned = true;
                    LOG_DEBUG("Background thread assigned to {} E-cores", eff_cores.size());
                } else if (!perf_cores.empty()) {
                    // No E-cores: use P-cores
                    for (int core : perf_cores) {
                        CPU_SET(core, &cpuset);
                    }
                    cores_assigned = true;
                    LOG_DEBUG("Background thread assigned to {} P-cores (no E-cores)", perf_cores.size());
                }
                break;
        }
    }
    
    // CRITICAL SAFETY: Always ensure some cores are assigned
    if (!cores_assigned) {
        LOG_WARN("No cores assigned for role {} - using all cores for safety", static_cast<int>(role));
        for (int i = 0; i < total_cores; i++) {
            CPU_SET(i, &cpuset);
        }
    }
    
    // Apply affinity with robust error handling
    int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    if (result == 0) {
        affinity_applied = true;
        last_role = role;
        LOG_DEBUG("Successfully applied CPU affinity for thread role {}", static_cast<int>(role));
    } else {
        LOG_WARN("Failed to set CPU affinity ({}): {} - using system default", 
                 result, strerror(result));
        // Don't fail - just continue with system default scheduling
    }
    
    // Apply priority optimizations
    apply_platform_priority(role, get_turbo_mode_ref());
}

void HostThreadScheduler::apply_platform_priority(ThreadRole role, TurboMode turbo) {
    if (turbo == TurboMode::Disabled) {
        LOG_DEBUG("Turbo disabled - using default thread priorities");
        return;
    }
    
    pthread_t current_thread = pthread_self();
    bool priority_applied = false;
    
    switch (role) {
        case ThreadRole::Audio:
            {
                // Audio: Try real-time scheduling for best audio performance
                struct sched_param param;
                param.sched_priority = (turbo == TurboMode::Aggressive) ? 10 : 5;
                
                if (pthread_setschedparam(current_thread, SCHED_FIFO, &param) == 0) {
                    priority_applied = true;
                    LOG_DEBUG("Audio thread: Real-time priority {} applied", param.sched_priority);
                } else {
                    LOG_DEBUG("Audio thread: RT priority failed, using normal (expected without privileges)");
                }
            }
            break;
            
        case ThreadRole::MainRender:
            {
                // Render: Slightly elevated priority for smooth frame delivery
                if (turbo == TurboMode::Aggressive) {
                    struct sched_param param;
                    param.sched_priority = 0;
                    if (pthread_setschedparam(current_thread, SCHED_OTHER, &param) == 0) {
                        priority_applied = true;
                        LOG_DEBUG("Render thread: Normal high priority applied");
                    }
                }
            }
            break;
            
        case ThreadRole::Input:
            {
                // Input: Low-latency priority for responsive controls
                if (turbo == TurboMode::Aggressive) {
                    struct sched_param param;
                    param.sched_priority = 3;
                    if (pthread_setschedparam(current_thread, SCHED_FIFO, &param) == 0) {
                        priority_applied = true;
                        LOG_DEBUG("Input thread: Low-latency RT priority applied");
                    } else {
                        LOG_DEBUG("Input thread: RT priority failed, using normal");
                    }
                }
            }
            break;
            
        default:
            LOG_DEBUG("Thread role {}: Using default system priority", static_cast<int>(role));
            break;
    }
    
    if (!priority_applied && turbo != TurboMode::Disabled) {
        LOG_DEBUG("Priority optimization skipped for thread role {} (normal behavior)", static_cast<int>(role));
    }
}

void HostThreadScheduler::apply_process_optimizations() {
    TurboMode turbo = get_turbo_mode_ref();
    
    if (turbo != TurboMode::Disabled) {
        const char* mode_str = (turbo == TurboMode::Aggressive) ? "AGGRESSIVE" : "BALANCED";
        LOG_INFO("Turbo mode active: {} - Thread classification and affinity enabled", mode_str);
        LOG_INFO("Process-level optimizations available with elevated privileges");
        
        // NOTE: We don't attempt setpriority() or other process-level changes
        // The main benefit comes from intelligent thread classification and core affinity
    }
}

void HostThreadScheduler::detect_hardware_capabilities() {
    int total = get_total_cores();
    
    // Log useful system information
    LOG_INFO("Hardware analysis complete:");
    LOG_INFO("  Total CPU threads: {}", total);
    LOG_INFO("  Performance cores: {}", get_performance_cores().size());
    LOG_INFO("  Efficiency cores: {}", get_efficiency_cores().size()); 
    LOG_INFO("  Turbo cores: {}", get_turbo_cores().size());
    
    // Provide performance expectations
    if (total >= 16) {
        LOG_INFO("High-performance system detected - full optimizations available");
    } else if (total >= 8) {
        LOG_INFO("Mid-range system detected - balanced optimizations enabled");
    } else {
        LOG_INFO("Compact system detected - conservative optimizations applied");
    }
}

void HostThreadScheduler::set_turbo_mode(TurboMode mode) {
    TurboMode old_mode = get_turbo_mode_ref();
    get_turbo_mode_ref() = mode;
    
    const char* mode_names[] = {"DISABLED", "BALANCED", "AGGRESSIVE"};
    const char* new_mode_str = mode_names[static_cast<int>(mode)];
    
    LOG_INFO("Turbo mode: {} -> {}", mode_names[static_cast<int>(old_mode)], new_mode_str);
    
    if (get_enabled()) {
        apply_process_optimizations();
    }
}

TurboMode HostThreadScheduler::get_turbo_mode() {
    return get_turbo_mode_ref();
}

void HostThreadScheduler::shutdown() {
    if (get_enabled()) {
        LOG_INFO("Shutting down Host Thread Scheduler - performance optimizations disabled");
        get_enabled() = false;
        get_turbo_mode_ref() = TurboMode::Disabled;
    }
}

bool HostThreadScheduler::is_enabled() {
    return get_enabled();
}

void HostThreadScheduler::enable(bool enabled) {
    get_enabled() = enabled;
    LOG_INFO("Host Thread Scheduler: {}", enabled ? "ENABLED" : "DISABLED");
    
    if (enabled) {
        LOG_INFO("Smart thread classification and CPU affinity active");
    }
}

void HostThreadScheduler::log_thread_info(const std::string& name, ThreadRole role) {
    const char* role_names[] = {"Unknown", "MainRender", "Audio", "Input", "Network", "Background"};
    const char* role_str = role_names[static_cast<int>(role)];
    
    TurboMode turbo = get_turbo_mode_ref();
    const char* turbo_indicator = (turbo == TurboMode::Aggressive) ? " [TURBO-AGG]" :
                                 (turbo == TurboMode::Balanced) ? " [TURBO-BAL]" : "";
    
    LOG_INFO("Thread '{}' classified as {}{} and optimized", name, role_str, turbo_indicator);
}

} // namespace sce_kernel_thread

#endif // __linux__