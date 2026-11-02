// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <catch2/catch_test_macros.hpp>
#include "common/fs/path_util.h"
#include "common/fs/profile_paths.h"

TEST_CASE("Bundle profile names cannot escape their profile directory", "[profile-paths]") {
    using namespace Common::FS;
    for (const auto* name : {"", ".", "..", "../eden", "/tmp/eden", "a/b", "a\\b"}) {
        REQUIRE_FALSE(IsValidProfileName(name));
    }
    REQUIRE_FALSE(IsValidProfileName(std::string(65, 'a')));
    REQUIRE(IsValidProfileName("mac-2.0.18"));
    const auto one = GetMacProfilePaths("/Users/example", "mac-2.0.18");
    const auto two = GetMacProfilePaths("/Users/example", "mac-2.0.19");
    REQUIRE(one.config != two.config);
    REQUIRE(one.cache != two.cache);
    REQUIRE(one.logs != two.logs);
}

TEST_CASE("A versioned bundle selects isolated settings and shader paths", "[.][profile-bundle]") {
    using namespace Common::FS;
    const auto* expected = std::getenv("EDEN_TEST_PROFILE_NAME");
    REQUIRE(expected != nullptr);
    REQUIRE(GetBundleProfileName() == expected);
    const auto paths = GetMacProfilePaths(GetHomeDirectory(), expected);
    REQUIRE(GetEdenPath(EdenPath::ConfigDir) == paths.config);
    REQUIRE(GetEdenPath(EdenPath::CacheDir) == paths.cache);
    REQUIRE(GetEdenPath(EdenPath::ShaderDir) == paths.cache / "shader");
    REQUIRE(GetEdenPath(EdenPath::LogDir) == paths.logs);
    REQUIRE(GetEdenPath(EdenPath::DumpDir) == paths.dumps);
    REQUIRE(GetEdenPath(EdenPath::CrashDumpsDir) == paths.crash_dumps);
    REQUIRE(GetEdenPath(EdenPath::NANDDir) != paths.config);
}
