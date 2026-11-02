// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include "common/dynamic_library.h"
#include "common/scope_exit.h"
#include "video_core/nx2.h"
#include "video_core/renderer_vulkan/blit_image.h"
#include "video_core/renderer_vulkan/vk_render_pass_cache.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "core/frontend/emu_window.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/renderer_vulkan/vk_compute_pass.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"

// Explicit opt-in: creates a headless GPU device, without starting the emulator.
TEST_CASE("Packaged Vulkan driver initializes the device and startup compute passes",
          "[.][vulkan-device-smoke]") {
    const char* path = std::getenv("EDEN_TEST_VULKAN_LIBRARY");
    REQUIRE(path != nullptr);
    Common::DynamicLibrary library{path};
    REQUIRE(library.IsOpen());
    Vulkan::vk::InstanceDispatch dispatch{};
    auto instance = Vulkan::CreateInstance(library, dispatch, VK_API_VERSION_1_1,
                                           Core::Frontend::WindowSystemType::Headless, false);
    const auto devices = instance.EnumeratePhysicalDevices();
    REQUIRE_FALSE(devices.empty());
    Vulkan::vk::PhysicalDevice physical{devices.front(), dispatch};
    const auto properties = physical.GetProperties();
    INFO(properties.deviceName);
    std::ostringstream identity;
    identity << std::hex << std::setfill('0');
    for (const u32 word : {32U, 1U, properties.vendorID, properties.deviceID}) {
        for (int byte = 0; byte < 4; ++byte) identity << std::setw(2) << ((word >> (byte * 8)) & 255);
    }
    for (const auto byte : properties.pipelineCacheUUID) identity << std::setw(2) << unsigned(byte);
    std::cout << "Pipeline-cache identity: " << identity.str() << '\n';
    if (const auto* expected = std::getenv("EDEN_TEST_CACHE_IDENTITY")) {
        REQUIRE(identity.str() == expected);
    }

    std::cout << properties.deviceName << ": maxUniformBufferRange="
              << properties.limits.maxUniformBufferRange << ", maxStorageBufferRange="
              << properties.limits.maxStorageBufferRange << '\n';
    REQUIRE(properties.limits.maxUniformBufferRange >= 65536);
    REQUIRE(properties.limits.maxStorageBufferRange >= 134217728);
    REQUIRE(properties.limits.maxImageDimension2D >= 4096);
    REQUIRE(properties.limits.maxBoundDescriptorSets >= 4);
    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    physical.GetProperties2(properties2);
    REQUIRE(properties2.properties.limits.maxUniformBufferRange ==
            properties.limits.maxUniformBufferRange);
    Vulkan::Device device{*instance, physical, VK_NULL_HANDLE, dispatch};
    REQUIRE(static_cast<bool>(device.GetLogical()));
    Vulkan::MemoryAllocator allocator{device};
    Vulkan::StateTracker tracker;
    Vulkan::Scheduler scheduler{device, tracker};
    Vulkan::DescriptorPool descriptors{device, scheduler};
    Vulkan::StagingBufferPool staging{device, allocator, scheduler};
    Vulkan::ComputePassDescriptorQueue queue{
        device, Vulkan::UpdateDescriptorQueue::COMPUTE_FRAME_PAYLOAD_SIZE};
    REQUIRE_NOTHROW([&] {
        Vulkan::Uint8Pass pass{device, scheduler, descriptors, staging, queue};
    }());
    REQUIRE_NOTHROW([&] {
        Vulkan::QuadIndexedPass pass{device, scheduler, descriptors, staging, queue};
    }());
    REQUIRE_NOTHROW([&] {
        Vulkan::ConditionalRenderingResolvePass pass{device, scheduler, descriptors, queue};
    }());
    REQUIRE_NOTHROW([&] {
        Vulkan::QueriesPrefixScanPass pass{device, scheduler, descriptors, queue};
    }());
    REQUIRE_NOTHROW([&] {
        Vulkan::ASTCDecoderPass pass{device, scheduler, descriptors, staging, queue, allocator};
    }());
    REQUIRE_NOTHROW([&] {
        Vulkan::BcEncoderPass pass{device, scheduler, descriptors, queue};
    }());
    REQUIRE_NOTHROW([&] {
        Vulkan::BlockLinearUnswizzle3DPass pass{device, scheduler, descriptors, staging, queue};
    }());
    {
        Vulkan::BlitImageHelper blit{device, scheduler, tracker, descriptors};
        Vulkan::RenderPassCache render_passes{device};
        Vulkan::TextureCacheRuntime runtime{device, scheduler, allocator, staging, blit,
                                            render_passes, descriptors, queue};
        constexpr GPUVAddr address = 0x100000000;
        constexpr u64 size = 32 * 1024 * 1024;
        Tegra::Nx2AddSparseSpan(address, size);
        Tegra::Nx2SparseRegionCreated(address, size, 65536);
        SCOPE_EXIT { Tegra::Nx2SparseRegionDestroyed(address); };
        REQUIRE(Tegra::Nx2SparseUnwritten(address, size));
        VideoCommon::ImageInfo info;
        info.type = VideoCommon::ImageType::e2D;
        info.format = VideoCore::Surface::PixelFormat::R16_UNORM;
        info.size = {4096, 4096, 1};
        info.resources = {1, 1};
        info.is_sparse = true;
        Vulkan::Image image{runtime, info, address, 0};
        // A guest sparse region does not make an ordinary host render target sparse.
        // It must have memory before the first GPU write, even without a prior CPU upload.
        REQUIRE(False(image.flags & VideoCommon::ImageFlagBits::Unbacked));
        REQUIRE(image.Handle() != VK_NULL_HANDLE);
    }
    // Pipeline creation submits no work. Finish() requires a renderer query cache,
    // which this headless construction-only fixture intentionally does not create.
    device.GetLogical().WaitIdle();
}
