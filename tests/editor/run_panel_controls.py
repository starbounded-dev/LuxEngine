#!/usr/bin/env python3
"""Run production panel-control and Beam tests in the editor with disposable per-user data."""
import os
from pathlib import Path
import subprocess
import time

from native_checks import require_clean_graphics_log

ROOT = Path(__file__).resolve().parents[2]


def main():
    platform = "windows" if os.name == "nt" else "linux"
    editor = ROOT / f"bin/Debug-{platform}-x86_64/Editor" / ("Editor.exe" if os.name == "nt" else "Editor")
    output = ROOT / "bin/panel-controls-smoke" / str(time.time_ns())
    output.mkdir(parents=True)
    for policy in ("Single", "Multi"):
        user = output / policy / "user"
        fixture = output / policy / "fixtures"
        user.mkdir(parents=True)
        (user / "UserPreferences.yaml").write_text("UserPrefs:\n  ShowSplashScreen: false\n  RecentProjects: []\n")
        env = os.environ.copy()
        env.update(LUX_USER_DATA_DIR=str(user), LUX_PANEL_CONTROLS_SELFTEST=str(fixture), LUX_THREADING_POLICY=policy, LUX_NO_WATCHDOG="1")
        log = output / f"{policy}.log"
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(editor)], cwd=ROOT / "Editor", env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        status = fixture.with_suffix(".status")
        if result.returncode or not status.is_file() or status.read_text() != "PASS\n":
            raise RuntimeError(f"{policy} panel-control tests failed (exit {result.returncode}): {status.read_text() if status.exists() else 'no status'}; log: {log}")
        require_clean_graphics_log(log)
        print(f"PASS: {policy} panel sizing/overflow; Beam save/recovery, Unicode paths, tabs, popup focus and responsive search", flush=True)
    print("Evidence:", output)


if __name__ == "__main__":
    main()
