// SPDX-License-Identifier: GPL-3.0-or-later
// Headless pipeline creation only; deliberately invalid descriptors must return errors.
#include <vulkan/vulkan.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#define CHECK(call) do { const auto result = (call); if (result != VK_SUCCESS) { \
    std::fprintf(stderr, "%s: %d\n", #call, result); return 1; } } while (0)

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    setenv("MTL_ERROR_MODE", "exception", 1);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance;
    CHECK(vkCreateInstance(&ici, nullptr, &instance));
    uint32_t count = 1;
    VkPhysicalDevice physical;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, &physical));
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    if (family == count) return 3;
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    VkDevice device;
    CHECK(vkCreateDevice(physical, &dci, nullptr, &device));
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (size_t i = 0; i < stages.size(); ++i) {
        std::ifstream file(argv[i + 1], std::ios::binary | std::ios::ate);
        const auto size = file.tellg();
        if (size <= 0 || size % 4) return 4;
        std::vector<uint32_t> code(size / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(code.data()), size);
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = size;
        ci.pCode = code.data();
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].pName = "main";
        CHECK(vkCreateShaderModule(device, &ci, nullptr, &stages[i].module));
    }
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout;
    CHECK(vkCreatePipelineLayout(device, &lci, nullptr, &layout));
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R8G8B8A8_UINT;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    VkRenderPassCreateInfo rci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rci.attachmentCount = 1;
    rci.pAttachments = &attachment;
    rci.subpassCount = 1;
    rci.pSubpasses = &subpass;
    VkRenderPass pass;
    CHECK(vkCreateRenderPass(device, &rci, nullptr, &pass));
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0, 0, 8, 8, 0, 1};
    VkRect2D scissor{{0, 0}, {8, 8}};
    VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = vs.scissorCount = 1;
    vs.pViewports = &viewport;
    vs.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = 15;
    blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = 1;
    bs.pAttachments = &blend;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = stages.size(); ci.pStages = stages.data();
    ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vs; ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms; ci.pColorBlendState = &bs;
    ci.layout = layout; ci.renderPass = pass;
    // Alternate invalid and valid requests to verify completion/wakeup and recovery.
    for (unsigned i = 0; i < 6; ++i) {
        const bool invalid = i % 2 == 0;
        blend.blendEnable = invalid && i != 2;
        ms.alphaToCoverageEnable = i == 2;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const auto result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
        if (invalid ? (result == VK_SUCCESS || pipeline != VK_NULL_HANDLE)
                              : (result != VK_SUCCESS || pipeline == VK_NULL_HANDLE)) {
            std::fprintf(stderr, "Unexpected pipeline result at %u: %d\n", i, result);
            return 5;
        }
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        std::printf("%s pipeline %u: %d (PASS)\n", invalid ? "Invalid" : "Valid", i, result);
        std::fflush(stdout);
    }
    vkDestroyRenderPass(device, pass, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    for (const auto& stage : stages) vkDestroyShaderModule(device, stage.module, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
