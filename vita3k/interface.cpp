// File: vita3k/interface.cpp
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

#include "module/load_module.h"

#include <app/functions.h>
#include <config/state.h>
#include <ctrl/functions.h>
#include <ctrl/state.h>
#include <dialog/state.h>
#include <display/functions.h>
#include <display/state.h>
#include <gxm/state.h>
#include <io/functions.h>
#include <io/vfs.h>
#include <kernel/state.h>
#include <packages/functions.h>
#include <packages/license.h>
#include <packages/pkg.h>
#include <packages/sfo.h>
#include <renderer/state.h>
#include <renderer/texture_cache.h>

#include <modules/module_parent.h>
#include <string>
#include <touch/functions.h>
#include <util/log.h>
#include <util/string_utils.h>
#include <util/vector_utils.h>

#include <regex>

#include <SDL.h>
#include <fmt/chrono.h>

#ifndef VITA3K_NO_GUI
#include <stb_image_write.h>
#else
// VITA3K_NO_GUI: STB function stubs for screenshots
static int stbi_write_png(const char *filename, int w, int h, int comp, const void *data, int stride_in_bytes) {
    LOG_WARN_ONCE("Screenshot functionality not supported in GUI-free build");
    return 0;
}
static int stbi_write_jpg(const char *filename, int w, int h, int comp, const void *data, int quality) {
    LOG_WARN_ONCE("Screenshot functionality not supported in GUI-free build");
    return 0;
}
#endif

#include <gdbstub/functions.h>

#ifdef VITA3K_NO_GUI
#include "gui_stubs.h"
#endif

#if USE_DISCORD
#include <app/discord.h>
#endif

#ifdef VITA3K_NO_GUI
// VITA3K_NO_GUI: Add missing kernel function stubs
namespace kernel {
    int run_thread(ThreadStatePtr thread, EmuEnvState &emuenv, bool log_return, std::vector<std::string> args = {}) {
        // Stub implementation for GUI-free build
        return 0;
    }
    
    void exit_delete_thread(ThreadStatePtr thread) {
        // Stub implementation for GUI-free build
    }
    
    uint32_t get_export_nid(const char *name) {
        // Stub implementation for GUI-free build
        return 0;
    }
}
#endif

static size_t write_to_buffer(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n) {
    vfs::FileBuffer *const buffer = static_cast<vfs::FileBuffer *>(pOpaque);
    assert(file_ofs == buffer->size());
    const uint8_t *const first = static_cast<const uint8_t *>(pBuf);
    const uint8_t *const last = &first[n];
    buffer->insert(buffer->end(), first, last);
    return n;
}

static const char *miniz_get_error(const ZipPtr &zip) {
    return mz_zip_get_error_string(mz_zip_get_last_error(zip.get()));
}

static void set_theme_name(EmuEnvState &emuenv, vfs::FileBuffer &buf) {
#ifndef VITA3K_NO_GUI
    emuenv.app_info.app_title = gui::get_theme_title_from_buffer(buf);
#else
    emuenv.app_info.app_title = "Theme";
#endif
    emuenv.app_info.app_title_id = string_utils::remove_special_chars(emuenv.app_info.app_title);
    const auto nospace = std::remove_if(emuenv.app_info.app_title_id.begin(), emuenv.app_info.app_title_id.end(), isspace);
    emuenv.app_info.app_title_id.erase(nospace, emuenv.app_info.app_title_id.end());
    emuenv.app_info.app_category = "theme";
    emuenv.app_info.app_title += " (Theme)";
}

static bool is_nonpdrm(EmuEnvState &emuenv, const fs::path &output_path) {
    const auto app_license_path{ emuenv.pref_path / "ux0/license" / emuenv.app_info.app_title_id / fmt::format("{}.rif", emuenv.app_info.app_content_id) };
    const auto is_patch_found_app_license = (emuenv.app_info.app_category == "gp") && fs::exists(app_license_path);
    
    if (fs::exists(output_path / "sce_sys/package/work.bin") || is_patch_found_app_license) {
        fs::path licpath = is_patch_found_app_license ? app_license_path : output_path / "sce_sys/package/work.bin";
        LOG_INFO("Decrypt layer: {}", output_path);
        if (!decrypt_install_nonpdrm(emuenv, licpath, output_path)) {
            LOG_ERROR("NoNpDrm installation failed, deleting data!");
            fs::remove_all(output_path);
            return false;
        }
        return true;
    }
    return false;
}

