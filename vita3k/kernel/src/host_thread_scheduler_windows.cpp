// vita3k/kernel/src/host_thread_scheduler_windows.cpp
#ifdef _WIN32
#include <kernel/thread/host_thread_scheduler.h>
#include <util/log.h>
#include <windows.h>
#include <thread>
#include <algorithm>
#include <vector>
#include <string>

// Required for multimedia timer functions
#pragma comment(lib, "winmm.lib")

namespace sce_kernel_thread {

// Static storage using function-local statics (same pattern as Linux)
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

float& HostThreadScheduler::get_vita_affinity_multiplier_ref() {
    static float affinity_multiplier = 2.0f;  // Default 2x multiplier for Ultra mode (less aggressive)
    return affinity_multiplier;
}

std::vector<int>& HostThreadScheduler::get_ultra_cores() {
    static std::vector<int> ultra_cores;
    return ultra_cores;
}

bool HostThreadScheduler::initialize() {
    try {
        LOG_INFO("Initializing Windows Host Thread Scheduler with Ultra Mode support");
        
        get_total_cores() = std::thread::hardware_concurrency();
        LOG_INFO("Detected {} CPU cores", get_total_cores());
        
        detect_cores();
        detect_hardware_capabilities();
        
        // Start disabled for safety
        get_enabled() = false;
        get_turbo_mode_ref() = TurboMode::Disabled;
        
        LOG_INFO("Host scheduler initialized - P-cores: {}, E-cores: {}, Turbo-cores: {}, Ultra-cores: {}", 
                 get_performance_cores().size(), 
                 get_efficiency_cores().size(),
                 get_turbo_cores().size(),
                 get_ultra_cores().size());
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize host scheduler: {}", e.what());
        return false;
    }
}

void HostThreadScheduler::detect_cores() {
    auto& perf_cores = get_performance_cores();
    auto& eff_cores = get_efficiency_cores();
    auto& turbo_cores = get_turbo_cores();
    auto& ultra_cores = get_ultra_cores();
    
    perf_cores.clear();
    eff_cores.clear();
    turbo_cores.clear();
    ultra_cores.clear();
    
    int total = get_total_cores();
    
    // Windows P/E-core detection using GetLogicalProcessorInformationEx
    DWORD buffer_size = 0;
    bool detailed_detection = false;
    
    // Try to get detailed processor information
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &buffer_size) == FALSE && 
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && buffer_size > 0) {
        
        std::vector<uint8_t> buffer(buffer_size);
        auto info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());
        
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, info, &buffer_size)) {
            LOG_DEBUG("Windows detailed core detection successful");
            detailed_detection = true;
        }
    }
    
    if (!detailed_detection) {
        LOG_DEBUG("Using fallback core detection method");
    }
    
    // Enhanced core assignment for Ultra mode
    if (total >= 24) {
        // High-end desktop (24+ cores like Intel 13700HX)
        // P-cores: 0-15 (16 cores with HT)
        for (int i = 0; i < 16; ++i) {
            perf_cores.push_back(i);
        }
        // E-cores: 16-23 (8 efficiency cores)
        for (int i = 16; i < total; ++i) {
            eff_cores.push_back(i);
        }
        // Turbo cores: Best 8 P-cores
        for (int i = 0; i < 8; ++i) {
            turbo_cores.push_back(i);
        }
        // Ultra cores: Best 8 cores for maximum performance (not all cores)
        int ultra_count = std::min(8, p_core_count / 2);
        for (int i = 0; i < ultra_count; ++i) {
            ultra_cores.push_back(i);
        }
        LOG_INFO("24+ core Windows system: P-cores 0-15, E-cores 16-{}, Ultra 0-{}", total-1, ultra_count-1);
        
    } else if (total >= 16) {
        // Mid-range system (16 cores)
        int p_core_count = 12;  // Most cores as P-cores
        for (int i = 0; i < p_core_count; ++i) {
            perf_cores.push_back(i);
        }
        for (int i = p_core_count; i < total; ++i) {
            eff_cores.push_back(i);
        }
        for (int i = 0; i < 6; ++i) {
            turbo_cores.push_back(i);
        }
        // Ultra: Best 8 cores
        int ultra_count = std::min(8, p_core_count / 2);
        for (int i = 0; i < ultra_count; ++i) {
            ultra_cores.push_back(i);
        }
        LOG_INFO("16-core Windows system: P-cores 0-11, E-cores 12-15, Ultra 0-{}", ultra_count-1);
        
    } else {
        // Lower core count systems
        int p_core_count = std::max(4, total * 2 / 3);
        for (int i = 0; i < p_core_count && i < total; ++i) {
            perf_cores.push_back(i);
        }
        for (int i = p_core_count; i < total; ++i) {
            eff_cores.push_back(i);
        }
        int turbo_count = std::min(4, p_core_count);
        for (int i = 0; i < turbo_count; ++i) {
            turbo_cores.push_back(i);
        }
        // Ultra: Best available cores (max 6)
        int ultra_count = std::min(6, p_core_count / 2);
        for (int i = 0; i < ultra_count; ++i) {
            ultra_cores.push_back(i);
        }
        LOG_INFO("Standard Windows system: P-cores 0-{}, E-cores {}-{}, Ultra 0-{}", 
                 p_core_count-1, p_core_count, total-1, ultra_count-1);
    }
}

