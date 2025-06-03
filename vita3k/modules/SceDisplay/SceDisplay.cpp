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

#include "SceDisplay.h"

#include <display/functions.h>
#include <display/state.h>
#include <io/state.h>
#include <kernel/state.h>
#include <packages/functions.h>
#include <renderer/state.h>
#include <util/lock_and_find.h>
#include <util/types.h>

#include <util/tracy.h>
#include <atomic>
#include <deque>
#include <cmath>

TRACY_MODULE_NAME(SceDisplay);

// Advanced frame pacing system for 60fps patches
namespace {
    struct FramePacer {
        std::atomic<uint32_t> pending_frames{0};
        std::atomic<uint64_t> total_frames{0};
        std::atomic<uint64_t> presented_frames{0};
        std::chrono::steady_clock::time_point last_frame_time;
        std::chrono::steady_clock::time_point start_time;
        std::mutex timing_mutex;
        std::deque<std::chrono::microseconds> frame_times;
        static constexpr size_t FRAME_TIME_HISTORY = 60;
        
        // Adaptive pacing parameters
        std::atomic<int> target_pending{2};      // Optimal queue depth
        std::atomic<int> sleep_us{100};          // Base sleep time
        std::atomic<bool> use_precise_timing{true};
        
        // Performance metrics
        std::atomic<double> current_fps{0.0};
        std::atomic<double> frame_time_variance{0.0};
        std::atomic<int> stutter_frames{0};
        
        FramePacer() : 
            last_frame_time(std::chrono::steady_clock::now()),
            start_time(std::chrono::steady_clock::now()) {}
        
        void on_frame_submit() {
            pending_frames++;
            total_frames++;
            
            // Track frame timing
            auto now = std::chrono::steady_clock::now();
            auto delta = std::chrono::duration_cast<std::chrono::microseconds>(
                now - last_frame_time);
            
            {
                std::lock_guard<std::mutex> lock(timing_mutex);
                frame_times.push_back(delta);
                if (frame_times.size() > FRAME_TIME_HISTORY) {
                    frame_times.pop_front();
                }
                
                // Detect stutters (frames taking > 20ms)
                if (delta.count() > 20000) {
                    stutter_frames++;
                }
            }
            
            last_frame_time = now;
            
            // Dynamic adjustment based on queue depth
            int pending = pending_frames.load();
            if (pending > target_pending + 2) {
                // Way too many pending, aggressive wait
                sleep_us = std::min(2000, sleep_us.load() + 100);
            } else if (pending > target_pending) {
                // Slightly too many, gentle increase
                sleep_us = std::min(1000, sleep_us.load() + 25);
            } else if (pending < target_pending && sleep_us > 50) {
                // Too few, decrease wait
                sleep_us = std::max(50, sleep_us.load() - 50);
            }
            
            // Update FPS every 30 frames
            if (total_frames % 30 == 0) {
                update_metrics();
            }
        }
        
        void on_frame_present() {
            if (pending_frames > 0) {
                pending_frames--;
            }
            presented_frames++;
        }
        
        void adaptive_wait() {
            int pending = pending_frames.load();
            
            if (use_precise_timing) {
                // Target 16.67ms frame time for 60fps
                static constexpr auto target_frame_time = std::chrono::microseconds(16667);
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    now - last_frame_time);
                
                if (elapsed < target_frame_time && pending <= target_pending) {
                    // We're running too fast and queue is healthy
                    auto wait_time = target_frame_time - elapsed;
                    
                    // High precision wait
                    if (wait_time > std::chrono::microseconds(1000)) {
                        std::this_thread::sleep_for(wait_time - std::chrono::microseconds(500));
                    }
                    
                    // Spin wait for precision
                    while (std::chrono::steady_clock::now() - last_frame_time < target_frame_time) {
                        std::this_thread::yield();
                    }
                    return;
                }
            }
            