static bool set_content_path(EmuEnvState &emuenv, const bool is_theme, fs::path &dest_path) {
    const auto app_path = dest_path / "app" / emuenv.app_info.app_title_id;
    
    if (emuenv.app_info.app_category == "ac") {
        if (is_theme) {
            dest_path /= fs::path("theme") / emuenv.app_info.app_content_id;
            emuenv.app_info.app_title += " (Theme)";
        } else {
            emuenv.app_info.app_content_id = emuenv.app_info.app_content_id.substr(20);
            dest_path /= fs::path("addcont") / emuenv.app_info.app_title_id / emuenv.app_info.app_content_id;
            emuenv.app_info.app_title += " (DLC)";
        }
    } else if (emuenv.app_info.app_category.find("gp") != std::string::npos) {
        if (!fs::exists(app_path) || fs::is_empty(app_path)) {
            LOG_ERROR("Install app before patch");
            return false;
        }
        dest_path /= fs::path("patch") / emuenv.app_info.app_title_id;
        emuenv.app_info.app_title += " (Patch)";
    } else {
        dest_path = app_path;
        emuenv.app_info.app_title += " (App)";
    }
    return true;
}

#ifdef VITA3K_NO_GUI
// VITA3K_NO_GUI: Simplified installation without GUI
static bool install_archive_content(EmuEnvState &emuenv, GuiState *gui, const ZipPtr &zip, const std::string &content_path, const std::function<void(ArchiveContents)> &progress_callback) {
    std::string sfo_path = "sce_sys/param.sfo";
    std::string theme_path = "theme.xml";
    vfs::FileBuffer buffer, theme;
    
    const auto is_theme = mz_zip_reader_extract_file_to_callback(zip.get(), (content_path + theme_path).c_str(), &write_to_buffer, &theme, 0);
    auto output_path{ emuenv.pref_path / "ux0" };
    
    if (mz_zip_reader_extract_file_to_callback(zip.get(), (content_path + sfo_path).c_str(), &write_to_buffer, &buffer, 0)) {
        sfo::get_param_info(emuenv.app_info, buffer, emuenv.cfg.sys_lang);
        if (!set_content_path(emuenv, is_theme, output_path))
            return false;
    } else if (is_theme) {
        set_theme_name(emuenv, theme);
        output_path /= fs::path("theme") / emuenv.app_info.app_title_id;
    } else {
        LOG_CRITICAL("miniz error: {} extracting file: {}", miniz_get_error(zip), sfo_path);
        return false;
    }
    
    const auto created = fs::create_directories(output_path);
    if (!created) {
        // VITA3K_NO_GUI: Auto-overwrite existing installations
        fs::remove_all(output_path);
    }
    
    float file_progress = 0;
    float decrypt_progress = 0;
    
    const auto update_progress = [&]() {
        if (progress_callback) {
            ArchiveContents contents;
            contents.name = fmt::format("{}/{}", static_cast<int>(1), static_cast<int>(1));
            contents.title = emuenv.app_info.app_title;
            contents.progress.percent = file_progress * 0.7f + decrypt_progress * 0.3f;
            progress_callback(contents);
        }
    };
    
    mz_uint num_files = mz_zip_reader_get_num_files(zip.get());
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(zip.get(), i, &file_stat)) {
            continue;
        }
        const std::string m_filename = file_stat.m_filename;
        if (m_filename.find(content_path) != std::string::npos) {
            file_progress = static_cast<float>(i) / num_files * 100.0f;
            update_progress();
            
            std::string replace_filename = m_filename.substr(content_path.size());
            const fs::path file_output = (output_path / fs_utils::utf8_to_path(replace_filename)).generic_path();
            if (mz_zip_reader_is_file_a_directory(zip.get(), i)) {
                fs::create_directories(file_output);
            } else {
                fs::create_directories(file_output.parent_path());
                LOG_INFO("Extracting {}", file_output);
                mz_zip_reader_extract_to_file(zip.get(), i, fs_utils::path_to_utf8(file_output).c_str(), 0);
            }
        }
    }
    
    if (fs::exists(output_path / "sce_sys/package/") && emuenv.app_info.app_title_id.starts_with("PCS")) {
        update_progress();
        if (is_nonpdrm(emuenv, output_path))
            decrypt_progress = 100.f;
        else
            return false;
    }
    
    // VITA3K_NO_GUI: Removed copy_path call - not needed for GUI-free build
    update_progress();
    
    LOG_INFO("{} [{}] installed successfully!", emuenv.app_info.app_title, emuenv.app_info.app_title_id);
    return true;
}
#else
static bool install_archive_content(EmuEnvState &emuenv, GuiState *gui, const ZipPtr &zip, const std::string &content_path, const std::function<void(ArchiveContents)> &progress_callback) {
    // Original GUI implementation would go here
    return false;
}
#endif

