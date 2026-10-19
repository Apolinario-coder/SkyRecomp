# Name entry, rendering and audio corrections

The keyboard HLE now observes `KeyboardConfig::IsUseUtf8` at offset `0x3bd`
([protocol layout](https://switchbrew.org/wiki/Software_Keyboard#KeyboardConfig)).
Previously it always returned UTF-8, so a UTF-16 consumer interpreted adjacent
ASCII letters as unrelated characters. Initial text also preserves Unicode now.
This prevents corrupt new names; it does not rewrite names already in saves.

The Vulkan backend now maps dual-source fragment outputs to location 0 with
indices 0 and 1 when attachment 0 uses SRC1 blend factors. Previously the
secondary output was declared at MRT location 1, leaving the secondary blend
source undefined. The mode participates in the program key; ordinary MRT shaders
retain their original locations. The same underlying output mapping issue is
documented for Skyward Sword HD in [Eden issue/PR 3637](https://git.eden-emu.dev/eden-emu/eden/pulls/3637).

Recorded NVN alpha comparisons and the alpha reference are also implemented
through fragment-shader discard. The reference is dynamic in `rscale[65].y`.

Nonzero NVN barriers now end dynamic rendering and issue a memory dependency.
The texture-view memo now keys on the resolved source of deferred depth copies,
so changing an alias within a frame cannot reuse the destination's old depth view.
First-use shader and pipeline compilation is synchronous by default, preventing
discarded draws/dispatches from leaving shadow maps incomplete. This can cause
first-use compilation stalls. `LARECOMP_SYNC_SHADERS=0` restores asynchronous
compilation for diagnostic comparisons, with its original draw-skipping behaviour.

Audio voices now honor `SkipPitchAndSampleRateConversion`. The mixer previously
applied rate/pitch conversion even when the guest requested bypass; the regression
case at 16 kHz with pitch 0.5 consumed 40 source samples per 160-sample block instead
of 160, stretching the effect fourfold. Bypass now copies samples directly. Normal
conversion is preserved. Diagnostic WAV recordings use the renderer's actual
sample rate (the local game requested 32 kHz), instead of a hardcoded 48 kHz.

The renderer's clock is now independent of output-device queue consumption.
Previously a 10 ms device period generated several 5 ms renderer blocks in a
burst. Manual-reset event signals coalesced, slowing the guest's sequence player
even when individual PCM samples retained their correct rate and pitch. The
renderer now uses a periodic deadline, prequeues silence for device buffering,
and allows one block of queue headroom so device-period alignment does not stall
alternate ticks. A host stall resets the deadline instead of generating a burst.

Windows guest waits now use high-resolution waitable timers. Both MinGW
`nanosleep` and `pthread_cond_timedwait` experimentally rounded a requested 3 ms
wait to about 15.6 ms on this host, despite `timeBeginPeriod(1)`. Changing
`SleepThread` alone did not change the in-game fanfare timing. Kernel event,
condition-variable and address waits now wait on a per-thread event together
with a high-resolution timer. Resetting the event under the kernel lock before
unlocking preserves wakeups between unlocking and entering the native wait.
Other platforms retain their pthread implementation. Remaining audio polling
sleeps use SDL's precise delay.

Splitter destination updates now skip the variable-length `SNDI` state records
before parsing `SNDD` destinations. The former parser started at the state
records and advanced in 112-byte destination strides. In the title-menu K-button
capture, reserved state bytes contained a stale `SNDD` magic at offset 144; the
following `SNDI` magic was interpreted as a float gain of 804069 for destination
3's left channel. That explains the brief, severely clipped left-only sound.
The parser now follows state counts, destination-ID counts and the 12 reserved
bytes, and bounds each record against the splitter section itself.

NVN `SetDepthClamp` is now recorded and applied to the Vulkan pipeline key and
rasterization state (and Metal depth clip mode). It was previously ignored.
Its effect on the reported outdoor shadow boundaries remains unverified.

## Verified locally

- Full Windows runtime build and link with the project's FFmpeg import libraries.
- Launcher starts using the bundled DLLs and initializes the RTX 3090 Vulkan device.
- Keyboard HLE regression: UTF-8, UTF-16, accented initial/input text, cancellation,
  output bounds, maximum UTF-16 length and surrogate-pair truncation.
- All 36 combinations of alpha mode, fragment depth output and dual-source mode
  compile to SPIR-V. Discard and output Location/Index decorations are checked.
- A real dual-source Vulkan graphics pipeline using SRC1 colour/alpha factors
  creates on the RTX 3090 with zero validation messages and zero failures.
- Audio regression verifies normal rate/pitch duration, exact samples and duration
  with SRC bypass, and WAV metadata at 32 kHz.
- A real SDL dummy device at 44.1 kHz with a 441-frame period reproduces the
  audio event burst: before the clock fix, 53 of 79 event gaps were below 1 ms.
  After the fix, none were below 1 ms (80 signals over approximately 405 ms).
- The wardrobe reproduction collected a Blue Rupee on the copied save. A clean
  binary voice trace measured successive fanfare note gaps at 0.715–0.755 s
  before the kernel event-wait fix and 0.140–0.145 s afterward. The original
  `FAN_ITEM_GET_MINI2` RSEQ specifies 139 BPM, timebase 96 and waits of 32 ticks,
  giving 0.143885 s. Individual voice pitches remained unchanged.
- The actual platform wait regression measured 3 ms event timeouts at 3.294 ms
  average. It also checks timeout lower bounds, pending signals, finite and
  indefinite wakeups, and repeated waiter reuse.
- The final captured fanfare was compared against the supplied
  `zelda_ss_small_item.mp3`. Time/frequency envelopes align (mean normalized
  spectral similarity 0.994 over the first 1.7 s after a 50 ms lead-in shift).
  This metric confirms timing and spectral agreement, not bit-exact mixing.
  Final listening feedback from the user remains pending.
- The menu reproduction (A/K at frame 400) had 2,771 full-scale samples on
  the left channel and none on the right before the splitter fix. The corrected
  run had no full-scale samples on either channel over 75.15 s, including
  title confirmation, file selection and the wardrobe pickup (peaks L=0.458,
  R=0.507). Fanfare note gaps remained 0.140–0.145 s. Capture summary:
  `build/render-validation/pop-fix-validation.json`; menu clip:
  `build/render-validation/menu-k-fixed.wav`.
- The synthetic splitter regression reproduces a stale `SNDD` header inside
  reserved state bytes followed by an `SNDI` magic. It checks both stereo gains,
  empty delta updates, every truncated input length and oversized ID counts.
- Game runs were performed against local game data using a copy of `save/` in
  `build/render-validation/`. File selection reproduced the legacy name squares.

From an MSYS2 UCRT64 shell at the repository root:

```sh
make -j8 DEPS=build/deps-win build/skyrecomp.exe
clang -Iruntime tools/test_swkbd.c -lSDL3 -o build/test_swkbd.exe
./build/test_swkbd.exe
clang tools/test_alpha_shader.c runtime/nvn/maxwell/translate.c -lshaderc_shared -o build/test_alpha_shader.exe
./build/test_alpha_shader.exe
clang -O1 -ffunction-sections -fdata-sections tools/test_audio_resample.c -Wl,--gc-sections -lSDL3 -o build/test_audio_resample.exe
./build/test_audio_resample.exe
clang -O1 -ffunction-sections -fdata-sections tools/test_host_sleep.c runtime/platform/windows.c -Wl,--gc-sections -lwinmm -lshell32 -lole32 -lwinpthread -o build/test_host_sleep.exe
./build/test_host_sleep.exe
tools/test_vk_pipelines.sh 0 1
```

## Still needs in-game validation

The user confirmed the eye correction. Check outdoor shadows while moving and
entering areas, NPC eyes during dialogue, and a newly entered name in dialogue. Test both fixed and dynamic resolution. Preserve the existing
save; do not create a new file over it just to test name entry.

Final rupee artifacts in `build/render-validation/`:
`rupee-voice-trace.bin` (before event-wait fix),
`rupee-voice-trace-fixed.bin`, `rupee-tempo-comparison.json`, and
`rupee-small-item-fixed.wav` (3 s fanfare excerpt). The reference conversion is
`ss-small-item-reference.wav`.

Earlier `rupee-fanfare-before.wav` / `rupee-fanfare-after.wav` captures were made
with verbose voice logging inside the mixer lock. That logging introduced long
lock stalls; their tempo comparison is not valid evidence of the final fix.
The isolated wardrobe-opening PCM matches the original RWAV samples after endian
conversion, at its declared 32 kHz. That opening sound is separate from the
sequenced item fanfare; no global pitch multiplier or asset replacement was used.

The original save still contains the malformed legacy name. Its bytes spell
`Link` as UTF-8 in a UTF-16 field. No speculative checksum repair or write to the
original save was performed.

For test automation, `LARECOMP_INPUT_FILE` accepts the existing frame-based game
and host input syntaxes and can be edited while a reproduction runs. It has no
effect unless explicitly configured.
