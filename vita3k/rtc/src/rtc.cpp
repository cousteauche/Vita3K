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

#include <rtc/rtc.h>

#include <util/log.h>
#include <algorithm>

// Virtual Overclocking System Implementation
namespace VirtualOverclock {
    static float cpu_multiplier = 1.0f;
    static float gpu_multiplier = 1.0f;  
    static float memory_multiplier = 1.0f;
    static bool enabled = false;
    
    void set_cpu_multiplier(float multiplier) {
        cpu_multiplier = std::max(0.5f, std::min(4.0f, multiplier));
        LOG_INFO("Virtual CPU Overclock: {:.1f}x ({} MHz equivalent)", 
                 cpu_multiplier, static_cast<int>(333 * cpu_multiplier));
    }
    
    void set_gpu_multiplier(float multiplier) {
        gpu_multiplier = std::max(0.5f, std::min(4.0f, multiplier));
        LOG_INFO("Virtual GPU Overclock: {:.1f}x ({} MHz equivalent)", 
                 gpu_multiplier, static_cast<int>(222 * gpu_multiplier));
    }
    
    void set_memory_multiplier(float multiplier) {
        memory_multiplier = std::max(0.5f, std::min(4.0f, multiplier));
        LOG_INFO("Virtual Memory Overclock: {:.1f}x ({} MHz equivalent)", 
                 memory_multiplier, static_cast<int>(166 * memory_multiplier));
    }
    
    float get_cpu_multiplier() { return cpu_multiplier; }
    float get_gpu_multiplier() { return gpu_multiplier; }
    float get_memory_multiplier() { return memory_multiplier; }
    bool is_enabled() { return enabled; }
    void enable(bool enable) { 
        enabled = enable; 
        if (enable) {
            LOG_INFO("Virtual Overclocking System ENABLED");
        } else {
            LOG_INFO("Virtual Overclocking System DISABLED");
        }
    }
}

static std::uint64_t rtc_ticks_since_epoch() {
    const auto now = std::chrono::high_resolution_clock::now();
    const auto now_timepoint = std::chrono::time_point_cast<VitaClocks>(now);
    return now_timepoint.time_since_epoch().count();
}

std::uint64_t rtc_base_ticks() {
    return RTC_OFFSET + std::time(nullptr) * VITA_CLOCKS_PER_SEC - rtc_ticks_since_epoch();
}

std::uint64_t rtc_get_ticks(uint64_t base_ticks) {
    uint64_t real_ticks = base_ticks + rtc_ticks_since_epoch();
    
    // Apply CPU virtual overclocking by accelerating time perception
    if (VirtualOverclock::is_enabled() && VirtualOverclock::get_cpu_multiplier() > 1.0f) {
        // Speed up time to make CPU appear faster
        uint64_t time_delta = real_ticks - base_ticks;
        uint64_t overclock_boost = static_cast<uint64_t>(
            time_delta * (VirtualOverclock::get_cpu_multiplier() - 1.0f)
        );
        real_ticks += overclock_boost;
        
        // Debug logging (throttled to avoid spam)
        static uint64_t last_log_time = 0;
        if (real_ticks - last_log_time > VITA_CLOCKS_PER_SEC) { // Log once per second
            LOG_DEBUG("Virtual CPU Overclock active: {:.1f}x speedup applied", 
                     VirtualOverclock::get_cpu_multiplier());
            last_log_time = real_ticks;
        }
    }
    
    return real_ticks;
}

// The following functions are from PPSSPP
// Copyright (c) 2012- PPSSPP Project.

void __RtcPspTimeToTm(tm *val, const SceDateTime *pt) {
    val->tm_year = pt->year - 1900;
    val->tm_mon = pt->month - 1;
    val->tm_mday = pt->day;
    val->tm_wday = -1;
    val->tm_yday = -1;
    val->tm_hour = pt->hour;
    val->tm_min = pt->minute;
    val->tm_sec = pt->second;
    val->tm_isdst = 0;
}

void __RtcTicksToPspTime(SceDateTime *t, std::uint64_t ticks) {
    int numYearAdd = 0;
    if (ticks < VITA_CLOCKS_PER_SEC) {
        t->year = 1;
        t->month = 1;
        t->day = 1;
        t->hour = 0;
        t->minute = 0;
        t->second = 0;
        t->microsecond = ticks;
        return;
    } else if (ticks < RTC_OFFSET) {
        // Need to get a year past 1970 for gmtime
        // Add enough 400 year to pass over 1970.
        numYearAdd = (int)((RTC_OFFSET - ticks) / RTC_400_YEAR_TICKS + 1);
        ticks += RTC_400_YEAR_TICKS * numYearAdd;
    }

    while (ticks >= RTC_OFFSET + RTC_400_YEAR_TICKS) {
        ticks -= RTC_400_YEAR_TICKS;
        --numYearAdd;
    }

    time_t time = (ticks - RTC_OFFSET) / VITA_CLOCKS_PER_SEC;
    t->microsecond = ticks % VITA_CLOCKS_PER_SEC;

    tm *local = gmtime(&time);
    if (!local) {
        LOG_ERROR("Date is too high/low to handle, pretending to work.");
        return;
    }

    t->year = local->tm_year + 1900 - numYearAdd * 400;
    t->month = local->tm_mon + 1;
    t->day = local->tm_mday;
    t->hour = local->tm_hour;
    t->minute = local->tm_min;
    t->second = local->tm_sec;
}

std::uint64_t __RtcPspTimeToTicks(const SceDateTime *pt) {
    tm local;
    __RtcPspTimeToTm(&local, pt);

    std::int64_t tickOffset = 0;
    while (local.tm_year < 70) {
        tickOffset -= RTC_400_YEAR_TICKS;
        local.tm_year += 400;
    }
    while (local.tm_year >= 470) {
        tickOffset += RTC_400_YEAR_TICKS;
        local.tm_year -= 400;
    }

    time_t seconds = rtc_timegm(&local);
    std::uint64_t result = RTC_OFFSET + (std::uint64_t)seconds * VITA_CLOCKS_PER_SEC;
    result += pt->microsecond;
    return result + tickOffset;
}