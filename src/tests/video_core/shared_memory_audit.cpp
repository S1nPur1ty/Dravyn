// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include "common/dynamic_library.h"
#include "core/frontend/emu_window.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader_environment.h"
#include "video_core/vulkan_common/vulkan_instance.h"

TEST_CASE("Audit shared memory in a disposable captured pipeline cache",
          "[.][shared-memory-audit]") {
    const char* path = std::getenv("EDEN_TEST_PIPELINE_CACHE_COPY");
    const char* output = std::getenv("EDEN_TEST_SHARED_AUDIT_DIR");
    REQUIRE(path != nullptr);
    REQUIRE(output != nullptr);
    const char* driver_path = std::getenv("EDEN_TEST_VULKAN_LIBRARY");
    REQUIRE(driver_path != nullptr);
    Common::DynamicLibrary library{driver_path};
    REQUIRE(library.IsOpen());
    Vulkan::vk::InstanceDispatch dispatch{};
    auto instance = Vulkan::CreateInstance(library, dispatch, VK_API_VERSION_1_1,
                                           Core::Frontend::WindowSystemType::Headless, false);
    const auto devices = instance.EnumeratePhysicalDevices();
    REQUIRE_FALSE(devices.empty());
    Vulkan::vk::PhysicalDevice physical{devices.front(), dispatch};
    Vulkan::Device device{*instance, physical, VK_NULL_HANDLE, dispatch};
    Shader::Profile profile{};
    profile.supported_spirv = device.SupportedSpirvVersion();
    profile.unified_descriptor_binding = true;
    profile.support_int64 = device.IsShaderInt64Supported();
    profile.support_descriptor_aliasing = device.IsDescriptorAliasingSupported();
    profile.support_quad_shuffles = device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_QUAD_BIT);
    profile.support_vote = device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_VOTE_BIT);
    profile.support_shuffle_relative =
        device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT);
    profile.min_ssbo_alignment = device.GetStorageBufferAlignment();
    size_t audited{};
    VideoCommon::LoadPipelines(
        {}, path, 18,
        [&](std::ifstream& file, VideoCommon::FileEnvironment env) {
            Vulkan::ComputePipelineCacheKey key;
            file.read(reinterpret_cast<char*>(&key), sizeof(key));
            if (env.SharedMemorySize() <= device.GetMaxComputeSharedMemorySize()) {
                return;
            }
            Shader::ObjectPool<Shader::IR::Inst> inst_pool;
            Shader::ObjectPool<Shader::IR::Block> block_pool;
            Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
            Shader::Maxwell::Flow::CFG cfg{env, flow_pool, env.StartAddress()};
            Shader::HostTranslateInfo host{};
            host.support_float16 = true;
            host.support_int64 = profile.support_int64;
            host.min_ssbo_alignment = profile.min_ssbo_alignment;
            host.ApplyDescriptorLimitPolicy();
            auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
            const auto name = fmt::format("{:016x}.txt", key.unique_hash);
            std::ofstream dump(std::filesystem::path(output) / name);
            dump << Shader::IR::DumpProgram(program);
            REQUIRE(dump.good());
            program.info.shared_memory_storage_size =
                Common::AlignUp(program.shared_memory_size, 4U);
            const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
            std::ofstream binary(std::filesystem::path(output) /
                                     fmt::format("{:016x}.spv", key.unique_hash),
                                 std::ios::binary);
            binary.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(u32));
            REQUIRE(binary.good());
            Vulkan::DescriptorLayoutBuilder builder{device};
            builder.Add(program.info, VK_SHADER_STAGE_COMPUTE_BIT);
            auto descriptors = builder.CreateDescriptorSetLayout(false);
            auto layout = builder.CreatePipelineLayout(*descriptors);
            auto module = Vulkan::BuildShader(device, spirv);
            const VkComputePipelineCreateInfo pipeline_info{
                .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage{
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                    .module = *module,
                    .pName = "main",
                },
                .layout = *layout,
            };
            INFO(name);
            auto pipeline = device.GetLogical().CreateComputePipeline(pipeline_info);
            REQUIRE(static_cast<bool>(pipeline));
            std::cout << name << " shared=" << program.shared_memory_size
                      << " storage-backed pipeline compiled\n";
            ++audited;
        },
        [](std::ifstream& file, std::vector<VideoCommon::FileEnvironment>) {
            Vulkan::GraphicsPipelineCacheKey key;
            file.read(reinterpret_cast<char*>(&key), sizeof(key));
        });
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(audited > 0);
}
