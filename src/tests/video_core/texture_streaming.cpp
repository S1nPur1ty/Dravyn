// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/fs/path_util.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "video_core/memory_manager.h"
#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/upload_batch.h"
#include "video_core/texture_cache/util.h"
#include "video_core/textures/astc_empty_blocks.h"

TEST_CASE("Texture upload batches bound giant arrays and reject oversized layers",
          "[texture-streaming]") {
    using namespace VideoCommon;
    for (const size_t layer_size : {size_t{1}, size_t{22U << 20}, TextureUploadChunkBytes}) {
        size_t remaining = 68;
        size_t processed = 0;
        while (remaining) {
            const size_t count = UploadBatchLayers(layer_size, remaining);
            REQUIRE(count > 0);
            REQUIRE(count * layer_size <= TextureUploadChunkBytes);
            REQUIRE(count <= remaining);
            processed += count;
            remaining -= count;
        }
        REQUIRE(processed == 68);
    }
    REQUIRE(UploadBatchLayers(0, 68) == 0);
    REQUIRE(UploadBatchLayers(TextureUploadChunkBytes + 1, 68) == 0);
    REQUIRE(UploadBatchLayers(16, 0) == 0);
}

TEST_CASE("Layer uploads preserve native ASTC mip contents and noncontiguous destinations",
          "[texture-streaming]") {
    using namespace VideoCommon;
    const auto old_nand = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir);
    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("eglantine-texture-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Restore {
        std::filesystem::path old_path, temporary;
        ~Restore() {
            Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, old_path);
            std::filesystem::remove_all(temporary);
        }
    } restore{old_nand, temporary};
    std::filesystem::create_directories(temporary);
    Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, temporary);
    auto system = std::make_unique<Core::System>();
    Core::DeviceMemory physical_memory;
    Tegra::MaxwellDeviceMemoryManager device_memory(physical_memory);
    Tegra::MemoryManager memory(*system, device_memory, 32, 1ULL << 30);
    memory.EnableNativeTiling();
    ImageInfo info;
    info.format = VideoCore::Surface::PixelFormat::ASTC_2D_8X8_SRGB;
    info.type = ImageType::e2D;
    info.size = {2048, 2048, 1};
    info.block = {0, 4, 0};
    info.resources = {12, 68};
    info.native_tiling = true;
    info.layer_stride = CalculateLayerStride(info);
    std::vector<u8> guest(CalculateGuestSizeInBytes(info));
    for (size_t i = 0; i < guest.size(); ++i)
        guest[i] = static_cast<u8>(i * 13 + (i >> 9));
    // Exercise the empty-block color fix as well as nonempty payloads.
    std::fill(guest.begin(), guest.begin() + 4096, 0);
    std::vector<u8> whole(CalculateUnswizzledSizeBytes(info));
    const auto reference = UnswizzleImage(memory, 0, info, guest, whole);
    Tegra::Texture::ASTC::NormalizeEmptyBlocks(whole);
    ImageInfo layer_info = info;
    layer_info.resources.layers = 1;
    const size_t layer_size = CalculateUnswizzledSizeBytes(layer_info);
    for (const bool sparse : {false, true}) {
        std::vector<u32> layers;
        for (u32 layer = 0; layer < 68; ++layer) {
            if (!sparse || layer % 3 == 0 || layer == 67)
                layers.push_back(layer);
        }
        std::span<const u32> pending{layers};
        size_t total = 0;
        while (!pending.empty()) {
            const size_t count = UploadBatchLayers(layer_size, pending.size());
            REQUIRE(count != 0);
            std::vector<u8> batch(count * layer_size);
            for (size_t i = 0; i < count; ++i) {
                const size_t layer = pending[i];
                const auto copies = UnswizzleImage(
                    memory, 0, layer_info,
                    std::span{guest}.subspan(layer * info.layer_stride, info.layer_stride),
                    std::span{batch}.subspan(i * layer_size, layer_size));
                for (const auto& copy : copies) {
                    auto actual = std::span{batch}.subspan(i * layer_size + copy.buffer_offset,
                                                           copy.buffer_size);
                    Tegra::Texture::ASTC::NormalizeEmptyBlocks(actual);
                    const auto& full = reference[copy.image_subresource.base_level];
                    const auto expected = std::span{whole}.subspan(
                        full.buffer_offset + layer * copy.buffer_size, copy.buffer_size);
                    REQUIRE(std::ranges::equal(actual, expected));
                    REQUIRE(copy.image_subresource.num_layers == 1);
                }
            }
            total += count;
            pending = pending.subspan(count);
        }
        REQUIRE(total == layers.size());
    }
}
