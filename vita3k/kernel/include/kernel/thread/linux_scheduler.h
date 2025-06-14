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
    
    // Simple functions to start with
    static ThreadRole classify_thread(const std::string& name);
    static void apply_affinity_hint(pthread_t thread, ThreadRole role);
    static void log_thread_info(const std::string& name, ThreadRole role);

private:
    static bool enabled_;
    static int total_cores_;
    static std::vector<int> performance_cores_;
    static std::vector<int> background_cores_;
    
    static void detect_cores();
};

} // namespace sce_kernel_thread

#endif // __linux__
