// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/shader_environment.h"

TEST_CASE("Shader reuse separates fixed function state from translation inputs", "[shader-reuse]") {
    Vulkan::GraphicsPipelineCacheKey key{};
    key.unique_hashes[1] = 123;
    key.unique_hashes[5] = 456;
    auto variant = key;
    variant.state.attachments[0].raw = 0xFFFFFFFF;
    variant.state.dynamic_state.raw1 = 0xFFFFFFFF;
    variant.state.dynamic_state.raw2 = 0xFFFFFFFF;
    variant.state.depth_enabled.Assign(1);
    variant.state.depth_format.Assign(7);
    variant.state.vertex_strides[0] = 64;
    variant.state.binding_divisors[0] = 2;
    variant.state.enabled_divisors = 1;
    variant.state.attributes[0].buffer.Assign(1);
    variant.state.attributes[0].offset.Assign(32);
    variant.state.attributes[0].size.Assign(5);
    REQUIRE(variant != key);
    REQUIRE(variant.ShaderKey() == key.ShaderKey());
    REQUIRE(variant.ShaderKey().Hash() == key.ShaderKey().Hash());
    REQUIRE(variant.ShaderKey().ShaderKey() == variant.ShaderKey());

    const auto distinct = [&key](auto change) {
        auto changed = key;
        change(changed);
        REQUIRE(changed.ShaderKey() != key.ShaderKey());
    };
    distinct([](auto& k) { ++k.unique_hashes[1]; });
    distinct([](auto& k) { k.state.attachment0_dual_source_blend.Assign(1); });
    distinct([](auto& k) { k.state.color_formats[0] = 1; });
    distinct([](auto& k) { k.state.alpha_test_func.Assign(1); });
    distinct([](auto& k) { k.state.alpha_test_ref = 1; });
    distinct([](auto& k) { k.state.early_z.Assign(1); });
    distinct([](auto& k) { k.state.y_negate.Assign(1); });
    distinct([](auto& k) { k.state.ndc_minus_one_to_one.Assign(1); });
    distinct([](auto& k) { k.state.point_size = 1; });
    distinct(
        [](auto& k) { k.state.topology.Assign(Vulkan::Maxwell::PrimitiveTopology::Triangles); });
    distinct([](auto& k) { k.state.tessellation_clockwise.Assign(1); });
    distinct([](auto& k) { k.state.tessellation_primitive.Assign(1); });
    distinct([](auto& k) { k.state.tessellation_spacing.Assign(1); });
    distinct([](auto& k) { k.state.attributes[0].type.Assign(1); });
    distinct([](auto& k) { k.state.attributes[0].enabled.Assign(1); });
    distinct([](auto& k) { k.state.xfb_enabled.Assign(1); });
    key.state.dynamic_vertex_input.Assign(1);
    distinct([](auto& k) { k.state.attribute_types = 1; });
}

