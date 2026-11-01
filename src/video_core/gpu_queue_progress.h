// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstddef>

#include "common/common_types.h"

namespace VideoCommon {

#if defined(__APPLE__) && defined(__aarch64__)
inline constexpr size_t GPU_QUEUE_CACHE_LINE = 128;
#else
inline constexpr size_t GPU_QUEUE_CACHE_LINE = 64;
#endif

/// Monotonic progress with one publisher and one waiter. Reset only while both are idle.
class GpuQueueProgress {
public:
    u64 Get() const {
        return value.load(std::memory_order_acquire);
    }

    void Publish(u64 progress) {
        value.store(progress, std::memory_order_seq_cst);
        if (waiting.load(std::memory_order_seq_cst)) {
            value.notify_one();
        }
    }

    u64 Wait(u64 target) {
        // Together with Publish's SC store/load, this prevents a missed notification
        // between advertising the wait and checking progress.
        waiting.store(true, std::memory_order_seq_cst);
        u64 seen = value.load(std::memory_order_seq_cst);
        while (seen < target) {
            value.wait(seen, std::memory_order_acquire);
            seen = value.load(std::memory_order_seq_cst);
        }
        waiting.store(false, std::memory_order_seq_cst);
        return seen;
    }

    void Reset() {
        value.store(0, std::memory_order_relaxed);
        waiting.store(false, std::memory_order_relaxed);
    }

private:
    alignas(GPU_QUEUE_CACHE_LINE) std::atomic<u64> value{};
    alignas(GPU_QUEUE_CACHE_LINE) std::atomic<bool> waiting{};
};

} // namespace VideoCommon
