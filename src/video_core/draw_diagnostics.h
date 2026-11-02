// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string_view>
#include "common/common_types.h"

namespace VideoCommon {
enum class DrawFailure : u32 {
    None,
    ShaderRejected,
    TranslationFailed,
    BuildFailed,
    PendingDepth,
    PendingLargeDraw,
    ConfigureFailed,
    Count,
};

constexpr std::string_view DrawFailureName(DrawFailure reason) {
    constexpr std::array names{"none", "shader-rejected", "translation-failed", "build-failed",
                               "pending-depth", "pending-large-draw", "configure-failed"};
    return names.at(static_cast<size_t>(reason));
}

// A submission with this key only proves the same bindings were used again, not that
// the contents of a previously skipped texture-generation pass were reconstructed.
using DrawDiagnosticKey = std::array<u64, 6>; // VS, PS, RT0, width, height, pipeline hash
class DrawDiagnostics {
public:
    struct Entry {
        u64 skipped{};
        u64 first_frame{};
        DrawFailure reason{};
    };
    static constexpr size_t MaxEntries = 256;
    static constexpr size_t MaxEvents = 64;

    bool Skip(const DrawDiagnosticKey& key, DrawFailure reason, u64 frame) {
        ++counts.at(static_cast<size_t>(reason));
        auto it = pending.find(key);
        if (it == pending.end()) {
            if (pending.size() >= MaxEntries) {
                ++untracked;
                return false;
            }
            it = pending.emplace(key, Entry{0, frame, reason}).first;
        }
        const bool changed = it->second.reason != reason;
        it->second.reason = reason;
        ++it->second.skipped;
        return (it->second.skipped == 1 || changed) && AllowEvent();
    }

    std::optional<Entry> Submit(const DrawDiagnosticKey& key) {
        const auto it = pending.find(key);
        if (it == pending.end()) {
            return std::nullopt;
        }
        const auto entry = it->second;
        pending.erase(it);
        ++resubmitted;
        return AllowEvent() ? std::optional{entry} : std::nullopt;
    }

    void ResetWindow() {
        pending.clear();
        counts.fill(0);
        events = untracked = resubmitted = 0;
    }

    std::map<DrawDiagnosticKey, Entry> pending;
    std::array<u64, static_cast<size_t>(DrawFailure::Count)> counts{};
    u64 untracked{};
    u64 resubmitted{};

private:
    bool AllowEvent() {
        if (events >= MaxEvents) {
            return false;
        }
        ++events;
        return true;
    }
    size_t events{};
};
} // namespace VideoCommon
