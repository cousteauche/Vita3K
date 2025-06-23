// vita3k/kernel/include/kernel/thread/host_thread_scheduler.h
#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cctype>
#ifdef __linux__
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

// Forward declaration for SceInt32
typedef int SceInt32;

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
    Aggressive,      // Maximum performance mode
    Ultra            // Break all limits - use all cores
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
    
    // Thread management - MOVED FROM COMMON
    static ThreadRole classify_thread(const std::string& name) {
        if (name.empty()) {
            return ThreadRole::Unknown;
        }
        
        // Convert to lowercase for case-insensitive matching
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        
        // Audio threads - real-time priority
        if (lower_name.find("audio") != std::string::npos ||
            lower_name.find("sound") != std::string::npos ||
            lower_name.find("music") != std::string::npos ||
            lower_name.find("atrac") != std::string::npos ||
            lower_name.find("snd") != std::string::npos ||
            lower_name.find("pcm") != std::string::npos) {
            return ThreadRole::Audio;
        }
        
        // Main render threads - highest priority
        if (lower_name.find("render") != std::string::npos ||
            lower_name.find("gxm") != std::string::npos ||
            lower_name.find("graphics") != std::string::npos ||
            lower_name.find("gpu") != std::string::npos ||
            lower_name.find("opengl") != std::string::npos ||
            lower_name.find("vulkan") != std::string::npos ||
            lower_name.find("draw") != std::string::npos ||
            lower_name.find("display") != std::string::npos) {
            return ThreadRole::MainRender;
        }
        
        // Input threads - low latency required
        if (lower_name.find("input") != std::string::npos ||
            lower_name.find("ctrl") != std::string::npos ||
            lower_name.find("pad") != std::string::npos ||
            lower_name.find("touch") != std::string::npos ||
            lower_name.find("controller") != std::string::npos ||
            lower_name.find("button") != std::string::npos) {
            return ThreadRole::Input;
        }
        
        // Network/IO threads
        if (lower_name.find("net") != std::string::npos ||
            lower_name.find("io") != std::string::npos ||
            lower_name.find("file") != std::string::npos ||
            lower_name.find("fios") != std::string::npos ||
            lower_name.find("socket") != std::string::npos ||
            lower_name.find("http") != std::string::npos ||
            lower_name.find("download") != std::string::npos) {
            return ThreadRole::Network;
        }
        
        // Everything else is background
        return ThreadRole::Background;
    }
    
    static void apply_affinity_hint_current_thread(ThreadRole role);
    static void log_thread_info(const std::string& name, ThreadRole role);

    // Platform-specific optimizations
    static void apply_process_optimizations();
    static void detect_hardware_capabilities();

    // GPU coordination 
    static void set_gpu_worker_cores(int gpu_cores);
    static int get_gpu_worker_cores();

    // Enhanced thread management for Vita overclocking
    static void set_vita_affinity_multiplier(float multiplier);
    static float get_vita_affinity_multiplier();
    static void apply_vita_thread_optimization(const std::string& name, int vita_priority, SceInt32 vita_affinity);
    static bool is_ultra_mode_active();
    
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
    static int& get_gpu_cores();
    static float& get_vita_affinity_multiplier_ref();  // Changed name to avoid conflict
    static std::vector<int>& get_ultra_cores();
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