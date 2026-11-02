// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "video_core/frame_trace.h"

TEST_CASE("Frame traces retain draw order and explicit completion", "[frame-trace]") {
    VideoCommon::FrameTrace trace;
    trace.Begin(0);
    trace.Record("draw id=0 status=submitted");
    trace.Record("draw id=1 status=shader-rejected");
    const auto result = trace.Finish();
    REQUIRE(result);
    REQUIRE(result->find("draw id=0") < result->find("draw id=1"));
    REQUIRE(result->find("records=2 complete=true truncated=false") != std::string::npos);
    REQUIRE_FALSE(trace.Finish());
    REQUIRE_FALSE(trace.Active());
}

TEST_CASE("Frame trace budgets never emit partial records", "[frame-trace]") {
    VideoCommon::FrameTrace trace{128};
    trace.Begin(0);
    trace.Record("draw id=0");
    trace.Record(std::string(256, 'x'));
    trace.Record("draw id=2");
    const auto result = trace.Finish();
    REQUIRE(result);
    REQUIRE(result->find("draw id=0") != std::string::npos);
    REQUIRE(result->find('x') == std::string::npos);
    REQUIRE(result->find("draw id=2") == std::string::npos);
    REQUIRE(result->find("complete=false truncated=true") != std::string::npos);
}

TEST_CASE("On-demand traces bypass sampling and interrupted frames stay incomplete",
          "[frame-trace]") {
    VideoCommon::FrameTrace trace;
    trace.Begin(1);
    REQUIRE_FALSE(trace.Active());
    REQUIRE_FALSE(trace.Finish());
    trace.Begin(2, true);
    REQUIRE(trace.Active());
    REQUIRE(trace.Finish(false)->find("complete=false truncated=false") != std::string::npos);
    trace.Begin(VideoCommon::FrameTrace::SampleInterval);
    REQUIRE(trace.Active());
    REQUIRE(trace.Finish()->find("complete=true") != std::string::npos);
}

TEST_CASE("Register trace deltas reset at frame boundaries", "[frame-trace]") {
    VideoCommon::FrameTrace trace;
    trace.Begin(0);
    trace.RecordRegisters(0, std::array<u32, 3>{1, 2, 3});
    trace.RecordRegisters(1, std::array<u32, 3>{1, 4, 3});
    const auto result = trace.Finish();
    REQUIRE(result->find("registers draw=0 full=true words=3 0=1 1=2 2=3") != std::string::npos);
    REQUIRE(result->find("registers draw=1 full=false words=3 1=4\n") != std::string::npos);
    trace.Begin(1, true);
    trace.RecordRegisters(0, std::array<u32, 3>{1, 4, 3});
    REQUIRE(trace.Finish()->find("full=true words=3 0=1 1=4 2=3") != std::string::npos);
}

TEST_CASE("Image metadata is shared while every draw binding and state change is retained",
          "[frame-trace]") {
    VideoCommon::FrameTrace trace;
    trace.Begin(0);
    trace.RecordImage(0, 0, 10, "image_address=0x100 flags=0x4");
    trace.RecordImage(1, 3, 11, "image_address=0x100 flags=0x4");
    trace.RecordImage(2, 0, 10, "image_address=0x100 flags=0x8");
    const auto result = trace.Finish();
    REQUIRE(result->find("image_ref draw=0 slot=0 tic=10 resource=0") != std::string::npos);
    REQUIRE(result->find("image_ref draw=1 slot=3 tic=11 resource=0") != std::string::npos);
    REQUIRE(result->find("image_ref draw=2 slot=0 tic=10 resource=1") != std::string::npos);
    REQUIRE(result->find("records=5 complete=true") != std::string::npos);
    trace.Begin(1, true);
    trace.RecordImage(0, 0, 10, "image_address=0x100 flags=0x4");
    REQUIRE(trace.Finish()->find("image_resource id=0") != std::string::npos);
}
