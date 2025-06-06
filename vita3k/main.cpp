// File: vita3k/src/main.cpp
// VITA3K_NO_GUI: Modified for GUI-free build

// Vita3K emulator project
// Copyright (C) 2025 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "interface.h"

#include <app/functions.h>
#include <config/functions.h>
#include <config/version.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <include/cpu.h>
#include <include/environment.h>
#include <io/state.h>
#include <kernel/state.h>
#include <modules/module_parent.h>
#include <packages/functions.h>
#include <packages/license.h>
#include <packages/pkg.h>
#include <packages/sfo.h>
#include <renderer/functions.h>
#include <renderer/shaders.h>
#include <renderer/state.h>
#include <shader/spirv_recompiler.h>
#include <util/log.h>
#include <util/string_utils.h>

#ifdef VITA3K_NO_GUI
#include "gui_stubs.h"
#endif

#if USE_DISCORD
#include <app/discord.h>
#endif

#ifdef _WIN32
#include <combaseapi.h>
#include <process.h>
#define SDL_MAIN_HANDLED
#endif

#include <SDL.h>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <tracy/Tracy.hpp>

static void run_execv(char *argv[], EmuEnvState &emuenv) {
    char const *args[10];
    args[0] = argv[0];
    args[1] = "-a";
    args[2] = "true";
    if (!emuenv.load_app_path.empty()) {
        args[3] = "-r";
        args[4] = emuenv.load_app_path.data();
        if (!emuenv.load_exec_path.empty()) {
            args[5] = "--self";
            args[6] = emuenv.load_exec_path.data();
            if (!emuenv.load_exec_argv.empty()) {
                args[7] = "--app-args";
                args[8] = emuenv.load_exec_argv.data();
                args[9] = nullptr;
            } else
                args[7] = nullptr;
        } else
            args[5] = nullptr;
    } else
        args[3] = nullptr;

        // Execute the emulator again with some arguments
#ifdef _WIN32
    FreeConsole();
    _execv(argv[0], args);
#elif defined(__unix__) || defined(__APPLE__) && defined(__MACH__)
    execv(argv[0], const_cast<char *const *>(args));
#endif
}

int main(int argc, char *argv[]) {
    ZoneScoped; // Tracy - Track main function scope
    Root root_paths;

    app::init_paths(root_paths);

    if (logging::init(root_paths, true) != Success)
        return InitConfigFailed;

    // Check admin privs before init starts to avoid creating of file as other user by accident
    bool adminPriv = false;
#ifdef _WIN32
    // https://stackoverflow.com/questions/8046097/how-to-check-if-a-process-has-the-administrative-rights
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
            adminPriv = Elevation.TokenIsElevated;
        }
    }
    if (hToken) {
        CloseHandle(hToken);
    }
#else
    auto uid = getuid();
    auto euid = geteuid();

    // if either effective uid or uid is the one of the root user assume running as root.
    // else if euid and uid are different then permissions errors can happen if its running
    // as a completely different user than the uid/euid
    if (uid == 0 || euid == 0 || uid != euid)
        adminPriv = true;
#endif

    if (adminPriv) {
        LOG_CRITICAL("PLEASE. DO NOT RUN VITA3K AS ADMIN OR WITH ADMIN PRIVILEGES.");
    }

    Config cfg{};
    EmuEnvState emuenv;
    const auto config_err = config::init_config(cfg, argc, argv, root_paths);

    fs::create_directories(cfg.get_pref_path());

    if (config_err != Success) {
        if (config_err == QuitRequested) {
            if (cfg.recompile_shader_path.has_value()) {
                LOG_INFO("Recompiling {}", *cfg.recompile_shader_path);
                shader::convert_gxp_to_glsl_from_filepath(*cfg.recompile_shader_path);
            }
            if (cfg.delete_title_id.has_value()) {
                LOG_INFO("Deleting title id {}", *cfg.delete_title_id);
                fs::remove_all(cfg.get_pref_path() / "ux0/app" / *cfg.delete_title_id);
                fs::remove_all(cfg.get_pref_path() / "ux0/addcont" / *cfg.delete_title_id);
                fs::remove_all(cfg.get_pref_path() / "ux0/user/00/savedata" / *cfg.delete_title_id);
                fs::remove_all(root_paths.get_cache_path() / "shaders" / *cfg.delete_title_id);
            }
            if (cfg.pup_path.has_value()) {
                LOG_INFO("Installing firmware file {}", *cfg.pup_path);
                install_pup(cfg.get_pref_path(), *cfg.pup_path, [](uint32_t progress) {
                    LOG_INFO("Firmware installation progress: {}%", progress);
                });
            }
            if (cfg.pkg_path.has_value() && cfg.pkg_zrif.has_value()) {
                LOG_INFO("Installing pkg from {} ", *cfg.pkg_path);
                emuenv.cache_path = root_paths.get_cache_path().generic_path();
                emuenv.pref_path = cfg.get_pref_path();
                auto pkg_path = fs_utils::utf8_to_path(*cfg.pkg_path);
                install_pkg(pkg_path, emuenv, *cfg.pkg_zrif, [](float) {});
            }
            return Success;
        }
        LOG_ERROR("Failed to initialise config");
        return InitConfigFailed;
    }

