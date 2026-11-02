// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::ProgramCaptureState;
constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0x000fea00}};
constexpr Instruction Nop{{0x00007918, 0, 0, 0x000fc000}};
} // namespace

TEST_CASE("Uniform branch flags are separate from signed branch displacements",
          "[ampere][ampere-branch]") {
    // Captured failing branch at +0x70: @!P0 BRA.U +0x1d0, to +0x250.
    Instruction branch{{0x00008947, 0x000001d1, 0x03800000, 0x000fea00}};
    REQUIRE(branch.BranchOffset() == 0x1d0);
    ProgramCaptureState capture;
    REQUIRE(capture.Observe(branch, 7) == ProgramCaptureState::Result::Continue);
    REQUIRE(capture.Observe(Exit, 8) == ProgramCaptureState::Result::Continue);
    REQUIRE(capture.Observe(Exit, 37) == ProgramCaptureState::Result::Complete);
    for (const s64 displacement : {s64{0x1d0}, s64{-16}, s64{-128}}) {
        const u64 raw = static_cast<u64>(displacement) & ((u64{1} << 50) - 1);
        branch.words[1] = static_cast<u32>(raw) | 1;
        branch.words[2] = 0x03800000 | static_cast<u32>(raw >> 32);
        REQUIRE(branch.BranchOffset() == displacement);
    }
}

TEST_CASE("Uniform and ordinary branches lower to the same control flow",
          "[ampere][ampere-branch]") {
    std::vector<Instruction> code(38, Nop);
    code[7] = {{0x00008947, 0x000001d1, 0x03800000, 0x000fea00}};
    code[8] = Exit;
    code.back() = Exit;
    const auto uniform = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(uniform));
    REQUIRE(uniform.source_size == code.size() * sizeof(Instruction));
    code[7].words[1] &= ~1U;
    const auto ordinary = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(ordinary));
    REQUIRE(uniform.instructions == ordinary.instructions);
    REQUIRE(uniform.source_instruction_offsets == ordinary.source_instruction_offsets);
}

TEST_CASE("Branch capture retains alignment and range validation", "[ampere][ampere-branch]") {
    for (const u32 offset : {4U, 0xffffffe0U, ProgramCaptureState::MaximumInstructions * 16U}) {
        ProgramCaptureState capture;
        Instruction branch{{0x00007947, offset | 1U, 0x03800000, 0}};
        if (offset == 0xffffffe0U)
            branch.words[2] |= 0x3ffff;
        REQUIRE(capture.Observe(branch, 0) == ProgramCaptureState::Result::InvalidTarget);
    }
}
