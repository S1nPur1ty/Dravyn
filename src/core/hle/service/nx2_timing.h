// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include "common/common_types.h"

namespace Service::Nx2Timing {

constexpr u64 MarioKartWorldProgramId = 0x0400C3F00006E000ULL;
constexpr std::chrono::nanoseconds NormalFrameInterval{16'666'667};

inline std::chrono::nanoseconds PresentInterval(u64 program_id, bool limited, u16 speed) {
    // This title advances gameplay with presentation. Turbo/unlimited must not accelerate it.
    if (program_id == MarioKartWorldProgramId) {
        return NormalFrameInterval;
    }
    if (!limited || speed == 0) {
        return std::chrono::nanoseconds::zero();
    }
    const s64 denominator = 60LL * speed;
    return std::chrono::nanoseconds{(100'000'000'000LL + denominator - 1) / denominator};
}

inline void WaitForPresentDeadline(std::chrono::steady_clock::time_point deadline) {
    using Clock = std::chrono::steady_clock;
#ifdef __APPLE__
    // macOS timer wakeups can overshoot the vblank. Sleep for the bulk of the wait,
    // then spend at most 500 us checking the clock, never returning before the deadline.
    constexpr auto guard = std::chrono::microseconds{500};
    if (Clock::now() < deadline - guard) {
        std::this_thread::sleep_until(deadline - guard);
    }
    while (Clock::now() < deadline) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
#else
    while (Clock::now() < deadline) {
        std::this_thread::sleep_until(deadline);
    }
#endif
}

class PresentPacer {
public:
    using Clock = std::chrono::steady_clock;

    Clock::time_point Deadline(Clock::time_point now, std::chrono::nanoseconds interval) const {
        return last_present ? std::max(now, *last_present + interval) : now;
    }

    void RecordPresent(Clock::time_point now) {
        last_present = now;
    }

private:
    std::optional<Clock::time_point> last_present;
};

} // namespace Service::Nx2Timing
