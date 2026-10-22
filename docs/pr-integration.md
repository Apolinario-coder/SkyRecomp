# Pull request integration review

Reviewed on 2026-10-21. Reviewed contributions are adapted into v1.0.6; the original GitHub pull requests have not been merged unchanged.

| PR | Changes adopted |
| --- | --- |
| [#45](https://github.com/Apolinario-coder/SkyRecomp/pull/45) | Optional free camera, sensitivity, L tap/hold controls, mode indicator, smaller FPS display and Metal dual-source output mapping. |
| [#43](https://github.com/Apolinario-coder/SkyRecomp/pull/43) | Explicit color-clear command types and defensive Metal clear dispatch. |
| [#42](https://github.com/Apolinario-coder/SkyRecomp/pull/42) | Preserve failed/interrupted build work and reuse matching completed objects. |
| [#41](https://github.com/Apolinario-coder/SkyRecomp/pull/41) | Run the game code builder during setup, provide sufficient extraction stack space, avoid large stack copy buffers and generate compatible ARM timing hooks. |
| [#29](https://github.com/Apolinario-coder/SkyRecomp/pull/29) | Linux AppImage release job, Docker fallback, optional randomizer helper and corrected package paths/licenses. |

Overlapping setup changes were combined once. The camera PR's unrelated change from `-O1` to `-O0` was omitted to preserve game code optimization. Its unconditional SIMD header addition was replaced by #41's compatible timing hook.

Additional review fixes:

- Recompute right-stick direction buttons after applying camera sensitivity.
- Ignore camera shortcuts while menus capture input; reset camera state when rebooting the game.
- Reject non-finite camera sensitivity values and handle extraction worker creation errors.
- Include dual-source mode in both Metal shader cache keys.
- Validate source, header and compiler contents before resuming build objects.
- Invoke Linux shell scripts through Bash so checkout executable-bit differences do not prevent packaging.

Regression checks live in `tools/test_free_camera.c`, `tools/test_build_resume.c`, `tools/test_clear_color.c`, `tools/test_alpha_shader.c` and `tools/test_gen.py`.

Local results: all 81 Windows runtime units compiled; camera, build-resume and clear-command checks passed; Metal output declarations and 36 Vulkan shader variants passed; all six generator regressions passed. Existing locale, live texture/ReShade toggle and portable visual-pack checks also passed. Linux shell syntax and release workflow YAML parsed successfully. The standalone Windows generator was rebuilt and its command-line startup checked.

Windows compilation and host-side regression checks are performed locally. Native macOS/Metal execution and a complete Linux AppImage build require their respective build environments and have not been validated on this Windows host.
