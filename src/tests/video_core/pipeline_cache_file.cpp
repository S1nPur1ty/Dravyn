// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <chrono>
#include <cstring>
#include <iterator>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/vk_pipeline_cache_file.h"

namespace {
struct CacheFile {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("eglantine-cache-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = directory / "cache.bin";
    CacheFile() {
        std::filesystem::create_directory(directory);
        std::ofstream{path, std::ios::binary} << "previous valid cache";
    }
    ~CacheFile() {
        std::filesystem::remove_all(directory);
    }
    std::string Read() const {
        std::ifstream file{path, std::ios::binary};
        return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }
};
constexpr std::array<char, 8> Magic{'y', 'u', 'z', 'u', 'v', 'k', 'c', 'h'};
} // namespace

TEST_CASE("Growing Vulkan caches retry before publishing a complete file",
          "[pipeline-cache-file]") {
    CacheFile file;
    size_t queries = 0, reads = 0;
    const auto result =
        Vulkan::SavePipelineCacheFile(file.path, Magic, 18, [&](size_t* size, void* data) {
            if (!data) {
                *size = ++queries == 1 ? 32 : 64;
                return VK_SUCCESS;
            }
            if (++reads == 1) {
                *size = 0;
                return VK_INCOMPLETE;
            }
            REQUIRE(*size == 64);
            std::memset(data, 42, *size);
            return VK_SUCCESS;
        });
    REQUIRE(result);
    REQUIRE(queries == 2);
    REQUIRE(reads == 2);
    const auto contents = file.Read();
    REQUIRE(contents.size() == 76);
    REQUIRE(contents.substr(0, 8) == "yuzuvkch");
    REQUIRE(contents.substr(12) == std::string(64, 42));
    REQUIRE_FALSE(std::filesystem::exists(file.path.string() + ".tmp"));
}

TEST_CASE("Incomplete or failed snapshots preserve the last valid Vulkan cache",
          "[pipeline-cache-file]") {
    for (const auto failure : {VK_INCOMPLETE, VK_ERROR_OUT_OF_HOST_MEMORY}) {
        CacheFile file;
        size_t reads = 0;
        const auto result =
            Vulkan::SavePipelineCacheFile(file.path, Magic, 18, [&](size_t* size, void* data) {
                if (!data) {
                    *size = 64;
                    return VK_SUCCESS;
                }
                ++reads;
                *size = 0;
                return failure;
            });
        REQUIRE_FALSE(result);
        REQUIRE(reads == (failure == VK_INCOMPLETE ? 3 : 1));
        REQUIRE(file.Read() == "previous valid cache");
    }
    CacheFile file;
    REQUIRE_FALSE(Vulkan::SavePipelineCacheFile(file.path, Magic, 18, [](size_t* size, void*) {
        *size = 0;
        return VK_SUCCESS;
    }));
    REQUIRE(file.Read() == "previous valid cache");
}

TEST_CASE("Failed temporary-file writes do not delete the saved Vulkan cache",
          "[pipeline-cache-file]") {
    CacheFile file;
    std::filesystem::create_directory(file.path.string() + ".tmp");
    REQUIRE_FALSE(Vulkan::SavePipelineCacheFile(file.path, Magic, 18, [](size_t* size, void* data) {
        *size = 32;
        if (data)
            std::memset(data, 0, 32);
        return VK_SUCCESS;
    }));
    REQUIRE(file.Read() == "previous valid cache");
}
