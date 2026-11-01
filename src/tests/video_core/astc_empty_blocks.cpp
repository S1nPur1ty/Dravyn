// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <span>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "video_core/textures/astc.h"
#include "video_core/textures/astc_empty_blocks.h"

namespace {
using Tegra::Texture::ASTC::NormalizeEmptyBlocks;

TEST_CASE("Empty ASTC blocks decode to transparent black at every footprint", "[astc-upload]") {
    constexpr std::array footprints{
        std::pair{4U, 4U}, std::pair{5U, 4U}, std::pair{5U, 5U}, std::pair{6U, 5U},
        std::pair{6U, 6U}, std::pair{8U, 5U}, std::pair{8U, 6U}, std::pair{8U, 8U},
        std::pair{10U, 5U}, std::pair{10U, 6U}, std::pair{10U, 8U}, std::pair{10U, 10U},
        std::pair{12U, 10U}, std::pair{12U, 12U},
    };
    for (const auto [width, height] : footprints) {
        std::array<u8, 16> block{};
        REQUIRE(NormalizeEmptyBlocks(block) == 1);
        std::vector<u8> pixels(width * height * 4, 0xff);
        Tegra::Texture::ASTC::Decompress(block, width, height, 1, width, height, pixels);
        REQUIRE(std::ranges::all_of(pixels, [](u8 value) { return value == 0; }));
        REQUIRE(NormalizeEmptyBlocks(block) == 0);
    }
}

TEST_CASE("ASTC normalization preserves every nonempty block", "[astc-upload]") {
    // Even a single nonzero bit is preserved, including malformed and HDR encodings.
    for (size_t bit = 0; bit < 128; ++bit) {
        std::array<u8, 16> block{};
        block[bit / 8] = u8{1} << (bit % 8);
        const auto original = block;
        REQUIRE(NormalizeEmptyBlocks(block) == 0);
        REQUIRE(block == original);
    }
}

TEST_CASE("ASTC normalization stays inside unaligned upload ranges", "[astc-upload]") {
    std::array<u8, 51> bytes{};
    bytes.front() = 0xa5;
    bytes[17] = 1;
    bytes.back() = 0x5a;
    auto upload = std::span{bytes}.subspan(1, 49);
    REQUIRE(NormalizeEmptyBlocks(upload) == 2);
    REQUIRE(bytes.front() == 0xa5);
    REQUIRE(bytes[17] == 1);
    REQUIRE(bytes[49] == 0);
    REQUIRE(bytes.back() == 0x5a);
    REQUIRE(NormalizeEmptyBlocks(upload) == 0);
    REQUIRE(NormalizeEmptyBlocks({}) == 0);
    REQUIRE(NormalizeEmptyBlocks(upload.first(15)) == 0);
}
} // namespace
