// vita3k/kernel/include/kernel/thread/linux_scheduler.h
#pragma once
#ifdef __linux__
#include <pthread.h>
#include <string>
#include <vector>

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

class SimpleLinuxScheduler {
public:
    static bool initialize();
    static void shutdown();
    static bool is_enabled();
    static void enable(bool enabled);
    
    // Turbo mode controls
    static void set_turbo_mode(TurboMode mode);
    static TurboMode get_turbo_mode();
    
    static ThreadRole classify_thread(const std::string& name);
    static void apply_affinity_hint(pthread_t thread, ThreadRole role);
    static void log_thread_info(const std::string& name, ThreadRole role);

private:
    static void detect_cores();
    static void apply_turbo_optimizations(pthread_t thread, ThreadRole role);
    
    // Use function-local statics to avoid initialization order issues
    static bool& get_enabled();
    static TurboMode& get_turbo_mode_ref();
    static int& get_total_cores();
    static std::vector<int>& get_performance_cores();
    static std::vector<int>& get_efficiency_cores();
    static std::vector<int>& get_turbo_cores();  // Best performance cores for turbo
};

} // namespace sce_kernel_thread

#endif // __linux__