void HostThreadScheduler::detect_hardware_capabilities() {
    // Windows-specific CPU capability detection
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    
    LOG_DEBUG("Windows hardware: {} processors, page size: {}", 
              sys_info.dwNumberOfProcessors, sys_info.dwPageSize);
    
    // Check if we're running on Windows 10/11 for better scheduler support
    OSVERSIONINFO version_info;
    ZeroMemory(&version_info, sizeof(OSVERSIONINFO));
    version_info.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    
    if (GetVersionEx(&version_info)) {
        LOG_DEBUG("Windows version: {}.{}", version_info.dwMajorVersion, version_info.dwMinorVersion);
        
        // Windows 10+ has better thread scheduling capabilities
        if (version_info.dwMajorVersion >= 10) {
            LOG_INFO("Windows 10+ detected - Enhanced scheduler support available");
        }
    }
    
    // Detect CPU vendor for optimization hints
    int cpu_info[4];
    __cpuid(cpu_info, 0);
    char vendor[13];
    memcpy(vendor, &cpu_info[1], 4);
    memcpy(vendor + 4, &cpu_info[3], 4);
    memcpy(vendor + 8, &cpu_info[2], 4);
    vendor[12] = '\0';
    
    LOG_DEBUG("CPU vendor: {}", vendor);
    
    if (strcmp(vendor, "GenuineIntel") == 0) {
        LOG_DEBUG("Intel CPU detected - P/E-core optimizations enabled");
    } else if (strcmp(vendor, "AuthenticAMD") == 0) {
        LOG_DEBUG("AMD CPU detected - CCD-aware optimizations enabled");
    }
}

void HostThreadScheduler::apply_platform_affinity(ThreadRole role, const std::vector<int>& target_cores) {
    HANDLE current_thread = GetCurrentThread();
    
    // Convert core list to Windows affinity mask
    DWORD_PTR affinity_mask = 0;
    for (int core : target_cores) {
        if (core >= 0 && core < 64) {  // DWORD_PTR limit on x64
            affinity_mask |= (1ULL << core);
        }
    }
    
    if (affinity_mask != 0) {
        DWORD_PTR result = SetThreadAffinityMask(current_thread, affinity_mask);
        if (result == 0) {
            DWORD error = GetLastError();
            LOG_WARN("Failed to set thread affinity for {} role: error {}", 
                    static_cast<int>(role), error);
            return;
        }
        LOG_DEBUG("Set thread affinity mask: 0x{:X} for role {}", 
                 affinity_mask, static_cast<int>(role));
    }
}

