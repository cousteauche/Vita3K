// VITA3K_NO_GUI: Performance optimization header
#pragma once

#ifdef COREY_BUILD

#include <atomic>
#include <chrono>

namespace corey {
    extern std::atomic<bool> game_is_running;
    extern std::atomic<bool> skip_heavy_ui;
    extern std::atomic<bool> minimal_overlay;
    
    inline bool should_skip_ui() {
        return game_is_running.load(std::memory_order_relaxed) && 
               skip_heavy_ui.load(std::memory_order_relaxed);
    }
}

#define COREY_SKIP_IF_GAME_RUNNING() if (::corey::should_skip_ui()) return
#define COREY_SKIP_IF_GAME_RUNNING_VAL(val) if (::corey::should_skip_ui()) return val

#else

#define COREY_SKIP_IF_GAME_RUNNING()
#define COREY_SKIP_IF_GAME_RUNNING_VAL(val)

#endif // COREY_BUILD
