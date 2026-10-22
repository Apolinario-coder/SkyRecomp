"""Build-time only: extract the pinned official Vulkan runtime without running its installer."""
import argparse
import hashlib
import io
from pathlib import Path
import subprocess
import zipfile

VERSION = "6.8.0"
SHA256 = "207aea16205fbf952bc8fe1879966672454cf04002e7ad34237c7990a5b3c0b4"


def extract(installer, output):
    data = installer.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError("ReShade installer checksum does not match the pinned release")
    for offset in range(0, len(data), 512):
        if data[offset:offset + 4] == b"PK\x03\x04":
            with zipfile.ZipFile(io.BytesIO(data[offset:])) as archive:
                output.mkdir(parents=True, exist_ok=True)
                for name in ("ReShade64.dll", "ReShade64.json"):
                    (output / name).write_bytes(archive.read(name))
            return
    raise ValueError("Official ReShade runtime archive was not found")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--installer", type=Path, default=Path(f"build/downloads/ReShade_Setup_{VERSION}.exe"))
    args = parser.parse_args()
    if not args.installer.exists():
        args.installer.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.installer.with_suffix(".download")
        subprocess.run(["curl", "--fail", "--location", "--retry", "2", "--output", str(temporary),
                        f"https://reshade.me/downloads/ReShade_Setup_{VERSION}.exe"], check=True)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            raise ValueError("Downloaded ReShade installer failed checksum verification")
        temporary.replace(args.installer)
    extract(args.installer, args.output)
    print(f"Bundled official ReShade {VERSION}: {args.output}")


if __name__ == "__main__":
    main()
