# Building Sky Recomp on macOS

This guide details how to build and package **Sky Recomp** on macOS (Apple Silicon arm64 and Intel x86-64).

---

## 1. System Requirements

- **Operating System**: macOS 11.0 Big Sur or later (macOS 15.0+ recommended for MetalFX upscaling).
- **Architecture**:
  - **Apple Silicon (M1 / M2 / M3 / M4)**: Native AArch64 execution (optimum performance and efficiency).
  - **Intel Mac**: x86-64 CPU with AVX2 support.
- **Graphics**: Native Apple **Metal** backend with MetalFX spatial/temporal upscaling support.

---

## 2. Prerequisites

1. **Xcode Command Line Tools**:
   ```bash
   xcode-select --install
   ```

2. **Package Manager & Build Tools**:
   Using [Homebrew](https://brew.sh):
   ```bash
   brew install cmake ninja
   ```

---

## 3. Building Shipped Dependencies

Sky Recomp bundles its own pinned, relocatable versions of SDL3 and an LGPL-only build of FFmpeg targeting Apple Silicon and macOS:

```bash
make deps
```

This compiles:
- `libSDL3.0.dylib`
- `libavcodec.dylib`, `libavformat.dylib`, `libavutil.dylib`, `libswscale.dylib`
- Relocatable Python & keystone helpers for the seed/code generators

---

## 4. Compiling the Application Bundle (`.app`)

Once dependencies are compiled, package the macOS application bundle:

```bash
make app
```

This command:
1. Compiles the native binary `build/mac15/skyrecomp`.
2. Packages the executable, frameworks, icon, resources, and compiler into `build/Sky Recomp.app`.
3. Performs automatic `@rpath` rewriting with `install_name_tool`.
4. Applies ad-hoc codesigning with `codesign`.

---

## 5. Creating the Installer Disk Image (`.dmg`)

To create a release `.dmg` with the standard drag-to-Applications shortcut:

```bash
make dmg
```

The output disk image is generated at:
```
build/dist/Sky Recomp-<version>-macOS.dmg
```

Double-click the `.dmg` file and drag **Sky Recomp** to your **Applications** folder.

---

## 6. Data Directory & Mods on macOS

On macOS, Sky Recomp keeps all user data separate from the `.app` bundle:
- **Game Data & Saves**: `~/Library/Application Support/com.skyrecomp.app/`
- **Mods Folder**: `~/Library/Application Support/com.skyrecomp.app/mods/`
  - Clicking **"Open Mods Folder"** in the in-game settings menu (`ESC`) invokes macOS `open` to automatically reveal the folder in Finder.
