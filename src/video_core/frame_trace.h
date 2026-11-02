// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <fmt/format.h>
#include "common/common_types.h"

namespace VideoCommon {

// Keep whole records and an explicit completion marker, even when a frame exceeds the budget.
class FrameTrace {
public:
    static constexpr size_t DefaultByteLimit = 16 * 1024 * 1024;
    static constexpr u64 SampleInterval = 300;
    static constexpr u64 FileCount = 32;

    explicit FrameTrace(size_t byte_limit_ = DefaultByteLimit) : byte_limit{byte_limit_} {}

    void Begin(u64 frame, bool requested = false) {
        active = requested || frame % SampleInterval == 0;
        truncated = false;
        records = 0;
        data.clear();
        registers.clear();
        resources.clear();
        if (active) {
            data = fmt::format("frame_trace version=1 frame={} requested={}\n", frame, requested);
        }
    }

    bool Active() const {
        return active && !truncated;
    }

    void Record(std::string_view record) {
        if (!Active()) {
            return;
        }
        if (record.size() + 1 > byte_limit - std::min(byte_limit, data.size())) {
            truncated = true;
            return;
        }
        data.append(record);
        data.push_back('\n');
        ++records;
    }

    void RecordImage(u64 draw, size_t slot, u32 tic, std::string metadata) {
        if (!Active()) {
            return;
        }
        const auto [entry, inserted] = resources.try_emplace(std::move(metadata), resources.size());
        if (inserted) {
            Record(fmt::format("image_resource id={} {}", entry->second, entry->first));
        }
        Record(fmt::format("image_ref draw={} slot={} tic={} resource={}", draw, slot, tic,
                           entry->second));
    }

    void RecordRegisters(u64 draw, std::span<const u32> values) {
        if (!Active()) {
            return;
        }
        const bool full = registers.size() != values.size();
        std::string record =
            fmt::format("registers draw={} full={} words={}", draw, full, values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            if (full || values[i] != registers[i]) {
                fmt::format_to(std::back_inserter(record), " {:x}={:x}", i, values[i]);
            }
        }
        Record(record);
        registers.assign(values.begin(), values.end());
    }

    std::optional<std::string> Finish(bool frame_ended = true) {
        if (!active) {
            return std::nullopt;
        }
        active = false;
        data += fmt::format("end_frame records={} complete={} truncated={}\n", records,
                            frame_ended && !truncated, truncated);
        return std::exchange(data, {});
    }

private:
    size_t byte_limit;
    std::string data;
    std::vector<u32> registers;
    std::map<std::string, u64> resources;
    u64 records{};
    bool active{};
    bool truncated{};
};

} // namespace VideoCommon
