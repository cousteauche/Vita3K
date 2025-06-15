// vita3k/kernel/src/host_thread_scheduler_common.cpp
// Platform-agnostic thread classification logic

#include <kernel/thread/host_thread_scheduler.h>
#include <algorithm>
#include <cctype>

namespace sce_kernel_thread {

ThreadRole HostThreadScheduler::classify_thread(const std::string& name) {
    if (name.empty()) {
        return ThreadRole::Unknown;
    }
    
    // Convert to lowercase for case-insensitive matching
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
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
    
    // Audio threads - real-time priority
    if (lower_name.find("audio") != std::string::npos ||
        lower_name.find("sound") != std::string::npos ||
        lower_name.find("music") != std::string::npos ||
        lower_name.find("atrac") != std::string::npos ||
        lower_name.find("snd") != std::string::npos ||
        lower_name.find("pcm") != std::string::npos) {
        return ThreadRole::Audio;
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

} // namespace sce_kernel_thread