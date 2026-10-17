# Sky Recomp - Native PC Recompilation

**Sky Recomp** is a native PC port (Windows x86-64 with Vulkan) based on static AArch64 -> C recompilation of *The Legend of Zelda: Skyward Sword HD*.

> [!IMPORTANT]
> **No game files are included.** Sky Recomp requires your own legally obtained copy of the game files for *The Legend of Zelda: Skyward Sword HD* (**v1.0.0**, without updates).
> Instructions on how to obtain these files will **NOT** be provided.
> This project consists exclusively of the open-source static recompiler tooling, compatibility layers, and the Vulkan/SDL3 native runtime.

![The Legend of Zelda: Skyward Sword HD running natively on PC via Vulkan](docs/gameplay.png)

---

## 🚀 Port Features

- **Native C/C++ Execution**: This is NOT an emulator (no JIT or dynamic interpretation). Guest AArch64 (ARM64) machine instructions are statically decompiled and lifted to native C code (`-march=x86-64-v3` with AVX2 and FMA).
- **Vulkan 1.3/1.4 Graphics Backend**: Graphics API calls and shaders are translated directly into high-performance Vulkan pipelines for NVIDIA, AMD, and Intel GPUs.
- **1:1 Virtual Memory Layout**: The runtime reserves a 12.5 GB virtual address window matching the 39-bit guest address space, enabling pointer operations with zero address-translation overhead.
- **SDL3 Input & Controller Support**: Seamless support for modern gamepads (Xbox, DualSense/DualShock, Nintendo Pro Controller) and keyboard/mouse. Includes standard button-controls mapping (right stick sword combat).
- **Solid 60 FPS Performance**: Harnesses modern multi-core CPUs and dedicated desktop GPUs for rock-solid 60.0 FPS gameplay at native resolutions (1080p, 1440p, 4K, and Ultrawide).
- **In-Game Settings Menu**: Press **`ESC`** at any time while playing to open the overlay menu (adjust graphics, toggle fullscreen/windowed, change aspect ratio, audio volume, or quit).
- **Embedded Setup GUI**: No batch scripts or command-line steps required. The standalone launcher GUI allows picking your game files folder on first launch and boots directly to PLAY on subsequent launches.

---

## 📐 Architecture & Pipeline

```
[ User-Provided Game Files Folder ]
                  │
                  ▼ (One-time setup in ~10s)
[ Local Game Data & Assets Directory ]
                  │
                  ▼ (Pre-compiled Native Recompiled Library)
[ skygame.dll ]
                  │
                  ▼ (skyrecomp.exe)
[ Native PC Execution via Vulkan at 60 FPS! ]
```

---

## 🎮 Getting Started

### System Requirements
- **Operating System**: Windows 10/11 64-bit
- **Processor**: x86-64 CPU with AVX2 support (Intel Haswell / 4th Gen Core or newer, AMD Ryzen or newer)
- **Graphics Card**: Vulkan 1.3+ compatible GPU (NVIDIA GeForce 900 series+, AMD Radeon RX 400+, Intel Arc / Iris Xe)
- **RAM**: 8 GB minimum (16 GB recommended)
- **Storage**: ~15 GB free space for game assets

### How to Play (Release Package)
1. Download the latest release `.zip` package and extract it to a folder.
2. Prepare your own legally obtained, uncompressed game files of *The Legend of Zelda: Skyward Sword HD* (v1.0.0) in a folder on your PC.
3. Launch `skyrecomp.exe`.
4. On the **Game Files** setup screen:
   - Click **Game files folder** and select the folder containing your game files.
5. Click **Set up game files**.
   - The launcher automatically prepares and validates the game files in seconds.
6. Click **PLAY** to start the game! (Press **`ESC`** at any time while playing to open the settings menu).

### Developer Setup (Compiling from Source)
If you wish to build or modify the runtime from source:
1. Ensure **MSYS2 UCRT64** with `clang`, `lld`, and `make` is installed.
2. Compile the launcher runtime:
   ```bash
   make build/skyrecomp.exe
   ```

---

## 🙏 Acknowledgments & Special Thanks

Sky Recomp was made possible thanks to the groundbreaking work of the **[LAS Recomp](https://github.com/peiikai/LASRecomp)** project by [@peiikai](https://github.com/peiikai) and contributors. Much of the architectural design, ARM64-to-C static recompilation pipeline, Maxwell NVN translation layer, Vulkan/SDL3 compatibility runtime, and launcher UI framework were originally developed by LAS Recomp and readapted for *The Legend of Zelda: Skyward Sword HD*. We extend our deepest gratitude to the LAS Recomp project and its community for proving the power and feasibility of native Nintendo Switch static recompilations.

---

## 🎖️ Credits

### Projects & Foundations
- **[LAS Recomp](https://github.com/peiikai/LASRecomp)** — Static recompiler architecture, NVN translation layer, Vulkan runtime, and launcher framework (MIT License).

### Libraries and Tools
- **[SDL 3](https://libsdl.org)** — Windowing, event processing, gamepad bindings, and audio output (zlib license).
- **[FFmpeg](https://ffmpeg.org)** — Video and movie playback decoding using an LGPL configuration (LGPL 2.1+).
- **[Dear ImGui](https://github.com/ocornut/imgui)** — In-game overlay settings menu and launcher user interface (MIT License).
- **[Baloo 2 Font](https://github.com/EkType/Baloo2)** by Ek Type — Typography used across the launcher and menu (SIL Open Font License 1.1, `Baloo2-OFL.txt`).
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

## ⚖️ Legal & License

Sky Recomp is an unofficial, community-driven fan project. It is not affiliated with, endorsed by, or sponsored by Nintendo. *The Legend of Zelda* and *Skyward Sword* are registered trademarks of Nintendo. **No copyrighted game files, binaries, textures, models, music, or fonts are included** in this repository or in release archives. Users must supply their own legally obtained game files.

Sky Recomp's original code is licensed under the **[MIT License](LICENSE)**. Bundled and linked third-party components maintain their respective original licenses.

---

## 📦 Source Code of Bundled Third-Party Components

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
| **SMAA** | master (2013) | MIT | https://github.com/iryoku/smaa |
| **Capstone Engine** (recompiler tooling) | 5.0.9 | BSD 3-clause | https://github.com/capstone-engine/capstone/releases/tag/5.0.9 |
| **Unicorn Engine** (recompiler tooling) | 2.1.4 | GPL 2.0 / BSD | https://github.com/unicorn-engine/unicorn/releases/tag/2.1.4 |
| **python-lz4** (recompiler tooling) | 4.4.5 | BSD 3-clause | https://github.com/python-lz4/python-lz4/releases/tag/v4.4.5 |

The Windows release libraries (`SDL3`, `shaderc`, `libiconv`, and runtime DLLs) are built via the MSYS2 UCRT64 toolchain environment ([MSYS2 Packages](https://packages.msys2.org)).

