// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <fstream>
#include <span>
#include <vector>
#include "common/common_types.h"
#include "common/scope_exit.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

// Compilation may grow the cache between the size query and the data query.
// Publish only a complete snapshot; a failed save must leave the previous file intact.
template <typename Read>
bool SavePipelineCacheFile(const std::filesystem::path& filename, std::span<const char> magic,
                           u32 version, Read&& read) {
    std::vector<char> data;
    bool complete = false;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        size_t size{};
        const VkResult query = read(&size, nullptr);
        if (query != VK_SUCCESS) {
            return false;
        }
        if (size < sizeof(VkPipelineCacheHeaderVersionOne)) {
            return false;
        }
        data.resize(size);
        const VkResult result = read(&size, data.data());
        if (result == VK_INCOMPLETE) {
            continue;
        }
        if (result != VK_SUCCESS) {
            return false;
        }
        if (size < sizeof(VkPipelineCacheHeaderVersionOne) || size > data.size()) {
            return false;
        }
        data.resize(size);
        complete = true;
        break;
    }
    if (!complete) {
        return false;
    }

    auto temporary = filename;
    temporary += ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    SCOPE_EXIT {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    };
    file.write(magic.data(), magic.size());
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(data.data(), data.size());
    file.close();
    if (file.fail()) {
        return false;
    }
    std::error_code error;
    std::filesystem::rename(temporary, filename, error);
    return !error;
}

} // namespace Vulkan
