// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <limits>
#include <catch2/catch_test_macros.hpp>
#include "core/memory_span.h"

namespace {
constexpr u64 PageSize = 4096;
struct SpanFixture {
    Common::PageTable table;
    alignas(PageSize) std::array<u8, PageSize * 8> backing{};
    SpanFixture() {
        table.Resize(16, 12);
        table.entries.CommitRegion(0, 16);
    }
    void Map(u64 guest_page, u64 host_page, u16 block = 1,
             Common::PageType type = Common::PageType::Memory) {
        const auto offset = reinterpret_cast<uintptr_t>(backing.data() + host_page * PageSize) -
                            guest_page * PageSize;
        table.entries.GetUnchecked(guest_page)
            .Store(type != Common::PageType::Memory, type, block, offset);
    }
};
} // namespace

TEST_CASE("Direct memory spans reject holes and discontiguous remaps inside matching endpoints",
          "[memory-span]") {
    SpanFixture f;
    f.Map(1, 0);
    f.Map(2, 1);
    f.Map(3, 2);
    REQUIRE(Core::Memory::GetContiguousMemorySpan(f.table, PageSize, 3 * PageSize - 1) ==
            f.backing.data());
    f.Map(2, 5, 2);
    REQUIRE_FALSE(Core::Memory::GetContiguousMemorySpan(f.table, PageSize, 3 * PageSize - 1));
    f.table.entries.GetUnchecked(2).Store(false, Common::PageType::Unmapped, 0, 0);
    REQUIRE_FALSE(Core::Memory::GetContiguousMemorySpan(f.table, PageSize, 3 * PageSize - 1));
}

TEST_CASE("Mapping ID rollover cannot make separate host allocations contiguous", "[memory-span]") {
    SpanFixture f;
    for (const u16 tag : {u16{0}, u16{65535}, u16{7}}) {
        f.Map(1, 0, tag);
        f.Map(2, 5, tag);
        REQUIRE(f.table.entries[1].Block() == f.table.entries[2].Block());
        REQUIRE_FALSE(Core::Memory::GetContiguousMemorySpan(f.table, PageSize, 2 * PageSize - 1));
    }
    // Contiguous pages remain valid even if independently mapped or rasterizer-tracked.
    f.Map(1, 0, 10);
    f.Map(2, 1, 11, Common::PageType::RasterizerCachedMemory);
    REQUIRE(Core::Memory::GetContiguousMemorySpan(f.table, PageSize, 2 * PageSize) ==
            f.backing.data());
}

TEST_CASE("Direct spans respect exclusive ends, address-space bounds and overflow",
          "[memory-span]") {
    SpanFixture f;
    f.Map(15, 0);
    REQUIRE(Core::Memory::GetContiguousMemorySpan(f.table, 15 * PageSize, PageSize) ==
            f.backing.data());
    REQUIRE(Core::Memory::GetContiguousMemorySpan(f.table, 15 * PageSize + 3, PageSize - 3) ==
            f.backing.data() + 3);
    REQUIRE_FALSE(Core::Memory::GetContiguousMemorySpan(f.table, 15 * PageSize, PageSize + 1));
    REQUIRE_FALSE(Core::Memory::GetContiguousMemorySpan(f.table, 16 * PageSize, 1));
    REQUIRE_FALSE(Core::Memory::GetContiguousMemorySpan(f.table, 15 * PageSize, 0));
    REQUIRE_FALSE(
        Core::Memory::GetContiguousMemorySpan(f.table, std::numeric_limits<u64>::max() - 1, 4));
}
