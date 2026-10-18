# Building Sky Recomp on Linux

This guide details how to build and package **Sky Recomp** on Linux distributions.

---

## 1. System Requirements

- **Distribution**: Modern 64-bit Linux distribution (Ubuntu 22.04+, Fedora 38+, Arch Linux, Debian 12+, SteamOS 3+).
- **Processor**: x86-64 CPU with AVX2 and FMA support (Intel 4th Gen Core / AMD Zen or newer). ARM64 (AArch64) is also supported.
- **GPU**: Vulkan 1.3+ capable GPU with modern drivers:
  - **AMD**: Mesa RADV (recommended) or AMDVLK
  - **NVIDIA**: Driver version 515+
  - **Intel**: Mesa ANV (Iris Xe, Arc, or newer)
- **Windowing & Display**: Wayland or X11 (handled natively by SDL3).

---

## 2. Dependencies

### Ubuntu / Debian (22.04+)
```bash
sudo apt update
sudo apt install -y \
    build-essential \
    clang \
    lld \
    make \
    cmake \
    ninja-build \
    pkg-config \
    patchelf \
    libvulkan-dev \
    libasound2-dev \
    libpulse-dev \
    libpipewire-0.3-dev \
    libwayland-dev \
    libx11-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxi-dev \
    libxkbcommon-dev
```

> [!NOTE]
> Sky Recomp requires **SDL3** and **shaderc**. If your distribution does not package SDL3 yet, you can build dependencies locally or use the containerized AppImage builder.

### Arch Linux / Manjaro
```bash
sudo pacman -S --needed \
    base-devel \
    clang \
    lld \
    make \
    cmake \
    ninja \
    pkgconf \
    vulkan-devel \
    vulkan-headers \
    shaderc \
    ffmpeg \
    sdl3
```

### Fedora (38+)
```bash
sudo dnf install -y \
    clang \
    lld \
    make \
    cmake \
    ninja-build \
    pkgconf-pkg-config \
    vulkan-loader-devel \
    vulkan-headers \
    shaderc-devel \
    ffmpeg-free-devel \
    SDL3-devel
```

---

## 3. Compiling the Binary

To compile the native Linux executable directly:

```bash
make build/skyrecomp
```

The resulting binary will be placed at `build/skyrecomp`.

---

## 4. Building the Standalone AppImage

Sky Recomp provides automated containerized AppImage creation using Podman / Docker to ensure compatibility across all glibc 2.35+ Linux distributions:

```bash
# Build x86_64 AppImage
make appimage

# Or build both x86_64 and aarch64 AppImages
make appimages
```

The generated standalone AppImage is written to:
```
build/dist/Sky Recomp-<version>-linux-x86_64.AppImage
```

Make it executable and run:
```bash
chmod +x "build/dist/Sky Recomp-<version>-linux-x86_64.AppImage"
./"build/dist/Sky Recomp-<version>-linux-x86_64.AppImage"
```

---

## 5. Data Directory & Mods on Linux

On Linux, Sky Recomp follows the XDG Base Directory specification:
- **Game Data & Saves**: `$XDG_DATA_HOME/skyrecomp` (defaults to `~/.local/share/skyrecomp/`)
- **Logs**: `~/.local/share/skyrecomp/skyrecomp.log`
- **Mods Folder**: `~/.local/share/skyrecomp/mods/`
  - Clicking **"Open Mods Folder"** in the in-game settings menu (`ESC`) launches `xdg-open` to open your default desktop file manager (Nautilus, Dolphin, Thunar, etc.) directly in the mods folder.