#ifdef _WIN32
    {
        auto res = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        LOG_ERROR_IF(res == S_FALSE, "Failed to initialize COM Library");
    }
#endif

#ifdef VITA3K_NO_GUI
    // VITA3K_NO_GUI: Initialize SDL for GUI-free build (minimal graphics context required)
    cfg.console = true;
    cfg.show_gui = false;
    // VITA3K_NO_GUI: CRITICAL FIX - Do NOT call logging::init again - already initialized at line 101
    
    // VITA3K_NO_GUI: Initialize SDL with minimal requirements for renderer
    std::atexit(SDL_Quit);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        LOG_ERROR("SDL initialisation failed for GUI-free build: {}", SDL_GetError());
        return SDLInitFailed;
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#else
    if (cfg.console) {
        cfg.show_gui = false;
        if (logging::init(root_paths, false) != Success)
            return InitConfigFailed;
    } else {
        std::atexit(SDL_Quit);

        // Enable HIDAPI rumble for DS4/DS
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");

        // Enable Switch controller
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1");

        // Enable High DPI support
#ifdef _WIN32
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
            app::error_dialog("SDL initialisation failed.");
            return SDLInitFailed;
        }
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    }
#endif

    LOG_INFO("{}", window_title);
    LOG_INFO("OS: {}", CppCommon::Environment::OSVersion());
    LOG_INFO("CPU: {} | {} Threads | {} GHz", CppCommon::CPU::Architecture(), CppCommon::CPU::LogicalCores(), static_cast<float>(CppCommon::CPU::ClockSpeed()) / 1000.f);
    LOG_INFO("Available ram memory: {} MiB", SDL_GetSystemRAM());
    
    // VITA3K_NO_GUI: Debug logging to identify config issue
    LOG_INFO("DEBUG: cfg.run_app_path = {}", cfg.run_app_path ? *cfg.run_app_path : "EMPTY");

    app::AppRunType run_type = app::AppRunType::Unknown;
    if (cfg.run_app_path)
        run_type = app::AppRunType::Extracted;
    
    // VITA3K_NO_GUI: Debug logging to verify run_type
    LOG_INFO("DEBUG: run_type = {}", static_cast<int>(run_type));

    if (!app::init(emuenv, cfg, root_paths)) {
#ifdef VITA3K_NO_GUI
        LOG_ERROR("Emulated environment initialization failed.");
#else
        app::error_dialog("Emulated environment initialization failed.", emuenv.window.get());
#endif
        return 1;
    }

#ifdef VITA3K_NO_GUI
    // VITA3K_NO_GUI: Skip GUI initialization and controller setup
    init_libraries(emuenv);
    GuiState gui; // Stub instance
