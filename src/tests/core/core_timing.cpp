// SPDX-FileCopyrightText: 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bitset>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "core/core.h"
#include "core/core_timing.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/hle/service/nx2_timing.h"

namespace {
// Numbers are chosen randomly to make sure the correct one is given.
constexpr std::array<u64, 5> calls_order{{2, 0, 1, 4, 3}};
std::array<s64, 5> delays{};
std::bitset<5> callbacks_ran_flags;
u64 expected_callback = 0;

template <unsigned int IDX>
std::optional<std::chrono::nanoseconds> HostCallbackTemplate(s64 time,
                                                             std::chrono::nanoseconds ns_late) {
    static_assert(IDX < callbacks_ran_flags.size(), "IDX out of range");
    callbacks_ran_flags.set(IDX);
    delays[IDX] = ns_late.count();
    ++expected_callback;
    return std::nullopt;
}

struct ScopeInit final {
    ScopeInit() {
        callbacks_ran_flags.reset();
        core_timing.SetMulticore(true);
        core_timing.Initialize([]() {});
    }

    Core::Timing::CoreTiming core_timing;
};

u64 TestTimerSpeed(Core::Timing::CoreTiming& core_timing) {
    const u64 start = core_timing.GetGlobalTimeNs().count();
    volatile u64 placebo = 0;
    for (std::size_t i = 0; i < 1000; i++) {
        placebo = placebo + core_timing.GetGlobalTimeNs().count();
    }
    const u64 end = core_timing.GetGlobalTimeNs().count();
    return end - start;
}

} // Anonymous namespace

TEST_CASE("Switch 2 counter keeps its advertised rate across speed changes", "[normal-speed]") {
    const bool was_switch2 = Settings::CurrentProgramIsSwitch2();
    const auto frequency = Common::g_wall_clock.GetGuestCNTFRQ();
    const auto clock = Settings::values.cpu_clock.GetValue();
    const auto sync = Settings::values.sync_core_speed.GetValue();
    const auto speed = Settings::values.speed_limit.GetValue();
    const auto mode = Settings::values.current_speed_mode.GetValue();
    SCOPE_EXIT {
        Settings::SetCurrentProgramIsSwitch2(was_switch2);
        Common::g_wall_clock.SetGuestCNTFRQ(frequency);
        Settings::values.cpu_clock.SetValue(clock);
        Settings::values.sync_core_speed.SetValue(sync);
        Settings::values.speed_limit.SetValue(speed);
        Settings::values.current_speed_mode.SetValue(mode);
    };
    Settings::SetCurrentProgramIsSwitch2(true);
    Settings::values.current_speed_mode.SetValue(Settings::SpeedMode::Standard);
    Core::Timing::CoreTiming timing;
    timing.SetMulticore(false);
    timing.cpu_ticks = Common::WallClock::CPUTickFreq;
    for (const u64 hz : {19'200'000ULL, 31'250'000ULL}) {
        Common::g_wall_clock.SetGuestCNTFRQ(hz);
        for (const auto cpu_clock : {Settings::CpuClock::Normal, Settings::CpuClock::Boost,
                                     Settings::CpuClock::Overclock}) {
            Settings::values.cpu_clock.SetValue(cpu_clock);
            for (const bool sync_speed : {false, true}) {
                Settings::values.sync_core_speed.SetValue(sync_speed);
                for (const u16 speed_percent : {0, 50, 100, 200}) {
                    Settings::values.speed_limit.SetValue(speed_percent);
                    REQUIRE(timing.GetClockTicks() == hz);
                }
            }
        }
    }
    // The multicore/JIT callback must also use the unscaled wall counter.
    timing.SetMulticore(true);
    const auto before = Common::g_wall_clock.GetCNTPCT();
    const auto ticks = timing.GetClockTicks();
    const auto after = Common::g_wall_clock.GetCNTPCT();
    REQUIRE(ticks >= before);
    REQUIRE(ticks <= after);
}