            // Fallback: Dynamic sleep based on queue depth
            if (pending > target_pending) {
                // Progressive backoff
                int sleep_time = sleep_us * (1 + (pending - target_pending) / 2);
                
                // Mix of sleep and yield for better granularity
                if (sleep_time > 500) {
                    std::this_thread::sleep_for(std::chrono::microseconds(sleep_time / 2));
                    std::this_thread::yield();
                    std::this_thread::sleep_for(std::chrono::microseconds(sleep_time / 2));
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(sleep_time));
                }
            } else {
                // Queue is healthy, just yield
                std::this_thread::yield();
            }
        }
        
        void update_metrics() {
            std::lock_guard<std::mutex> lock(timing_mutex);
            
            if (frame_times.size() < 10) return;
            
            // Calculate average frame time and variance
            double sum = 0.0;
            double sum_sq = 0.0;
            for (const auto& ft : frame_times) {
                double ms = ft.count() / 1000.0;
                sum += ms;
                sum_sq += ms * ms;
            }
            
            double avg = sum / frame_times.size();
            double variance = (sum_sq / frame_times.size()) - (avg * avg);
            frame_time_variance = std::sqrt(variance);
            
            // Calculate FPS from actual presentation rate
            auto now = std::chrono::steady_clock::now();
            auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start_time).count();
            
            if (total_time > 0) {
                current_fps = (presented_frames.load() * 1000.0) / total_time;
            }
            
            // Adjust precision timing based on variance
            if (frame_time_variance > 5.0) {
                // High variance, disable precision timing
                use_precise_timing = false;
            } else if (frame_time_variance < 2.0) {
                // Low variance, enable precision timing
                use_precise_timing = true;
            }
        }
        
        void log_stats() const {
            LOG_INFO("WipEout Frame Pacing Stats:");
            LOG_INFO("  FPS: {:.1f}, Pending: {}, Sleep: {}us", 
                     current_fps.load(), pending_frames.load(), sleep_us.load());
            LOG_INFO("  Frame variance: {:.2f}ms, Stutters: {}", 
                     frame_time_variance.load(), stutter_frames.load());
            LOG_INFO("  Precision timing: {}", use_precise_timing.load() ? "ON" : "OFF");
        }
    };
    
    // Global pacer instance
    FramePacer g_wipeout_pacer;
}

static int display_wait(EmuEnvState &emuenv, SceUID thread_id, int vcount, const bool is_since_setbuf, const bool is_cb) {
    const auto &thread = emuenv.kernel.get_thread(thread_id);
    int original_vcount = vcount;

    // WipEout 2048 Enhanced 60FPS Mode
    if (emuenv.display.fps_hack && 
        (emuenv.io.title_id == "PCSF00007" || emuenv.io.title_id == "PCSA00015")) {
        
        if (is_since_setbuf) {
            static int skip_count = 0;
            skip_count++;
            
            if (skip_count % 60 == 0) {
                LOG_INFO("WipEout 60FPS: Bypassed {} frame waits", skip_count);
                g_wipeout_pacer.log_stats();
            }
            
            // Return immediately - this is the key to 60fps
            return SCE_DISPLAY_ERROR_OK;
        }
        
        // For non-SetFrameBuf waits, minimal wait
        vcount = 0;
    }

    // Original fps_hack for other games
    if (emuenv.display.fps_hack && vcount > 1) {
        vcount = 1;
    }

    uint64_t target_vcount;
    if (is_since_setbuf) {
        target_vcount = emuenv.display.last_setframe_vblank_count + vcount;
    } else {
        const uint64_t next_vsync = emuenv.display.vblank_count + 1;
        const uint64_t min_vsync = thread->last_vblank_waited + vcount;
        thread->last_vblank_waited = std::max(next_vsync, min_vsync);
        target_vcount = thread->last_vblank_waited;
    }

    wait_vblank(emuenv.display, emuenv.kernel, thread, target_vcount, is_cb);

    if (emuenv.display.abort.load())
        return SCE_DISPLAY_ERROR_NO_PIXEL_DATA;

    return SCE_DISPLAY_ERROR_OK;
}

