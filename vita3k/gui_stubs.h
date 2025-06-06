// File: vita3k/gui_stubs.h
// VITA3K_NO_GUI: GUI function stubs for GUI-free build

#pragma once

#ifdef VITA3K_NO_GUI

#include <string>
#include <map>

struct GuiState {
    struct {
        bool home_screen = true;
        bool live_area_screen = false;
        bool information_bar = true;
    } vita_area;
    
    struct {
        bool do_clear_screen = true;
    } *imgui_state = nullptr;
    
    std::map<std::string, void*> apps_background;
    void* vita_font[1] = {nullptr};
    int current_font_level = 0;
};

struct AppInfo {
    std::string app_ver = "1.00";
    std::string category = "gd";
    bool addcont = false;
    bool savedata = false;
    std::string title = "Application";
    std::string stitle = "App";
    std::string title_id;
    std::string content_id;
    std::string trophy;
    tm updated{};
    size_t size = 0;
};

// Forward declarations
struct EmuEnvState;

namespace gui {
    inline void reset_controller_binding(EmuEnvState &emuenv) {}
    inline void pre_init(GuiState &gui, EmuEnvState &emuenv) {}
    inline void draw_initial_setup(GuiState &gui, EmuEnvState &emuenv) {}
    inline void init(GuiState &gui, EmuEnvState &emuenv) {}
    inline void init_home(GuiState &gui, EmuEnvState &emuenv) {}
    inline void init_user_app(GuiState &gui, EmuEnvState &emuenv, const std::string &app_path) {}
    inline void draw_begin(GuiState &gui, EmuEnvState &emuenv) {}
    inline void draw_end(GuiState &gui) {}
    inline void draw_vita_area(GuiState &gui, EmuEnvState &emuenv) {}
    inline void draw_ui(GuiState &gui, EmuEnvState &emuenv) {}
    inline void set_config(GuiState &gui, EmuEnvState &emuenv, const std::string &app_path) {}
    inline void init_app_background(GuiState &gui, EmuEnvState &emuenv, const std::string &app_path) {}
    inline void update_last_time_app_used(GuiState &gui, EmuEnvState &emuenv, const std::string &app_path) {}
    inline void draw_background(GuiState &gui, EmuEnvState &emuenv) {}
    inline void draw_pre_compiling_shaders_progress(GuiState &gui, EmuEnvState &emuenv, uint32_t total) {}
    inline void draw_common_dialog(GuiState &gui, EmuEnvState &emuenv) {}
    inline void set_shaders_compiled_display(GuiState &gui, EmuEnvState &emuenv) {}
    inline void draw_perf_overlay(GuiState &gui, EmuEnvState &emuenv) {}
    inline void draw_touchpad_cursor(EmuEnvState &emuenv) {}
    inline void save_apps_cache(GuiState &gui, EmuEnvState &emuenv) {}
    inline void update_notice_info(GuiState &gui, EmuEnvState &emuenv, const std::string &type) {}
    inline std::string get_theme_title_from_buffer(const vfs::FileBuffer &buf) { return "Theme"; }
    inline void close_and_run_new_app(EmuEnvState &emuenv, const std::string &app_path) {}
    inline void browse_users_management(GuiState &gui, EmuEnvState &emuenv, uint32_t button) {}
    inline void browse_pages_manual(GuiState &gui, EmuEnvState &emuenv, uint32_t button) {}
    inline void browse_home_apps_list(GuiState &gui, EmuEnvState &emuenv, uint32_t button) {}
    inline void browse_live_area_apps_list(GuiState &gui, EmuEnvState &emuenv, uint32_t button) {}
    inline void browse_save_data_dialog(GuiState &gui, EmuEnvState &emuenv, uint32_t button) {}
    inline void open_live_area(GuiState &gui, EmuEnvState &emuenv, const std::string &app_path) {}
    inline bool get_sys_apps_state(GuiState &gui) { return true; }
    inline void close_system_app(GuiState &gui, EmuEnvState &emuenv) {}
    inline void update_time_app_used(GuiState &gui, EmuEnvState &emuenv, const std::string &app_path) {}
    inline void get_modules_list(GuiState &gui, EmuEnvState &emuenv) {}
    inline void init_theme(GuiState &gui, EmuEnvState &emuenv, const std::string &theme_id) {}
    inline void draw_reinstall_dialog(int *status, GuiState &gui, EmuEnvState &emuenv) { *status = 2; } // UNK_STATE
    
    inline auto get_app_index(GuiState &gui, const std::string &app_path) {
        static AppInfo stub_app;
        stub_app.app_ver = "1.00";
        stub_app.category = "gd";
        stub_app.addcont = false;
        stub_app.savedata = false;
        stub_app.title = "Application";
        stub_app.stitle = "App";
        stub_app.title_id = app_path;
        return &stub_app;
    }
    
    inline auto get_live_area_current_open_apps_list_index(GuiState &gui, const std::string &app_path) { 
        return std::vector<std::string>{}.end(); 
    }
}

#endif // VITA3K_NO_GUI