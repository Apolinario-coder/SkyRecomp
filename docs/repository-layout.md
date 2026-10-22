# Repository layout

The repository contains source and development resources. Downloadable programs and their runtime libraries belong in release packages.

| Location | Contents |
| --- | --- |
| `runtime/` | Host runtime, platform support, graphics, audio, input and interface code. |
| `tools/` | Recompiler, development helpers, packaging scripts and regression checks. Run helpers from the repository root, e.g. `python tools/build_game.py`. Normal game setup is performed by the launcher. |
| `fonts/` | Bundled interface font and its license. Packages copy these next to the application resources. |
| `third_party/` | Dependencies and their license notices. |
| `docs/` | Documentation and application icon sources. |
| `.github/` | Build and source-check workflows. |

Keep `README.md`, `CHANGELOG.md`, `Makefile`, `.gitignore` and project license notices at the root for discoverability.

## Local files and release resources

`build/`, `cache/`, `save/`, `extracted/`, `Rom Game/`, `mods/`, generated game sources, user settings and logs are local data and are ignored by Git. Removing them from the source index does not delete an existing player's files.

Release packaging creates `gamelib-src/` from the canonical headers in `runtime/` and the entry source in `tools/`. It is not maintained as a second source copy. Similarly, release `icon.png` is copied from `docs/icon/icon_1024.png`; the runtime continues to use the same packaged resource names.

Root executables, DLLs and development import libraries may remain locally for testing but are not source files. Fonts are stored under `fonts/` in a clean checkout; existing root copies remain usable locally. The retired extractor notice is now `tools/extract_nsp.py`.

This organization does not remove files from historical commits or previously published releases.
