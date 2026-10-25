# Changelog

## [2.0.0] - 2026-10-25

This update focuses on stability, eliminating unexpected crashes and freezes during gameplay (especially at 60 and 120 FPS).

### Crash and freeze fixes
- **Fixed random gameplay crashes:** The game no longer crashes unexpectedly during gameplay or in scenes with heavy draw activity at 60 and 120 FPS.
- **Better GPU compatibility:** Resolved video memory issues that caused crashes on graphics cards with limited memory or without hardware features like Resizable BAR.
- **Texture stability:** Safer loading for custom texture packs, preventing freezes when loading new areas.
- **Setup fix:** Fixed a crash that could occur during the initial ROM setup on Windows.

### Windows v1.0.6 setup hotfix - 2026-10-22

- Fixed the packaged game-code generator terminating before writing a log during game setup. The Windows compatibility adjustment now runs automatically whenever the helper is packaged.
- Failed generator reports now include the exit code and diagnostic log path, including when the helper produces no output.

## [1.0.6] - 2026-10-21

**Sharper visuals. Freer camera control. A new way to play with keyboard and mouse.** This release brings three major upgrades: Henriko Magnifico's 4K texture pack, an optional free camera, and mouse camera/combat controls.

## The highlight: 4K Textures by Henriko Magnifico

