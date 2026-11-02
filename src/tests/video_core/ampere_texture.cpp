// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <numeric>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/maxwell/decode.h"
#include "shader_recompiler/frontend/maxwell/opcodes.h"

namespace {
using Shader::Ampere::Instruction;
constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};

Instruction Texture(bool scalar, u32 dimension, u32 lod) {
    // TEX R8, R20, R40, c[2][0], with all four output components.
    return {{0x14087b60, 40U | (2U << 22) | (u32{scalar} << 28) | (dimension << 29),
             255U | (15U << 8) | (7U << 17) | (lod << 23), 0}};
}

struct Sample {
    std::array<u32, 4> coordinates{};
    std::array<u32, 3> metadata{};
};

Sample ReadSample(Instruction instruction) {
    const auto lowered = Shader::Ampere::LowerToMaxwell(std::array{instruction, Exit});
    REQUIRE(static_cast<bool>(lowered));
    std::array<u32, 256> registers;
    std::iota(registers.begin(), registers.end(), 0);
    for (const u64 word : lowered.instructions) {
        const auto opcode = Shader::Maxwell::Decode(word);
        if (opcode == Shader::Maxwell::Opcode::MOV_reg) {
            registers[word & 255] = registers[(word >> 20) & 255];
        } else if (opcode == Shader::Maxwell::Opcode::TEX) {
            Sample sample;
            const u32 coordinate = (word >> 8) & 255;
            const u32 metadata = (word >> 20) & 255;
            for (u32 i = 0; i < sample.coordinates.size(); ++i) {
                sample.coordinates[i] = registers[(coordinate + i) & 255];
            }
            for (u32 i = 0; i < sample.metadata.size(); ++i) {
                sample.metadata[i] = registers[(metadata + i) & 255];
            }
            return sample;
        }
    }
    FAIL("Expected a general TEX instruction");
    return {};
}
} // namespace

TEST_CASE("Vector cubemap samples keep direction coordinates separate from LOD",
          "[ampere][ampere-texture]") {
    for (const u32 dimension : {3U, 7U}) {
        for (const u32 lod : {0U, 1U, 2U, 3U}) {
            const auto sample = ReadSample(Texture(false, dimension, lod));
            REQUIRE(sample.coordinates[0] == 20);
            REQUIRE(sample.coordinates[1] == 21);
            REQUIRE(sample.coordinates[2] == 22);
            if (dimension == 7) {
                REQUIRE(sample.coordinates[3] == 23);
            }
            if (lod >= 2) {
                REQUIRE(sample.metadata[0] == 40);
            }
        }
    }
}

TEST_CASE("Scalar cubemap samples retain the split coordinate layout",
          "[ampere][ampere-texture]") {
    const auto cube = ReadSample(Texture(true, 3, 1));
    REQUIRE(cube.coordinates[0] == 20);
    REQUIRE(cube.coordinates[1] == 21);
    REQUIRE(cube.coordinates[2] == 40);
    const auto array = ReadSample(Texture(true, 7, 1));
    REQUIRE(array.coordinates == std::array<u32, 4>{20, 21, 40, 41});
}

TEST_CASE("Vector texture metadata starts at the second source register",
          "[ampere][ampere-texture]") {
    for (const u32 dimension : {0U, 1U, 4U, 5U}) {
        auto instruction = Texture(false, dimension, 3);
        // Offset, then depth comparison reference, follow the LOD in source B.
        instruction.words[2] |= (1U << 12) | (1U << 14);
        instruction.words[2] &= ~(15U << 8);
        instruction.words[2] |= 1U << 8;
        const auto sample = ReadSample(instruction);
        REQUIRE(sample.metadata == std::array<u32, 3>{40, 41, 42});
    }
}