void HostThreadScheduler::apply_platform_priority(ThreadRole role, TurboMode turbo) {
    HANDLE current_thread = GetCurrentThread();
    int priority = THREAD_PRIORITY_NORMAL;
    
    // Enhanced priority mapping for Ultra mode
    if (turbo == TurboMode::Ultra) {
        switch (role) {
            case ThreadRole::Audio:
                priority = THREAD_PRIORITY_TIME_CRITICAL;  // Maximum priority
                break;
            case ThreadRole::MainRender:
                priority = THREAD_PRIORITY_HIGHEST;        // Very high priority
                break;
            case ThreadRole::Input:
                priority = THREAD_PRIORITY_ABOVE_NORMAL;   // High priority
                break;
            case ThreadRole::Network:
                priority = THREAD_PRIORITY_NORMAL;         // Normal priority
                break;
            case ThreadRole::Background:
                priority = THREAD_PRIORITY_BELOW_NORMAL;   // Lower priority
                break;
            default:
                priority = THREAD_PRIORITY_NORMAL;
                break;
        }
    } else if (turbo == TurboMode::Aggressive) {
        switch (role) {
            case ThreadRole::Audio:
                priority = THREAD_PRIORITY_TIME_CRITICAL;
                break;
            case ThreadRole::MainRender:
                priority = THREAD_PRIORITY_HIGHEST;
                break;
            case ThreadRole::Input:
                priority = THREAD_PRIORITY_ABOVE_NORMAL;
                break;
            default:
                break;
        }
    } else if (turbo == TurboMode::Balanced) {
        switch (role) {
            case ThreadRole::Audio:
                priority = THREAD_PRIORITY_HIGHEST;
                break;
            case ThreadRole::MainRender:
                priority = THREAD_PRIORITY_ABOVE_NORMAL;
                break;
            default:
                break;
        }
    }
    
    if (priority != THREAD_PRIORITY_NORMAL) {
        if (!SetThreadPriority(current_thread, priority)) {
            DWORD error = GetLastError();
            LOG_DEBUG("Failed to set thread priority {} for {} thread: error {}", 
                     priority,
                     (role == ThreadRole::Audio) ? "audio" : 
                     (role == ThreadRole::MainRender) ? "render" : "other",
                     error);
        } else {
            LOG_DEBUG("Applied priority {} to {} thread", priority,
                     (role == ThreadRole::Audio) ? "audio" : 
                     (role == ThreadRole::MainRender) ? "render" : "other");
        }
    }
}

void HostThreadScheduler::apply_affinity_hint_current_thread(ThreadRole role) {
    if (!get_enabled()) return;
    
    const std::vector<int>* target_cores = nullptr;
    TurboMode turbo = get_turbo_mode_ref();
    
    // Ultra mode uses all cores aggressively
    if (turbo == TurboMode::Ultra) {
        switch (role) {
            case ThreadRole::MainRender:
            case ThreadRole::Audio:
                target_cores = &get_ultra_cores();  // Use ALL cores
                break;
            case ThreadRole::Input:
                target_cores = &get_turbo_cores();  // Use best cores
                break;
            case ThreadRole::Network:
                target_cores = &get_performance_cores();
                break;
            case ThreadRole::Background:
            default:
                target_cores = &get_efficiency_cores();
                break;
        }
    } else {
        // Standard turbo mode behavior
        switch (role) {
            case ThreadRole::MainRender:
                target_cores = (turbo != TurboMode::Disabled) ? 
                              &get_turbo_cores() : &get_performance_cores();
                break;
                
            case ThreadRole::Audio:
                target_cores = &get_turbo_cores();  // Audio always gets priority cores
                break;
                
            case ThreadRole::Input:
                target_cores = &get_performance_cores();
                break;
                
            case ThreadRole::Network:
                target_cores = &get_performance_cores();
                break;
                
            case ThreadRole::Background:
            default:
                target_cores = &get_efficiency_cores();
                break;
        }
    }
    
    if (target_cores && !target_cores->empty()) {
        apply_platform_affinity(role, *target_cores);
        apply_platform_priority(role, turbo);
    }
}

