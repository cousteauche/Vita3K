// File: vita3k/corey/src/corey_window.cpp
// VITA3K_NO_GUI: SDL window management for Corey

#ifdef COREY_BUILD

#include <corey/corey_perf.h>
#include <emuenv/state.h>
#include <display/state.h>
#include <renderer/state.h>
#include <SDL.h>

namespace corey {

void handle_sdl_window_resize(EmuEnvState &emuenv) {
    // Get current window size from SDL
    int window_width, window_height;
    SDL_GetWindowSize(emuenv.window.get(), &window_width, &window_height);
    
    // Update viewport without ImGui involvement
    emuenv.drawable_size.x = window_width;
    emuenv.drawable_size.y = window_height;
    
    // Calculate logical viewport maintaining aspect ratio
    const float vita_aspect = 960.0f / 544.0f;
    const float window_aspect = static_cast<float>(window_width) / static_cast<float>(window_height);
    
    if (window_aspect > vita_aspect) {
        // Window is wider than Vita aspect ratio
        emuenv.logical_viewport_size.y = window_height;
        emuenv.logical_viewport_size.x = window_height * vita_aspect;
        emuenv.logical_viewport_pos.x = (window_width - emuenv.logical_viewport_size.x) / 2.0f;
        emuenv.logical_viewport_pos.y = 0.0f;
    } else {
        // Window is taller than Vita aspect ratio
        emuenv.logical_viewport_size.x = window_width;
        emuenv.logical_viewport_size.y = window_width / vita_aspect;
        emuenv.logical_viewport_pos.x = 0.0f;
        emuenv.logical_viewport_pos.y = (window_height - emuenv.logical_viewport_size.y) / 2.0f;
    }
    
    // Update GUI scale
    emuenv.gui_scale.x = emuenv.logical_viewport_size.x / 960.0f;
    emuenv.gui_scale.y = emuenv.logical_viewport_size.y / 544.0f;
    
    // VITA3K_USE_COREY: Removed set_screen_size call - renderer handles resize through SDL events
    // The renderer will pick up the new window size from SDL events automatically
}

} // namespace corey

#endif // COREY_BUILD