EXPORT(SceInt32, _sceDisplayGetFrameBuf, SceDisplayFrameBuf *pFrameBuf, SceDisplaySetBufSync sync, uint32_t *pFrameBuf_size) {
    TRACY_FUNC(_sceDisplayGetFrameBuf, pFrameBuf, sync, pFrameBuf_size);
    if (pFrameBuf->size != sizeof(SceDisplayFrameBuf) && pFrameBuf->size != sizeof(SceDisplayFrameBuf2))
        return RET_ERROR(SCE_DISPLAY_ERROR_INVALID_VALUE);
    else if (sync != SCE_DISPLAY_SETBUF_NEXTFRAME && sync != SCE_DISPLAY_SETBUF_IMMEDIATE)
        return RET_ERROR(SCE_DISPLAY_ERROR_INVALID_UPDATETIMING);

    const std::lock_guard<std::mutex> guard(emuenv.display.display_info_mutex);

    // ignore value of sync in GetFrameBuf
    DisplayFrameInfo *info = &emuenv.display.sce_frame;

    pFrameBuf->base = info->base;
    pFrameBuf->pitch = info->pitch;
    pFrameBuf->pixelformat = info->pixelformat;
    pFrameBuf->width = info->image_size.x;
    pFrameBuf->height = info->image_size.y;

    return SCE_DISPLAY_ERROR_OK;
}

EXPORT(int, _sceDisplayGetFrameBufInternal) {
    TRACY_FUNC(_sceDisplayGetFrameBufInternal);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, _sceDisplayGetMaximumFrameBufResolution, SceInt32 *width, SceInt32 *height) {
    TRACY_FUNC(_sceDisplayGetMaximumFrameBufResolution, width, height);
    if (!width || !height)
        return 0;
    if (emuenv.cfg.pstv_mode) {
        *width = 1920;
        *height = 1088;
    } else {
        // PSVita does this exact same check
        auto &title_id = emuenv.io.title_id;
        bool cond = (title_id == "PCSG80001")
            || (title_id == "PCSG80007")
            || (title_id == "PCSG00318")
            || (title_id == "PCSG00319")
            || (title_id == "PCSG00320")
            || (title_id == "PCSG00321")
            || (title_id == "PCSH00059");
        if (cond) {
            *width = 960;
            *height = 544;

        } else {
            *width = 1280;
            *height = 725;
        }
    }
    return 0;
}

