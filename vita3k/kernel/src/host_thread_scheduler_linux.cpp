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
#include <sys/resource.h>  // Add this
#include <unistd.h>        // Add this

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
    LOG_INFO("Scheduler informed: GPU using {} worker cores (0-{})", gpu_cores, gpu_cores - 1);
}

int HostThreadScheduler::get_gpu_worker_cores() {
    return get_gpu_cores();
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
    int total_cores = get_total_cores();
    int gpu_cores = get_gpu_worker_cores();
    
    // Smart handling based on core count
    if (total_cores <= 2) {
        // Single/Dual core: No affinity restrictions, let OS schedule
        // Scheduler would hurt more than help on these systems
        for (int i = 0; i < total_cores; i++) {
            CPU_SET(i, &cpuset);
        }
        LOG_DEBUG("Low core count ({}): No affinity restrictions for role {}", 
                 total_cores, static_cast<int>(role));
        
    } else if (total_cores <= 4) {
        // Quad core: Only basic priority separation, minimal affinity
        switch (role) {
            case ThreadRole::Audio:
                // Audio priority: prefer first cores but allow all
                for (int i = 0; i < total_cores; i++) {
                    CPU_SET(i, &cpuset);
                }
                LOG_DEBUG("Quad core: Audio gets all cores with high priority");
                break;
                
            default:
                // Everything else: all cores, normal priority
                for (int i = 0; i < total_cores; i++) {
                    CPU_SET(i, &cpuset);
                }
                break;
        }
        
    } else {
        // 6+ cores: Use proper separation
        int gpu_reserved_cores = (gpu_cores > 0) ? gpu_cores : std::max(2, total_cores / 3);
        int available_cpu_cores = total_cores - gpu_reserved_cores;
        
        // Only separate if we have enough cores to make it worthwhile
        if (available_cpu_cores < 2) {
            // Not enough cores for separation - disable affinity
            for (int i = 0; i < total_cores; i++) {
                CPU_SET(i, &cpuset);
            }
            LOG_DEBUG("Insufficient cores for separation ({}), using all cores", total_cores);
            
        } else {
            // Enough cores: apply proper separation
            switch (role) {
                case ThreadRole::MainRender:
                    // Render gets cores after GPU reservation
                    for (int i = 0; i < 2 && (gpu_reserved_cores + i) < total_cores; i++) {
                        CPU_SET(gpu_reserved_cores + i, &cpuset);
                    }
                    LOG_DEBUG("Render assigned cores {}-{}", gpu_reserved_cores, gpu_reserved_cores + 1);
                    break;
                    
                case ThreadRole::Audio:
                    // Audio gets next cores after render
                    if (available_cpu_cores >= 4) {
                        int audio_start = gpu_reserved_cores + 2;
                        for (int i = 0; i < 2 && (audio_start + i) < total_cores; i++) {
                            CPU_SET(audio_start + i, &cpuset);
                        }
                        LOG_DEBUG("Audio assigned cores {}-{}", audio_start, audio_start + 1);
                    } else {
                        // Share with render but apply priority
                        for (int i = gpu_reserved_cores; i < total_cores; i++) {
                            CPU_SET(i, &cpuset);
                        }
                        LOG_DEBUG("Audio shares CPU cores with render (insufficient cores)");
                    }
                    break;
                    
                case ThreadRole::Input:
                case ThreadRole::Network:
                case ThreadRole::Background:
                    // Background: E-cores if available, else remaining cores
                    if (!eff_cores.empty()) {
                        for (int core : eff_cores) {
                            CPU_SET(core, &cpuset);
                        }
                        LOG_DEBUG("Background assigned E-cores");
                    } else {
                        // No E-cores: use remaining cores
                        int bg_start = gpu_reserved_cores + 4;
                        if (bg_start >= total_cores) bg_start = gpu_reserved_cores;
                        for (int i = bg_start; i < total_cores; i++) {
                            CPU_SET(i, &cpuset);
                        }
                        LOG_DEBUG("Background assigned remaining cores");
                    }
                    break;
                    
                default:
                    // Unknown: avoid GPU cores if possible
                    for (int i = gpu_reserved_cores; i < total_cores; i++) {
                        CPU_SET(i, &cpuset);
                    }
                    break;
            }
        }
    }
    
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        LOG_WARN("Failed to set CPU affinity for thread role {}", static_cast<int>(role));
    }
    
    // Apply priority regardless of affinity
    apply_platform_priority(role, get_turbo_mode_ref());
}

void HostThreadScheduler::apply_platform_priority(ThreadRole role, TurboMode turbo) {
    pthread_t current_thread = pthread_self();
    
    switch (role) {
        case ThreadRole::Audio:
            {
                // Try real-time priority first, fall back gracefully
                struct sched_param param;
                param.sched_priority = 10;  // Lower RT priority that might work
                if (pthread_setschedparam(current_thread, SCHED_FIFO, &param) != 0) {
                    // Fallback: Just log and continue with normal scheduling
                    LOG_DEBUG("Audio thread using normal priority (RT requires privileges)");
                }
            }
            break;
            
        case ThreadRole::MainRender:
            {
                // Use normal scheduling - no nice() calls
                struct sched_param param;
                param.sched_priority = 0;
                pthread_setschedparam(current_thread, SCHED_OTHER, &param);
                LOG_DEBUG("Render thread using normal priority");
            }
            break;
            
        case ThreadRole::Input:
            {
                // Try moderate RT priority, fall back gracefully  
                struct sched_param param;
                param.sched_priority = 5;  // Very low RT priority
                if (pthread_setschedparam(current_thread, SCHED_FIFO, &param) != 0) {
                    LOG_DEBUG("Input thread using normal priority (RT requires privileges)");
                }
            }
            break;
            
        default:
            // Other threads use normal scheduling
            break;
    }
}


void HostThreadScheduler::apply_process_optimizations() {
    TurboMode turbo = get_turbo_mode_ref();
    
    if (turbo != TurboMode::Disabled) {
        LOG_INFO("Turbo mode enabled: {} (process-level optimizations require privileges)", 
                 turbo == TurboMode::Aggressive ? "AGGRESSIVE" : "BALANCED");
        
        // Don't attempt privileged operations
        // The CPU affinity and thread classification is the main benefit
        LOG_DEBUG("Thread affinity and classification active (no elevated privileges needed)");
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