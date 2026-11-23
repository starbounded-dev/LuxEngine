#!/usr/bin/env python3
"""Rebuild the committed engine shader pack when the shader sources changed, then verify it.

    python scripts/Update-ShaderPack.py [--check]

The Dist editor can't compile shaders and refuses a pack older than the sources, and CI fails
(tests/shaders/check_engine_pack.py) until the pack is rebuilt, so any change under Editor/Resources/Shaders
needs this before it is committed. With no flag it runs `Editor.exe --build-shader-pack` from the Editor
folder using the newest Release or Debug editor it finds, then re-checks. With --check it only reports.

Exit code 0 when the pack matches the sources; 1 when it is stale (or could not be rebuilt).
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECK = ROOT / "tests" / "shaders" / "check_engine_pack.py"


def is_current() -> bool:
    return subprocess.run([sys.executable, str(CHECK)], cwd=ROOT, capture_output=True, text=True).returncode == 0


def find_editor():
    suffix = ".exe" if os.name == "nt" else ""
    platform = "windows" if os.name == "nt" else "linux"
    candidates = [ROOT / "bin" / f"{config}-{platform}-x86_64" / "Editor" / f"Editor{suffix}" for config in ("Release", "Debug")]
    existing = [c for c in candidates if c.is_file()]
    return max(existing, key=lambda c: c.stat().st_mtime) if existing else None


def main() -> int:
    if is_current():
        print("The engine shader pack is current.")
        return 0
    if "--check" in sys.argv:
        print("The engine shader pack is stale: run python scripts/Update-ShaderPack.py")
        return 1
    editor = find_editor()
    if not editor:
        print("The engine shader pack is stale and no Release or Debug editor is built. Build one, then run this again.")
        return 1
    print(f"Rebuilding the engine shader pack with {editor} ...")
    result = subprocess.run([str(editor), "--build-shader-pack"], cwd=ROOT / "Editor")
    if result.returncode != 0 or not is_current():
        print("The pack is still stale after the rebuild (is the editor older than the shader sources? rebuild it first).")
        return 1
    print("Done. Commit Editor/Resources/ShaderPack.lsp and ShaderPack.lsp.stamp.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