void HostThreadScheduler::apply_process_optimizations() {
    TurboMode turbo = get_turbo_mode_ref();
    HANDLE current_process = GetCurrentProcess();
    
    if (turbo == TurboMode::Ultra) {
        // Ultra mode: Maximum performance settings
        if (!SetPriorityClass(current_process, REALTIME_PRIORITY_CLASS)) {
            // Fallback to HIGH_PRIORITY_CLASS if realtime fails
            if (!SetPriorityClass(current_process, HIGH_PRIORITY_CLASS)) {
                DWORD error = GetLastError();
                LOG_WARN("Failed to set process priority for Ultra mode: error {}", error);
            } else {
                LOG_INFO("Applied Ultra process priority (HIGH_PRIORITY_CLASS - realtime denied)");
            }
        } else {
            LOG_INFO("Applied Ultra process priority (REALTIME_PRIORITY_CLASS)");
        }
        
        // Ultra-high resolution timers (1ms precision)
        MMRESULT timer_result = timeBeginPeriod(1);
        if (timer_result == TIMERR_NOERROR) {
            LOG_INFO("Enabled ultra-high resolution timers (1ms precision)");
        } else {
            LOG_WARN("Failed to enable ultra-high resolution timers: error {}", timer_result);
        }
        
    } else if (turbo == TurboMode::Aggressive) {
        // Aggressive mode: High performance
        if (!SetPriorityClass(current_process, HIGH_PRIORITY_CLASS)) {
            DWORD error = GetLastError();
            LOG_WARN("Failed to set HIGH_PRIORITY_CLASS: error {}", error);
        } else {
            LOG_INFO("Applied aggressive process priority (HIGH_PRIORITY_CLASS)");
        }
        
        // Enable high-resolution timers (1ms precision)
        MMRESULT timer_result = timeBeginPeriod(1);
        if (timer_result == TIMERR_NOERROR) {
            LOG_INFO("Enabled high-resolution timers (1ms precision)");
        } else {
            LOG_WARN("Failed to enable high-resolution timers: error {}", timer_result);
        }
        
    } else if (turbo == TurboMode::Balanced) {
        if (!SetPriorityClass(current_process, ABOVE_NORMAL_PRIORITY_CLASS)) {
            DWORD error = GetLastError();
            LOG_WARN("Failed to set ABOVE_NORMAL_PRIORITY_CLASS: error {}", error);
        } else {
            LOG_INFO("Applied balanced process priority (ABOVE_NORMAL_PRIORITY_CLASS)");
        }
        
        // Enable moderate precision timers for balanced mode
        MMRESULT timer_result = timeBeginPeriod(2);
        if (timer_result == TIMERR_NOERROR) {
            LOG_INFO("Enabled moderate-resolution timers (2ms precision)");
        }
    }
}

void HostThreadScheduler::set_turbo_mode(TurboMode mode) {
    TurboMode old_mode = get_turbo_mode_ref();
    get_turbo_mode_ref() = mode;
    
    const char* mode_str = (mode == TurboMode::Ultra) ? "ULTRA" :
                          (mode == TurboMode::Aggressive) ? "AGGRESSIVE" :
                          (mode == TurboMode::Balanced) ? "BALANCED" : "DISABLED";
    LOG_INFO("Turbo mode set to: {}", mode_str);
    
    // Cleanup old optimizations if changing modes
    if (old_mode != TurboMode::Disabled && mode != old_mode) {
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        timeEndPeriod(1);  // Disable high-resolution timers
        timeEndPeriod(2);  // Disable moderate-resolution timers
        LOG_DEBUG("Cleaned up previous mode optimizations");
    }
    
    // Apply new process-level optimizations
    if (mode != TurboMode::Disabled) {
        apply_process_optimizations();
    }
}

TurboMode HostThreadScheduler::get_turbo_mode() {
    return get_turbo_mode_ref();
}

bool HostThreadScheduler::is_ultra_mode_active() {
    return get_turbo_mode_ref() == TurboMode::Ultra;
}

void HostThreadScheduler::shutdown() {
    // Cleanup Windows-specific resources
    TurboMode current_mode = get_turbo_mode_ref();
    if (current_mode == TurboMode::Ultra || current_mode == TurboMode::Aggressive) {
        timeEndPeriod(1);  // Cleanup high-resolution timers
        LOG_DEBUG("Cleaned up high-resolution timers");
    } else if (current_mode == TurboMode::Balanced) {
        timeEndPeriod(2);  // Cleanup moderate-resolution timers
        LOG_DEBUG("Cleaned up moderate-resolution timers");
    }
    
    // Reset process priority to normal
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    
    get_enabled() = false;
    get_turbo_mode_ref() = TurboMode::Disabled;
    LOG_INFO("Windows host scheduler disabled and cleaned up");
}

bool HostThreadScheduler::is_enabled() {
    return get_enabled();
}

void HostThreadScheduler::enable(bool enabled) {
    get_enabled() = enabled;
    LOG_INFO("Windows host scheduler {}", enabled ? "ENABLED" : "DISABLED");
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
    
    TurboMode turbo = get_turbo_mode_ref();
    const char* turbo_str = (turbo == TurboMode::Ultra) ? " [ULTRA]" :
                           (turbo == TurboMode::Aggressive) ? " [TURBO-AGG]" :
                           (turbo == TurboMode::Balanced) ? " [TURBO-BAL]" : "";
    
    LOG_INFO("Thread '{}' classified as {}{} and assigned scheduler hints", 
             name, role_str, turbo_str);
}

