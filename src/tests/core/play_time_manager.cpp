// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include "common/fs/path_util.h"
#include "frontend_common/play_time_manager.h"

TEST_CASE("Play-time readers survive concurrent updates and timer shutdown", "[play-time]") {
    using namespace Common::FS;
    const auto original = GetEdenPath(EdenPath::PlayTimeDir);
    const auto directory = std::filesystem::temp_directory_path() /
        ("eglantine-playtime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    SetEdenPath(EdenPath::PlayTimeDir, directory);
    struct Restore {
        std::filesystem::path original, temporary;
        ~Restore() {
            SetEdenPath(EdenPath::PlayTimeDir, original);
            std::filesystem::remove_all(temporary);
        }
    } restore{original, directory};
    std::atomic<bool> valid{true};
    {
        PlayTime::PlayTimeManager manager;
        manager.SetProgramId(1);
        manager.Start();
        std::jthread reader([&] {
            for (u32 i = 0; i < 100000; ++i) {
                if (manager.GetPlayTime(2) > 500) {
                    valid = false;
                }
            }
        });
        for (u32 i = 0; i < 500; ++i) {
            manager.SetPlayTime(2, i);
            manager.ResetProgramPlayTime(2);
        }
        reader.join();
        manager.SetPlayTime(2, 123);
        // The manager must join its active timer before saving and destroying its database.
    }
    PlayTime::PlayTimeManager reloaded;
    REQUIRE(valid.load());
    REQUIRE(reloaded.GetPlayTime(2) == 123);
}