#else
    if (emuenv.cfg.controller_binds.empty() || (emuenv.cfg.controller_binds.size() != 15))
        gui::reset_controller_binding(emuenv);

    init_libraries(emuenv);

    GuiState gui;
    if (!cfg.console) {
        gui::pre_init(gui, emuenv);
        if (!emuenv.cfg.initial_setup) {
            while (!emuenv.cfg.initial_setup) {
                if (handle_events(emuenv, gui)) {
                    gui::draw_begin(gui, emuenv);
                    gui::draw_initial_setup(gui, emuenv);
                    gui::draw_end(gui);
                    emuenv.renderer->swap_window(emuenv.window.get());
                } else
                    return QuitRequested;
            }
            run_execv(argv, emuenv);
        }
        gui::init(gui, emuenv);
        app::update_viewport(emuenv);
    }
#endif

    if (cfg.content_path.has_value()) {
#ifdef VITA3K_NO_GUI
        auto gui_ptr = nullptr;
#else
        auto gui_ptr = cfg.console ? nullptr : &gui;
#endif
        const auto extention = string_utils::tolower(cfg.content_path->extension().string());
        const auto is_archive = (extention == ".vpk") || (extention == ".zip");
        const auto is_rif = (extention == ".rif") || (extention == "work.bin");
        const auto is_directory = fs::is_directory(*cfg.content_path);

        const auto content_is_app = [&]() {
            std::vector<ContentInfo> contents_info = install_archive(emuenv, gui_ptr, *cfg.content_path);
            const auto content_index = std::find_if(contents_info.begin(), contents_info.end(), [&](const ContentInfo &c) {
                return c.category == "gd";
            });
            if ((content_index != contents_info.end()) && content_index->state) {
                emuenv.app_info.app_title_id = content_index->title_id;
                return true;
            }

            return false;
        };
        if ((is_archive && content_is_app()) || (is_directory && (install_contents(emuenv, gui_ptr, *cfg.content_path) == 1) && (emuenv.app_info.app_category == "gd")))
            run_type = app::AppRunType::Extracted;
        else {
            if (is_rif)
                copy_license(emuenv, *cfg.content_path);
            else if (!is_archive && !is_directory)
                LOG_ERROR("File dropped: [{}] is not supported.", *cfg.content_path);

            emuenv.cfg.content_path.reset();
#ifndef VITA3K_NO_GUI
            if (!cfg.console)
                gui::init_home(gui, emuenv);
#endif
        }
    }

    if (run_type == app::AppRunType::Extracted) {
#ifdef VITA3K_NO_GUI
        // VITA3K_NO_GUI: Ensure app_path is set correctly for GUI-free builds
        emuenv.io.app_path = *cfg.run_app_path;
        LOG_INFO("Initializing app: {}", emuenv.io.app_path);
#else
        emuenv.io.app_path = cfg.run_app_path ? *cfg.run_app_path : emuenv.app_info.app_title_id;
        gui::init_user_app(gui, emuenv, emuenv.io.app_path);
#endif
        if (emuenv.cfg.run_app_path.has_value())
            emuenv.cfg.run_app_path.reset();
        else if (emuenv.cfg.content_path.has_value())
            emuenv.cfg.content_path.reset();
    }

#ifdef VITA3K_NO_GUI
    // VITA3K_NO_GUI: Skip GUI main loop, require app to be specified
    if (run_type == app::AppRunType::Unknown) {
        LOG_ERROR("No application specified for GUI-free build. Use -r <app_path> to run an application.");
        return 1;
    }
#else
    if (!cfg.console) {
#if USE_DISCORD
        auto discord_rich_presence_old = emuenv.cfg.discord_rich_presence;
#endif

        std::chrono::system_clock::time_point present = std::chrono::system_clock::now();
        std::chrono::system_clock::time_point later = std::chrono::system_clock::now();
        const double frame_time = 1000.0 / 60.0;
        // Application not provided via argument, show app selector
        while (run_type == app::AppRunType::Unknown) {
            // get the current time & get the time we worked for
            present = std::chrono::system_clock::now();
            std::chrono::duration<double, std::milli> work_time = present - later;
            // check if we are running faster than ~60fps (16.67ms)
            if (work_time.count() < frame_time) {
                // sleep for delta time.
                std::chrono::duration<double, std::milli> delta_ms(frame_time - work_time.count());
                auto delta_ms_duration = std::chrono::duration_cast<std::chrono::milliseconds>(delta_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delta_ms_duration.count()));
            }
            // save the later time
            later = std::chrono::system_clock::now();

            if (handle_events(emuenv, gui)) {
                ZoneScopedN("UI rendering"); // Tracy - Track UI rendering loop scope
                gui::draw_begin(gui, emuenv);

#if USE_DISCORD
                discordrpc::update_init_status(emuenv.cfg.discord_rich_presence, &discord_rich_presence_old);
#endif
                gui::draw_vita_area(gui, emuenv);
                gui::draw_ui(gui, emuenv);

                gui::draw_end(gui);
                emuenv.renderer->swap_window(emuenv.window.get());
                FrameMark; // Tracy - Frame end mark for UI rendering loop
            } else {
                return QuitRequested;
            }

            if (!emuenv.io.app_path.empty()) {
                run_type = app::AppRunType::Extracted;
                gui.vita_area.home_screen = false;
                gui.vita_area.live_area_screen = false;
            }
        }
    }
