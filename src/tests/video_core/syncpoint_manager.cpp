// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include "video_core/host1x/syncpoint_manager.h"

using Tegra::Host1x::SyncpointManager;

TEST_CASE("GPU syncpoint indices cannot escape their storage", "[video_core][syncpoint]") {
    SyncpointManager manager;
    for (const u32 id : {192U, 1675U, 65535U, ~0U}) {
        bool called = false;
        manager.IncrementGuest(id);
        manager.IncrementHost(id);
        REQUIRE(manager.GetGuestSyncpointValue(id) == 0);
        REQUIRE(manager.GetHostSyncpointValue(id) == 0);
        REQUIRE_FALSE(manager.IsReadyGuest(id, 0));
        REQUIRE_FALSE(manager.IsReadyHost(id, 0));
        manager.RegisterGuestAction(id, 0, [&] { called = true; });
        manager.RegisterHostAction(id, 0, [&] { called = true; });
        manager.DeregisterGuestAction(id, {});
        manager.DeregisterHostAction(id, {});
        manager.WaitGuest(id, 0);
        manager.WaitHost(id, 0);
        REQUIRE_FALSE(manager.WaitGuestInterruptible(id, 0, [] { return false; }, {}));
        REQUIRE_FALSE(called);
    }
    // Invalid commands must not corrupt the last valid fence or its callbacks.
    bool called = false;
    manager.RegisterGuestAction(191, 1, [&] { called = true; });
    manager.IncrementGuest(191);
    REQUIRE(called);
    REQUIRE(manager.GetGuestSyncpointValue(191) == 1);
    REQUIRE(manager.GetGuestSyncpointValue(0) == 0);
}

TEST_CASE("A four second GPU stall does not complete a frame fence", "[video_core][syncpoint]") {
    SyncpointManager manager;
    std::atomic<bool> warned{};
    std::atomic<bool> returned{};
    std::atomic<bool> cancel{};
    bool ready = false;
    std::jthread waiter([&] {
        ready = manager.WaitGuestInterruptible(1, 2, [&] { return cancel.load(); },
                                               [&] { warned = true; });
        returned = true;
    });
    manager.IncrementGuest(1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{6};
    while (!warned && !returned && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    const bool saw_warning = warned.load();
    const bool premature_return = returned.load();
    manager.IncrementGuest(1);
    cancel = !saw_warning;
    waiter.join();
    REQUIRE(saw_warning);
    REQUIRE_FALSE(premature_return);
    REQUIRE(ready);
}

TEST_CASE("Shutdown cancels an unfinished frame fence", "[video_core][syncpoint]") {
    SyncpointManager manager;
    std::atomic<bool> cancel{};
    bool ready = true;
    std::jthread waiter([&] {
        ready = manager.WaitGuestInterruptible(1, 500, [&] { return cancel.load(); }, {});
    });
    cancel = true;
    waiter.join();
    REQUIRE_FALSE(ready);
    REQUIRE(manager.GetGuestSyncpointValue(1) == 0);
}
