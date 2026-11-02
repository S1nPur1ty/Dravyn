// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <filesystem>
#include <string_view>

namespace Common::FS {

// Bundle profiles are single path components, never paths supplied by the bundle.
inline bool IsValidProfileName(std::string_view name) {
    return !name.empty() && name.size() <= 64 && name != "." && name != ".." &&
           std::ranges::all_of(name, [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
           });
}

struct ProfilePaths {
    std::filesystem::path config;
    std::filesystem::path cache;
    std::filesystem::path logs;
    std::filesystem::path dumps;
    std::filesystem::path crash_dumps;
};

inline ProfilePaths GetMacProfilePaths(const std::filesystem::path& home,
                                      std::string_view name) {
    const auto root = home / "Library/Application Support/Eglantine/Profiles" / name;
    return {root / "config", home / "Library/Caches/Eglantine" / name,
            root / "log", root / "dump", root / "crash_dumps"};
}

} // namespace Common::FS