#endif

    // When backend render is changed before boot app, reboot emu in new backend render and run app
    if (emuenv.renderer && emuenv.renderer->current_backend != emuenv.backend_renderer) {
        emuenv.load_app_path = emuenv.io.app_path;
        run_execv(argv, emuenv);
        return Success;
    }

#ifdef VITA3K_NO_GUI
    // VITA3K_NO_GUI: Create app data for GUI-free build
    std::string app_ver = "1.00";
    std::string category = "gd";
    bool addcont = false;
    std::string content_id = "";
    bool savedata = false;
    std::string title = "Unknown App";
    std::string stitle = "App";
    std::string title_id = emuenv.io.app_path;
    
    // Try to read app info from param.sfo if available
    vfs::FileBuffer param_sfo;
    if (vfs::read_app_file(param_sfo, emuenv.pref_path, emuenv.io.app_path, "sce_sys/param.sfo")) {
        sfo::SfoAppInfo sfo_info;
        sfo::get_param_info(sfo_info, param_sfo, emuenv.cfg.sys_lang);
        if (!sfo_info.app_title.empty()) title = sfo_info.app_title;
        if (!sfo_info.app_title_id.empty()) title_id = sfo_info.app_title_id;
        if (!sfo_info.app_version.empty()) app_ver = sfo_info.app_version;
        if (!sfo_info.app_category.empty()) category = sfo_info.app_category;
    }
    
    emuenv.app_info.app_version = app_ver;
    emuenv.app_info.app_category = category;
    emuenv.io.addcont = addcont;
    emuenv.io.content_id = content_id;
    emuenv.io.savedata = savedata;
    emuenv.current_app_title = title;
    emuenv.app_info.app_short_title = stitle;
    emuenv.io.title_id = title_id;
    
    LOG_INFO("Loading app: {} ({})", emuenv.current_app_title, emuenv.io.title_id);
#else
    gui::set_config(gui, emuenv, emuenv.io.app_path);

    const auto APP_INDEX = gui::get_app_index(gui, emuenv.io.app_path);
    emuenv.app_info.app_version = APP_INDEX->app_ver;
    emuenv.app_info.app_category = APP_INDEX->category;
    emuenv.io.addcont = APP_INDEX->addcont;
    emuenv.io.content_id = APP_INDEX->content_id;
    emuenv.io.savedata = APP_INDEX->savedata;
    emuenv.current_app_title = APP_INDEX->title;
    emuenv.app_info.app_short_title = APP_INDEX->stitle;
    emuenv.io.title_id = APP_INDEX->title_id;
#endif

    // Check license for PS App Only
    get_license(emuenv, emuenv.io.title_id, emuenv.io.content_id);

#ifndef VITA3K_NO_GUI
    // VITA3K_NO_GUI: Move console waiting logic to non-GUI builds only
    if (cfg.console) {
        auto main_thread = emuenv.kernel.get_thread(emuenv.main_thread_id);
        auto lock = std::unique_lock<std::mutex>(main_thread->mutex);
        main_thread->status_cond.wait(lock, [&]() {
            return main_thread->status == ThreadStatus::dormant;
        });
        return Success;
    } else {
        gui.imgui_state->do_clear_screen = false;
    }
#endif

