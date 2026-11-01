// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/vk_swapchain_acquire.h"

namespace {

template <typename T>
T FakeHandle(uintptr_t value) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(value);
    } else {
        return static_cast<T>(value);
    }
}

struct AcquireDispatch {
    static thread_local AcquireDispatch* current;
    Vulkan::vk::DeviceDispatch dispatch{};
    Vulkan::vk::Device device;
    Vulkan::vk::Fence fence;
    VkResult acquire_result{VK_SUCCESS};
    VkResult wait_result{VK_SUCCESS};
    VkSemaphore received_semaphore{};
    VkFence received_fence{};
    std::vector<int> calls;

    AcquireDispatch() {
        current = this;
        dispatch.vkAcquireNextImageKHR = [](VkDevice, VkSwapchainKHR, uint64_t,
                                           VkSemaphore semaphore, VkFence fence, uint32_t* index) {
            current->calls.push_back(1);
            current->received_semaphore = semaphore;
            current->received_fence = fence;
            if (current->acquire_result == VK_SUCCESS ||
                current->acquire_result == VK_SUBOPTIMAL_KHR) {
                *index = 2;
            }
            return current->acquire_result;
        };
        dispatch.vkWaitForFences = [](VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t) {
            current->calls.push_back(2);
            return current->wait_result;
        };
        dispatch.vkResetFences = [](VkDevice, uint32_t, const VkFence*) {
            current->calls.push_back(3);
            return VK_SUCCESS;
        };
        dispatch.vkDestroyFence = [](VkDevice, VkFence, const VkAllocationCallbacks*) {};
        dispatch.vkDestroyDevice = [](VkDevice, const VkAllocationCallbacks*) {};
        device = Vulkan::vk::Device{FakeHandle<VkDevice>(1), dispatch};
        fence = Vulkan::vk::Fence{FakeHandle<VkFence>(2), *device, dispatch};
    }
};

thread_local AcquireDispatch* AcquireDispatch::current{};

} // namespace

TEST_CASE("MoltenVK acquire fences are waited and reset before reuse", "[swapchain-acquire]") {
    AcquireDispatch mock;
    for (const auto result : {VK_SUCCESS, VK_SUBOPTIMAL_KHR, VK_SUCCESS}) {
        mock.acquire_result = result;
        mock.calls.clear();
        u32 index = 99;
        REQUIRE(Vulkan::AcquireSwapchainImage(mock.device, FakeHandle<VkSwapchainKHR>(3),
                    VK_NULL_HANDLE, mock.fence, index) == result);
        REQUIRE(index == 2);
        REQUIRE(mock.received_semaphore == VK_NULL_HANDLE);
        REQUIRE(mock.received_fence == *mock.fence);
        REQUIRE(mock.calls == std::vector<int>{1, 2, 3});
    }
}

TEST_CASE("Outdated swapchains do not wait on an unsignaled acquisition fence",
          "[swapchain-acquire]") {
    AcquireDispatch mock;
    mock.acquire_result = VK_ERROR_OUT_OF_DATE_KHR;
    u32 index = 99;
    REQUIRE(Vulkan::AcquireSwapchainImage(mock.device, FakeHandle<VkSwapchainKHR>(3),
                VK_NULL_HANDLE, mock.fence, index) == VK_ERROR_OUT_OF_DATE_KHR);
    REQUIRE(index == 99);
    REQUIRE(mock.calls == std::vector<int>{1});
}

TEST_CASE("Failed image acquisition cannot be mistaken for a valid frame", "[swapchain-acquire]") {
    AcquireDispatch mock;
    for (const auto result : {VK_ERROR_DEVICE_LOST, VK_ERROR_SURFACE_LOST_KHR,
                              VK_ERROR_OUT_OF_DEVICE_MEMORY}) {
        mock.acquire_result = result;
        mock.calls.clear();
        u32 index = 99;
        try {
            Vulkan::AcquireSwapchainImage(mock.device, FakeHandle<VkSwapchainKHR>(3),
                                          VK_NULL_HANDLE, mock.fence, index);
            FAIL("Expected an acquisition failure");
        } catch (const Vulkan::vk::Exception& error) {
            REQUIRE(error.GetResult() == result);
        }
        REQUIRE(index == 99);
        REQUIRE(mock.calls == std::vector<int>{1});
    }
}

TEST_CASE("Failed acquisition fence wait is not reset or treated as completion",
          "[swapchain-acquire]") {
    AcquireDispatch mock;
    mock.wait_result = VK_ERROR_DEVICE_LOST;
    u32 index = 99;
    REQUIRE_THROWS_AS(Vulkan::AcquireSwapchainImage(mock.device, FakeHandle<VkSwapchainKHR>(3),
                         VK_NULL_HANDLE, mock.fence, index), Vulkan::vk::Exception);
    REQUIRE(mock.calls == std::vector<int>{1, 2});
}

TEST_CASE("Other Vulkan drivers retain semaphore-based image acquisition", "[swapchain-acquire]") {
    AcquireDispatch mock;
    u32 index = 99;
    const auto semaphore = FakeHandle<VkSemaphore>(4);
    REQUIRE(Vulkan::AcquireSwapchainImage(mock.device, FakeHandle<VkSwapchainKHR>(3),
                semaphore, Vulkan::vk::Fence{}, index) == VK_SUCCESS);
    REQUIRE(index == 2);
    REQUIRE(mock.received_semaphore == semaphore);
    REQUIRE(mock.received_fence == VK_NULL_HANDLE);
    REQUIRE(mock.calls == std::vector<int>{1});
}
