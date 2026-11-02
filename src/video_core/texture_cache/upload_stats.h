// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <chrono>
#include "common/common_types.h"

namespace VideoCommon {

// CPU wall time only; enqueue time is not GPU execution time.
struct UploadStatistics {
    using Clock = std::chrono::steady_clock;
    u64 requests{}, total_ns{}, max_ns{}, over_16ms{};
    u64 staging_ns{}, read_ns{}, unswizzle_ns{}, enqueue_ns{}, submit_ns{};
    u64 streamed_chunks{}, peak_staging_bytes{};

    struct Request {
        UploadStatistics& stats;
        Clock::time_point start{Clock::now()};
        ~Request() {
            const auto ns = static_cast<u64>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
            ++stats.requests;
            stats.total_ns += ns;
            stats.max_ns = std::max(stats.max_ns, ns);
            stats.over_16ms += ns > 16'666'667;
        }
    };

    struct Phase {
        u64& total;
        Clock::time_point start{Clock::now()};
        ~Phase() {
            total += static_cast<u64>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
        }
    };
};

} // namespace VideoCommon