#ifndef VITA3K_NO_GUI
    gui::init_app_background(gui, emuenv, emuenv.io.app_path);
    gui::update_last_time_app_used(gui, emuenv, emuenv.io.app_path);
#endif

    if (!app::late_init(emuenv)) {
#ifdef VITA3K_NO_GUI
        LOG_ERROR("Failed to initialize Vita3K");
#else
        app::error_dialog("Failed to initialize Vita3K", emuenv.window.get());
#endif
        return 1;
    }

#ifndef VITA3K_NO_GUI
    const auto draw_app_background = [](GuiState &gui, EmuEnvState &emuenv) {
        const auto pos_min = ImVec2(emuenv.logical_viewport_pos.x, emuenv.logical_viewport_pos.y);
        const auto pos_max = ImVec2(pos_min.x + emuenv.logical_viewport_size.x, pos_min.y + emuenv.logical_viewport_size.y);

        if (gui.apps_background.contains(emuenv.io.app_path))
            // Display application background
            ImGui::GetBackgroundDrawList()->AddImage(gui.apps_background[emuenv.io.app_path], pos_min, pos_max);
        // Application background not found
        else
            gui::draw_background(gui, emuenv);
    };
#endif

    int32_t main_module_id;
    {
        const auto err = load_app(main_module_id, emuenv);
        if (err != Success)
            return err;
    }
#ifndef VITA3K_NO_GUI
    gui.vita_area.information_bar = false;
#endif

    // Pre-Compile Shaders
    emuenv.renderer->set_app(emuenv.io.title_id.c_str(), emuenv.self_name.c_str());
    if (renderer::get_shaders_cache_hashs(*emuenv.renderer) && cfg.shader_cache) {
#ifdef VITA3K_NO_GUI
        LOG_INFO("Compiling {} cached shaders...", emuenv.renderer->shaders_cache_hashs.size());
#else
        SDL_SetWindowTitle(emuenv.window.get(), fmt::format("{} | {} ({}) | Please wait, compiling shaders...", window_title, emuenv.current_app_title, emuenv.io.title_id).c_str());
#endif
        for (const auto &hash : emuenv.renderer->shaders_cache_hashs) {
#ifdef VITA3K_NO_GUI
            emuenv.renderer->precompile_shader(hash);
#else
            handle_events(emuenv, gui);
            gui::draw_begin(gui, emuenv);
            draw_app_background(gui, emuenv);

            emuenv.renderer->precompile_shader(hash);
            gui::draw_pre_compiling_shaders_progress(gui, emuenv, static_cast<uint32_t>(emuenv.renderer->shaders_cache_hashs.size()));

            gui::draw_end(gui);
            emuenv.renderer->swap_window(emuenv.window.get());
#endif
        }
    }
    {
        const auto err = run_app(emuenv, main_module_id);
        if (err != Success)
            return err;
    }

#ifdef VITA3K_NO_GUI
    LOG_INFO("Application {} ({}) loaded successfully", emuenv.current_app_title, emuenv.io.title_id);
    
    // VITA3K_NO_GUI: Test basic window functionality first
    LOG_INFO("Starting GUI-free build with basic window management...");
    LOG_INFO("Entering main emulation loop...");
    
    bool running = true;
    int frame_counter = 0;
    
    while (running && !emuenv.load_exec) {
        ZoneScopedN("Game rendering"); // Tracy - Track game rendering loop scope
        
        // VITA3K_NO_GUI: Essential SDL event handling
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                LOG_INFO("Received SDL_QUIT, exiting emulation loop");
                running = false;
                break;
            }
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    LOG_INFO("Received window close, exiting emulation loop");
                    running = false;
                    break;
                }
                if (event.window.event == SDL_WINDOWEVENT_EXPOSED || 
                    event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    LOG_INFO("Window needs refresh");
                    app::update_viewport(emuenv);
                }
            }
        }
        
        // VITA3K_NO_GUI: BASIC window refresh - just clear to black
        // This should prevent the ghost window issue
        if (emuenv.renderer) {
            // Just swap buffers to keep window alive - no game rendering yet
            emuenv.renderer->swap_window(emuenv.window.get());
        }
        
        // Update frame counter
        frame_counter++;
        
        // VITA3K_NO_GUI: Log progress periodically
        if (frame_counter % 300 == 0) { // Every 5 seconds at 60fps
            LOG_INFO("GUI-free window test running... Frame: {}", frame_counter);
        }
        
        // VITA3K_NO_GUI: Timeout for testing
        if (frame_counter > 600) { // 10 seconds at 60fps
            LOG_INFO("Window test completed - 10 seconds without ghost window");
            running = false;
            break;
        }
        
        // Maintain 60fps timing
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        
        FrameMark; // Tracy - Frame end mark for game rendering loop
    }
    
    LOG_INFO("Window test ended successfully after {} frames", frame_counter);
