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
// MERCHANTABILITY and FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

#include <kernel/thread/thread_state.h>
#include <cstdint>

// Forward declarations for structures used in display functions.
struct DisplayState;
struct KernelState;
struct EmuEnvState;
struct DisplayFrameInfo;

// Global function declarations (as display.cpp uses global namespace for its functions)
void start_sync_thread(EmuEnvState &emuenv);
void wait_vblank(DisplayState &display, KernelState &kernel, const ThreadStatePtr &wait_thread, const uint64_t target_vcount, const bool is_cb);
DisplayFrameInfo *predict_next_image(EmuEnvState &emuenv, Address sync_object);
void update_prediction(EmuEnvState &emuenv, DisplayFrameInfo &frame);

// <--- ADDED: Global declaration for the frame presented notification function
void notify_frame_presented();