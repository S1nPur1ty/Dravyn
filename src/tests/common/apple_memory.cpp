// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include "common/host_memory.h"
#include "common/sparse_large_vector.h"

TEST_CASE("Apple memory: fallback preserves guest subpages and ownership", "[apple-memory]") {
#ifdef __APPLE__
    if (Common::HostPageSize == 0x1000) {
        SKIP("Requires a host with pages larger than guest pages");
    }
    constexpr size_t backing_size = 0x10000;
    auto source = std::make_unique<Common::HostMemory>(backing_size, 1ULL << 39);
    REQUIRE(source->BackingBasePointer() != nullptr);
    REQUIRE(source->VirtualBasePointer() == nullptr);
    auto* pointer = source->BackingBasePointer();
    for (size_t subpage = 0; subpage < 16; ++subpage) {
        source->ClearBackingRegion(subpage * 0x1000, 0x1000, static_cast<u32>(subpage + 1));
    }
    source->Map(0x1000, 0x3000, 0x1000, Common::MemoryPermission::ReadWrite, false);
    source->Protect(0x1000, 0x1000, Common::MemoryPermission::Read);
    source->Unmap(0x1000, 0x1000, false);
    Common::HostMemory moved{std::move(*source)};
    REQUIRE(source->BackingBasePointer() == nullptr);
    source.reset();
    REQUIRE(moved.BackingBasePointer() == pointer);
    Common::HostMemory assigned{backing_size, 1ULL << 39};
    assigned = std::move(moved);
    REQUIRE(moved.BackingBasePointer() == nullptr);
    REQUIRE(assigned.BackingBasePointer() == pointer);
    for (size_t offset = 0; offset < backing_size; ++offset) {
        REQUIRE(pointer[offset] == offset / 0x1000 + 1);
    }
#else
    SKIP("Apple-specific mapping test");
#endif
}

TEST_CASE("Apple memory: sparse decommit zeros full pages and preserves neighbors", "[apple-memory]") {
#ifdef __APPLE__
    const size_t per_page = Common::HostPageSize / sizeof(u64);
    Common::SparseLargeVector<u64> memory{3 * per_page};
    memory.CommitRegion(0, memory.size());
    for (size_t i = 0; i < memory.size(); ++i) {
        memory.GetUnchecked(i) = i + 1;
    }
    // Partial tails share a host page with live entries; the middle page is decommitted.
    const size_t first = per_page / 2;
    const size_t last = 2 * per_page + per_page / 2;
    memory.ZeroRegion(first, last);
    for (size_t i = 0; i < memory.size(); ++i) {
        REQUIRE(memory[i] == (i >= first && i < last ? 0 : i + 1));
    }
    memory.CommitRegion(per_page, 2 * per_page);
    memory.GetUnchecked(per_page + 3) = 1234;
    REQUIRE(memory[per_page + 3] == 1234);
    memory.ZeroRegion(0, memory.size());
    for (size_t i = 0; i < memory.size(); ++i) {
        REQUIRE(memory[i] == 0);
    }
#else
    SKIP("Apple-specific decommit test");
#endif
}
