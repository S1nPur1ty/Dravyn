# Dravyn on macOS

The public preview targets Apple Silicon. Its bundled Homebrew libraries currently require
macOS 26, even though the emulator executable itself was built with a macOS 15 deployment
target. Do not advertise an older minimum until all dependencies have been rebuilt for it.

## Build

Use a full Xcode installation and its command-line tools, Homebrew, CMake, and Ninja.
The release environment uses Qt 6, SDL 3, FFmpeg, Boost, OpenSSL, libusb, and their dependencies.
Install the development packages before configuring:

```sh
brew install cmake ninja pkg-config qt sdl3 ffmpeg boost openssl@3 libusb
```

Dependency versions and other fetched libraries are specified in `cpmfile.json` and CMake.
The customized MoltenVK/SPIRV-Cross source revisions and patches are preserved in
`tools/macos/build-moltenvk.sh`. Build that driver first:

```sh
bash tools/macos/build-moltenvk.sh
cmake -S . -B build/macos-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DYUZU_USE_BUNDLED_QT=OFF -DYUZU_USE_BUNDLED_SDL3=OFF \
  -DYUZU_USE_BUNDLED_FFMPEG=OFF -DYUZU_USE_BUNDLED_OPENSSL=OFF \
  -DYUZU_USE_BUNDLED_MOLTENVK=OFF \
  -DMOLTENVK_LIBRARY="$PWD/build/macos-moltenvk-source/Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" \
  -DENABLE_QT_TRANSLATION=OFF -DUSE_DISCORD_PRESENCE=OFF \
  -DENABLE_UPDATE_CHECKER=OFF -DENABLE_WERROR=OFF
cmake --build build/macos-arm64 --target yuzu --parallel 8
macdeployqt build/macos-arm64/bin/Dravyn.app -no-codesign \
  -libpath="$(brew --prefix)/lib"
```

These are build instructions, not a claim of bit-for-bit reproducibility. Compiler, SDK,
and installed library versions affect the output. Preserve required license notices when
redistributing dependencies.

## Public package

Use a fresh output directory and a deployed app bundle. `package_release.py` removes
capture environment settings, uses a stable public profile, includes license notices,
ad-hoc signs the app, and creates a DMG plus SHA-256 checksum:

```sh
python3 tools/macos/package_release.py \
  --bundle build/macos-arm64/bin/Dravyn.app \
  --binary build/macos-arm64/bin/Dravyn.app/Contents/MacOS/Dravyn \
  --output build/public-release --version 2.0.54 --minimum-os 26.0
```

Check all bundled Mach-O minimum OS values and `otool -L` dependencies before release.
No game files, keys, firmware, profiles, saves, logs, shader caches, or GPU captures belong
in the package. Publish a release against the exact source commit used to build the executable.
This packaging process does not notarize the app with Apple.

## Profiles and diagnostics

The public package uses `EglantineProfile=dravyn`, preserving that name across updates.
Configuration and logs live under `~/Library/Application Support/Eglantine/Profiles/dravyn`;
caches live under `~/Library/Caches/Eglantine/dravyn`. Game data retains the existing data
path conventions for compatibility. Developer diagnostic packages use separate profiles.

On-demand capture tooling is documented in [macOS GPU capture](../developer/macos-gpu-capture.md).
Capture is not enabled in the public package. Do not ship local diagnostic environment paths.