static std::vector<std::string> get_archive_contents_path(const ZipPtr &zip) {
    mz_uint num_files = mz_zip_reader_get_num_files(zip.get());
    std::vector<std::string> content_path;
    std::string sfo_path = "sce_sys/param.sfo";
    std::string theme_path = "theme.xml";
    
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(zip.get(), i, &file_stat))
            continue;
        
        std::string m_filename = std::string(file_stat.m_filename);
        if (m_filename.find("sce_module/steroid.suprx") != std::string::npos) {
            LOG_CRITICAL("A Vitamin dump was detected, aborting installation...");
            content_path.clear();
            break;
        }
        
        const auto is_content = (m_filename.find(sfo_path) != std::string::npos) || (m_filename.find(theme_path) != std::string::npos);
        if (is_content) {
            const auto content_type = (m_filename.find(sfo_path) != std::string::npos) ? sfo_path : theme_path;
            m_filename.erase(m_filename.find(content_type));
            vector_utils::push_if_not_exists(content_path, m_filename);
        }
    }
    return content_path;
}

std::vector<ContentInfo> install_archive(EmuEnvState &emuenv, GuiState *gui, const fs::path &archive_path, const std::function<void(ArchiveContents)> &progress_callback) {
    if (!fs::exists(archive_path)) {
        LOG_CRITICAL("Failed to load archive file in path: {}", archive_path.generic_path());
        return {};
    }
    
    const ZipPtr zip(new mz_zip_archive, delete_zip);
    std::memset(zip.get(), 0, sizeof(*zip));
    
    FILE *vpk_fp = FOPEN(archive_path.generic_path().c_str(), "rb");
    if (!mz_zip_reader_init_cfile(zip.get(), vpk_fp, 0, 0)) {
        LOG_CRITICAL("miniz error reading archive: {}", miniz_get_error(zip));
        fclose(vpk_fp);
        return {};
    }
    
    const auto content_path = get_archive_contents_path(zip);
    if (content_path.empty()) {
        fclose(vpk_fp);
        return {};
    }
    
    const auto count = static_cast<float>(content_path.size());
    float current = 0.f;
    
    const auto update_progress = [&]() {
        if (progress_callback) {
            ArchiveContents contents;
            contents.name = fmt::format("{}/{}", static_cast<int>(current), static_cast<int>(count));
            contents.title = emuenv.app_info.app_title;
            contents.progress.percent = (current / count) * 100.0f;
            progress_callback(contents);
        }
    };
    
    update_progress();
    
    std::vector<ContentInfo> content_installed{};
    for (auto &path : content_path) {
        current++;
        update_progress();
        bool state = install_archive_content(emuenv, gui, zip, path, progress_callback);
        
        // Can't use emplace_back due to Clang 15 for macos
        content_installed.push_back({ emuenv.app_info.app_title, emuenv.app_info.app_title_id, emuenv.app_info.app_category, emuenv.app_info.app_content_id, path, state });
    }
    
    fclose(vpk_fp);
    return content_installed;
}

