// File: vita3k/compat/include/compat/functions.h
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

#pragma once

#include <emuenv/state.h>
#ifndef VITA3K_NO_GUI
#include <gui/state.h>
#endif

namespace compat {

#ifdef VITA3K_NO_GUI
// VITA3K_NO_GUI: GUI-free function signatures
bool load_app_compat_db(EmuEnvState &emuenv);
bool update_app_compat_db(EmuEnvState &emuenv);
#else
bool load_app_compat_db(GuiState &gui, EmuEnvState &emuenv);
bool update_app_compat_db(GuiState &gui, EmuEnvState &emuenv);
#endif

} // namespace compat