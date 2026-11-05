#!/usr/bin/env python3
"""Run the production Log, History and Asset Manager controls in disposable editor projects."""
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    platform = "windows" if os.name == "nt" else "linux"
    editor = ROOT / f"bin/Debug-{platform}-x86_64/Editor" / ("Editor.exe" if os.name == "nt" else "Editor")
    editor = Path(os.environ.get("LUX_TEST_EDITOR", editor))
    output = ROOT / "bin/diagnostic-controls-smoke" / str(time.time_ns())
    for policy in ("Single", "Multi"):
        root = output / policy
        user, project = root / "user", root / "project"
        user.mkdir(parents=True)
        for folder in ("One", "Two"):
            (project / "Assets" / folder).mkdir(parents=True)
            (project / "Assets" / folder / "Duplicate.cs").write_text("// Asset diagnostics fixture\n")
        (project / "DiagnosticsTest.luxproj").write_text("Project:\n  Name: DiagnosticsTest\n  AssetDirectory: Assets\n  AssetRegistry: Assets/AssetRegistry.lzr\n  AutoSave: false\n")
        (project / "Assets/AssetRegistry.lzr").write_text("Assets: []\n")
        (user / "UserPreferences.yaml").write_text(f"UserPrefs:\n  ShowSplashScreen: false\n  StartupProject: {project.as_posix()}/DiagnosticsTest.luxproj\n  RecentProjects: []\n")
        env = dict(os.environ, LUX_USER_DATA_DIR=str(user), LUX_DIAGNOSTIC_CONTROLS_SELFTEST=str(project), LUX_THREADING_POLICY=policy, LUX_NO_WATCHDOG="1")
        log = root / "editor.log"
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(editor)], cwd=ROOT / "Editor", env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        status = project.with_suffix(".status")
        if result.returncode or not status.is_file() or status.read_text() != "PASS\n":
            raise RuntimeError(f"{policy} diagnostic controls failed (exit {result.returncode}): {status.read_text() if status.exists() else 'no status'}; log: {log}")
        text = log.read_text(encoding="utf-8", errors="replace")
        if any(error in text for error in ("VUID-", "Validation Error", "Assertion failed", "Verify Failed")):
            raise RuntimeError(f"Rendering or UI validation failure: {log}")
        print(f"PASS: {policy} Log filtering/follow/details, asset metadata refresh and History navigation", flush=True)
    print("Evidence:", output)


if __name__ == "__main__":
    main()
