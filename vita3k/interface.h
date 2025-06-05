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

#pragma once

#include <util/exit_code.h>
#include <util/fs.h>

#include <miniz.h>

#include <memory>
#include <optional>
#include <vector>

struct GuiState;
struct EmuEnvState;

typedef std::shared_ptr<mz_zip_archive> ZipPtr;

inline void delete_zip(mz_zip_archive *zip) {
    mz_zip_reader_end(zip);
    delete zip;
}

// VITA3K_NO_GUI: Restored correct ArchiveContents structure from original
struct ArchiveContents {
    std::string name;
    std::string title;
    struct Progress {
        float percent;
    } progress;
};

// VITA3K_NO_GUI: ContentInfo definition for GUI-free build
struct ContentInfo {
    std::string title;
    std::string title_id;
    std::string category;
    std::string content_id;
    fs::path path;
    bool state;
};

bool handle_events(EmuEnvState &emuenv, GuiState &gui);

ExitCode load_app(int32_t &main_module_id, EmuEnvState &emuenv);
ExitCode run_app(EmuEnvState &emuenv, int32_t main_module_id);

std::vector<ContentInfo> install_archive(EmuEnvState &emuenv, GuiState *gui, const fs::path &archive_path, const std::function<void(ArchiveContents)> &progress_callback = {});
uint32_t install_contents(EmuEnvState &emuenv, GuiState *gui, const fs::path &path);

bool install_pkg(const fs::path &pkg_path, EmuEnvState &emuenv, GuiState *gui, const std::function<void(ArchiveContents)> &progress_callback, std::string &app_path);
bool copy_license(EmuEnvState &emuenv, const fs::path &license_path);

// REMOVED: Duplicate install_pup declaration to resolve ambiguity with packages/functions.h