#!/usr/bin/env python3
"""Exercise browser reflow/navigation, view state and asset-picker identity in a private project."""
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
    output = ROOT / "bin/browser-panel-smoke" / str(time.time_ns())
    for policy in ("Single", "Multi"):
        root = output / policy
        user, project = root / "user", root / "project"
        user.mkdir(parents=True)
        for folder in ("One", "Two", "Empty", "Long folder name for narrow navigation/Nested"):
            (project / "Assets" / folder).mkdir(parents=True)
        for folder in ("One", "Two"):
            (project / f"Assets/{folder}/Duplicate.cs").write_text("// Browser identity fixture\n")
        (project / "BrowserTest.luxproj").write_text("Project:\n  Name: BrowserTest\n  AssetDirectory: Assets\n  AssetRegistry: Assets/AssetRegistry.lzr\n  AutoSave: false\n")
        (project / "Assets/AssetRegistry.lzr").write_text("Assets: []\n")
        (user / "UserPreferences.yaml").write_text(f"UserPrefs:\n  ShowSplashScreen: false\n  StartupProject: {project.as_posix()}/BrowserTest.luxproj\n  RecentProjects: []\n")
        env = dict(os.environ, LUX_USER_DATA_DIR=str(user), LUX_BROWSER_PANELS_SELFTEST=str(project), LUX_THREADING_POLICY=policy, LUX_NO_WATCHDOG="1")
        log = root / "editor.log"
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(editor)], cwd=ROOT / "Editor", env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        status = project.with_suffix(".status")
        if result.returncode or not status.is_file() or status.read_text() != "PASS\n":
            raise RuntimeError(f"{policy} browser tests failed: {status.read_text() if status.exists() else 'no status'}; log: {log}")
        require_clean_graphics_log(log)
        print(f"PASS: {policy} browser reflow, navigation, text focus, picker identity/refresh and rename undo", flush=True)
    print("Evidence:", output)


if __name__ == "__main__":
    main()
