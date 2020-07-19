// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/uint128.h"
#include "common/wall_clock.h"
#include <iostream>

#ifdef ARCHITECTURE_x86_64
#include "common/x64/cpu_detect.h"
#include "common/x64/native_clock.h"
#endif

namespace Common {

using base_timer = std::chrono::steady_clock;
using base_time_point = std::chrono::time_point<base_timer>;

class StandardWallClock : public WallClock {
public:
    StandardWallClock(u64 emulated_cpu_frequency, u64 emulated_clock_frequency)
        : WallClock(emulated_cpu_frequency, emulated_clock_frequency, false) {
        start_time = base_timer::now();
    }

    std::chrono::nanoseconds GetTimeNS() override {
        base_time_point current = base_timer::now();
        auto elapsed = current - start_time;
        return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
    }

    std::chrono::microseconds GetTimeUS() override {
        base_time_point current = base_timer::now();
        auto elapsed = current - start_time;
        return std::chrono::duration_cast<std::chrono::microseconds>(elapsed);
    }

    std::chrono::milliseconds GetTimeMS() override {
        base_time_point current = base_timer::now();
        auto elapsed = current - start_time;
        return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    }

    u64 GetClockCycles() override {
        std::chrono::nanoseconds time_now = GetTimeNS();
        const u128 temporary =
            Common::Multiply64Into128(time_now.count(), emulated_clock_frequency);
        return Common::Divide128On32(temporary, 1000000000).first;
    }

    u64 GetCPUCycles() override {
        std::chrono::nanoseconds time_now = GetTimeNS();
        const u128 temporary = Common::Multiply64Into128(time_now.count(), emulated_cpu_frequency);
        return Common::Divide128On32(temporary, 1000000000).first;
    }

    void Pause(bool is_paused) override {
        // Do nothing in this clock type.
    }

private:
    base_time_point start_time;
};

#ifdef ARCHITECTURE_x86_64

std::unique_ptr<WallClock> CreateBestMatchingClock(u32 emulated_cpu_frequency,
                                                   u32 emulated_clock_frequency) {
    return std::make_unique<StandardWallClock>(emulated_cpu_frequency,
                                               emulated_clock_frequency);
}

#else

std::unique_ptr<WallClock> CreateBestMatchingClock(u32 emulated_cpu_frequency,
                                                   u32 emulated_clock_frequency) {
    return std::make_unique<StandardWallClock>(emulated_cpu_frequency, emulated_clock_frequency);
}

#endif

} // namespace Common