EXPORT(int, _sceDisplayGetResolutionInfoInternal) {
    TRACY_FUNC(_sceDisplayGetResolutionInfoInternal);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, _sceDisplaySetFrameBuf, const SceDisplayFrameBuf *pFrameBuf, SceDisplaySetBufSync sync, uint32_t *pFrameBuf_size) {
    TRACY_FUNC(_sceDisplaySetFrameBuf, pFrameBuf, sync, pFrameBuf_size);
    
    // Track frame submission for WipEout
    if ((emuenv.io.title_id == "PCSF00007" || emuenv.io.title_id == "PCSA00015")) {
        g_wipeout_pacer.on_frame_submit();
    }
    
    // WipEout 2048 FPS tracking
    if ((emuenv.io.title_id == "PCSF00007" || emuenv.io.title_id == "PCSA00015")) {
        static int frame_count = 0;
        static auto last_time = std::chrono::high_resolution_clock::now();
        
        frame_count++;
        
        if (frame_count % 60 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
            float fps = (60.0f * 1000.0f) / duration;
            
            LOG_INFO("WipEout FPS: {:.1f} (sync: {}, base: 0x{:X})", 
                     fps, static_cast<int>(sync), 
                     pFrameBuf ? reinterpret_cast<uintptr_t>(pFrameBuf->base.get(emuenv.mem)) : 0);
            last_time = now;
        }
    }
    
    if (!pFrameBuf)
        return SCE_DISPLAY_ERROR_OK;
    if (pFrameBuf->size != sizeof(SceDisplayFrameBuf) && pFrameBuf->size != sizeof(SceDisplayFrameBuf2)) {
        return RET_ERROR(SCE_DISPLAY_ERROR_INVALID_VALUE);
    }
    if (!pFrameBuf->base) {
        return RET_ERROR(SCE_DISPLAY_ERROR_INVALID_ADDR);
    }
    if (pFrameBuf->pitch < pFrameBuf->width) {
        return RET_ERROR(SCE_DISPLAY_ERROR_INVALID_PITCH);
    }
    if (pFrameBuf->pixelformat != SCE_DISPLAY_PIXELFORMAT_A8B8G8R8) {
        return RET_ERROR(SCE_DISPLAY_ERROR_INVALID_PIXELFORMAT);
    }
    if (sync != SCE_DISPLAY_SETBUF_NEXTFRAME && sync != SCE_DISPLAY_SETBUF_IMMEDIATE) {
        return RET_ERROR(SCE_DISPLAY_ERROR_INVALID_UPDATETIMING);
    }
    if ((pFrameBuf->width < 480) || (pFrameBuf->height < 272) || (pFrameBuf->pitch < 480))
        return RET_ERROR(SCE_DISPLAY_ERROR_INVALID_RESOLUTION);

    if (sync == SCE_DISPLAY_SETBUF_IMMEDIATE) {
        STUBBED("SCE_DISPLAY_SETBUF_IMMEDIATE is not supported");
    }

    DisplayFrameInfo &info = emuenv.display.sce_frame;

    info.base = pFrameBuf->base;
    info.pitch = pFrameBuf->pitch;
    info.pixelformat = pFrameBuf->pixelformat;
    info.image_size.x = pFrameBuf->width;
    info.image_size.y = pFrameBuf->height;
    update_prediction(emuenv, info);

    emuenv.display.last_setframe_vblank_count = emuenv.display.vblank_count.load();
    emuenv.frame_count++;

    // Enhanced adaptive frame pacing for WipEout
    if (emuenv.display.fps_hack && 
        (emuenv.io.title_id == "PCSF00007" || emuenv.io.title_id == "PCSA00015")) {
        
        // Use adaptive waiting instead of simple yield
        g_wipeout_pacer.adaptive_wait();
        
        // Additional optimization: prefetch next frame data
        __builtin_prefetch(pFrameBuf->base.get(emuenv.mem), 0, 3);
    }

#ifdef TRACY_ENABLE
    FrameMarkNamed("SCE frame buffer");
#endif

    return SCE_DISPLAY_ERROR_OK;
}

EXPORT(int, _sceDisplaySetFrameBufForCompat) {
    TRACY_FUNC(_sceDisplaySetFrameBufForCompat);
    return UNIMPLEMENTED();
}