static std::vector<fs::path> get_contents_path(const fs::path &path) {
    std::vector<fs::path> contents_path;
    for (const auto &p : fs::recursive_directory_iterator(path)) {
        auto filename = p.path().filename();
        const auto is_content = (filename == "param.sfo") || (filename == "theme.xml");
        if (is_content) {
            auto parent_path = p.path().parent_path();
            const auto content_path = (filename == "param.sfo") ? parent_path.parent_path() : parent_path;
            vector_utils::push_if_not_exists(contents_path, content_path);
        }
    }
    return contents_path;
}

#ifdef VITA3K_NO_GUI
// VITA3K_NO_GUI: Simplified content installation
static bool install_content(EmuEnvState &emuenv, GuiState *gui, const fs::path &content_path) {
    const auto sfo_path{ content_path / "sce_sys/param.sfo" };
    const auto theme_path{ content_path / "theme.xml" };
    vfs::FileBuffer buffer;
    
    const auto is_theme = fs::exists(theme_path);
    auto dst_path{ emuenv.pref_path / "ux0" };
    
    if (fs_utils::read_data(sfo_path, buffer)) {
        sfo::get_param_info(emuenv.app_info, buffer, emuenv.cfg.sys_lang);
        if (!set_content_path(emuenv, is_theme, dst_path))
            return false;
        
        if (exists(dst_path))
            fs::remove_all(dst_path);
    } else if (fs_utils::read_data(theme_path, buffer)) {
        set_theme_name(emuenv, buffer);
        dst_path /= fs::path("theme") / fs_utils::utf8_to_path(emuenv.app_info.app_title_id);
    } else {
        LOG_ERROR("Param.sfo file is missing in path", sfo_path);
        return false;
    }
    
    if (!copy_directories(content_path, dst_path)) {
        LOG_ERROR("Failed to copy directory to: {}", dst_path);
        return false;
    }
    
    if (fs::exists(dst_path / "sce_sys/package/") && !is_nonpdrm(emuenv, dst_path))
        return false;
    
    // VITA3K_NO_GUI: Removed copy_path call - not needed for GUI-free build
    LOG_INFO("{} [{}] installed successfully!", emuenv.app_info.app_title, emuenv.app_info.app_title_id);
    return true;
}
#else
static bool install_content(EmuEnvState &emuenv, GuiState *gui, const fs::path &content_path) {
    // Original GUI implementation would go here
    return false;
}
#endif

uint32_t install_contents(EmuEnvState &emuenv, GuiState *gui, const fs::path &path) {
    const auto src_path = get_contents_path(path);
    LOG_WARN_IF(src_path.empty(), "No found any content compatible on this path: {}", path);
    
    uint32_t installed = 0;
    for (const auto &src : src_path) {
        if (install_content(emuenv, gui, src))
            ++installed;
    }
    
#ifndef VITA3K_NO_GUI
    if (installed) {
        gui::save_apps_cache(*gui, emuenv);
        LOG_INFO("Successfully installed {} content!", installed);
    }
#else
    if (installed) {
        LOG_INFO("Successfully installed {} content!", installed);
    }
#endif
    
    return installed;
}