TEST_CASE("Display ticks skip missed intervals while other timers retain catch-up", "[normal-speed]") {
    using namespace std::chrono_literals;
    for (const auto mode : {Core::Timing::LoopingEventMode::CatchUp,
                            Core::Timing::LoopingEventMode::SkipMissed}) {
        Core::Timing::CoreTiming timing;
        timing.SetMulticore(false);
        unsigned calls = 0;
        auto event = Core::Timing::CreateEvent("late periodic event", [&](s64, auto) {
            ++calls;
            return std::nullopt;
        });
        timing.ScheduleLoopingEvent(10ms, 10ms, event, false, mode);
        timing.cpu_ticks = Common::WallClock::CPUTickFreq / 4; // 250 ms stall
        REQUIRE(timing.Advance() == 260'000'000);
        REQUIRE(calls == (mode == Core::Timing::LoopingEventMode::SkipMissed ? 1 : 25));
        const auto delivered = calls;
        REQUIRE(timing.Advance() == 260'000'000);
        REQUIRE(calls == delivered);
        timing.UnscheduleEvent(event);
        REQUIRE_FALSE(timing.Advance().has_value());
    }
}

TEST_CASE("Display timer starts next interval after callback and respects cancellation",
          "[normal-speed]") {
    using namespace std::chrono_literals;
    Core::Timing::CoreTiming timing;
    timing.SetMulticore(false);
    unsigned calls = 0;
    std::shared_ptr<Core::Timing::EventType> event;
    event = Core::Timing::CreateEvent("slow display callback", [&](s64, auto) {
        ++calls;
        timing.cpu_ticks += Common::WallClock::CPUTickFreq / 10;
        if (calls == 2) {
            timing.UnscheduleEvent(event, Core::Timing::UnscheduleEventType::NoWait);
        }
        return std::optional{20ms};
    });
    timing.ScheduleLoopingEvent(10ms, 10ms, event, false,
                               Core::Timing::LoopingEventMode::SkipMissed);
    timing.cpu_ticks = Common::WallClock::CPUTickFreq / 10;
    REQUIRE(timing.Advance() == 220'000'000);
    REQUIRE(calls == 1);
    timing.cpu_ticks = Common::WallClock::CPUTickFreq / 2;
    REQUIRE_FALSE(timing.Advance().has_value());
    REQUIRE(calls == 2);
}

TEST_CASE("Mario Kart World pacing stays normal even with turbo or unlimited selected",
          "[normal-speed]") {
    using namespace Service::Nx2Timing;
    for (const bool limited : {false, true}) {
        for (const u16 speed : {0, 50, 100, 200, 9999}) {
            REQUIRE(PresentInterval(MarioKartWorldProgramId, limited, speed) == NormalFrameInterval);
        }
    }
    REQUIRE(PresentInterval(1, false, 100).count() == 0);
    REQUIRE(PresentInterval(1, true, 0).count() == 0);
    REQUIRE(PresentInterval(1, true, 100) == NormalFrameInterval);
    REQUIRE(PresentInterval(1, true, 200).count() == 8'333'334);
}

TEST_CASE("Presentation never catches up after stalls or late wakes", "[normal-speed]") {
    using namespace std::chrono_literals;
    using namespace Service::Nx2Timing;
    PresentPacer pacer;
    auto last = PresentPacer::Clock::time_point{};
    REQUIRE(pacer.Deadline(last, NormalFrameInterval) == last);
    pacer.RecordPresent(last);
    for (unsigned frame = 0; frame < 1000; ++frame) {
        const auto work = frame % 17 == 0 ? 500ms : 1ms;
        const auto now = last + work;
        const auto deadline = pacer.Deadline(now, NormalFrameInterval);
        REQUIRE(deadline >= now);
        REQUIRE(deadline - last >= NormalFrameInterval);
        if (work == 500ms) {
            REQUIRE(deadline == now);
        }
        last = deadline + (frame % 3 == 0 ? 4ms : 0ms);
        pacer.RecordPresent(last);
        REQUIRE(pacer.Deadline(last, NormalFrameInterval) == last + NormalFrameInterval);
    }
}

