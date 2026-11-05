#!/usr/bin/env python3
"""Exercise material saves, undo and adaptive graph composition in an isolated editor project."""
import os
from pathlib import Path
import subprocess
import shutil
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    platform = "windows" if os.name == "nt" else "linux"
    editor = ROOT / f"bin/Debug-{platform}-x86_64/Editor" / ("Editor.exe" if os.name == "nt" else "Editor")
    editor = Path(os.environ.get("LUX_TEST_EDITOR", editor))
    output = ROOT / "bin/material-panel-smoke" / str(time.time_ns())
    for policy in ("Single", "Multi"):
        root = output / policy
        user, project = root / "user", root / "project"
        user.mkdir(parents=True)
        (project / "Assets").mkdir(parents=True)
        (project / "MaterialTest.luxproj").write_text("Project:\n  Name: MaterialTest\n  AssetDirectory: Assets\n  AssetRegistry: Assets/AssetRegistry.lzr\n  AutoSave: false\n")
        (project / "Assets/Surface.lmat").write_text("Material:\n  AlbedoColor: [0.3, 0.6, 0.2]\n  Roughness: 0.45\n  Metalness: 0.1\n")
        (project / "Assets/AssetRegistry.lzr").write_text("Assets: []\n")
        shutil.copytree(ROOT / "Editor/LuxSampleProject/Assets/Meshes/Source/Default", project / "Assets/Meshes/Source/Default")
        (user / "UserPreferences.yaml").write_text(f"UserPrefs:\n  ShowSplashScreen: false\n  StartupProject: {project.as_posix()}/MaterialTest.luxproj\n  RecentProjects: []\n")
        env = dict(os.environ, LUX_USER_DATA_DIR=str(user), LUX_MATERIAL_PANELS_SELFTEST=str(project), LUX_THREADING_POLICY=policy, LUX_NO_WATCHDOG="1")
        log = root / "editor.log"
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(editor)], cwd=ROOT / "Editor", env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        status = project.with_suffix(".status")
        if result.returncode or not status.is_file() or status.read_text() != "PASS\n":
            raise RuntimeError(f"{policy} material tests failed: {status.read_text() if status.exists() else 'no status'}; log: {log}")
        text = log.read_text(encoding="utf-8", errors="replace")
        if any(error in text for error in ("Validation Error", "VUID-", "Resource is null!", "Bake - Validate failed", "Verify Failed", "Assertion failed")):
            raise RuntimeError(f"Rendering or UI validation failure: {log}")
        print(f"PASS: {policy} material saves/retry/reload, undo, graph scopes and adaptive presentation", flush=True)
    print("Evidence:", output)


if __name__ == "__main__":
    main()
