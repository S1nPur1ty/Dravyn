// SPDX-License-Identifier: GPL-3.0-or-later
// Headless driver regression: restore without compilation, then concurrent first use.
#include <vulkan/vulkan.h>
#include <mvk_private_api.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>
#define CHECK(call) do { const auto r = (call); if (r != VK_SUCCESS) { \
    std::fprintf(stderr, "%s failed: %d\n", #call, r); std::exit(1); } } while (0)
#define REQUIRE(test) do { if (!(test)) { std::fprintf(stderr, "%s failed\n", #test); std::exit(1); } } while (0)
using Clock = std::chrono::steady_clock;
std::vector<char> Read(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    REQUIRE(file.good());
    std::vector<char> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0); file.read(bytes.data(), bytes.size()); REQUIRE(file.good());
    return bytes;
}
int main(int argc, char** argv) {
    REQUIRE(argc >= 2);
    setenv("MVK_CONFIG_PERFORMANCE_TRACKING", "1", 1);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
    VkInstance instance; CHECK(vkCreateInstance(&ici, nullptr, &instance));
    uint32_t count = 1; VkPhysicalDevice gpu; CHECK(vkEnumeratePhysicalDevices(instance, &count, &gpu));
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
    uint32_t family = 0; while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
    REQUIRE(family < count);
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    VkPhysicalDevicePipelineCreationCacheControlFeatures cache_control{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES};
    cache_control.pipelineCreationCacheControl = VK_TRUE; dci.pNext = &cache_control;
    VkDevice device; CHECK(vkCreateDevice(gpu, &dci, nullptr, &device));
    auto stats = [&] { MVKPerformanceStatistics s{}; size_t size = sizeof(s); CHECK(vkGetPerformanceStatisticsMVK(device, &s, &size)); return s; };
    auto restore = [&](const std::vector<char>& data, size_t offset = 0) {
        VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO}; ci.initialDataSize = data.size() - offset; ci.pInitialData = data.data() + offset;
        VkPipelineCache cache; CHECK(vkCreatePipelineCache(device, &ci, nullptr, &cache)); return cache;
    };
    auto save = [&](VkPipelineCache cache) {
        size_t size = 0; CHECK(vkGetPipelineCacheData(device, cache, &size, nullptr));
        std::vector<char> bytes(size); CHECK(vkGetPipelineCacheData(device, cache, &size, bytes.data())); bytes.resize(size); return bytes;
    };
    if (std::strcmp(argv[1], "--game-cache") == 0) {
        REQUIRE(argc == 3); auto data = Read(argv[2]); REQUIRE(data.size() > 44);
        const auto before = stats(); const auto start = Clock::now();
        const auto cache = restore(data, 12);
        const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        const auto after = stats(); auto saved = save(cache);
        std::printf("Cache input=%zu output=%zu restored=%u compiles=%u time=%.2f ms\n", data.size()-12, saved.size(), after.pipelineCache.readPipelineCache.count-before.pipelineCache.readPipelineCache.count, after.shaderCompilation.mslCompile.count-before.shaderCompilation.mslCompile.count, ms);
        VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(gpu, &properties);
        std::printf("Driver UUID: "); for (auto byte : properties.pipelineCacheUUID) std::printf("%02x", byte);
        std::printf("\nCache UUID: "); for (size_t i=28;i<44;++i) std::printf("%02x", static_cast<unsigned char>(data[i])); std::printf("\n");
        REQUIRE(saved.size() == data.size() - 12);
        REQUIRE(after.shaderCompilation.mslCompile.count == before.shaderCompilation.mslCompile.count);
        REQUIRE(after.pipelineCache.readPipelineCache.count > before.pipelineCache.readPipelineCache.count);
        std::printf("PASS: restored %zu bytes, %u libraries in %.2f ms; zero Metal compilations; cache size preserved\n", saved.size(), after.pipelineCache.readPipelineCache.count - before.pipelineCache.readPipelineCache.count, ms);
        vkDestroyPipelineCache(device, cache, nullptr);
    } else {
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; si.bindingCount = 1; si.pBindings = &binding;
        VkDescriptorSetLayout set; CHECK(vkCreateDescriptorSetLayout(device, &si, nullptr, &set));
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; li.setLayoutCount = 1; li.pSetLayouts = &set;
        VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(device, &li, nullptr, &layout));
        std::vector<VkShaderModule> modules;
        for (int i = 1; i < argc; ++i) {
            const auto bytes = Read(argv[i]); std::vector<uint32_t> words(bytes.size() / 4); std::memcpy(words.data(), bytes.data(), bytes.size());
            VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize = bytes.size(); ci.pCode = words.data();
            VkShaderModule module; CHECK(vkCreateShaderModule(device, &ci, nullptr, &module)); modules.push_back(module);
        }
        auto build = [&](VkPipelineCache cache, VkShaderModule module, VkPipelineCreateFlags flags = 0) {
            VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr}; ci.layout = layout; ci.flags = flags;
            VkPipeline pipeline = VK_NULL_HANDLE;
            const auto result = vkCreateComputePipelines(device, cache, 1, &ci, nullptr, &pipeline);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            return result;
        };
        auto cache = restore({}); for (auto module : modules) CHECK(build(cache, module));
        auto bytes = save(cache); vkDestroyPipelineCache(device, cache, nullptr);
        const auto before = stats(); cache = restore(bytes);
        REQUIRE(stats().shaderCompilation.mslCompile.count == before.shaderCompilation.mslCompile.count);
        // Copy a cache while its libraries are still deferred.
        auto merged = restore({}); CHECK(vkMergePipelineCaches(device, merged, 1, &cache)); vkDestroyPipelineCache(device, cache, nullptr); cache = merged;
        REQUIRE(build(cache, modules.front(), VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) == VK_PIPELINE_COMPILE_REQUIRED);
        REQUIRE(stats().shaderCompilation.mslCompile.count == before.shaderCompilation.mslCompile.count);
        std::vector<std::thread> threads;
        for (int repeat = 0; repeat < 4; ++repeat) for (auto module : modules) threads.emplace_back([&, module] { CHECK(build(cache, module)); });
        for (auto& thread : threads) thread.join();
        const auto after = stats();
        CHECK(build(cache, modules.front(), VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT));
        REQUIRE(after.shaderCompilation.mslCompile.count - before.shaderCompilation.mslCompile.count == modules.size());
        for (auto module : modules) CHECK(build(cache, module));
        REQUIRE(stats().shaderCompilation.mslCompile.count == after.shaderCompilation.mslCompile.count);
        std::printf("PASS: cache restore and merge compile nothing; %zu libraries compile exactly once across %zu concurrent requests; repeated use reuses libraries; compile-required flags honored\n", modules.size(), threads.size());
        vkDestroyPipelineCache(device, cache, nullptr);
        for (auto module : modules) vkDestroyShaderModule(device, module, nullptr);
        vkDestroyPipelineLayout(device, layout, nullptr); vkDestroyDescriptorSetLayout(device, set, nullptr);
    }
    vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
}