The **Windows visual package includes 568 replacement textures by [Henriko Magnifico](https://www.henrikomagnifico.com/zelda-skyward-sword-4k)**, his ReShade presets, and the effects they need. All credit for the texture artwork and supplied presets belongs to Henriko Magnifico — thank you for making this visual upgrade possible! Please visit his page to see and support his work.

The pack improves matching textures throughout the game, rather than only the opening room. It does **not** replace every original texture; anything absent from the pack keeps its original appearance.

- Extract the entire Windows ZIP and open **skyrecomp.exe**. No separate ReShade download or installation is needed.
- Your own prepared **Skyward Sword HD v1.0.0** game files are still required. Game data and console keys are not included.
- Open **ESC > Video** to enable or disable **4K Textures** and **ReShade**. Texture changes and already-loaded effects apply while playing and are saved.
- **Ultra without motion blur** is the default ReShade preset. Unwanted motion-blur effects were removed from the supplied no-motion-blur presets.
- If the application started with ReShade disabled, enable it and close/reopen the application to load it.
- 4K replacements currently use Vulkan. The bundled ReShade setup is Windows-only; the macOS package does not include the 4K/ReShade visual pack.

## Highlight: Free Camera

Camera control has been a recurring point of frustration for players. The new optional **Free Camera** lets you look around with the right stick without holding L, making exploration more comfortable. Thank you to **[@kyrill-bo](https://github.com/kyrill-bo)** for the contribution in [#45](https://github.com/Apolinario-coder/SkyRecomp/pull/45).

- Added an optional **free camera**, adjustable camera sensitivity, a camera-mode indicator and a smaller FPS display.
- Tap **L** on a controller to switch camera mode; hold it temporarily for sword-stick controls. Existing aiming and item controls remain available.
## Highlight: Keyboard & Mouse Controls

Enable **Free camera look** and **Mouse camera and combat** in **ESC > Controls**, and use the game's button-only control mode. Move the mouse to look around, explore with WASD, and use mouse buttons for combat and items:

- **Left click:** sword slash. Move the mouse before clicking to choose a direction.
- **Right click:** hold target lock; pressing also sends a shield bash when a shield is equipped.
- **Middle click:** sword thrust.
- **Hold Left Alt or Mouse 4:** aim the selected item with the mouse. With the bow ready, hold left click to draw and release to fire.
- Adjust **Mouse sensitivity** in the menu. Tap **Q** to switch free camera mode; hold it temporarily for the original sword-stick behavior.
- Mouse controls release the cursor in the ESC menu and clear pending actions when focus is lost. Explicit mouse bindings take priority over shortcuts.
- See the [complete keyboard/mouse controls](https://github.com/Apolinario-coder/SkyRecomp/blob/v1.0.6/docs/mouse-controls.md), including item selection and bow aiming. Use the game's button-only control mode.

## Fixes and improvements

- ReShade now obeys its saved Off choice, switches loaded effects immediately, and keeps the choice when returning to the launcher or recreating the window.
- Texture replacement can be toggled during play, including textures already displayed. Replacements load as needed, with original textures retained when a replacement cannot be loaded.
- Reduced Windows memory use during startup and texture loading; corrected buffer handling and fallback behavior when memory is limited.
- Fixed a Windows memory allocation race and error handling for failed game thread creation. These address identified failure cases; other crashes may still occur.
- Made input sampling consistent across repeated reads and independent of stalled rendering.
- Fixed Windows configuration files causing language and other text settings to fall back to defaults.
- Improved automatic system-language detection, including **Simplified Chinese (`zh-Hans` / `zh-CN`)** and Traditional Chinese variants.
- Changing language in the ESC menu now shows when a restart is needed and offers **Apply language and restart...**. Save first: unsaved progress will be lost when restarting. The port's settings interface remains in English.
- Fixed identified macOS screen-clearing crashes and shader transparency handling, and improved game setup on Apple Silicon.
- Interrupted game setup can resume completed work. Reused work is checked against the current compiler, sources and headers.
- Added Linux AppImage packaging with the required video libraries, alongside the existing Linux archives.
- Corrected font packaging and included missing third-party license notices.
- Updated the launcher version to **1.0.6**.

## Crash and freeze reports: please include your log

Windows now opens a separate **SkyRecomp diagnostics** console and writes **skyrecomp.log next to skyrecomp.exe**. Closing that console does not close the game. It remains available after a crash.

If the game freezes:

1. Wait **at least 15 seconds** so the automatic diagnostic report can be written. Long loading can also produce a possible-freeze report; tell us what was happening.
2. Copy **skyrecomp.log before launching again** — the next normal launch replaces it.
3. [Open an issue](https://github.com/Apolinario-coder/SkyRecomp/issues/new) and attach the log. Include **v1.0.6**, your operating system, CPU/GPU, graphics driver, location, action, controller, language and frame-rate setting, plus whether **4K Textures** and **ReShade** were enabled.
4. Add a screenshot and a save near the problem if available. Check logs for personal paths before sharing. Linux/macOS users can attach captured terminal output and system crash reports.

Logs are not uploaded automatically. [More diagnostic details](https://github.com/Apolinario-coder/SkyRecomp/blob/v1.0.6/docs/diagnostics.md).

## A cleaner main branch

- Removed compiled programs, DLLs, obsolete extraction tools and local data placeholders from the current source tree.
- Grouped fonts under **fonts/** and build helpers under **tools/**, and removed redundant resource/header copies.
- Retired the old image-specific extractor containing a fixed key and clarified the prepared-game-file setup instructions.
- Added source checks to help keep generated binaries, game files and embedded keys out of future changes.
- Preserved local game files and saves. Historical commits and older releases are unchanged.

## Thank you to the contributors

- **[@kyrill-bo](https://github.com/kyrill-bo)** — free camera, sensitivity, toggle/hold controls, mode display and graphics contributions in [#45](https://github.com/Apolinario-coder/SkyRecomp/pull/45).
- **[@LiamPalmqvist](https://github.com/LiamPalmqvist)** — macOS graphics fixes, Apple Silicon setup and resumable game preparation in [#43](https://github.com/Apolinario-coder/SkyRecomp/pull/43), [#41](https://github.com/Apolinario-coder/SkyRecomp/pull/41) and [#42](https://github.com/Apolinario-coder/SkyRecomp/pull/42).
- **[@BlAnK182C-bak](https://github.com/BlAnK182C-bak)** — Linux AppImage packaging in [#29](https://github.com/Apolinario-coder/SkyRecomp/pull/29).
- **Henriko Magnifico** — the texture artwork and supplied ReShade presets highlighted above.
- **[peiikai and LASRecomp contributors](https://github.com/peiikai/LASRecomp)** — the foundation of this port; **Patrick Mours and ReShade contributors** — ReShade.

Thank you also to everyone reporting bugs, sending logs, testing fixes and submitting improvements. Reviewed PR contributions have been adapted into this release; this wording does not imply that every original PR has been merged unchanged.

## Status and updating

This is still an **experimental project**. The reported inventory/map freezes and visual differences involving Mia and the Loftwing-area trees have not yet been confirmed fixed. Full-game stability has not been established. **120 FPS remains experimental**, carried over from v1.0.5; try 60 FPS when investigating a problem.

Back up **save/** before updating. Extract into a fresh folder, then copy your saves and prepared game files from your previous installation. Keep logs from a failed run before reopening the game. Do not copy an older executable or DLLs over the new release. Native macOS and Linux gameplay has not been verified on the Windows build machine.

## [1.0.5] - 2026-10-20

### Added

- **120 FPS (Experimental)**: Enjoy smoother gameplay with the new option in **Video > Frame rate**. Game speed and audio timing adjust independently of the higher frame rate. 60 FPS remains the default; performance depends on your hardware and settings.

- Included the tools needed to prepare game files on a new installation.

### Fixed

- Fixed Link's shadow disappearing and reappearing when moving through certain spots.
- Fixed objects appearing to slide when rotating the camera with **Aspect Unlocked** on ultrawide displays.
- Prevented small controller stick drift from sending unwanted movement or sword commands.
- Fixed the causes identified in crash reports involving the door-repairing NPC after rescuing the pet and entering the cave.
- Improved game setup so older prepared game files are rebuilt when needed for the latest fixes.
- Error reports now display the correct **SkyRecomp** application name.

### Notes

- The 120 FPS option is experimental. A turning animation followed a different path during testing; combat, flight and all cutscenes have not yet been fully checked. Switch back to 60 FPS if you encounter problems.
- The reported NPC and cave crashes could not be replayed with the original player's save. Other random crashes and objects disappearing outside the usual screen width may still occur.
- Includes the previous fixes for oversized eyes, newly entered names appearing as squares, slow item sounds and the short distorted sound in the left audio channel.

## [1.0.4] - 2026-10-19

### Fixed

- **Character eye rendering**: Eyes no longer appear exaggeratedly large. Dual-source fragment outputs are now mapped to location 0 with indices 0 and 1 when attachment 0 uses SRC1 blend factors (Vulkan). NVN alpha comparisons and alpha reference are implemented through fragment-shader discard, with the reference dynamic in `rscale[65].y`. NVN `SetDepthClamp` is now recorded and applied to the Vulkan pipeline key, rasterization state, and Metal depth clip mode.

- **Typed names in dialogs**: New names no longer appear as squares. The keyboard HLE (`swkbd`) now observes `KeyboardConfig::IsUseUtf8` (offset `0x3bd`) to select UTF-8 or UTF-16LE output encoding. Initial text preserves Unicode, output is bounds-checked, and surrogate pairs are handled.

- **Sound effects and short music**: Playback is no longer slow, including the sound when receiving a rupee. Audio voices honor `SkipPitchAndSampleRateConversion` (bypass copies samples directly instead of stretching them). The renderer clock is independent of output-device queue consumption, using a periodic deadline with prequeued silence. Windows guest waits now use high-resolution waitable timers instead of `nanosleep`/`pthread_cond_timedwait` (which rounded 3 ms waits to ~15.6 ms). Splitter destination updates skip variable-length `SNDI` state records before parsing `SNDD` destinations, fixing clipped left-only audio. Diagnostic WAV recordings use the renderer's actual sample rate instead of a hardcoded 48 kHz.
