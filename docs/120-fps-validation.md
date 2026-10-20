# Experimental 120 FPS mode

Enable **Video > Frame rate > 120 FPS (Experimental)**. New installations default to 60 FPS.

The hooks apply only to Skyward Sword HD 1.0.0, build ID `d824d4b1bbd45d03668380d6d041ba4ca00a64ff`. Unknown builds retain 60 Hz. The new game-library identity forces regeneration of older libraries.

## Timing

The stores at `0x700104a1ac` and `0x700104a1d0` publish the shared simulation step (in original 30 Hz units) and its duration in milliseconds. Both registers are updated before their stores: the subsequent fractional accumulator therefore continues producing 30 integer ticks per second.

At 120 FPS the step follows monotonic elapsed time, including when the target cannot be sustained. Steps are capped at 33.3 ms; stalls of 100 ms or longer restart the clock instead of advancing through loading time. The display timer, Vulkan limiter, neutral motion-sensor period and dynamic-resolution budget follow the selected target. The audio renderer retains its independent sample clock.

## Validation

- `python tools/test_gen.py`: patch gating and register updates for portable C and AArch64 emission, plus indirect-branch regressions.
- `clang -O1 tools/test_frame_rate.c runtime/frame_rate.c -o build/test_frame_rate.exe`: 60/120 targets, unsupported builds, real-time advancement at 60/80/100/120 actual FPS, integer ticks, millisecond conversion and stall handling.
- Windows/Vulkan room tests reached 120.3 FPS over a measurement interval. This used an RTX 3090, a 1280x720 window, 50% internal resolution and optimized guest code; it is not a performance guarantee for other scenes or settings.
- A scripted 0.75-second forward input travelled 283.32 units at 60 FPS and 279.24 units in 120 FPS mode, a 1.44% difference within the input sampling interval. The integrated game clock matched elapsed wall time.
- Both audio recordings used 32 kHz stereo with approximately 200 renderer blocks per second. Samples were finite and remained below full scale. Increasing visual FPS did not double the audio clock.

These checks cover the title screen, save loading and the initial room. A reverse turn from idle did not follow an identical trajectory in both runs; that test does not establish equivalence of every animation transition. Combat, flight, extended play and all cutscenes still require validation, so the option remains experimental.
