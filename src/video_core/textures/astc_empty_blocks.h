// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include "common/common_types.h"

namespace Tegra::Texture::ASTC {

// A zero-filled ASTC block is invalid. Match our software decoder's transparent black
// on native decoders, which otherwise return magenta. Only the host upload is changed.
inline size_t NormalizeEmptyBlocks(std::span<u8> data) {
    constexpr std::array<u8, 16> transparent_black{
        0xfc, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    size_t replaced = 0;
    for (size_t offset = 0; offset + 16 <= data.size(); offset += 16) {
        std::array<u64, 2> words;
        std::memcpy(words.data(), data.data() + offset, sizeof(words));
        if ((words[0] | words[1]) != 0) {
            continue;
        }
        std::ranges::copy(transparent_black, data.data() + offset);
        ++replaced;
    }
    return replaced;
}

} // namespace Tegra::Texture::ASTC
