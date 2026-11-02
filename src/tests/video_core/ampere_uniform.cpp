// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/program_address_map.h"
#include "shader_recompiler/frontend/ampere/program_capture.h"
#include "shader_recompiler/frontend/maxwell/decode.h"
#include "shader_recompiler/frontend/maxwell/indirect_branch_table_track.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "video_core/shader_environment.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
constexpr Instruction Compare{{0x0500788c, 2, 0x0c702470, 0x000fcc00}};
constexpr Instruction Address{{0x0604d891, 1, 0x0f8e103f, 0x000fca00}};
constexpr Instruction FloatCompare{{0xff007c0b, 5, 0x0bf53000, 0x000fe400}};

class BranchEnvironment final : public VideoCommon::GenericEnvironment {
public:
    explicit BranchEnvironment(const Shader::Ampere::LoweredProgram& lowered) {
        stage = Shader::Stage::Compute;
        start_address = 0x50;
        is_proprietary_driver = true;
        workgroup_size = {1, 1, 1};
        native_source_size = lowered.source_size;
        code.resize(0x50 / sizeof(u64));
        for (u64 instruction : lowered.instructions) {
            if (code.size() % 4 == 0)
                code.push_back(0);
            code.push_back(instruction);
        }
        Shader::Ampere::AppendNativeAddressMap(code, lowered.source_instruction_offsets);
        cached_lowest = 0;
        cached_highest = static_cast<u32>(code.size() * sizeof(u64));
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override {
        REQUIRE(bank == 1);
        REQUIRE(offset >= 0x40);
        REQUIRE(offset < 0x4c);
        REQUIRE(offset % 4 == 0);
        ++table_reads;
        return (6 + (offset - 0x40) / 4) * sizeof(Instruction);
    }
    bool IsCbufEnabled(u32) const override {
        return true;
    }
    bool IsTextureHandleValid(u32) const override {
        return true;
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
    size_t table_reads{};
};

// Small scalar reference executor for the Maxwell instructions emitted by these lowerings.
// Results are checked against native arithmetic, not against an expected instruction sequence.
struct State {
    std::array<u32, 256> registers{};
    std::array<bool, 8> predicates{};

    State() {
        predicates[7] = true;
    }

    void Execute(const Shader::Ampere::LoweredProgram& program) {
        using Shader::Maxwell::Opcode;
        for (const u64 word : program.instructions) {
            const auto bits = [&](u32 offset, u32 count) {
                return static_cast<u32>((word >> offset) & ((u64{1} << count) - 1));
            };
            if (predicates[bits(16, 3)] == static_cast<bool>(bits(19, 1))) {
                continue;
            }
            const u32 dst = bits(0, 8), a = registers[bits(8, 8)];
            const u32 b = registers[bits(20, 8)];
            switch (Shader::Maxwell::Decode(word)) {
            case Opcode::MOV32I:
                registers[dst] = bits(20, 32);
                break;
            case Opcode::P2R_imm: {
                const u32 mask = bits(20, 7);
                u32 packed = 0;
                for (u32 i = 0; i < 7; ++i) {
                    packed |= u32{predicates[i]} << i;
                }
                registers[dst] = (a & ~mask) | (packed & mask);
                break;
            }
            case Opcode::R2P_imm:
                for (u32 i = 0; i < 7; ++i) {
                    if (bits(20 + i, 1)) {
                        predicates[i] = (a & (1U << i)) != 0;
                    }
                }
                break;
            case Opcode::ISETP_reg:
            case Opcode::FSETP_reg: {
                bool less{}, equal{}, greater{}, unordered{};
                if (Shader::Maxwell::Decode(word) == Opcode::FSETP_reg) {
                    const float lhs = std::bit_cast<float>(a), rhs = std::bit_cast<float>(b);
                    less = lhs < rhs;
                    equal = lhs == rhs;
                    greater = lhs > rhs;
                    unordered = !less && !equal && !greater;
                } else {
                    less = bits(48, 1) ? static_cast<s32>(a) < static_cast<s32>(b) : a < b;
                    equal = a == b;
                    greater = !less && !equal;
                }
                const u32 mode =
                    Shader::Maxwell::Decode(word) == Opcode::FSETP_reg ? bits(48, 4) : bits(49, 3);
                const bool comparison = ((mode & 1) && less) || ((mode & 2) && equal) ||
                                        ((mode & 4) && greater) || ((mode & 8) && unordered);
                const bool accum = predicates[bits(39, 3)] != static_cast<bool>(bits(42, 1));
                const auto combine = [&](bool value) {
                    switch (bits(45, 2)) {
                    case 0:
                        return value && accum;
                    case 1:
                        return value || accum;
                    case 2:
                        return value != accum;
                    default:
                        FAIL("Unsupported predicate combination");
                        return false;
                    }
                };
                const bool first = combine(comparison), second = combine(!comparison);
                if (bits(3, 3) != 7)
                    predicates[bits(3, 3)] = first;
                if (bits(0, 3) != 7)
                    predicates[bits(0, 3)] = second;
                break;
            }
            case Opcode::IMNMX_imm: {
                REQUIRE(bits(48, 1) == 0); // Unsigned clamp used by the native table index.
                const u32 immediate = bits(20, 19);
                const bool minimum = predicates[bits(39, 3)] != static_cast<bool>(bits(42, 1));
                registers[dst] = minimum ? std::min(a, immediate) : std::max(a, immediate);
                break;
            }
            case Opcode::ISCADD_reg:
                registers[dst] = (a << bits(39, 5)) + (bits(48, 1) ? -b : b);
                break;
            case Opcode::EXIT:
                return;
            default:
                FAIL("Unexpected opcode in uniform lowering");
            }
            registers[255] = 0;
        }
    }
};
} // namespace

TEST_CASE("Uniform comparisons accumulate without clobbering other predicates",
          "[ampere][ampere-uniform]") {
    for (const bool immediate : {false, true}) {
        for (u32 combine = 0; combine < 3; ++combine) {
            for (const u32 source : {0U, 3U, 7U}) {
                for (const bool invert : {false, true}) {
                    for (const u32 input : {1U, 2U, 0xffffffffU}) {
                        for (const u32 packed : {0U, 0x25U, 0x7fU}) {
                            auto op = Compare;
                            op.words[0] = (op.words[0] & ~0xfffU) | (immediate ? 0x88c : 0x28c);
                            op.words[1] = immediate ? 2 : 6;
                            op.words[2] = (op.words[2] & ~0x07800c00U) | (source << 23) |
                                          (u32{invert} << 26) | (combine << 10);
                            const auto lowered = LowerToMaxwell(std::array{op, Exit});
                            REQUIRE(static_cast<bool>(lowered));
                            State state;
                            state.registers[221] = input; // UR5
                            state.registers[222] = 2;     // UR6
                            state.registers[239] = packed;
                            state.predicates = {true, false, true, false, true, false, true, true};
                            const auto original = state.predicates;
                            state.Execute(lowered);
                            const bool accum =
                                (source == 7 || ((packed >> (6 - source)) & 1)) != invert;
                            const bool comparison = input == 2;
                            const bool expected = combine == 0   ? comparison && accum
                                                  : combine == 1 ? comparison || accum
                                                                 : comparison != accum;
                            REQUIRE(state.registers[239] ==
                                    ((packed & ~64U) | (u32{expected} << 6)));
                            REQUIRE(state.predicates == original);
                            REQUIRE(state.registers[221] == input);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Uniform address guards preserve the destination when false",
          "[ampere][ampere-uniform]") {
    for (const bool immediate : {false, true}) {
        for (u32 guard = 0; guard < 16; ++guard) {
            for (const u32 packed : {0U, 0x25U, 0x7fU}) {
                for (const u32 input : {3U, 0xffffffffU}) {
                    auto op = Address;
                    op.words[0] =
                        (op.words[0] & ~0xffffU) | (guard << 12) | (immediate ? 0x891 : 0x291);
                    op.words[1] = immediate ? 1 : 5;
                    const auto lowered = LowerToMaxwell(std::array{op, Exit});
                    REQUIRE(static_cast<bool>(lowered));
                    State state;
                    state.registers[220] = 0x12345678; // UR4 destination
                    state.registers[221] = 1;
                    state.registers[222] = input; // UR6 source
                    state.registers[239] = packed;
                    state.predicates = {true, false, true, false, true, false, true, true};
                    const auto original = state.predicates;
                    state.Execute(lowered);
                    const u32 slot = guard & 7;
                    const bool enabled =
                        (slot == 7 || ((packed >> (6 - slot)) & 1)) != bool(guard & 8);
                    REQUIRE(state.registers[220] == (enabled ? (input << 2) + 1 : 0x12345678));
                    REQUIRE(state.registers[239] == packed);
                    REQUIRE(state.predicates == original);
                }
            }
        }
    }
}

TEST_CASE("Floating comparisons read the uniform bank", "[ampere][ampere-uniform]") {
    const auto lowered = LowerToMaxwell(std::array{FloatCompare, Exit});
    REQUIRE(static_cast<bool>(lowered));
    for (const float value : {-1.0f, 0.0f, 1.0f}) {
        State state;
        state.registers[221] = std::bit_cast<u32>(value);
        state.registers[5] = std::bit_cast<u32>(-value); // Distinguish UR5 from R5.
        state.Execute(lowered);
        REQUIRE(state.predicates[2] == (0.0f <= value));
    }
    auto invalid = FloatCompare;
    invalid.words[1] = 28; // Outside the representable uniform bank.
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{invalid, Exit})));
}

TEST_CASE("Uniform comparison extensions retain unsupported-mode rejection",
          "[ampere][ampere-uniform]") {
    for (const u32 bits : {1U << 8, 3U << 10}) {
        auto op = Compare;
        op.words[2] |= bits;
        REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{op, Exit})));
    }
    auto op = Compare;
    op.words[2] &= ~(7U << 20); // A second predicate destination is not implemented.
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{op, Exit})));
}

