// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "video_core/draw_diagnostics.h"

using namespace VideoCommon;

TEST_CASE("Draw diagnostics distinguish failed and later submitted bindings", "[draw-diagnostics]") {
    DrawDiagnostics diagnostic;
    const DrawDiagnosticKey face{1, 2, 3, 64, 64, 4};
    auto other = face;
    other[5] = 5;
    REQUIRE(diagnostic.Skip(face, DrawFailure::PendingDepth, 10));
    REQUIRE_FALSE(diagnostic.Skip(face, DrawFailure::PendingDepth, 11));
    REQUIRE_FALSE(diagnostic.Submit(other));
    const auto result = diagnostic.Submit(face);
    REQUIRE(result.has_value());
    REQUIRE(result->skipped == 2);
    REQUIRE(result->first_frame == 10);
    REQUIRE(result->reason == DrawFailure::PendingDepth);
    REQUIRE(diagnostic.pending.empty());
    REQUIRE(diagnostic.resubmitted == 1);
    REQUIRE(diagnostic.Skip(face, DrawFailure::PendingDepth, 12));
    REQUIRE(diagnostic.Skip(face, DrawFailure::BuildFailed, 13));
    REQUIRE(diagnostic.counts[static_cast<size_t>(DrawFailure::BuildFailed)] == 1);
}

TEST_CASE("Draw diagnostic logging and storage remain bounded and renew each window", "[draw-diagnostics]") {
    DrawDiagnostics diagnostic;
    size_t messages{};
    for (u64 i = 0; i < 10000; ++i) {
        messages += diagnostic.Skip({i, 2, 3, 64, 64, 4}, DrawFailure::ShaderRejected, 0);
    }
    REQUIRE(messages == DrawDiagnostics::MaxEvents);
    REQUIRE(diagnostic.pending.size() == DrawDiagnostics::MaxEntries);
    REQUIRE(diagnostic.untracked == 10000 - DrawDiagnostics::MaxEntries);
    REQUIRE(diagnostic.counts[static_cast<size_t>(DrawFailure::ShaderRejected)] == 10000);
    REQUIRE_FALSE(diagnostic.Submit({0, 2, 3, 64, 64, 4})); // Budget exhausted, still remove entry.
    REQUIRE(diagnostic.pending.size() == DrawDiagnostics::MaxEntries - 1);
    REQUIRE(diagnostic.resubmitted == 1);
    diagnostic.ResetWindow();
    REQUIRE(diagnostic.pending.empty());
    REQUIRE(diagnostic.untracked == 0);
    REQUIRE(diagnostic.resubmitted == 0);
    REQUIRE(diagnostic.Skip({1, 2, 3, 64, 64, 4}, DrawFailure::PendingDepth, 600));
}