EXPORT(int, _sceDisplaySetFrameBufInternal) {
    TRACY_FUNC(_sceDisplaySetFrameBufInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceDisplayGetPrimaryHead) {
    TRACY_FUNC(sceDisplayGetPrimaryHead);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceDisplayGetRefreshRate, float *pFps) {
    TRACY_FUNC(sceDisplayGetRefreshRate, pFps);
    
    // Enhanced: Report variable refresh rate for WipEout when fps_hack is on
    if (emuenv.display.fps_hack && 
        (emuenv.io.title_id == "PCSF00007" || emuenv.io.title_id == "PCSA00015")) {
        
        // Report current achieved FPS to encourage game to adapt
        float current = g_wipeout_pacer.current_fps.load();
        if (current > 55.0f && current < 65.0f) {
            *pFps = current;  // Report actual FPS when close to 60
        } else {
            *pFps = 119.8801f;  // Otherwise report 120Hz to encourage 60fps target
        }
        
        LOG_INFO("WipEout: Reporting {:.1f}Hz refresh rate", *pFps);
    } else {
        *pFps = 59.94005f;  // Standard Vita refresh rate
    }
    return 0;
}

EXPORT(SceInt32, sceDisplayGetVcount) {
    TRACY_FUNC(sceDisplayGetVcount);
    
    // For WipEout with fps_hack, simulate faster vcount increment
    if (emuenv.display.fps_hack && 
        (emuenv.io.title_id == "PCSF00007" || emuenv.io.title_id == "PCSA00015")) {
        // Double the vcount rate to simulate 120Hz display
        return static_cast<SceInt32>((emuenv.display.vblank_count.load() * 2) & 0xFFFF);
    }
    
    return static_cast<SceInt32>(emuenv.display.vblank_count.load()) & 0xFFFF;
}

EXPORT(int, sceDisplayGetVcountInternal) {
    TRACY_FUNC(sceDisplayGetVcountInternal);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceDisplayRegisterVblankStartCallback, SceUID uid) {
    TRACY_FUNC(sceDisplayRegisterVblankStartCallback, uid);

    const auto cb = lock_and_find(uid, emuenv.kernel.callbacks, emuenv.kernel.mutex);
    if (!cb)
        return RET_ERROR(SCE_DISPLAY_ERROR_INVALID_VALUE);

    std::lock_guard<std::mutex> guard(emuenv.display.mutex);
    emuenv.display.vblank_callbacks[uid] = cb;

    return 0;
}

EXPORT(SceInt32, sceDisplayUnregisterVblankStartCallback, SceUID uid) {
    TRACY_FUNC(sceDisplayUnregisterVblankStartCallback, uid);
    if (!emuenv.display.vblank_callbacks.contains(uid))
        return RET_ERROR(SCE_DISPLAY_ERROR_INVALID_VALUE);

    std::lock_guard<std::mutex> guard(emuenv.display.mutex);
    emuenv.display.vblank_callbacks.erase(uid);

    return 0;
}

EXPORT(SceInt32, sceDisplayWaitSetFrameBuf) {
    TRACY_FUNC(sceDisplayWaitSetFrameBuf);
    return display_wait(emuenv, thread_id, 1, true, false);
}

EXPORT(SceInt32, sceDisplayWaitSetFrameBufCB) {
    TRACY_FUNC(sceDisplayWaitSetFrameBufCB);
    return display_wait(emuenv, thread_id, 1, true, true);
}

EXPORT(SceInt32, sceDisplayWaitSetFrameBufMulti, SceUInt vcount) {
    TRACY_FUNC(sceDisplayWaitSetFrameBufMulti, vcount);
    return display_wait(emuenv, thread_id, static_cast<int>(vcount), true, false);
}

EXPORT(SceInt32, sceDisplayWaitSetFrameBufMultiCB, SceUInt vcount) {
    TRACY_FUNC(sceDisplayWaitSetFrameBufMultiCB, vcount);
    return display_wait(emuenv, thread_id, static_cast<int>(vcount), true, true);
}

EXPORT(SceInt32, sceDisplayWaitVblankStart) {
    TRACY_FUNC(sceDisplayWaitVblankStart);
    return display_wait(emuenv, thread_id, 1, false, false);
}

EXPORT(SceInt32, sceDisplayWaitVblankStartCB) {
    TRACY_FUNC(sceDisplayWaitVblankStartCB);
    return display_wait(emuenv, thread_id, 1, false, true);
}

EXPORT(SceInt32, sceDisplayWaitVblankStartMulti, SceUInt vcount) {
    TRACY_FUNC(sceDisplayWaitVblankStartMulti, vcount);
    return display_wait(emuenv, thread_id, static_cast<int>(vcount), false, false);
}

EXPORT(SceInt32, sceDisplayWaitVblankStartMultiCB, SceUInt vcount) {
    TRACY_FUNC(sceDisplayWaitVblankStartMultiCB, vcount);
    return display_wait(emuenv, thread_id, static_cast<int>(vcount), false, true);
}

EXPORT(SceInt32, sceDisplayWaitVblankStartMultiCB, SceUInt vcount) {
    TRACY_FUNC(sceDisplayWaitVblankStartMultiCB, vcount);
    return display_wait(emuenv, thread_id, static_cast<int>(vcount), false, true);
}

} // namespace MODULES

// Renderer callback integration (must be outside module namespace)
namespace display {
    void notify_frame_presented() {
        g_wipeout_pacer.on_frame_present();
    }
}