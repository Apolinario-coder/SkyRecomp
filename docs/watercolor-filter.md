# Watercolor filter

Skyward Sword's **Video ? Watercolor filter** option applies immediately in VR and non-VR:

- **Original** keeps the game's parameters and appearance. This is the default.
- **Resolution-aware** keeps the paint effect but uses the actual rendered image dimensions for its pixel offsets.
  This avoids stretching its sampling footprint when the renderer scales targets, including nonuniform VR scaling.
- **Off** centers its neighbor samples, preserving fine details such as ladder rungs and fence rails that can
  disappear or shimmer under the brightest-neighbor paint pass.

The saved `watercolor_filter` setting accepts `original`, `resolution_aware`, or `off`. Existing configurations
without this key retain the original behavior. The game's textures, bloom, fades and other post-processing remain.

Vulkan and Metal adjust only the known paint shader's copied width/height parameters. Guest memory, alpha, UVs
and subsequent composites remain unchanged. Unknown shaders and parameter layouts are left untouched. No shader
recompilation or game restart is required when changing the option.
