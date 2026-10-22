# Sky Recomp - Native PC Recompilation

**Sky Recomp** is a native cross-platform port (**Windows**, **Linux**, and **macOS**) based on static AArch64 -> C recompilation of *The Legend of Zelda: Skyward Sword HD*.

**Development status: experimental.** Full-game stability has not been established. Crashes, menu freezes and rendering differences are still being investigated. Use the [diagnostic log](docs/diagnostics.md) to report a problem; see the [review of reported issues](docs/video-review.md) for confirmed changes and remaining checks.

Source folders and local runtime files are described in [Repository layout](docs/repository-layout.md).

## v1.0.6 highlights: 4K Textures, Free Camera & Mouse Controls

### 4K Textures by Henriko Magnifico

The Windows visual release includes **568 replacement textures by [Henriko Magnifico](https://www.henrikomagnifico.com/zelda-skyward-sword-4k)**, together with his ReShade presets and the effects they need. All credit for the texture artwork and supplied presets belongs to Henriko Magnifico. Visit his project page to see and support his work. The pack enhances matching textures throughout the game; it does not replace every original texture.

Extract the complete Windows package and open `skyrecomp.exe`; no separate ReShade installation is needed. Your own prepared game files are still required. In **ESC > Video**, use **4K Textures** and **ReShade** to choose the look you prefer. The default preset is **Ultra without motion blur**. Loaded ReShade effects switch immediately; if you started with ReShade disabled, close and reopen the application after enabling it. See [texture pack instructions](docs/texture-packs.md). 4K replacements currently require Vulkan; the bundled ReShade setup is Windows-only.

### Free Camera

Look around with the right stick **without holding L**. Optional **Free camera look** offers adjustable sensitivity and a mode indicator; tap L to switch camera mode or hold it temporarily for sword-stick controls. This gives players a more comfortable way to explore while preserving the game's aiming and item actions. Special thanks to **kyrill-bo** for the free camera contribution in [#45](https://github.com/Apolinario-coder/SkyRecomp/pull/45).

### Keyboard & Mouse Controls

Turn the camera with the mouse, move with WASD, **left click to slash**, **right click to target/use the shield**, and **middle click to thrust**. Hold **Left Alt or Mouse 4** to aim an item; with the bow ready, hold left click to draw and release to fire. Enable **Free camera look** and **Mouse camera and combat** under **ESC > Controls** and use the game's button-only control mode. Mouse sensitivity is adjustable, and the cursor is released when opening the menu. See the [complete controls](docs/mouse-controls.md).

This release also adds resumable game setup, language fixes, crash diagnostics and a cleaner source tree. Experimental 120 FPS remains available from v1.0.5; it is not a new or fully validated feature in v1.0.6.

Thank you to [kyrill-bo](https://github.com/kyrill-bo) for the free camera contribution ([#45](https://github.com/Apolinario-coder/SkyRecomp/pull/45)), [LiamPalmqvist](https://github.com/LiamPalmqvist) for the macOS graphics/setup and build-resume contributions ([#43](https://github.com/Apolinario-coder/SkyRecomp/pull/43), [#41](https://github.com/Apolinario-coder/SkyRecomp/pull/41), [#42](https://github.com/Apolinario-coder/SkyRecomp/pull/42)), and [BlAnK182C-bak](https://github.com/BlAnK182C-bak) for the Linux AppImage contribution ([#29](https://github.com/Apolinario-coder/SkyRecomp/pull/29)). Their reviewed changes have been adapted into this release. Thank you also to everyone who supplied bug reports, logs and testing, and to [peiikai/LASRecomp](https://github.com/peiikai/LASRecomp) for the foundation of this port.

## Reporting bugs and crashes

Please [open a GitHub issue](https://github.com/Apolinario-coder/SkyRecomp/issues/new) with your version, operating system, CPU/GPU, graphics driver, location and steps to reproduce. Include your frame-rate setting, controller, language, and whether 4K Textures and ReShade were enabled.

On Windows, a separate diagnostic console opens alongside the game and follows **`skyrecomp.log` next to `skyrecomp.exe`**. If the game freezes, wait **at least 15 seconds**, then copy and attach the log **before starting the game again**, since the next launch replaces it. Closing the console does not close the game. Possible-freeze reports can also occur during long loading; please describe what was happening. Attach a screenshot or a save near the problem when available; check logs for personal paths before sharing. Linux/macOS users can capture terminal output and attach their system crash report if available. See [diagnostic instructions](docs/diagnostics.md).

> [!IMPORTANT]
> **No game files are included.** Sky Recomp requires your own legally obtained copy of the game files for *The Legend of Zelda: Skyward Sword HD* (**v1.0.0**, without updates).
> Instructions on how to obtain these files will **NOT** be provided.
> This project consists exclusively of the open-source static recompiler tooling, compatibility layers, and the native runtime.

![The Legend of Zelda: Skyward Sword HD running natively on PC](docs/gameplay.png)

---

## Port Features

- **Native C/C++ Execution**: This is NOT an emulator (no JIT or dynamic interpretation). Guest AArch64 (ARM64) machine instructions are statically decompiled and lifted to native C code (`-march=x86-64-v3` with AVX2 and FMA, or native ARM64 on Apple Silicon).
- **Multi-Platform Support**: Runs natively on:
  - **Windows**: Windows 10/11 (64-bit) with Vulkan 1.3+.
  - **Linux**: Ubuntu, Fedora, Arch, Steam Deck / SteamOS with Vulkan 1.3+ and standalone AppImage distribution.
  - **macOS**: Apple Silicon (M1/M2/M3/M4) and Intel Macs running macOS 11.0+ via native Metal 3 and MetalFX upscaling.
- **High-Performance Graphics Backends**: Shaders and rendering commands are translated directly into optimized **Vulkan 1.3/1.4** pipelines (Windows and Linux) and native Apple **Metal 3** (macOS).
- **1:1 Virtual Memory Layout**: The runtime reserves a 12.5 GB virtual address window matching the 39-bit guest address space, enabling pointer operations with zero address-translation overhead.
- **SDL3 Input & Controller Support**: Seamless support for modern gamepads (Xbox, DualSense/DualShock, Nintendo Pro Controller) and keyboard/mouse. Includes standard button-controls mapping (right stick sword combat).
- **Mouse Camera and Combat**: Optional mouse look, directional sword clicks, shield targeting and ranged item aiming. See the [keyboard and mouse guide](docs/mouse-controls.md).
- **60 FPS Default**: Supports native resolutions and ultrawide displays. Performance depends on the hardware, scene and shader compilation; a constant 60 FPS is not guaranteed.
- **Experimental 120 FPS Mode**: Choose **Video > Frame rate > 120 FPS** in the in-game settings. The shared game clock adjusts to elapsed time while audio retains its own clock. 60 FPS remains the default; reaching 120 FPS depends on your hardware and settings.
- **Included 4K Textures and ReShade on Windows**: Extract the visual release and launch the game; no separate ReShade installer is required. **ESC > Video** lets you switch 4K textures and loaded ReShade effects while playing. Starting with ReShade disabled requires reopening the application to enable it. Ultra without motion blur is the default preset. See [visual pack details](docs/texture-packs.md).
- **In-Game Settings Menu**: Press **`ESC`** at any time while playing to open the overlay menu (adjust graphics, toggle fullscreen/windowed, change aspect ratio, audio volume, or quit).
- **Cross-Platform Mod Manager**: Dedicated **Mods** tab with one-click folder opening (`ShellExecute` on Windows, `xdg-open` on Linux, `open` on macOS), live rescan, individual mod enable/disable toggles, and LayeredFS priority reordering. Supports loose `romfs/` files and `romfs_metadata.bin`.
- **Embedded Setup GUI**: No batch scripts or command-line steps required. The standalone launcher GUI allows picking your game files folder on first launch and boots directly to PLAY on subsequent launches.

---

## Architecture & Pipeline

```
[ User-Provided Game Files Folder ]
                  │
                  ▼ (One-time setup in ~10s)
[ Local Game Data & Assets Directory ]
                  │
                  ▼ (Pre-compiled Native Recompiled Library)
[ skygame.dll (Win) / skygame.so (Linux) / skygame.dylib (macOS) ]
                  │
                  ▼
[ skyrecomp.exe / skyrecomp / Sky Recomp.app ]
                  │
                  ▼
[ Native Execution at 60 FPS (Vulkan / Metal) ]
```

---

## Getting Started

### System Requirements

| Specification | Windows | Linux | macOS |
|---|---|---|---|
| **OS** | Windows 10/11 64-bit | Glibc 2.35+ (Ubuntu 22.04+, Arch, Fedora, SteamOS) | macOS 11.0+ (15.0+ recommended) |
| **CPU** | x86-64 with AVX2 & FMA (Intel 4th Gen+, AMD Zen+) | x86-64 with AVX2 or ARM64 | Apple Silicon (M1+) or Intel AVX2 |
| **GPU** | Vulkan 1.3+ (NVIDIA 900+, AMD RX 400+, Intel Arc) | Vulkan 1.3+ (RADV, ANV, NVIDIA 515+) | Apple Metal 3 (MetalFX supported) |
| **RAM** | 8 GB minimum (16 GB recommended) | 8 GB minimum (16 GB recommended) | 8 GB unified memory or higher |
| **Storage** | ~15 GB free space | ~15 GB free space | ~15 GB free space |

### How to Play (Release Packages)
1. Download the release package for your operating system:
   - **Windows**: `Sky Recomp-<version>-windows.zip`
   - **Linux**: `Sky Recomp-<version>-linux-x86_64.AppImage` (or `aarch64`)
   - **macOS**: `Sky Recomp-<version>-macOS.dmg`
2. Prepare your legally obtained, uncompressed game files of *The Legend of Zelda: Skyward Sword HD* (v1.0.0).
3. Start Sky Recomp:
   - **Windows**: Run `skyrecomp.exe`.
   - **Linux**: Make executable (`chmod +x *.AppImage`) and run.
   - **macOS**: Open the `.dmg`, drag **Sky Recomp** to Applications, and open.
4. On the **Game Files** setup screen, click **Game files folder** and select your game files.
5. Click **Set up game files**.
   The launcher imports prepared files; it does not decrypt NSP/XCI images or accept console keys. See [game file setup](docs/game-files.md).
6. Click **PLAY** to start the game! (Press **`ESC`** at any time while playing to open the settings menu).

---

## Compiling from Source

- **Windows**:
  Ensure **MSYS2 UCRT64** with `clang`, `lld`, and `make` is installed. Run:
  ```bash
  make build/skyrecomp.exe
  ```
- **Linux**:
  See detailed instructions in [docs/BUILD-linux.md](docs/BUILD-linux.md).
  ```bash
  make build/skyrecomp
  make appimage
  ```
- **macOS**:
  See detailed instructions in [docs/BUILD-macos.md](docs/BUILD-macos.md).
  ```bash
  make deps
  make app
  make dmg
  ```

---

## Acknowledgments & Special Thanks

Sky Recomp was made possible thanks to the groundbreaking work of the **[LAS Recomp](https://github.com/peiikai/LASRecomp)** project by [@peiikai](https://github.com/peiikai) and contributors. Much of the architectural design, ARM64-to-C static recompilation pipeline, Maxwell NVN translation layer, Vulkan/SDL3 compatibility runtime, and launcher UI framework were originally developed by LAS Recomp and readapted for *The Legend of Zelda: Skyward Sword HD*. We extend our deepest gratitude to the LAS Recomp project and its community for proving the power and feasibility of native Nintendo Switch static recompilations.

---

## Credits

### Projects & Foundations
- **[LAS Recomp](https://github.com/peiikai/LASRecomp)** — Static recompiler architecture, NVN translation layer, Vulkan runtime, and launcher framework (MIT License).

### Libraries and Tools
- **[SDL 3](https://libsdl.org)** — Windowing, event processing, gamepad bindings, and audio output (zlib license).
- **[FFmpeg](https://ffmpeg.org)** — Video and movie playback decoding using an LGPL configuration (LGPL 2.1+).
- **[Dear ImGui](https://github.com/ocornut/imgui)** — In-game overlay settings menu and launcher user interface (MIT License).
- **[Baloo 2 Font](https://github.com/EkType/Baloo2)** by Ek Type — Typography used across the launcher and menu (SIL Open Font License 1.1, `fonts/Baloo2-OFL.txt`).
- **[Vulkan Memory Allocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)** — GPU memory management for Vulkan (MIT License).
- **[SMAA](https://github.com/iryoku/smaa)** by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, and Diego Gutierrez — Subpixel Morphological Anti-Aliasing (MIT License).
- **[AMD FidelityFX Super Resolution 1 (FSR 1)](https://github.com/GPUOpen-Effects/FidelityFX-FSR)** — Spatial upscaler and sharpening filter, ported to GLSL (MIT License).
- **[shaderc](https://github.com/google/shaderc)** by Google — Online GLSL to SPIR-V shader compilation (Apache 2.0).
- **[Capstone Engine](https://www.capstone-engine.org)** — Disassembly framework for AArch64 binary analysis (BSD 3-clause).
- **[Unicorn Engine](https://www.unicorn-engine.org)** — CPU emulator framework used to evaluate and resolve indirect jump tables (GPL 2.0 / BSD).
- **[python-lz4](https://github.com/python-lz4/python-lz4)** — High-speed compression/decompression for Nintendo Switch NSO binaries (BSD 3-clause; LZ4: BSD 2-clause).
- **[envytools](https://github.com/envytools/envytools)** — NVIDIA GPU documentation used for the Maxwell shader translation layer.
- **[Switchbrew](https://switchbrew.org)** — Nintendo Switch technical documentation and system architecture references.

---

## Legal & License

Sky Recomp is an unofficial, community-driven fan project. It is not affiliated with, endorsed by, or sponsored by Nintendo. *The Legend of Zelda* and *Skyward Sword* are registered trademarks of Nintendo. **Game executables and original game data are not bundled.** Users must supply their own game files. Open-source dependencies, the launcher font and optional visual packs retain their authors' credits and applicable terms.

Sky Recomp's original code is licensed under the **[MIT License](LICENSE)**. Portions derived from [LAS Recomp](https://github.com/peiikai/LASRecomp) remain under the LAS Recomp copyright and license in **[LICENSE-LASRecomp](LICENSE-LASRecomp)**. Bundled and linked third-party components maintain their respective original licenses.

---

## Source Code of Bundled Third-Party Components

Release builds distribute pre-compiled binaries and dynamic libraries of open-source third-party dependencies. In compliance with open-source licensing requirements (LGPL, GPL, Apache 2.0, zlib, MIT, OFL), the corresponding source code for the exact versions bundled or used to build release artifacts is available at the links below:

| Component | Version | License | Source Code Repository / Download |
|---|---|---|---|
| **FFmpeg** (LGPL build, video decoding) | 9.0.2 | LGPL 2.1+ | https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz |
| **SDL 3** | 3.4.18 | zlib | https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18 |
| **shaderc** (GLSL to SPIR-V compiler) | 2026.4 | Apache 2.0 | https://github.com/google/shaderc |
| **libiconv** (character encoding conversion) | 1.19 | LGPL 2.1+ | https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.19.tar.gz |
| **GCC Runtime** (`libgcc`, `libstdc++`) | 16.2.0 | GPL 3.0 w/ Runtime Exception | https://gcc.gnu.org/ |
| **winpthreads** (mingw-w64 threading) | 14.0.0 | MIT / BSD | https://www.mingw-w64.org/ |
| **Baloo 2 Font** (SemiBold) | 1.640 | SIL OFL 1.1 | https://github.com/EkType/Baloo2 |
| **Dear ImGui** | 1.91.5 | MIT | https://github.com/ocornut/imgui/releases/tag/v1.91.5 |
| **Vulkan Memory Allocator (VMA)** | 3.1.0 | MIT | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/releases/tag/v3.1.0 |
| **xxHash** (texture pack lookup) | 0.8.3 | BSD 2-clause | https://github.com/Cyan4973/xxHash/releases/tag/v0.8.3 |
| **SMAA** | master (2013) | MIT | https://github.com/iryoku/smaa |
| **ReShade** (Windows visual release) | 6.8.0 | BSD 3-clause | https://github.com/crosire/reshade/tree/v6.8.0 |
| **Capstone Engine** (recompiler tooling) | 5.0.9 | BSD 3-clause | https://github.com/capstone-engine/capstone/releases/tag/5.0.9 |
| **Unicorn Engine** (recompiler tooling) | 2.1.4 | GPL 2.0 / BSD | https://github.com/unicorn-engine/unicorn/releases/tag/2.1.4 |
| **python-lz4** (recompiler tooling) | 4.4.5 | BSD 3-clause | https://github.com/python-lz4/python-lz4/releases/tag/v4.4.5 |

The Windows release libraries (`SDL3`, `shaderc`, `libiconv`, and runtime DLLs) are built via the MSYS2 UCRT64 toolchain environment ([MSYS2 Packages](https://packages.msys2.org)).
