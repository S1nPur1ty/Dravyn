// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <catch2/catch_test_macros.hpp>
#include "video_core/command_buffer.h"

namespace {
struct CommandMemory {
    std::array<u32, 4> words{0x200208e3, 0x38, 0x102072c1, 0x8000068b};
    bool flushed{};
    void ReadBlock(u64 address, void* out, size_t size) {
        flushed = true;
        ReadBlockUnsafe(address, out, size);
    }
    void ReadBlockUnsafe(u64 address, void* out, size_t size) {
        std::memcpy(out, reinterpret_cast<const u8*>(words.data()) + address, size);
    }
};
}

TEST_CASE("Executing a command cannot overwrite its remaining decoded entry",
          "[video_core][command-buffer]") {
    for (const bool safe : {false, true}) {
        CommandMemory memory;
        const auto submitted = memory.words;
        Common::ScratchBuffer<u32> storage;
        const auto commands = Tegra::ReadCommandBuffer(memory, 0, 4, safe, storage);
        // Simulate the guest recycling its command memory after a fence in this entry.
        memory.words.fill(0xdeadbeef);
        REQUIRE(std::equal(commands.begin(), commands.end(), submitted.begin()));
        REQUIRE(memory.flushed == safe);
        // After a suspended acquire, newly produced words must be read again.
        const auto resumed = Tegra::ReadCommandBuffer(memory, 8, 2, safe, storage);
        REQUIRE(resumed.size() == 2);
        REQUIRE(resumed[0] == 0xdeadbeef);
        REQUIRE(resumed[1] == 0xdeadbeef);
    }
}
