// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/shader_environment.h"

// Use only a disposable cache copy: the cache loader can remove invalid files.
TEST_CASE("Inspect captured render-target blend state without executing shaders", "[.][pipeline-cache-audit]") {
    const auto* path = std::getenv("EDEN_TEST_PIPELINE_CACHE_COPY");
    REQUIRE(path != nullptr);
    size_t graphics{}, compute{}, integer_blends{}, integer_coverage{};
    VideoCommon::LoadPipelines({}, path, 18,
        [&](std::ifstream& file, VideoCommon::FileEnvironment) {
            Vulkan::ComputePipelineCacheKey key;
            file.read(reinterpret_cast<char*>(&key), sizeof(key));
            ++compute;
        },
        [&](std::ifstream& file, std::vector<VideoCommon::FileEnvironment>) {
            Vulkan::GraphicsPipelineCacheKey key;
            file.read(reinterpret_cast<char*>(&key), sizeof(key));
            ++graphics;
            for (size_t i = 0; i < key.state.color_formats.size(); ++i) {
                const auto format = static_cast<Tegra::RenderTargetFormat>(key.state.color_formats[i]);
                if (format == Tegra::RenderTargetFormat::NONE) continue;
                if (i == 0 && key.state.alpha_to_coverage_enabled &&
                    VideoCore::Surface::IsPixelFormatInteger(
                        VideoCore::Surface::PixelFormatFromRenderTargetFormat(format))) {
                    ++integer_coverage;
                }
                if (key.state.attachments[i].enable && VideoCore::Surface::IsPixelFormatInteger(
                        VideoCore::Surface::PixelFormatFromRenderTargetFormat(format))) {
                    ++integer_blends;
                }
            }
        });
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(graphics + compute > 0);
    std::cout << "Captured cache: " << graphics << " graphics, " << compute
              << " compute; " << integer_blends << " integer attachments with guest blending enabled; "
              << integer_coverage << " integer coverage states\n";
}