namespace {
class TestEnvironment final : public VideoCommon::GenericEnvironment {
public:
    u32 constant = 42;
    Shader::TextureType texture = Shader::TextureType::Color2D;
    Shader::TexturePixelFormat format{};
    u32 viewport = 1;
    bool enabled = true;
    bool texture_valid = true;
    bool IsTextureHandleValid(u32) const override {
        return texture_valid;
    }
    bool IsCbufEnabled(u32) const override {
        return enabled;
    }
    u32 ReadCbufValue(u32 index, u32 offset) override {
        REQUIRE(enabled);
        cbuf_values.emplace((u64{index} << 32) | offset, constant);
        return constant;
    }
    Shader::TextureType ReadTextureType(u32 handle) override {
        REQUIRE(texture_valid);
        texture_types.emplace(handle, texture);
        return texture;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32 handle) override {
        REQUIRE(texture_valid);
        texture_pixel_formats.emplace(handle, format);
        return format;
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }
    u32 ReadViewportTransformState() override {
        return viewport_transform_state = viewport;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void SetHeader() {
        reinterpret_cast<u8*>(&sph)[0] ^= 1;
    }
    void SetStart() {
        ++start_address;
    }
    void SetDriver() {
        is_proprietary_driver = !is_proprietary_driver;
    }
    void SetPassthrough() {
        ++gp_passthrough_mask[0];
    }
    void SetHLE() {
        has_hle_engine_state = true;
    }
    void SetUnbound() {
        has_unbound_instructions = true;
    }
    void SetLocalMemory() {
        ++local_memory_size;
    }
    void SetTextureBuffer() {
        ++texture_bound;
    }
};
} // namespace

TEST_CASE("Shader reuse validates captured specialization dependencies", "[shader-reuse]") {
    TestEnvironment captured;
    captured.ReadCbufValue(2, 16);
    captured.ReadTextureType(5);
    captured.ReadTexturePixelFormat(6);
    TestEnvironment current;
    REQUIRE(captured.MatchesShaderContext(current));
    SECTION("Changed constant") {
        current.constant++;
    }
    SECTION("Disabled constant buffer") {
        current.enabled = false;
    }
    SECTION("Invalid texture handle") {
        current.texture_valid = false;
    }
    SECTION("Changed texture type") {
        current.texture = Shader::TextureType::Color3D;
    }
    SECTION("Changed texture format") {
        current.format = static_cast<Shader::TexturePixelFormat>(1);
    }
    SECTION("Changed viewport") {
        current.viewport = 0;
    }
    SECTION("Changed local memory") {
        current.SetLocalMemory();
    }
    SECTION("Changed texture buffer") {
        current.SetTextureBuffer();
    }
    SECTION("Changed header") {
        current.SetHeader();
    }
    SECTION("Changed start address") {
        current.SetStart();
    }
    SECTION("Changed driver ABI") {
        current.SetDriver();
    }
    SECTION("Changed passthrough mask") {
        current.SetPassthrough();
    }
    SECTION("HLE context") {
        current.SetHLE();
    }
    SECTION("Captured HLE context") {
        captured.SetHLE();
    }
    SECTION("Unbounded shader") {
        captured.SetUnbound();
    }
    REQUIRE_FALSE(captured.MatchesShaderContext(current));
}

// Optional read-only audit of an existing cache; never starts the renderer or a game.
TEST_CASE("Audit shader variant reuse in a saved cache", "[.][shader-cache-audit]") {
    const char* path = std::getenv("EGLANTINE_SHADER_AUDIT");
    if (!path) {
        SKIP("Set EGLANTINE_SHADER_AUDIT to a vulkan.bin cache");
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    REQUIRE(file.is_open());
    const auto end = file.tellg();
    file.seekg(0);
    std::array<char, 8> magic{};
    u32 version{};
    file.read(magic.data(), magic.size()).read(reinterpret_cast<char*>(&version), sizeof(version));
    REQUIRE(std::string_view(magic.data(), magic.size()) == "yuzucach");
    REQUIRE(version == 18);
    std::unordered_set<Vulkan::GraphicsPipelineCacheKey> pipelines, translations;
    size_t graphics{}, compute{};
    constexpr std::array<size_t, 4> capacities{64, 128, 256, 512};
    std::array<size_t, 4> reuse_candidates{};
    std::array<std::deque<Vulkan::GraphicsPipelineCacheKey>, 4> recent;
    while (file.tellg() < end) {
        u32 count{};
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        REQUIRE(count > 0);
        REQUIRE(count <= 6);
        std::vector<VideoCommon::FileEnvironment> envs(count);
        for (auto& env : envs) {
            env.Deserialize(file);
        }
        REQUIRE(file.good());
        if (envs.front().ShaderStage() == Shader::Stage::Compute) {
            Vulkan::ComputePipelineCacheKey key;
            file.read(reinterpret_cast<char*>(&key), sizeof(key));
            ++compute;
        } else {
            Vulkan::GraphicsPipelineCacheKey key;
            file.read(reinterpret_cast<char*>(&key), sizeof(key));
            ++graphics;
            if (pipelines.insert(key).second) {
                const auto shader_key = key.ShaderKey();
                translations.insert(shader_key);
                for (size_t i = 0; i < capacities.size(); ++i) {
                    if (std::ranges::find(recent[i], shader_key) != recent[i].end()) {
                        ++reuse_candidates[i];
                    } else {
                        if (recent[i].size() == capacities[i])
                            recent[i].pop_front();
                        recent[i].push_back(shader_key);
                    }
                }
            }
        }
        REQUIRE(file.good());
    }
    std::cout << "Cache audit: " << graphics << " graphics records, " << compute
              << " compute records, " << pipelines.size() << " unique graphics pipelines, "
              << translations.size() << " shader keys.\n";
    for (size_t i = 0; i < capacities.size(); ++i) {
        std::cout << capacities[i] << " entries: " << reuse_candidates[i]
                  << " candidate translation reuses (before context/size validation).\n";
    }
}
