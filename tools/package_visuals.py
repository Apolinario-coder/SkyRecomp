"""Copy the complete supplied visual pack into a self-contained Windows release."""
import argparse
import json
from pathlib import Path
import shutil

PRESET = "HenrikoMagnifico_Ultra_NO_MOTION_BLUR_v8.ini"


def package(source, output):
    source, output = source.resolve(), output.resolve()
    if source == output or source in output.parents or output in source.parents:
        raise ValueError("Visual pack source and release destination must be separate directories")
    candidates = [p for p in [source, *source.rglob("textures")] if any(p.glob("tex2d_*.png"))]
    if len(candidates) != 1:
        raise ValueError("Expected one directory containing the tex2d PNG textures")
    shaders = source / "reshade-shaders"
    if not (shaders / "Shaders").is_dir() or not (source / PRESET).is_file():
        raise ValueError("The shader directory and selected Henriko preset are required")
    output.mkdir(parents=True, exist_ok=True)
    for destination in [output / "mods/textures", output / "reshade-shaders"]:
        if destination.exists():
            raise ValueError(f"Use a fresh release directory; already present: {destination}")
    # copytree follows source junctions; the resulting release contains real files, never developer-machine links.
    shutil.copytree(candidates[0], output / "mods/textures")
    shutil.copytree(shaders, output / "reshade-shaders")
    for preset in source.glob("HenrikoMagnifico_*.ini"):
        # ReShade presets put their technique list before the first section.
        text = preset.read_text(encoding="utf-8-sig")
        if "NO_MOTION_BLUR" in preset.name:
            lines = text.splitlines()
            for i, line in enumerate(lines):
                if line.startswith("Techniques="):
                    techniques = line.partition("=")[2].split(",")
                    lines[i] = "Techniques=" + ",".join(t for t in techniques if
                        "DRME@" not in t and "LinearMotionBlur@" not in t)
            text = "\n".join(lines) + "\n"
        (output / preset.name).write_text(text, encoding="utf-8")
    (output / "ReShade.ini").write_text(
        "[GENERAL]\nEffectSearchPaths=.\\reshade-shaders\\Shaders\\**\n"
        "TextureSearchPaths=.\\reshade-shaders\\Textures\\**\n"
        f"PresetPath=.\\{PRESET}\nPerformanceMode=1\nSkipLoadingDisabledEffects=1\n"
        "PreprocessorDefinitions=RESHADE_DEPTH_LINEARIZATION_FAR_PLANE=1000.0,RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN=1,RESHADE_DEPTH_INPUT_IS_REVERSED=1,RESHADE_DEPTH_INPUT_IS_LOGARITHMIC=0\n"
        "\n[INPUT]\nKeyOverlay=36,0,0,0\nKeyEffects=123,0,0,0\n"
        "\n[OVERLAY]\nTutorialProgress=4\nShowFPS=0\n", encoding="utf-8")
    repo = Path(__file__).resolve().parents[1]
    shutil.copy2(repo / "third_party/reshade/LICENSE", output / "LICENSE-ReShade.txt")
    (output / "VISUAL-PACK.txt").write_text(
        "SkyRecomp visual pack\n\nTextures and presets: Henriko Magnifico\n"
        "https://www.henrikomagnifico.com/zelda-skyward-sword-4k\n"
        "ReShade 6.8.0: Patrick Mours and contributors (LICENSE-ReShade.txt)\n"
        "https://reshade.me/\n\nNo ReShade installer is needed.\n"
        "ESC > Video > 4K Textures: applies while playing.\n"
        "ESC > Video > ReShade: switches loaded effects immediately.\n"
        "If launched with ReShade off, close and reopen the application to enable it.\n", encoding="utf-8")
    textures = list((output / "mods/textures").rglob("tex2d_*.png"))
    (output / "visual-pack.json").write_text(json.dumps({"textures": len(textures), "preset": PRESET,
        "reshade": "6.8.0"}, indent=2) + "\n", encoding="utf-8")
    print(f"Bundled {len(textures)} textures and all shaders: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    package(args.source, args.output)


if __name__ == "__main__":
    main()
