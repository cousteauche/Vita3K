// File: vita3k/corey/include/corey/corey_perf.h
// VITA3K_NO_GUI: Performance optimization header
#pragma once

#ifdef COREY_BUILD

#include <atomic>
#include <chrono>

// Forward declarations
struct GuiState;
struct EmuEnvState;

namespace corey {
    extern std::atomic<bool> game_is_running;
    extern std::atomic<bool> skip_heavy_ui;
    extern std::atomic<bool> minimal_overlay;
    
    inline bool should_skip_ui() {
        return game_is_running.load(std::memory_order_relaxed) && 
               skip_heavy_ui.load(std::memory_order_relaxed);
    }
    
    // Performance settings management
    void apply_performance_settings(GuiState &gui, EmuEnvState &emuenv);
    bool should_update_ui();
    
    // Window resize handling through SDL
    void handle_sdl_window_resize(EmuEnvState &emuenv);
}

#define COREY_SKIP_IF_GAME_RUNNING() if (::corey::should_skip_ui()) return
#define COREY_SKIP_IF_GAME_RUNNING_VAL(val) if (::corey::should_skip_ui()) return val
#define COREY_RATE_LIMIT_UI() if (!::corey::should_update_ui()) return

#else

#define COREY_SKIP_IF_GAME_RUNNING()
#define COREY_SKIP_IF_GAME_RUNNING_VAL(val)
#define COREY_RATE_LIMIT_UI()

#endif // COREY_BUILD