TEST_CASE("Captured native shaders lower completely", "[.][ampere-capture-audit]") {
    const char* directory = std::getenv("EDEN_TEST_NATIVE_SHADER_DIR");
    REQUIRE(directory != nullptr);
    size_t checked = 0;
    for (const auto& file : std::filesystem::directory_iterator(directory)) {
        if (file.path().extension() != ".bin")
            continue;
        INFO(file.path().filename().string());
        const auto bytes = file.file_size();
        REQUIRE(bytes % sizeof(Instruction) == 0);
        std::vector<Instruction> code(bytes / sizeof(Instruction));
        std::ifstream input(file.path(), std::ios::binary);
        input.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(bytes));
        REQUIRE(input.good());
        Shader::Ampere::ProgramCaptureState capture;
        bool complete = false;
        for (size_t i = 0; i < code.size(); ++i) {
            const auto status = capture.Observe(code[i], i);
            REQUIRE(status != Shader::Ampere::ProgramCaptureState::Result::InvalidTarget);
            if (status == Shader::Ampere::ProgramCaptureState::Result::Complete) {
                code.resize(i + 1);
                complete = true;
                break;
            }
        }
        REQUIRE(complete);
        const auto lowered = LowerToMaxwell(code);
        INFO(lowered.error);
        INFO(lowered.error_offset);
        REQUIRE(static_cast<bool>(lowered));
        // Capture can extend past a linked function's RET; lowering stops once all
        // reachable calls and branches have been resolved.
        REQUIRE(lowered.source_size > 0);
        REQUIRE(lowered.source_size <= code.size() * sizeof(Instruction));
        REQUIRE(lowered.source_instruction_offsets.size() ==
                lowered.source_size / sizeof(Instruction));
        BranchEnvironment env{lowered};
        Shader::Maxwell::Location pc{0x50};
        for (const u64 word : lowered.instructions) {
            const auto opcode = Shader::Maxwell::Decode(word);
            if (opcode == Shader::Maxwell::Opcode::BRX || opcode == Shader::Maxwell::Opcode::JMX) {
                INFO(pc.Offset());
                const auto table = Shader::Maxwell::TrackIndirectBranchTable(env, pc, 0x50);
                REQUIRE(table.has_value());
                REQUIRE(table->num_entries > 0);
            }
            ++pc;
        }
        ++checked;
    }
    REQUIRE(checked > 0);
}

