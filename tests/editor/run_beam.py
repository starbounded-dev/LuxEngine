#!/usr/bin/env python3
"""Beam never loses work and builds C#: dirty flag, atomic save, external changes, backups and session restore, open at a line,
and a real `dotnet build` with errors shown in place. Runs inside a real editor on a disposable project."""
import os
from pathlib import Path
import subprocess
import time

from native_checks import require_clean_graphics_log

ROOT = Path(__file__).resolve().parents[2]


def main():
    platform = "windows" if os.name == "nt" else "linux"
    editor = ROOT / f"bin/Debug-{platform}-x86_64/Editor" / ("Editor.exe" if os.name == "nt" else "Editor")
    editor = Path(os.environ.get("LUX_TEST_EDITOR", editor))
    output = ROOT / "bin/beam-smoke" / str(time.time_ns())
    for policy in ("Single", "Multi"):
        root = output / policy
        user, project = root / "user", root / "project"
        user.mkdir(parents=True)
        (project / "Assets").mkdir(parents=True)
        (project / "Beam.luxproj").write_text(
            "Project:\n  Name: Beam\n  AssetDirectory: Assets\n  AssetRegistry: Assets/AssetRegistry.lzr\n  AutoSave: false\n", encoding="utf-8")
        (project / "Assets/AssetRegistry.lzr").write_text("Assets: []\n", encoding="utf-8")
        (user / "UserPreferences.yaml").write_text(
            f"UserPrefs:\n  ShowSplashScreen: false\n  StartupProject: {project.as_posix()}/Beam.luxproj\n  RecentProjects: []\n", encoding="utf-8")
        env = dict(os.environ, LUX_USER_DATA_DIR=str(user), LUX_BEAM_SELFTEST=str(project), LUX_THREADING_POLICY=policy, LUX_NO_WATCHDOG="1")
        log = root / "editor.log"
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(editor)], cwd=ROOT / "Editor", env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=600)
        status = project.with_suffix(".status")
        if result.returncode or not status.is_file() or status.read_text() != "PASS\n":
            raise RuntimeError(f"{policy} Beam checks failed (exit {result.returncode}): "
                               f"{status.read_text() if status.exists() else 'no status'}; log: {log}")
        require_clean_graphics_log(log)
        print(f"PASS: {policy} dirty flag, atomic save, external changes, backups and session, open at line, C# build with errors in place", flush=True)
    print("Evidence:", output)


if __name__ == "__main__":
    main()