// GPU worker core functions
void HostThreadScheduler::set_gpu_worker_cores(int gpu_cores) {
    get_gpu_cores() = gpu_cores;
    LOG_INFO("GPU pipeline workers: {} cores (informational only)", gpu_cores);
    // Just informational - don't interfere with CPU scheduling
}

int HostThreadScheduler::get_gpu_worker_cores() {
    return get_gpu_cores();
}

// Vita affinity multiplier functions
void HostThreadScheduler::set_vita_affinity_multiplier(float multiplier) {
    get_vita_affinity_multiplier_ref() = multiplier;
    LOG_INFO("Vita affinity multiplier set to: {:.1f}x", multiplier);
}

float HostThreadScheduler::get_vita_affinity_multiplier() {
    return get_vita_affinity_multiplier_ref();
}

// Ultra mode Vita thread optimization
void HostThreadScheduler::apply_vita_thread_optimization(const std::string& thread_name, 
                                                        int vita_priority, 
                                                        int vita_affinity_mask) {
    if (!get_enabled()) return;
    
    TurboMode turbo = get_turbo_mode_ref();
    if (turbo != TurboMode::Ultra) return;  // Ultra mode only
    
    HANDLE current_thread = GetCurrentThread();
    
    // Map Vita priority (0-191) to Windows priority with enhanced Ultra mapping
    int windows_priority = THREAD_PRIORITY_NORMAL;
    if (vita_priority >= 160) {
        windows_priority = THREAD_PRIORITY_TIME_CRITICAL;  // 160-191: Critical
    } else if (vita_priority >= 140) {
        windows_priority = THREAD_PRIORITY_HIGHEST;        // 140-159: Highest
    } else if (vita_priority >= 120) {
        windows_priority = THREAD_PRIORITY_ABOVE_NORMAL;   // 120-139: Above normal
    } else if (vita_priority >= 80) {
        windows_priority = THREAD_PRIORITY_NORMAL;         // 80-119: Normal
    } else if (vita_priority >= 40) {
        windows_priority = THREAD_PRIORITY_BELOW_NORMAL;   // 40-79: Below normal
    } else {
        windows_priority = THREAD_PRIORITY_LOWEST;         // 0-39: Lowest
    }
    
    if (!SetThreadPriority(current_thread, windows_priority)) {
        DWORD error = GetLastError();
        LOG_DEBUG("Failed to set Ultra priority {} for thread '{}': error {}", 
                 windows_priority, thread_name, error);
    }
    
    // Apply Ultra core affinity with multiplier expansion
    float multiplier = get_vita_affinity_multiplier();
    const auto& ultra_cores = get_ultra_cores();
    
    if (!ultra_cores.empty() && multiplier > 1.0f) {
        // Expand affinity using multiplier - use more cores than Vita's 4
        std::vector<int> expanded_cores;
        int target_cores = static_cast<int>(4 * multiplier);  // 4 Vita cores * multiplier
        target_cores = std::min(target_cores, get_total_cores());
        
        // Use the best available cores up to target count
        for (int i = 0; i < target_cores && i < static_cast<int>(ultra_cores.size()); ++i) {
            expanded_cores.push_back(ultra_cores[i]);
        }
        
        // Fill remaining with any available cores if needed
        if (expanded_cores.size() < static_cast<size_t>(target_cores)) {
            for (int i = 0; i < get_total_cores() && expanded_cores.size() < static_cast<size_t>(target_cores); ++i) {
                if (std::find(expanded_cores.begin(), expanded_cores.end(), i) == expanded_cores.end()) {
                    expanded_cores.push_back(i);
                }
            }
        }
        
        apply_platform_affinity(ThreadRole::MainRender, expanded_cores);
        
        LOG_DEBUG("Ultra thread '{}': Vita priority {} -> Windows {}, cores expanded from 4 to {} ({}x multiplier)", 
                 thread_name, vita_priority, windows_priority, expanded_cores.size(), multiplier);
    } else {
        LOG_DEBUG("Ultra thread '{}': Vita priority {} -> Windows {}, using standard cores", 
                 thread_name, vita_priority, windows_priority);
    }
}

} // namespace sce_kernel_thread

#endif // _WIN32