"""Small packaging checks; run with Python, no game assets required."""
import json
from pathlib import Path
import tempfile

from fetch_reshade import extract
from package_visuals import PRESET, package


with tempfile.TemporaryDirectory() as temporary:
    assert PRESET == "HenrikoMagnifico_Ultra_NO_MOTION_BLUR_v8.ini"
    root = Path(temporary)
    source = root / "source"
    textures = source / "4K texturas/contents/title/textures"
    textures.mkdir(parents=True)
    png = textures / "tex2d_0123456789abcdef0123456789abcdef_2x2x1_0x0.png"
    png.write_bytes(b"texture fixture")
    shaders = source / "reshade-shaders/Shaders/nested"
    shaders.mkdir(parents=True)
    (shaders / "effect.fx").write_text("shader fixture")
    preset = "Techniques=DPX@DPX.fx,DRME@MotionEstimation.fx,LinearMotionBlur@LinearMotionBlur.fx\n[DPX.fx]\nStrength=0.4\n"
    (source / PRESET).write_text(preset)
    output = root / "relocated release"
    package(source, output)
    assert (output / "mods/textures" / png.name).read_bytes() == png.read_bytes()
    assert not (output / "mods/textures").is_symlink()
    assert (output / "reshade-shaders/Shaders/nested/effect.fx").is_file()
    result = (output / PRESET).read_text()
    assert "DRME@" not in result and "LinearMotionBlur@" not in result and "DPX@DPX.fx" in result
    assert (source / PRESET).read_text() == preset
    assert str(source) not in (output / "ReShade.ini").read_text()
    assert json.loads((output / "visual-pack.json").read_text())["textures"] == 1
    assert (output / "LICENSE-ReShade.txt").is_file()
    for destination in [source, source / "nested", output]:
        try:
            package(source, destination)
            raise AssertionError("Unsafe or existing destination was accepted")
        except ValueError:
            pass
    installer = root / "wrong.exe"
    installer.write_bytes(b"wrong installer")
    try:
        extract(installer, root / "runtime")
        raise AssertionError("Invalid installer checksum was accepted")
    except ValueError:
        pass
    assert not (root / "runtime").exists()
print("Portable visual pack checks passed")
