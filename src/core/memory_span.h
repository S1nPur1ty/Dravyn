// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/page_table.h"

namespace Core::Memory {

// A partial remap can split an old block, and its 16-bit ID can be reused. Validate
// every page's host offset before exposing the range as a single host span.
inline u8* GetContiguousMemorySpan(const Common::PageTable& table, u64 address,
                                   size_t size) noexcept {
    if (size == 0) {
        return nullptr;
    }
    const u64 last_address = address + (size - 1);
    const u64 last_page = last_address >> table.current_page_bits;
    if (last_address < address || last_page >= table.entries.size()) {
        return nullptr;
    }
    const u64 first_page = address >> table.current_page_bits;
    const auto first = table.entries[first_page].Raw();
    const auto offset = Common::PageTable::PageEntryData::ExtractPointer(first, true);
    if (!offset || static_cast<Common::PageType>(first.type) == Common::PageType::Unmapped) {
        return nullptr;
    }
    for (u64 page = first_page + 1; page <= last_page; ++page) {
        const auto entry = table.entries[page].Raw();
        if (static_cast<Common::PageType>(entry.type) == Common::PageType::Unmapped ||
            Common::PageTable::PageEntryData::ExtractPointer(entry, true) != offset) {
            return nullptr;
        }
    }
    return reinterpret_cast<u8*>(offset + address);
}

} // namespace Core::Memory
