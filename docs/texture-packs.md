# Optional texture packs and ReShade

Vulkan builds can replace 2D game textures with PNG files using Ryujinx's `tex2d_<XXH3-128>_<width>x<height>x1_0x0.png` naming scheme. Keep the original filenames and put the texture directory in `mods/textures/`. Subdirectories are supported. A directory link avoids copying a large installed pack; `SKYRECOMP_TEXTURE_DIR` can override the directory for testing. Restart the game after changing packs.

The renderer checks the original texture data, loads matching PNGs on demand, preserves the texture's color space and generates mipmaps. Unsupported textures, invalid PNGs and failed replacement allocations retain the original texture. Built-in button prompt edits take priority. The pack is not loaded into RAM all at once, but large replacements still use additional graphics and staging memory.

The Windows visual release includes the supplied [Henriko Magnifico Skyward Sword pack](https://www.henrikomagnifico.com/zelda-skyward-sword-4k), its shaders and presets as ordinary files. Nothing points back to the developer's texture folder. All matching textures are available throughout the game; textures absent from the pack keep their original appearance. The large asset files are not stored in the source repository.

In **ESC > Video**, **4K Textures** switches replacements on and off while playing. Already uploaded textures are refreshed even when the original game texture has not changed. The choice is saved in `skyrecomp.ini`.

## ReShade on Windows

Windows releases bundle the official ReShade 6.8.0 runtime, so players do not download or run a ReShade installer. The local Vulkan installation uses `reshade/ReShade64.dll` and `reshade/ReShade64.json` next to `skyrecomp.exe`, with `ReShade.ini`, the selected preset and `reshade-shaders/` beside the executable. The launcher enables this local layer only for its process; it does not register a system-wide layer. **ESC > Video > ReShade** saves the choice immediately. If ReShade is loaded, its effects switch on/off while playing and remain synchronized across swapchain recreation and returning to the launcher. Starting with Off prevents loading the layer, including an implicit ReShade layer; to turn it on from that state, close and reopen the application. `SKYRECOMP_RESHADE=0` disables local loading for troubleshooting, but environment variables cannot override the saved Off choice.

Shader and texture search paths must include the preset's subdirectories. The supplied Henriko configuration was adapted locally to use `reshade-shaders/Shaders/**` and `reshade-shaders/Textures/**`. Only enabled effects are compiled at startup.

The default preset is **HenrikoMagnifico_Ultra_NO_MOTION_BLUR_v8.ini**. Its supplied copy still enabled motion estimation and motion blur; these two techniques are disabled in the packaged no-motion-blur presets. The original files in the user's pack folder remain unchanged. Other presets retain their original settings, and their older motion estimation shader can report a read/write conflict with ReShade 6.8. The separate Remove blur setting controls the game's tilt-shift blur; it does not control ReShade.

## Validation

`tools/test_visual_pack.py` checks relocatable paths, complete copying, preset preparation, unsafe destination rejection and installer checksum rejection. `tools/test_texture_toggle.c`, linked with the runtime and its application `main` renamed, checks real Vulkan uploads through on/off/on changes without a new frame or modified game data, plus reading and saving both settings.

`tools/test_custom_tex.c` checks a known XXH3-128 vector, filename validation, loading replacement pixels, matching mip zero when a mip chain exists, changed source data, render-target exclusion and corrupt PNG fallback. Compile it with `runtime/nvn/tegra.c` and SDL3; pass an empty temporary directory.

Windows now backs guest heap pages on access instead of committing the entire requested heap at startup. File I/O commits guest buffers before entering the OS, and external GPU imports still commit the complete imported range. The optional vertex import falls back to copied vertices when Windows lacks memory headroom. `tools/test_guest_io.c`, linked with `runtime/platform/windows.c`, verifies lazy page access and file reads/writes using previously reserved pages; pass a temporary filename.

Windows/Vulkan testing covered the launcher-to-game transition at 3840x2160 and loading the initial bedroom at 1280x720 with 94 Henriko PNG replacements. ReShade was tested separately and together with the pack; the selected no-motion-blur preset compiled without shader errors. A complete playthrough and Metal support have not been validated.

## Building a Windows release

Run `make dist VISUAL_PACK=/path/to/4K` after preparing the normal Windows build dependencies and helpers. The pack directory must contain `reshade-shaders/`, the Henriko presets, and one directory of `tex2d_*.png` files (the supplied nested `4K texturas/contents/.../textures/` layout is supported). `PACK_PY` selects the build machine's Python executable if it is not `python3`.

`tools/fetch_reshade.py` downloads the official installer **only on the build machine**, checks its pinned SHA-256 and extracts just the 64-bit Vulkan runtime without executing the installer. `tools/package_visuals.py` copies the complete provided visual pack and writes portable configuration, credits and the [ReShade BSD license](https://github.com/crosire/reshade/blob/v6.8.0/LICENSE.md). Packaging fails if the visual assets are missing, rather than producing a release with empty folders. Neither script runs on players' machines.
