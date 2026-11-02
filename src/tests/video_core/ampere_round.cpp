// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"
#include "video_core/shader_environment.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
// From the rejected vertex program in the 19:25 track rendering log.
constexpr Instruction CapturedRound{{0x00047b07, 0x01c00000, 0x00201000, 0x001e2200}};

class RoundEnvironment final : public VideoCommon::GenericEnvironment {
public:
    bool IsCbufEnabled(u32) const override {
        return true;
    }
    bool IsTextureHandleValid(u32) const override {
        return true;
    }
    u32 ReadCbufValue(u32, u32) override {
        return 0;
    }
    Shader::TextureType ReadTextureType(u32) override {
        return Shader::TextureType::Color2D;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return {};
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
};
} // namespace

TEST_CASE("Captured constant-buffer rounding reaches the existing floating-point IR",
          "[ampere-round]") {
    using Shader::IR::Opcode;
    const std::array operations{Opcode::FPRoundEven32, Opcode::FPFloor32, Opcode::FPCeil32,
                                Opcode::FPTrunc32};
    for (u32 mode = 0; mode < operations.size(); ++mode) {
        auto instruction = CapturedRound;
        instruction.words[2] |= mode << 14;
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        REQUIRE(static_cast<bool>(lowered));
        REQUIRE(lowered.instructions.size() == 2);
        RoundEnvironment env;
        Shader::ObjectPool<Shader::IR::Inst> pool;
        Shader::IR::Block block{pool};
        Shader::Maxwell::TranslatorVisitor visitor{env, block};
        visitor.F2F_cbuf(lowered.instructions[0]);
        bool rounded = false, buffer_read = false, result_written = false;
        for (const auto& op : block.Instructions()) {
            rounded |= op.GetOpcode() == operations[mode];
            if (op.GetOpcode() == Opcode::GetCbufF32) {
                REQUIRE(op.Arg(0).U32() == 7);
                REQUIRE(op.Arg(1).U32() == 0);
                buffer_read = true;
            }
            if (op.GetOpcode() == Opcode::SetRegister) {
                REQUIRE(op.Arg(0).Reg() == Shader::IR::Reg::R4);
                result_written = true;
            }
        }
        REQUIRE(rounded);
        REQUIRE(buffer_read);
        REQUIRE(result_written);
    }
}

TEST_CASE("Rounding retains predication and modifiers without changing the register form",
          "[ampere-round]") {
    for (u32 guard = 0; guard < 15; ++guard) {
        auto instruction = CapturedRound;
        instruction.words[0] = (instruction.words[0] & ~0xf000U) | (guard << 12);
        instruction.words[1] |= 0xc0000000U; // Absolute, then negate.
        instruction.words[2] |= 1U << 16;    // Flush denormals.
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        REQUIRE(static_cast<bool>(lowered));
        const u64 word = lowered.instructions[0];
        REQUIRE(((word >> 16) & 15) == guard);
        REQUIRE(((word >> 49) & 1) == 1);
        REQUIRE(((word >> 45) & 1) == 1);
        REQUIRE(((word >> 44) & 1) == 1);
    }
    auto instruction = CapturedRound;
    instruction.words[0] = 0x00047307;
    instruction.words[1] = 9;
    const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions[0] == 0x5ca8040000970a04ULL);
}

TEST_CASE("Unrepresentable rounding inputs are rejected instead of miscompiled", "[ampere-round]") {
    auto instruction = CapturedRound;
    instruction.words[1] = 19U << 22; // Unsupported constant-buffer bank.
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{instruction, Exit})));
    instruction = CapturedRound;
    instruction.words[1] |= 1U << 6; // Unaligned byte offset.
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{instruction, Exit})));
    instruction = CapturedRound;
    instruction.words[2] ^= 1U << 20; // F64 source with the F32 opcode.
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{instruction, Exit})));
}
