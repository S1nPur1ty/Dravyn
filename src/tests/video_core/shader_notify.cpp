// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "video_core/shader_notify.h"

TEST_CASE("Shader status reports outstanding work instead of the entire build burst", "[shader-notify]") {
    VideoCore::ShaderNotify notify;
    REQUIRE(notify.ShadersBuilding() == 0);
    for (int i = 0; i < 794; ++i) notify.MarkShaderBuilding();
    for (int i = 0; i < 793; ++i) notify.MarkShaderComplete();
    REQUIRE(notify.ShadersBuilding() == 1);
    notify.MarkShaderComplete();
    REQUIRE(notify.ShadersBuilding() == 0);
    notify.MarkShaderBuilding();
    REQUIRE(notify.ShadersBuilding() == 1);
}
