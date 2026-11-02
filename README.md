<p align="center">
  <img src="dist/dravyn.png" alt="Dravyn dragon" width="160" />
</p>
<h1 align="center">Dravyn</h1>
<p align="center"><strong>Your games. Your Mac.</strong></p>
<p align="center">A fork of Eglantine with significant improvements for Apple Silicon.</p>
<p align="center">
  <a href="https://dravyn-six.vercel.app"><strong>Download for Mac</strong></a>
  &nbsp; · &nbsp;
  <a href="https://github.com/S1nPur1ty/Dravyn/releases">Releases</a>
  &nbsp; · &nbsp;
  <a href="docs/build/Dravyn-macOS.md">Build</a>
  &nbsp; · &nbsp;
  <a href="https://github.com/S1nPur1ty/Dravyn/issues">Report an issue</a>
</p>

---

### Made for the Mac

A clean game library, familiar controller setup, and a refined dark interface. Underneath: ARM64 execution, memory mapping, synchronization, and GPU resource improvements, with Vulkan graphics through MoltenVK.

**Smooth on M4 Max.** User testing reports around **59–60 FPS at 1×** in some Mario Kart World scenes. Results vary by game, scene, and Mac.

| Mac preview | Requirements |
| :--- | :--- |
| Platform | Apple Silicon · macOS 26+ |
| Tested Mac | M4 Max · 128 GB unified memory |
| Minimum RAM | Not yet verified — see memory notes below |

<details>
<summary><strong>Getting started</strong></summary>

1. [Download the DMG](https://dravyn-six.vercel.app) and drag Dravyn to Applications.
2. Add your game folder and required console files.
3. Connect your controller and check its bindings in Settings.

Games, firmware, and keys are not included. This preview is ad-hoc signed, not Apple-notarized. If macOS blocks it, review the warning and use **System Settings → Privacy & Security → Open Anyway** only if you trust the download. Do not disable Gatekeeper globally.

The public build uses a stable `dravyn` settings/cache profile. Experimental diagnostics are disabled by default. This download does not support Intel Macs.

</details>

<details>
<summary><strong>Memory usage & hardware guidance</strong></summary>

Memory use depends on the game, scene, shader compilation, and caches. Apple Silicon shares memory between the CPU and GPU; macOS and other apps also need headroom.

| Historical observation | Process physical footprint |
| :--- | ---: |
| Startup/loading samples | 2.4–2.5 GB |
| Development build 2.0.1 | 36.7 GB |
| Development build 2.0.2 | 40.7 GB |

These macOS samples were collected on **November 1, 2026**. They are not a typical range or a measured peak for the current release. Loading figures are not gameplay RAM requirements, and the tested Mac's 128 GB is not a minimum.

A reliable minimum needs current-release measurements across representative games and lower-memory Macs. Smooth performance on 16 GB or 32 GB Macs has not been established. The emulator's “4 GB DRAM” setting describes guest console memory, not total Mac memory usage.

To assess your Mac, use **Activity Monitor → Memory** during shader preparation and an extended gameplay session. Check Dravyn's memory, overall **Memory Pressure**, and **Swap Used**. Report the version, game, scale, Mac model, and installed memory with your results. Persistent pressure or growing swap indicates insufficient headroom for that workload; enough RAM alone does not guarantee the desired frame rate.

</details>

<details>
<summary><strong>Preview status</strong></summary>

Compatibility varies. Missing character geometry, reflections, and other rendering issues remain under investigation. Build and package verification does not replace gameplay testing or human review.

The M4 Max performance above is user-reported, including screenshots near 60 FPS at 1× in some scenes. It is not a benchmark guarantee for every scene or Mac.

</details>

<details>
<summary><strong>Credits & license</strong></summary>

Dravyn is a fork of [Eglantine](https://github.com/diamond3500/Eglantine), built on the work of Eden, Yuzu, and Sudachi. Original copyright and license notices are retained.

[License](LICENSE.txt) · [Third-party notices](LICENSES) · [Upstream documentation](docs/UPSTREAM_README.md)

The macOS changes and interface were developed with AI assistance. Dravyn is an independent project, not affiliated with Nintendo.

</details>