#else
    SDL_SetWindowTitle(emuenv.window.get(), fmt::format("{} | {} ({}) | Please wait, loading...", window_title, emuenv.current_app_title, emuenv.io.title_id).c_str());

    while (handle_events(emuenv, gui) && (emuenv.frame_count == 0) && !emuenv.load_exec) {
        ZoneScopedN("Game loading"); // Tracy - Track game loading loop scope
        // Driver acto!
        renderer::process_batches(*emuenv.renderer.get(), emuenv.renderer->features, emuenv.mem, emuenv.cfg);

        const SceFVector2 viewport_pos = { emuenv.drawable_viewport_pos.x, emuenv.drawable_viewport_pos.y };
        const SceFVector2 viewport_size = { emuenv.drawable_viewport_size.x, emuenv.drawable_viewport_size.y };
        emuenv.renderer->render_frame(viewport_pos, viewport_size, emuenv.display, emuenv.gxm, emuenv.mem);

        gui::draw_begin(gui, emuenv);
        gui::draw_common_dialog(gui, emuenv);
        draw_app_background(gui, emuenv);

        gui::draw_end(gui);
        emuenv.renderer->swap_window(emuenv.window.get());
        FrameMark; // Tracy - Frame end mark for game loading loop
    }

    while (handle_events(emuenv, gui) && !emuenv.load_exec) {
        ZoneScopedN("Game rendering"); // Tracy - Track game rendering loop scope
        // Driver acto!
        renderer::process_batches(*emuenv.renderer.get(), emuenv.renderer->features, emuenv.mem, emuenv.cfg);

        const SceFVector2 viewport_pos = { emuenv.drawable_viewport_pos.x, emuenv.drawable_viewport_pos.y };
        const SceFVector2 viewport_size = { emuenv.drawable_viewport_size.x, emuenv.drawable_viewport_size.y };
        emuenv.renderer->render_frame(viewport_pos, viewport_size, emuenv.display, emuenv.gxm, emuenv.mem);
        // Calculate FPS
        app::calculate_fps(emuenv);

        // Set shaders compiled display
        gui::set_shaders_compiled_display(gui, emuenv);

        gui::draw_begin(gui, emuenv);
        if (!emuenv.kernel.is_threads_paused())
            gui::draw_common_dialog(gui, emuenv);
        gui::draw_vita_area(gui, emuenv);

        if (emuenv.cfg.performance_overlay && !emuenv.kernel.is_threads_paused() && (emuenv.common_dialog.status != SCE_COMMON_DIALOG_STATUS_RUNNING)) {
            ImGui::PushFont(gui.vita_font[emuenv.current_font_level]);
            gui::draw_perf_overlay(gui, emuenv);
           ImGui::PopFont();
       }

       if (emuenv.cfg.current_config.show_touchpad_cursor && !emuenv.kernel.is_threads_paused())
           gui::draw_touchpad_cursor(emuenv);

       if (emuenv.display.imgui_render) {
          gui::draw_ui(gui, emuenv);
      }

      gui::draw_end(gui);
      emuenv.renderer->swap_window(emuenv.window.get());
      FrameMark; // Tracy - Frame end mark for game rendering loop
  }
#endif

#ifdef _WIN32
  CoUninitialize();
#endif

  emuenv.renderer->preclose_action();
#ifdef VITA3K_NO_GUI
  app::destroy(emuenv, nullptr);
#else
  app::destroy(emuenv, gui.imgui_state.get());
#endif

  if (emuenv.load_exec)
      run_execv(argv, emuenv);

  return Success;
}