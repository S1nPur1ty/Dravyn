// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdlib>
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/profile.h"
#include "video_core/shader_environment.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0x000fea00}};
constexpr Instruction AddSurface{{0x0400759f, 0x60000c00, 0x0010e000, 0x000fe200}};

class SurfaceEnvironment final : public VideoCommon::GenericEnvironment {
public:
    explicit SurfaceEnvironment(const Shader::Ampere::LoweredProgram& lowered) {
        stage = Shader::Stage::Compute;
        start_address = 0x50;
        is_proprietary_driver = true;
        workgroup_size = {32, 1, 1};
        texture_bound = lowered.required_texture_buffer;
        native_source_size = lowered.source_size;
        code.resize(0x50 / sizeof(u64));
        for (const u64 instruction : lowered.instructions) {
            if (code.size() % 4 == 0) {
                code.push_back(0);
            }
            code.push_back(instruction);
        }
        cached_lowest = 0;
        cached_highest = static_cast<u32>(code.size() * sizeof(u64));
    }
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
        return true;
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
};
} // namespace

TEST_CASE("Native image reductions preserve descriptor, coordinates, data and predicate",
          "[ampere-surface]") {
    for (u32 guard = 0; guard < 15; ++guard) {
        auto op = AddSurface;
        op.words[0] = (op.words[0] & ~0xf000U) | (guard << 12);
        const auto lowered = LowerToMaxwell(std::array{op, Exit});
        REQUIRE(static_cast<bool>(lowered));
        REQUIRE(lowered.required_texture_buffer == 0);
        REQUIRE(lowered.instructions.size() == 2);
        // Independently disassembled with NVIDIA nvdisasm for SM52:
        // SURED.D.2D.ADD.IGN [R4], R0, 0xc, with the original guard.
        REQUIRE(lowered.instructions[0] == (0xeb5800c600000400ULL | (u64{guard} << 16)));
    }
    auto op = AddSurface;
    op.words[1] |= 3U << 22;
    const auto lowered = LowerToMaxwell(std::array{op, Exit});
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.required_texture_buffer == 3);
}

TEST_CASE("Unsupported reduction modes and scratch aliases still reject the program",
          "[ampere-surface]") {
    for (const u32 mode : {0x0010e200U, 0x0090e000U, 0x0010e100U}) {
        auto op = AddSurface;
        op.words[2] = mode;
        REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{op, Exit})));
    }
    auto op = AddSurface;
    op.words[0] = (op.words[0] & 0x00ffffffU) | (245U << 24);
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{op, Exit})));
    op = AddSurface;
    op.words[1] |= 246U;
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{op, Exit})));
    op = AddSurface;
    op.words[1] |= 1U << 27; // Trap, rather than IGN.
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{op, Exit})));
    op = AddSurface;
    op.words[1] &= ~(7U << 29); // Unverified one-dimensional form.
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{op, Exit})));
}

TEST_CASE("Native surface reduction survives optimization and emits a SPIR-V atomic",
          "[ampere-surface]") {
    // Synthetic workload: every invocation atomically adds three to image pixel (0,0).
    const std::array code{
        Instruction{{0x00007802, 3, 0x00000f00, 0}},
        Instruction{{0x00047802, 0, 0x00000f00, 0}},
        Instruction{{0x00057802, 0, 0x00000f00, 0}},
        AddSurface,
        Exit,
    };
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    SurfaceEnvironment env{lowered};
    Shader::ObjectPool<Shader::IR::Inst> inst_pool;
    Shader::ObjectPool<Shader::IR::Block> block_pool;
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, env.StartAddress()};
    Shader::HostTranslateInfo host{};
    host.ApplyDescriptorLimitPolicy();
    auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
    size_t atomics = 0;
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            atomics += inst.GetOpcode() == Shader::IR::Opcode::ImageAtomicIAdd32;
        }
    }
    REQUIRE(atomics == 1);
    REQUIRE(program.info.image_descriptors.size() == 1);
    Shader::Profile profile{};
    profile.unified_descriptor_binding = true;
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
    size_t spirv_atomics = 0;
    for (size_t offset = 5; offset < spirv.size();) {
        const u32 words = spirv[offset] >> 16;
        REQUIRE(words > 0);
        REQUIRE(offset + words <= spirv.size());
        spirv_atomics += (spirv[offset] & 0xffff) == 234; // SPIR-V OpAtomicIAdd.
        offset += words;
    }
    REQUIRE(spirv_atomics == 1);
    if (const char* path = std::getenv("EDEN_TEST_SURFACE_SPIRV")) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(u32));
        REQUIRE(file.good());
    }
}
