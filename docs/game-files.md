# Game file setup

The launcher imports a **prepared game-files folder** for Skyward Sword HD v1.0.0 or v1.0.1. Select the folder containing `exefs/` and either `romfs.bin` or `romfs/`, then choose **Set up game files**.

The ExeFS folder must contain `main`, `sdk`, `subsdk0` and `subsdk1`. These are supplied by the player, along with the game's data. The launcher builds the native game code from those files.

The launcher does not import encrypted NSP/XCI files or console keys. The former `extract_nsp.py` tool used an embedded key and fixed offsets for one image layout. It has been retired rather than retaining an unreliable extraction path. Its old prebuilt executable is no longer part of the source tree or release packaging.

No game decryption keys are supplied with Sky Recomp. Keep all game files, generated game code and personal keys outside version control.
