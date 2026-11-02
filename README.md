# Dravyn

<img src="dist/dravyn.png" alt="Dravyn dragon" width="128" />

**Dravyn is a macOS-focused fork of [Eglantine](https://github.com/diamond3500/Eglantine), with significant improvements for Apple Silicon Macs.** It combines native ARM64 execution improvements, Mac memory mapping and synchronization work, GPU command and resource handling improvements, and a redesigned game library and settings interface. Its Vulkan renderer runs through MoltenVK.

User testing on an **Apple M4 Max** reports smooth Mario Kart World gameplay, with screenshots showing approximately **59–60 FPS at 1× scale** in some scenes. This is a reported result from that Mac, not a guarantee for every scene, game, or Apple Silicon model.

**Preview software.** Game compatibility varies. Missing character geometry, reflections, and other rendering issues remain under investigation. A successful build is not a guarantee that every game works.

- [Website and download](https://dravyn-six.vercel.app)
- [Mac preview releases](https://github.com/S1nPur1ty/Dravyn/releases)
- [Build and package for macOS](docs/build/Dravyn-macOS.md)
- [Report an issue](https://github.com/S1nPur1ty/Dravyn/issues)
- [Upstream project documentation](docs/UPSTREAM_README.md)

## Mac preview

The distributed ARM64 build requires an Apple Silicon Mac running macOS 26 or later, matching its bundled dependencies. Intel Macs are not supported by this download. The app is ad-hoc signed, not Apple-notarized.

Open the DMG, drag Dravyn to Applications, and launch it. If macOS blocks the app, review the warning and use System Settings → Privacy & Security → Open Anyway only if you trust the download. Do not disable Gatekeeper globally.

Game files, firmware, and console keys are not included. Supply your own required files through the app. The public build has its own stable `dravyn` settings/cache profile; experimental diagnostics are disabled by default.

## Hardware and memory

| Component | Current status |
| --- | --- |
| Platform | Apple Silicon, macOS 26 or later for the distributed preview |
| Tested configuration | Apple M4 Max with 128 GB unified memory |
| Minimum chip and RAM for smooth gameplay | Not yet established through testing on lower-spec Macs |
| Graphics memory | Shared with system memory on Apple Silicon; leave room for macOS and other apps |

**Memory use varies with the game, scene, shader compilation, and cache state.** Saved macOS process samples from November 1, 2026 measured approximately **2.4–2.5 GB during startup/loading**, and **36.7 GB and 40.7 GB physical footprints in earlier development builds (2.0.1 and 2.0.2)**. These are historical observations, not a typical range or a measured peak for the current 2.0.54 release. The smaller loading figures must not be used as gameplay RAM requirements.

The tested Mac's 128 GB is not a minimum requirement. Conversely, we cannot currently promise that a 16 GB or 32 GB Mac will run demanding games smoothly. A reliable minimum needs current-release measurements across representative games and lower-memory Macs. The emulator's guest memory setting (for example, “4 GB DRAM”) is emulated console memory, not the app's total Mac memory use.

To assess your Mac, open **Activity Monitor → Memory** while playing. Check Dravyn's memory use during shader preparation and an extended gameplay session, together with overall **Memory Pressure** and **Swap Used**. Record the app version, game, scale, Mac model, and installed memory when reporting results. Persistent memory pressure or growing swap indicates insufficient headroom for that workload; enough RAM alone does not guarantee the desired frame rate.

## Credits and license

Dravyn is derived from [Eglantine](https://github.com/diamond3500/Eglantine), Eden, Yuzu, and Sudachi. Original copyright and license notices are retained. See [LICENSE.txt](LICENSE.txt), [LICENSES](LICENSES), and individual file headers for applicable terms. It is an independent project, not affiliated with Nintendo.

The macOS changes and interface were developed with AI assistance. Current build/package verification does not replace gameplay testing or human code review.
