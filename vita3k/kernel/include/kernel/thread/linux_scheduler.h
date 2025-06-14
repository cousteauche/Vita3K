#pragma once

#ifdef __linux__
#include <pthread.h>
#include <string>
#include <vector>

namespace sce_kernel_thread {

enum class ThreadRole {
    Unknown,
    Render,
    Audio,
    Background
};

class SimpleLinuxScheduler {
public:
    static bool initialize();
    static void shutdown();
    static bool is_enabled();
    static void enable(bool enabled);
    
    static ThreadRole classify_thread(const std::string& name);
    static void apply_affinity_hint(pthread_t thread, ThreadRole role);
    static void log_thread_info(const std::string& name, ThreadRole role);

private:
    static void detect_cores();
    
    // Use function-local statics to avoid initialization order issues
    static bool& get_enabled();
    static int& get_total_cores();
    static std::vector<int>& get_performance_cores();
    static std::vector<int>& get_background_cores();
};

} // namespace sce_kernel_thread

#endif // __linux__