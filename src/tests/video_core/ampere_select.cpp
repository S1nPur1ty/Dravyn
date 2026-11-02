// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/maxwell/decode.h"

namespace {
using Shader::Ampere::Instruction;
constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};

struct SelectState {
    std::array<u32, 256> registers{};
    std::array<bool, 8> predicates{false, false, false, false, false, false, false, true};
    u32 constant{};

    void Execute(Instruction instruction) {
        const auto program = Shader::Ampere::LowerToMaxwell(std::array{instruction, Exit});
        REQUIRE(static_cast<bool>(program));
        for (const u64 word : program.instructions) {
            const auto bits = [&](u32 start, u32 count) {
                return static_cast<u32>((word >> start) & ((u64{1} << count) - 1));
            };
            if (predicates[bits(16, 3)] == static_cast<bool>(bits(19, 1)))
                continue;
            const u32 dst = bits(0, 8);
            using Shader::Maxwell::Opcode;
            switch (Shader::Maxwell::Decode(word)) {
            case Opcode::MOV_reg:
                registers[dst] = registers[bits(20, 8)];
                break;
            case Opcode::MOV32I:
                registers[dst] = bits(20, 32);
                break;
            case Opcode::MOV_cbuf:
                REQUIRE(bits(34, 5) == 3);
                REQUIRE(bits(20, 14) * 4 == 0x20);
                registers[dst] = constant;
                break;
            case Opcode::LOP32I:
                REQUIRE((bits(53, 2) == 0 || bits(53, 2) == 2));
                registers[dst] = bits(53, 2) == 0 ? registers[bits(8, 8)] & bits(20, 32)
                                                  : registers[bits(8, 8)] ^ bits(20, 32);
                break;
            case Opcode::SEL_reg:
                registers[dst] = predicates[bits(39, 3)] != static_cast<bool>(bits(42, 1))
                                     ? registers[bits(8, 8)]
                                     : registers[bits(20, 8)];
                break;
            case Opcode::EXIT:
                return;
            default:
                FAIL("Unexpected instruction in select lowering");
            }
            registers[255] = 0;
        }
    }
};

u32 Modify(u32 value, bool absolute, bool negative) {
    if (absolute)
        value &= 0x7fffffff;
    if (negative)
        value ^= 0x80000000;
    return value;
}
} // namespace

TEST_CASE("Floating selects preserve operand modifiers, predicates, and aliased destinations",
          "[ampere][ampere-select]") {
    for (const u32 form : {0x208U, 0x808U, 0xa08U}) {
        for (u32 modifiers = 0; modifiers < 16; ++modifiers) {
            for (const bool condition : {false, true}) {
                for (const bool invert : {false, true}) {
                    for (const bool guard : {false, true}) {
                        for (const u32 value :
                             {0U, 0x80000000U, 0xc0600000U, 0xff800000U, 0xffc12345U}) {
                            const u32 other = value ^ 0x80000000;
                            Instruction op{{0x01011000U | form, 2, 2U << 23, 0}};
                            op.words[2] |= (modifiers & 3) << 8;
                            op.words[2] |= u32{invert} << 26;
                            if (form == 0x808) {
                                op.words[1] = other;
                            } else {
                                if (form == 0xa08)
                                    op.words[1] = (3U << 22) | (0x20U << 6);
                                op.words[1] |= (modifiers >> 2) << 30;
                            }
                            SelectState state;
                            state.registers[1] = value;
                            state.registers[2] = other;
                            state.constant = other;
                            state.predicates[1] = guard;
                            state.predicates[2] = condition;
                            const auto predicates = state.predicates;
                            state.Execute(op);
                            const u32 a = Modify(value, modifiers & 2, modifiers & 1);
                            const u32 b =
                                form == 0x808 ? other : Modify(other, modifiers & 4, modifiers & 8);
                            REQUIRE(state.registers[1] ==
                                    (guard ? (condition != invert ? a : b) : value));
                            REQUIRE(state.registers[2] == other);
                            REQUIRE(state.predicates == predicates);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Captured lighting selects retain opposite signs for the same source register",
          "[ampere][ampere-select]") {
    // Pixel shader c41c016d08f228c6, native offsets 0x320 and 0x350.
    SelectState state;
    state.registers[24] = 0x40400000; // 3.0
    state.Execute({{0x18139208, 0x00000018, 0x05000100, 0x000fe200}});
    REQUIRE(state.registers[19] == 0xc0400000); // -3.0
    state.registers[21] = 0x40400000;
    state.Execute({{0x15149208, 0x80000015, 0x00000000, 0x000fe200}});
    REQUIRE(state.registers[20] == 0xc0400000);
}

TEST_CASE("Integer select keeps immediate float-like sign bits unchanged",
          "[ampere][ampere-select]") {
    SelectState state;
    state.registers[1] = 0x7fc12345;
    state.Execute({{0x01017807, 0xc0600000, 0, 0}});
    REQUIRE(state.registers[1] == 0xc0600000);
    state.predicates[0] = true;
    state.registers[1] = 0x7fc12345;
    state.Execute({{0x01017807, 0xc0600000, 0, 0}});
    REQUIRE(state.registers[1] == 0x7fc12345);
}
