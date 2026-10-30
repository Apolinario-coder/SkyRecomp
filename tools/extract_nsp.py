"""Compatibility notice for the retired, image-specific extractor."""
import sys

if __name__ == "__main__":
    sys.exit(
        "The legacy encrypted-image extractor has been retired.\n"
        "Start Sky Recomp and select your prepared ExeFS/RomFS folder in Game files folder.\n"
        "No keys or encrypted game images are accepted by this script. See docs/game-files.md."
    )