TEST_CASE("Uniform jump tables reach control flow and SPIR-V for every predicate slot",
          "[ampere][ampere-branch-table]") {
    for (u32 predicate = 0; predicate < 7; ++predicate) {
        // Captured UISETP/USEL/USHF/ULDC/BRXU chain, with a three-entry test table.
        const std::array code{
            Instruction{{0x0400788c, 2, 0x0bf01070U | (predicate << 17), 0}},
            Instruction{{0x04047887, 2, 0x08000000U | (predicate << 23), 0}},
            Instruction{{0x04047899, 2, 0x0800063f, 0}},
            Instruction{{0x04047abb, 0x00401000, 0x08000800, 0}},
            Instruction{{0x3f057899, 31, 0x08011404, 0}},
            Instruction{{0x04007958, 0xffffffa0, 0x0b83ffff, 0}},
            Exit,
            Exit,
            Exit,
            Instruction{{0x00007947, 0xfffffff0, 0x0383ffff, 0}},
        };
        const auto lowered = LowerToMaxwell(code);
        REQUIRE(static_cast<bool>(lowered));
        BranchEnvironment env{lowered};
        Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
        Shader::Maxwell::Flow::CFG cfg{env, flow_pool, env.StartAddress()};
        REQUIRE(env.table_reads == 3);
        size_t tables = 0;
        for (const auto& function : cfg.Functions()) {
            for (const auto& block : function.blocks) {
                if (block.end_class != Shader::Maxwell::Flow::EndClass::IndirectBranch)
                    continue;
                REQUIRE(block.indirect_branches.size() == 3);
                for (size_t i = 0; i < 3; ++i) {
                    const auto& branch = block.indirect_branches[i];
                    REQUIRE(branch.address == (6 + i) * sizeof(Instruction));
                    REQUIRE(branch.block->begin.Offset() ==
                            env.TranslateProgramAddress(branch.address));
                }
                ++tables;
            }
        }
        REQUIRE(tables == 1);
        Shader::ObjectPool<Shader::IR::Inst> inst_pool;
        Shader::ObjectPool<Shader::IR::Block> block_pool;
        Shader::HostTranslateInfo host{};
        host.ApplyDescriptorLimitPolicy();
        auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
        Shader::Profile profile{};
        const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
        REQUIRE_FALSE(spirv.empty());
    }
}