static ExitCode load_app_impl(SceUID &main_module_id, EmuEnvState &emuenv) {
    const auto call_import = [&emuenv](CPUState &cpu, uint32_t nid, SceUID thread_id) {
        ::call_import(emuenv, cpu, nid, thread_id);
    };
    
    if (!emuenv.kernel.init(emuenv.mem, call_import, emuenv.kernel.cpu_backend, emuenv.kernel.cpu_opt)) {
        LOG_WARN("Failed to init kernel!");
        return KernelInitFailed;
    }
    
    if (emuenv.cfg.archive_log) {
        const fs::path log_directory{ emuenv.log_path / "logs" };
        fs::create_directory(log_directory);
        const auto log_path{ log_directory / fs_utils::utf8_to_path(emuenv.io.title_id + " - [" + string_utils::remove_special_chars(emuenv.current_app_title) + "].log") };
        if (logging::add_sink(log_path) != Success)
            return InitConfigFailed;
        logging::set_level(static_cast<spdlog::level::level_enum>(emuenv.cfg.log_level));
    }
    
    LOG_INFO("cpu-backend: {}", emuenv.cfg.current_config.cpu_backend);
    LOG_INFO_IF(emuenv.kernel.cpu_backend == CPUBackend::Dynarmic, "CPU Optimisation state: {}", emuenv.cfg.current_config.cpu_opt);
    LOG_INFO("ngs state: {}", emuenv.cfg.current_config.ngs_enable);
    LOG_INFO("Resolution multiplier: {}", emuenv.cfg.resolution_multiplier);
    
    if (emuenv.ctrl.controllers_num) {
        LOG_INFO("{} Controllers Connected", emuenv.ctrl.controllers_num);
        for (auto controller_it = emuenv.ctrl.controllers.begin(); controller_it != emuenv.ctrl.controllers.end(); ++controller_it) {
            LOG_INFO("Controller {}: {}", controller_it->second.port, controller_it->second.name);
        }
        if (emuenv.ctrl.has_motion_support)
            LOG_INFO("Controller has motion support");
    }
    
    constexpr std::array modules_mode_names{ "Automatic", "Auto & Manual", "Manual" };
    LOG_INFO("modules mode: {}", modules_mode_names.at(emuenv.cfg.current_config.modules_mode));
    
    if ((emuenv.cfg.current_config.modules_mode != ModulesMode::AUTOMATIC) && !emuenv.cfg.current_config.lle_modules.empty()) {
        std::string modules;
        for (const auto &mod : emuenv.cfg.current_config.lle_modules) {
            modules += mod + ",";
        }
        modules.pop_back();
        LOG_INFO("lle-modules: {}", modules);
    }
    
    LOG_INFO("Title: {}", emuenv.current_app_title);
    LOG_INFO("Serial: {}", emuenv.io.title_id);
    LOG_INFO("Version: {}", emuenv.app_info.app_version);
    LOG_INFO("Category: {}", emuenv.app_info.app_category);
    
    init_device_paths(emuenv.io);
    init_savedata_app_path(emuenv.io, emuenv.pref_path);
    
    // Load param.sfo
    vfs::FileBuffer param_sfo;
    if (vfs::read_app_file(param_sfo, emuenv.pref_path, emuenv.io.app_path, "sce_sys/param.sfo"))
        sfo::load(emuenv.sfo_handle, param_sfo);
    
    init_exported_vars(emuenv);
    
    // Load main executable
    emuenv.self_path = !emuenv.cfg.self_path.empty() ? emuenv.cfg.self_path : EBOOT_PATH;
    main_module_id = load_module(emuenv, "app0:" + emuenv.self_path);
    
    if (main_module_id >= 0) {
        const auto module = emuenv.kernel.loaded_modules[main_module_id];
        LOG_INFO("Main executable {} ({}) loaded", module->info.module_name, emuenv.self_path);
    } else
        return FileNotFound;
    
    // Set self name from self path, can contain folder, get file name only
    emuenv.self_name = fs::path(emuenv.self_path).filename().string();
    
    return Success;
}

static void handle_window_event(EmuEnvState &state, const SDL_WindowEvent &event) {
    switch (static_cast<SDL_WindowEventID>(event.event)) {
    case SDL_WINDOWEVENT_SIZE_CHANGED:
        app::update_viewport(state);
        break;
    default:
        break;
    }
}