TEST_CASE("CoreTiming[BasicOrder]", "[core]") {
    ScopeInit guard;
    auto& core_timing = guard.core_timing;
    std::vector<std::shared_ptr<Core::Timing::EventType>> events{
        Core::Timing::CreateEvent("callbackA", HostCallbackTemplate<0>),
        Core::Timing::CreateEvent("callbackB", HostCallbackTemplate<1>),
        Core::Timing::CreateEvent("callbackC", HostCallbackTemplate<2>),
        Core::Timing::CreateEvent("callbackD", HostCallbackTemplate<3>),
        Core::Timing::CreateEvent("callbackE", HostCallbackTemplate<4>),
    };

    expected_callback = 0;

    core_timing.SyncPause(true);

    const u64 one_micro = 1000U;
    for (std::size_t i = 0; i < events.size(); i++) {
        const u64 order = calls_order[i];
        const auto future_ns = std::chrono::nanoseconds{static_cast<s64>(i * one_micro + 100)};

        core_timing.ScheduleEvent(future_ns, events[order]);
    }
    /// test pause
    REQUIRE(callbacks_ran_flags.none());

    core_timing.Pause(false); // No need to sync

    while (core_timing.HasPendingEvents())
        ;

    REQUIRE(callbacks_ran_flags.all());

    for (std::size_t i = 0; i < delays.size(); i++) {
        const double delay = static_cast<double>(delays[i]);
        const double micro = delay / 1000.0f;
        const double mili = micro / 1000.0f;
        printf("HostTimer Pausing Delay[%zu]: %.3f %.6f\n", i, micro, mili);
    }
}

TEST_CASE("CoreTiming[BasicOrderNoPausing]", "[core]") {
    ScopeInit guard;
    auto& core_timing = guard.core_timing;
    std::vector<std::shared_ptr<Core::Timing::EventType>> events{
        Core::Timing::CreateEvent("callbackA", HostCallbackTemplate<0>),
        Core::Timing::CreateEvent("callbackB", HostCallbackTemplate<1>),
        Core::Timing::CreateEvent("callbackC", HostCallbackTemplate<2>),
        Core::Timing::CreateEvent("callbackD", HostCallbackTemplate<3>),
        Core::Timing::CreateEvent("callbackE", HostCallbackTemplate<4>),
    };

    core_timing.SyncPause(true);
    core_timing.SyncPause(false);

    expected_callback = 0;

    const u64 start = core_timing.GetGlobalTimeNs().count();
    const u64 one_micro = 1000U;

    for (std::size_t i = 0; i < events.size(); i++) {
        const u64 order = calls_order[i];
        const auto future_ns = std::chrono::nanoseconds{static_cast<s64>(i * one_micro + 100)};
        core_timing.ScheduleEvent(future_ns, events[order]);
    }

    const u64 end = core_timing.GetGlobalTimeNs().count();
    const double scheduling_time = static_cast<double>(end - start);
    const double timer_time = static_cast<double>(TestTimerSpeed(core_timing));

    while (core_timing.HasPendingEvents())
        ;

    REQUIRE(callbacks_ran_flags.all());

    for (std::size_t i = 0; i < delays.size(); i++) {
        const double delay = static_cast<double>(delays[i]);
        const double micro = delay / 1000.0f;
        const double mili = micro / 1000.0f;
        printf("HostTimer No Pausing Delay[%zu]: %.3f %.6f\n", i, micro, mili);
    }

    const double micro = scheduling_time / 1000.0f;
    const double mili = micro / 1000.0f;
    printf("HostTimer No Pausing Scheduling Time: %.3f %.6f\n", micro, mili);
    printf("HostTimer No Pausing Timer Time: %.3f %.6f\n", timer_time / 1000.f,
           timer_time / 1000000.f);
}
