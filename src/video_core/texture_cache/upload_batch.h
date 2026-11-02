// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>

namespace VideoCommon {

constexpr size_t TextureUploadChunkBytes = 64U << 20;

// Zero means that even one complete layer cannot use the bounded upload path.
constexpr size_t UploadBatchLayers(size_t layer_bytes, size_t remaining) {
    return layer_bytes == 0 ? 0 : std::min(remaining, TextureUploadChunkBytes / layer_bytes);
}

} // namespace VideoCommon
