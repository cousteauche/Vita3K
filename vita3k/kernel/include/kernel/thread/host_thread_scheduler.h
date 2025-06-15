// vita3k/kernel/include/kernel/thread/host_thread_scheduler.h
#pragma once

#include <string>
#include <vector>
#include <pthread.h>

namespace sce_kernel_thread {

enum class ThreadRole {
    Unknown,
    MainRender,      // Primary graphics/GXM threads
    Audio,           // Audio processing threads  
    Input,           // Input/controller threads
    Network,         // Network/IO threads
    Background       // Everything else
};

enum class TurboMode {
    Disabled,
    Balanced,        // Conservative performance boost
    Aggressive       // Maximum performance mode
};

class HostThreadScheduler {
public:
    // Core lifecycle
    static bool initialize();
    static void shutdown();
    static bool is_enabled();
    static void enable(bool enabled);
    
    // Turbo mode controls
    static void set_turbo_mode(TurboMode mode);
    static TurboMode get_turbo_mode();
    
    // Thread management
    static ThreadRole classify_thread(const std::string& name);
    static void apply_affinity_hint_current_thread(ThreadRole role);
    static void log_thread_info(const std::string& name, ThreadRole role);

    // Platform-specific optimizations
    static void apply_process_optimizations();
    static void detect_hardware_capabilities();

private:
    // Platform abstraction (implemented per-platform)
    static void detect_cores();
    static void apply_platform_affinity(ThreadRole role, const std::vector<int>& cores);
    static void apply_platform_priority(ThreadRole role, TurboMode turbo);
    
    // Shared state management (common across platforms)
    static bool& get_enabled();
    static TurboMode& get_turbo_mode_ref();
    static int& get_total_cores();
    static std::vector<int>& get_performance_cores();
    static std::vector<int>& get_efficiency_cores();
    static std::vector<int>& get_turbo_cores();
};

// Convenience macros for easy thread registration
#define HOST_THREAD_REGISTER(name) do { \
    auto role = sce_kernel_thread::HostThreadScheduler::classify_thread(name); \
    sce_kernel_thread::HostThreadScheduler::apply_affinity_hint_current_thread(role); \
    sce_kernel_thread::HostThreadScheduler::log_thread_info(name, role); \
} while(0)

#define HOST_THREAD_REGISTER_ROLE(name, role) do { \
    sce_kernel_thread::HostThreadScheduler::apply_affinity_hint_current_thread(role); \
    sce_kernel_thread::HostThreadScheduler::log_thread_info(name, role); \
} while(0)

} // namespace sce_kernel_thread
