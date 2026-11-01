// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

// A suboptimal acquisition still owns an image. Finish that presentation before recreating
// the swapchain; an out-of-date acquisition owns nothing and must not use its image index.
inline VkResult AcquireSwapchainImage(const vk::Device& device, VkSwapchainKHR swapchain,
                                     VkSemaphore semaphore, const vk::Fence& fence,
                                     u32& image_index) {
    const auto result = device.AcquireNextImageKHR(
        swapchain, (std::numeric_limits<u64>::max)(), semaphore,
        fence ? *fence : VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return result;
    }
    if (result != VK_SUBOPTIMAL_KHR) {
        vk::Check(result);
    }
    if (fence) {
        vk::Check(fence.Wait());
        fence.Reset();
    }
    return result;
}

} // namespace Vulkan
