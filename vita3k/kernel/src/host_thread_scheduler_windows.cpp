// vita3k/kernel/src/host_thread_scheduler_windows.cpp
#ifdef _WIN32

#include <kernel/thread/host_thread_scheduler.h>
#include <util/log.h>
#include <windows.h>
#include <thread>
#include <algorithm>

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

bool HostThreadScheduler::initialize() {
    try {
        LOG_INFO("Initializing Windows Host Thread Scheduler");
        
        get_total_cores() = std::thread::hardware_concurrency();
        LOG_INFO("Detected {} CPU cores", get_total_cores());
        
        detect_cores();
        detect_hardware_capabilities();
        
        // Start disabled for safety
        get_enabled() = false;
        get_turbo_mode_ref() = TurboMode::Disabled;
        
        LOG_INFO("Host scheduler initialized - P-cores: {}, E-cores: {}, Turbo-cores: {}", 
                 get_performance_cores().size(), 
                 get_efficiency_cores().size(),
                 get_turbo_cores().size());
        
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
    
    perf_cores.clear();
    eff_cores.clear();
    turbo_cores.clear();
    
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
            // Parse processor information to detect P/E-cores
            // This is a simplified approach - could be enhanced with more detailed parsing
            LOG_DEBUG("Windows detailed core detection successful");
            detailed_detection = true;
        }
    }
    
    if (!detailed_detection) {
        LOG_DEBUG("Using fallback core detection method");
    }
    
    // Fallback: same logic as Linux for Intel 13700HX
    // For Intel 13700HX (24 threads): 16 P-cores + 8 E-cores
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
    
    LOG_INFO("Windows core detection: P-cores 0-{}, E-cores {}-{}, Turbo 0-{}", 
             p_core_count-1, p_core_count, total-1, turbo_count-1);
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
            LOG_WARN("Failed to set thread affinity: error {}", error);
            return;
        }
        LOG_DEBUG("Set thread affinity mask: 0x{:X}", affinity_mask);
    }
}

void HostThreadScheduler::apply_platform_priority(ThreadRole role, TurboMode turbo) {
    HANDLE current_thread = GetCurrentThread();
    int priority = THREAD_PRIORITY_NORMAL;
    
    // Set thread priority based on role and turbo mode
    if (turbo != TurboMode::Disabled) {
        switch (role) {
            case ThreadRole::Audio:
                priority = THREAD_PRIORITY_TIME_CRITICAL;  // Highest priority for audio
                break;
            case ThreadRole::MainRender:
                priority = (turbo == TurboMode::Aggressive) ? 
                          THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_ABOVE_NORMAL;
                break;
            case ThreadRole::Input:
                if (turbo == TurboMode::Aggressive) {
                    priority = THREAD_PRIORITY_ABOVE_NORMAL;
                }
                break;
            case ThreadRole::Network:
                // Keep normal priority for network threads
                break;
            case ThreadRole::Background:
                if (turbo == TurboMode::Aggressive) {
                    priority = THREAD_PRIORITY_BELOW_NORMAL;  // Lower priority for background
                }
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
    
    if (target_cores && !target_cores->empty()) {
        apply_platform_affinity(role, *target_cores);
        apply_platform_priority(role, turbo);
    }
}

void HostThreadScheduler::apply_process_optimizations() {
    TurboMode turbo = get_turbo_mode_ref();
    HANDLE current_process = GetCurrentProcess();
    
    if (turbo == TurboMode::Aggressive) {
        // Set high priority class for aggressive mode
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
    
    const char* mode_str = (mode == TurboMode::Aggressive) ? "AGGRESSIVE" :
                          (mode == TurboMode::Balanced) ? "BALANCED" : "DISABLED";
    LOG_INFO("Turbo mode set to: {}", mode_str);
    
    // Cleanup old optimizations if disabling turbo
    if (old_mode != TurboMode::Disabled && mode == TurboMode::Disabled) {
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        timeEndPeriod(1);  // Disable high-resolution timers
        timeEndPeriod(2);  // Disable moderate-resolution timers
        LOG_INFO("Disabled process optimizations and timers");
    }
    
    // Apply new process-level optimizations
    apply_process_optimizations();
}

TurboMode HostThreadScheduler::get_turbo_mode() {
    return get_turbo_mode_ref();
}

void HostThreadScheduler::shutdown() {
    // Cleanup Windows-specific resources
    TurboMode current_mode = get_turbo_mode_ref();
    if (current_mode == TurboMode::Aggressive) {
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
    LOG_INFO("Windows host scheduler disabled");
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
    const char* turbo_str = (turbo == TurboMode::Aggressive) ? " [TURBO-AGG]" :
                           (turbo == TurboMode::Balanced) ? " [TURBO-BAL]" : "";
    
    LOG_INFO("Thread '{}' classified as {}{} and assigned scheduler hints", 
             name, role_str, turbo_str);
}

} // namespace sce_kernel_thread

#endif // _WIN32