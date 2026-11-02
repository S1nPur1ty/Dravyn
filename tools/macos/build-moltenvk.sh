#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the pinned driver with native texture atomics and deferred shader-cache compilation.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../.." && pwd)
driver_source="${1:-$repo_root/build/macos-moltenvk-source}"
revision=bf6d2d4b304c95e7de8feef133cc80ee917d027f
if [[ ! -d "$driver_source/.git" ]]; then
    git clone --depth 1 --branch v1.4.1-ryujinx \
        https://github.com/V380-Ori/Ryujinx.MoltenVK.git "$driver_source"
fi
[[ "$(git -C "$driver_source" rev-parse HEAD)" == "$revision" ]] || {
    echo 'Unexpected MoltenVK source revision.' >&2
    exit 1
}
cache_patch="$repo_root/tools/macos/moltenvk-shader-cache.patch"
git -C "$driver_source" diff --cached --exit-code --quiet
if git -C "$driver_source" diff --exit-code --quiet; then
    git -C "$driver_source" apply "$cache_patch"
elif ! cmp -s "$cache_patch" <(git -C "$driver_source" diff --binary); then
    echo 'MoltenVK source has unexpected changes; refusing to use it.' >&2
    exit 1
fi
cd "$driver_source"
cross_source="$driver_source/External/SPIRV-Cross"
cross_patch="$repo_root/tools/macos/spirv-cross-subgroup-fence.patch"
# fetchDependencies force-checks out dependencies. Preserve unrelated local work.
if [[ -d "$cross_source/.git" ]]; then
    git -C "$cross_source" diff --cached --exit-code --quiet
    if ! git -C "$cross_source" diff --exit-code --quiet &&
       ! cmp -s "$cross_patch" <(git -C "$cross_source" diff --binary); then
        echo 'SPIRV-Cross source has unexpected changes; refusing to reset it.' >&2
        exit 1
    fi
fi
./fetchDependencies --none
[[ "$(git -C "$cross_source" rev-parse HEAD)" == adec7acbf41a988713cdb85f93f26c8ca5ea863e ]] || {
    echo 'Unexpected SPIRV-Cross source revision.' >&2
    exit 1
}
git -C "$cross_source" diff --cached --exit-code --quiet
if git -C "$cross_source" diff --exit-code --quiet; then
    git -C "$cross_source" apply "$cross_patch"
elif ! cmp -s "$cross_patch" <(git -C "$cross_source" diff --binary); then
    echo 'SPIRV-Cross source has unexpected changes; refusing to use it.' >&2
    exit 1
fi
# Dependencies must be built after patching: the MoltenVK make target links an
# already-built SPIRV-Cross archive and does not rebuild its source itself.
KEEP_CACHE=Y xcodebuild -project ExternalDependencies.xcodeproj -scheme ExternalDependencies-macOS \
    -destination 'generic/platform=macOS' -configuration Release \
    -derivedDataPath External/build/Intermediates/macOS -quiet build
make macos MVK_USE_MSL_2_4=0
printf '\nConfigure Eglantine with:\n  -DYUZU_USE_BUNDLED_MOLTENVK=OFF\n  -DMOLTENVK_LIBRARY=%s/Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib\n' "$driver_source"
