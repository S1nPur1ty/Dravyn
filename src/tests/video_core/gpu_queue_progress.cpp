// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <atomic>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "video_core/gpu_queue_progress.h"

TEST_CASE("GPU progress waits accept already completed batches and reset",
          "[video_core][gpu-queue]") {
    VideoCommon::GpuQueueProgress progress;
    progress.Publish(24);
    REQUIRE(progress.Wait(12) == 24);
    progress.Reset();
    REQUIRE(progress.Get() == 0);
    REQUIRE(progress.Wait(0) == 0);
}

TEST_CASE("GPU progress survives repeated publish/wait races and preserves payload visibility",
          "[video_core][gpu-queue]") {
    VideoCommon::GpuQueueProgress produced;
    VideoCommon::GpuQueueProgress consumed;
    constexpr u64 iterations = 50000;
    std::array<u64, 8> payload{};
    std::atomic<bool> correct{true};
    std::jthread writer([&] {
        for (u64 i = 1; i <= iterations; ++i) {
            consumed.Wait(i - 1);
            payload.fill(i);
            produced.Publish(i);
            if (i % 127 == 0) {
                std::this_thread::yield();
            }
        }
    });
    std::jthread reader([&] {
        for (u64 i = 1; i <= iterations; ++i) {
            produced.Wait(i);
            for (u64 word : payload) {
                if (word != i) {
                    correct.store(false, std::memory_order_relaxed);
                }
            }
            consumed.Publish(i);
            if (i % 113 == 0) {
                std::this_thread::yield();
            }
        }
    });
    writer.join();
    reader.join();
    REQUIRE(correct.load());
    REQUIRE(consumed.Get() == iterations);
}

TEST_CASE("GPU progress wakes a waiter across partial batch completion",
          "[video_core][gpu-queue]") {
    VideoCommon::GpuQueueProgress progress;
    std::jthread publisher([&] {
        for (u64 i = 1; i <= 4096; ++i) {
            progress.Publish(i);
            std::this_thread::yield();
        }
    });
    REQUIRE(progress.Wait(4096) >= 4096);
}
