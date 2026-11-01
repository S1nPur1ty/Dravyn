// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <type_traits>
#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_update_descriptor.h"

namespace {
template <typename T>
T Handle(uintptr_t value) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(value);
    } else {
        return static_cast<T>(value);
    }
}
} // namespace

TEST_CASE("Draw descriptors reuse equal payloads but invalidate on resource or binding changes",
          "[video_core][descriptor-reuse]") {
    Vulkan::DescriptorPayloadCache cache;
    std::array<Vulkan::DescriptorUpdateEntry, 2> entries{
        VkDescriptorBufferInfo{VK_NULL_HANDLE, 16, 64},
        VkDescriptorImageInfo{VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL},
    };
    REQUIRE(cache.Update(entries, true));
    REQUIRE_FALSE(cache.Update(entries, false));
    entries[0].buffer.offset = 32;
    REQUIRE(cache.Update(entries, false));
    REQUIRE_FALSE(cache.Update(entries, false));
    entries[0].buffer.range = 128;
    REQUIRE(cache.Update(entries, false));
    entries[0].buffer.buffer = Handle<VkBuffer>(1);
    REQUIRE(cache.Update(entries, false));
    entries[1].image.imageView = Handle<VkImageView>(2);
    REQUIRE(cache.Update(entries, false));
    entries[1].image.sampler = Handle<VkSampler>(3);
    REQUIRE(cache.Update(entries, false));
    entries[1].image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    REQUIRE(cache.Update(entries, false));
    REQUIRE_FALSE(cache.Update(entries, false));
    // A new command buffer, pipeline switch or helper draw must restore bindings.
    REQUIRE(cache.Update(entries, true));
    REQUIRE_FALSE(cache.Update(entries, false));
    REQUIRE(cache.Update(std::span{entries}.first(1), false));
    REQUIRE(cache.Update(entries, false));
}

TEST_CASE("Descriptor cache owns its comparison payload", "[video_core][descriptor-reuse]") {
    Vulkan::DescriptorPayloadCache cache;
    std::array<Vulkan::DescriptorUpdateEntry, 1> entries{
        VkDescriptorBufferInfo{VK_NULL_HANDLE, 8, 32}};
    REQUIRE(cache.Update(entries, true));
    // The descriptor upload ring is reused independently of this cache.
    entries[0].buffer.offset = 128;
    REQUIRE(cache.Update(entries, false));
    REQUIRE(cache.Update({}, false));
    REQUIRE_FALSE(cache.Update({}, false));
    REQUIRE(cache.Update({}, true));
}