TEST_CASE("Uniform table clamps preserve values and both predicate banks",
          "[ampere][ampere-branch-table]") {
    for (u32 predicate = 0; predicate < 7; ++predicate) {
        const std::array code{
            Instruction{{0x0400788c, 2, 0x0bf01070U | (predicate << 17), 0}},
            Instruction{{0x04047887, 2, 0x08000000U | (predicate << 23), 0}},
            Exit,
        };
        const auto lowered = LowerToMaxwell(code);
        REQUIRE(static_cast<bool>(lowered));
        for (const u32 input : {0U, 1U, 2U, 3U, 0xffffffffU}) {
            State state;
            state.registers[220] = input;
            state.registers[239] = 0x55;
            state.predicates = {true, false, true, false, true, false, true, true};
            const auto original = state.predicates;
            state.Execute(lowered);
            REQUIRE(state.registers[220] == std::min(input, 2U));
            const u32 bit = 1U << (6 - predicate);
            REQUIRE(state.registers[239] == ((0x55 & ~bit) | (input < 2 ? bit : 0)));
            REQUIRE(state.predicates == original);
        }
    }
}

TEST_CASE("Uniform select recognizes only a matching unsigned clamp",
          "[ampere][ampere-branch-table]") {
    for (u32 variant = 0; variant < 5; ++variant) {
        Instruction compare{{0x0400788c, 2, 0x0bf21070, 0}};
        Instruction select{{0x04047887, 2, 0x08800000, 0}};
        switch (variant) {
        case 0:
            compare.words[2] ^= 1U << 17;
            break; // Different predicate output.
        case 1:
            compare.words[2] |= 1U << 9;
            break; // Signed comparison.
        case 2:
            compare.words[1] = 3;
            break; // Different bound.
        case 3:
            select.words[2] |= 1U << 26;
            break; // Inverted selection.
        case 4:
            compare.words[0] ^= 1U << 24;
            break; // Different source register.
        }
        const auto lowered = LowerToMaxwell(std::array{compare, select, Exit});
        REQUIRE(static_cast<bool>(lowered));
        for (const u64 word : lowered.instructions) {
            REQUIRE(Shader::Maxwell::Decode(word) != Shader::Maxwell::Opcode::IMNMX_imm);
        }
    }
}
