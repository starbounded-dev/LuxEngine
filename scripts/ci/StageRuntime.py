#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025-2026 starbounded-dev
"""Stage a built Lux-Runtime as the prebuilt player an installed editor exports with.

The release zip carries this folder as Runtime/<Config>/ beside the editor. Export copies the executable
and its libraries from it, so a game maker with no source tree and no compiler can export. Only what a
player ships goes in: the executable and the libraries Editor/Source/RuntimeExportUtils.cpp
(ShipsLibrary) lets into an export, no debug symbols, no editor-only libraries.

    python scripts/ci/StageRuntime.py --config Dist --dest artifacts/Runtime-Dist [--platform windows|linux]
"""
import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def ships(name: str, config: str) -> bool:
    """Mirror of RuntimeExport::ShipsLibrary; keep the two in step."""
    low = name.lower()
    if "vaudionative" in low:
        return False

    def named(prefix: str) -> bool:
        return low.startswith(prefix) or low.startswith("lib" + prefix)

    if named("assimp"):
        return False
    if config == "Dist" and (named("shaderc") or named("dxcompiler")):
        return False
    return not named("fmod")


def stage(source: Path, dest: Path, platform: str, config: str) -> list[str]:
    exe = "Lux-Runtime.exe" if platform == "windows" else "Lux-Runtime"
    if not (source / exe).is_file():
        raise SystemExit(f"Lux-Runtime was not built: {source / exe}")

    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    staged = []

    def copy(path: Path, relative: Path):
        target = dest / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target, follow_symlinks=True)
        staged.append(str(relative).replace("\\", "/"))

    copy(source / exe, Path(exe))
    if platform == "windows":
        for path in sorted(source.glob("*.dll")):
            if ships(path.name, config):
                copy(path, Path(path.name))
    else:
        # Export copies every shared object from the runtime folder and its lib/ into the game's lib/.
        for folder, relative in ((source, Path()), (source / "lib", Path("lib"))):
            for path in sorted(folder.glob("*.so*")):
                if path.is_file() and ships(path.name, config):
                    copy(path, relative / path.name)
    return staged


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="Dist", choices=["Debug", "Release", "Dist"])
    parser.add_argument("--platform", default="windows" if sys.platform == "win32" else "linux", choices=["windows", "linux"])
    parser.add_argument("--source", type=Path, help="the runtime build folder (default: bin/<Config>-<platform>-x86_64/Lux-Runtime)")
    parser.add_argument("--dest", type=Path, required=True)
    args = parser.parse_args()

    source = args.source or ROOT / "bin" / f"{args.config}-{args.platform}-x86_64" / "Lux-Runtime"
    for name in stage(source, args.dest, args.platform, args.config):
        print("staged", name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
