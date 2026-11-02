// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>
#include "common/common_types.h"
#include "common/scratch_buffer.h"

namespace Tegra {

// Commands can release fences that allow the guest to reuse their backing memory.
// Keep this entry stable while executing it; take a fresh snapshot after an acquire resumes.
template <typename Memory, typename Command>
std::span<const Command> ReadCommandBuffer(Memory& memory, GPUVAddr address, size_t count,
                                           bool safe, Common::ScratchBuffer<Command>& storage) {
    storage.resize_destructive(count);
    if (safe) {
        memory.ReadBlock(address, storage.data(), count * sizeof(Command));
    } else {
        memory.ReadBlockUnsafe(address, storage.data(), count * sizeof(Command));
    }
    return {storage.data(), count};
}

} // namespace Tegra