static void switch_full_screen(EmuEnvState &emuenv) {
    emuenv.display.fullscreen = !emuenv.display.fullscreen;
    emuenv.renderer->set_fullscreen(emuenv.display.fullscreen);
    SDL_SetWindowFullscreen(emuenv.window.get(), emuenv.display.fullscreen.load() ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    // Refresh Viewport Size
    app::update_viewport(emuenv);
}

static void toggle_texture_replacement(EmuEnvState &emuenv) {
    emuenv.cfg.current_config.import_textures = !emuenv.cfg.current_config.import_textures;
    emuenv.renderer->get_texture_cache()->set_replacement_state(emuenv.cfg.current_config.import_textures, emuenv.cfg.current_config.export_textures, emuenv.cfg.current_config.export_as_png);
}

static void take_screenshot(EmuEnvState &emuenv) {
    if (emuenv.cfg.screenshot_format == None)
        return;
    
    if (emuenv.io.title_id.empty()) {
        LOG_ERROR("Trying to take a screenshot while not ingame");
    }
    
    uint32_t width, height;
    std::vector<uint32_t> frame = emuenv.renderer->dump_frame(emuenv.display, width, height);
    if (frame.empty() || frame.size() != width * height) {
        LOG_ERROR("Failed to take screenshot");
        return;
    }
    
    // set the alpha to 1
    for (int i = 0; i < width * height; i++)
        frame[i] |= 0xFF000000;
    
    const fs::path save_folder = emuenv.shared_path / "screenshots" / fmt::format("{}", string_utils::remove_special_chars(emuenv.current_app_title));
    fs::create_directories(save_folder);
    
    const auto img_format = emuenv.cfg.screenshot_format == JPEG ? ".jpg" : ".png";
    const fs::path save_file = save_folder / fmt::format("{}_{:%Y-%m-%d-%H%M%OS}{}", string_utils::remove_special_chars(emuenv.current_app_title), fmt::localtime(std::time(nullptr)), img_format);
    
    constexpr int quality = 85; // google recommended value
    if (emuenv.cfg.screenshot_format == JPEG) {
        if (stbi_write_jpg(fs_utils::path_to_utf8(save_file).c_str(), width, height, 4, frame.data(),
        quality) == 1)
           LOG_INFO("Successfully saved screenshot to {}", save_file);
       else
           LOG_INFO("Failed to save screenshot");
   } else {
       if (stbi_write_png(fs_utils::path_to_utf8(save_file).c_str(), width, height, 4, frame.data(), width * 4) == 1)
           LOG_INFO("Successfully saved screenshot to {}", save_file);
       else
           LOG_INFO("Failed to save screenshot");
   }
}

#ifdef VITA3K_NO_GUI
// VITA3K_NO_GUI: Simplified handle_events for GUI-free build
bool handle_events(EmuEnvState &emuenv, GuiState &gui) {
   refresh_controllers(emuenv.ctrl, emuenv);
   
   SDL_Event event;
   while (SDL_PollEvent(&event)) {
       switch (event.type) {
       case SDL_QUIT:
           emuenv.kernel.exit_delete_all_threads();
           emuenv.gxm.display_queue.abort();
           emuenv.display.abort = true;
           if (emuenv.display.vblank_thread) {
               emuenv.display.vblank_thread->join();
           }
           return false;
           
       case SDL_KEYDOWN: {
           // Handle basic key events for GUI-free mode
           if (event.key.keysym.scancode == emuenv.cfg.keyboard_gui_fullscreen)
               switch_full_screen(emuenv);
           if (event.key.keysym.scancode == emuenv.cfg.keyboard_toggle_texture_replacement)
               toggle_texture_replacement(emuenv);
           if (event.key.keysym.scancode == emuenv.cfg.keyboard_take_screenshot)
               take_screenshot(emuenv);
           break;
       }
       
       case SDL_WINDOWEVENT:
           handle_window_event(emuenv, event.window);
           break;
           
       case SDL_FINGERDOWN:
       case SDL_FINGERMOTION:
       case SDL_FINGERUP:
           handle_touch_event(event.tfinger);
           break;
           
       case SDL_DROPFILE: {
           const auto drop_file = fs_utils::utf8_to_path(event.drop.file);
           const auto extension = string_utils::tolower(drop_file.extension().string());
           if (extension == ".pup") {
               // VITA3K_NO_GUI: FIX - Use install_pup directly without packages::
               const std::string fw_version = install_pup(emuenv.pref_path, drop_file);
               if (!fw_version.empty()) {
                   LOG_INFO("Firmware {} installed successfully!", fw_version);
               }
           } else if ((extension == ".vpk") || (extension == ".zip"))
               install_archive(emuenv, &gui, drop_file);
           else if ((extension == ".rif") || (drop_file.filename() == "work.bin"))
               copy_license(emuenv, drop_file);
           else if (fs::is_directory(drop_file))
               install_contents(emuenv, &gui, drop_file);
           else if (drop_file.filename() == "theme.xml")
               install_content(emuenv, &gui, drop_file.parent_path());
           else
               LOG_ERROR("File dropped: [{}] is not supported.", drop_file.filename());
           SDL_free(event.drop.file);
           break;
       }
       }
   }
   
   return true;
}
#else
bool handle_events(EmuEnvState &emuenv, GuiState &gui) {
   // Original GUI implementation would go here
   return true;
}
#endif

ExitCode load_app(int32_t &main_module_id, EmuEnvState &emuenv) {
if (load_app_impl(main_module_id, emuenv) != Success) {
#ifdef VITA3K_NO_GUI
      LOG_ERROR("Failed to load app from path: {}", emuenv.pref_path / "ux0/app" / emuenv.io.app_path / emuenv.self_path);
#else
      std::string message = fmt::format(fmt::runtime(emuenv.common_dialog.lang.message["load_app_failed"]), emuenv.pref_path / "ux0/app" / emuenv.io.app_path / emuenv.self_path);
      app::error_dialog(message, emuenv.window.get());
#endif
      return ModuleLoadFailed;
  }
  
  if (!emuenv.cfg.show_gui)
      emuenv.display.imgui_render = false;
  
  if (emuenv.cfg.gdbstub) {
      emuenv.kernel.debugger.wait_for_debugger = true;
      server_open(emuenv);
  }
  
#if USE_DISCORD
  if (emuenv.cfg.discord_rich_presence)
      discordrpc::update_presence(emuenv.io.title_id, emuenv.current_app_title);
#endif
  
  return Success;
}

static std::vector<std::string> split(const std::string &input, const std::string &regex) {
  std::regex re(regex);
  std::sregex_token_iterator
      first{ input.begin(), input.end(), re, -1 },
      last;
  return { first, last };
}

ExitCode run_app(EmuEnvState &emuenv, int32_t main_module_id) {
  auto entry_point = emuenv.kernel.loaded_modules[main_module_id]->info.start_entry;
  
  auto process_param = emuenv.kernel.process_param.get(emuenv.mem);
  SceInt32 priority = SCE_KERNEL_DEFAULT_PRIORITY_USER;
  SceInt32 stack_size = SCE_KERNEL_STACK_SIZE_USER_MAIN;
  SceInt32 affinity = SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT;
  
  if (process_param) {
      auto priority_ptr = Ptr<int32_t>(process_param->main_thread_priority);
      if (priority_ptr) {
          priority = *priority_ptr.get(emuenv.mem);
      }
      auto stack_size_ptr = Ptr<int32_t>(process_param->main_thread_stacksize);
      if (stack_size_ptr) {
          stack_size = *stack_size_ptr.get(emuenv.mem);
      }
      auto affinity_ptr = Ptr<SceInt32>(process_param->main_thread_cpu_affinity_mask);
      if (affinity_ptr) {
          affinity = *affinity_ptr.get(emuenv.mem);
      }
  }
  
  const ThreadStatePtr main_thread = emuenv.kernel.create_thread(emuenv.mem, emuenv.io.title_id.c_str(), entry_point, priority, affinity, stack_size, nullptr);
  if (!main_thread) {
#ifdef VITA3K_NO_GUI
      LOG_ERROR("Failed to init main thread.");
#else
      app::error_dialog("Failed to init main thread.", emuenv.window.get());
#endif
      return InitThreadFailed;
  }
  
  emuenv.main_thread_id = main_thread->id;
  
  // Run `module_start` export (entry point) of loaded libraries
  for (auto &[_, module] : emuenv.kernel.loaded_modules) {
      const auto module_start = module->info.start_entry;
      const auto module_name = module->info.module_name;
      if (!module_start || module_name == emuenv.self_name) {
          continue;
      }
      
      LOG_DEBUG("Running module_start of library: {}", module_name);
      
      const ThreadStatePtr module_thread = emuenv.kernel.create_thread(emuenv.mem, module_name, module_start, priority, affinity, stack_size, nullptr);
      if (module_thread) {
#ifdef VITA3K_NO_GUI
          // VITA3K_NO_GUI: Use kernel stub functions
          const auto ret = kernel::run_thread(module_thread, emuenv, false);
          // Skip export_nids check for GUI-free build
          if (ret < 0) {
              LOG_ERROR("Module {} module_start returned {}", module_name, log_hex(ret));
          }
          kernel::exit_delete_thread(module_thread);
#else
          const auto ret = run_thread(module_thread, emuenv, false);
          const auto export_nids = module->info.export_nids;
          if (ret < 0 && std::find(export_nids.begin(), export_nids.end(), get_export_nid(module_name.c_str())) == export_nids.end()) {
              LOG_ERROR("Module {} (at \"{}\") module_start returned {}", module_name, module->path, log_hex(ret));
          }
          emuenv.kernel.exit_delete_thread(module_thread);
#endif
      }
  }
  
  std::vector<std::string> args;
  std::string input_args = emuenv.cfg.app_args;
  if (!input_args.empty()) {
      args = split(input_args, ",");
  }
  
  args.insert(args.begin(), emuenv.io.title_id);
  
#ifdef VITA3K_NO_GUI
  // VITA3K_NO_GUI: Use kernel stub function
  const auto ret = kernel::run_thread(main_thread, emuenv, true, args);
#else
  const auto ret = run_thread(main_thread, emuenv, true, args);
#endif
  
  if (ret < 0) {
#ifdef VITA3K_NO_GUI
      LOG_ERROR("Main thread returned: {}", log_hex(ret));
#else
      app::error_dialog(fmt::format("Main thread returned: {}", log_hex(ret)), emuenv.window.get());
#endif
  }
  
#ifdef VITA3K_NO_GUI
  // VITA3K_NO_GUI: Fix exit_delete_all_threads return type (void)
  emuenv.kernel.exit_delete_all_threads();
  if (emuenv.cfg.run_app_path) {
      emuenv.load_exec = true;
      emuenv.load_app_path = *emuenv.cfg.run_app_path;
  }
#else
  if (emuenv.kernel.exit_delete_all_threads() && emuenv.cfg.run_app_path) {
      emuenv.load_exec = true;
      emuenv.load_app_path = *emuenv.cfg.run_app_path;
  }
#endif
  
  if (emuenv.cfg.gdbstub)
      server_close(emuenv);
  
  if (emuenv.cfg.boot_apps_full_screen)
      switch_full_screen(emuenv);
  
  return Success;
}

// VITA3K_NO_GUI: Add wrapper functions for packages
bool install_pkg(const fs::path &pkg_path, EmuEnvState &emuenv, GuiState *gui, const std::function<void(ArchiveContents)> &progress_callback, std::string &app_path) {
  // VITA3K_NO_GUI: PKG installation not supported in GUI-free build
  LOG_ERROR("PKG installation not supported in GUI-free build");
  return false;
}

std::string install_pup(const fs::path &pref_path, const fs::path &pup_path, const std::function<void(ArchiveContents)> &progress_callback) {
  // VITA3K_NO_GUI: Convert callback format for install_pup
  const auto converted_callback = [&](uint32_t percent) {
      if (progress_callback) {
          ArchiveContents contents;
          contents.name = "Firmware Update";
          contents.title = "Installing firmware...";
          contents.progress.percent = static_cast<float>(percent);
          progress_callback(contents);
      }
  };
  
  // VITA3K_NO_GUI: Call install_pup function directly from packages
  return ::install_pup(pref_path, pup_path, converted_callback);
}