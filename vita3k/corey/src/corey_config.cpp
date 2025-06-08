// File: vita3k/corey/src/corey_config.cpp
// VITA3K_NO_GUI: Corey configuration management

#ifdef COREY_BUILD

#include <corey/corey_perf.h>
#include <gui/state.h>
#include <emuenv/state.h>
#include <imgui.h>

namespace corey {

// Performance configuration
struct CoreyConfig {
    bool disable_window_decorations = true;
    bool disable_transparency = true;
    bool disable_rounded_corners = true;
    bool cache_calculations = true;
    bool minimal_ui_when_gaming = true;
    float ui_update_rate = 30.0f; // FPS for UI updates when game is running
};

static CoreyConfig config;

void apply_performance_settings(GuiState &gui, EmuEnvState &emuenv) {
    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle &style = ImGui::GetStyle();
    
    if (config.disable_window_decorations) {
        io.ConfigWindowsResizeFromEdges = false;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    
    if (config.disable_transparency) {
        style.Alpha = 1.0f;
        // Force all window backgrounds to be opaque
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        style.Colors[ImGuiCol_ChildBg].w = 1.0f;
        style.Colors[ImGuiCol_PopupBg].w = 1.0f;
        style.Colors[ImGuiCol_FrameBg].w = 1.0f;
    }
    
    if (config.disable_rounded_corners) {
        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.PopupRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 0.0f;
    }
}

// Rate limiter for UI updates when game is running
bool should_update_ui() {
    if (!game_is_running.load(std::memory_order_relaxed))
        return true;
        
    static auto last_update = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count();
    
    if (elapsed >= (1000.0f / config.ui_update_rate)) {
        last_update = now;
        return true;
    }
    
    return false;
}

} // namespace corey

#endif // COREY_BUILD