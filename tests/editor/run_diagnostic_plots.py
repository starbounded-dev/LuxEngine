#!/usr/bin/env python3
"""Exercise production diagnostic charts with known measurements and isolated ImGui/ImPlot state."""
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    platform = "windows" if os.name == "nt" else "linux"
    editor = ROOT / f"bin/Debug-{platform}-x86_64/Editor" / ("Editor.exe" if os.name == "nt" else "Editor")
    output = ROOT / "bin/diagnostic-plot-smoke" / str(time.time_ns())
    for policy in ("Single", "Multi"):
        root = output / policy
        user = root / "user"
        user.mkdir(parents=True)
        (user / "UserPreferences.yaml").write_text("UserPrefs:\n  ShowSplashScreen: false\n  RecentProjects: []\n")
        env = dict(os.environ, LUX_USER_DATA_DIR=str(user), LUX_DIAGNOSTIC_PLOTS_SELFTEST=str(root / "charts"), LUX_THREADING_POLICY=policy, LUX_NO_WATCHDOG="1")
        log = root / "editor.log"
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(editor)], cwd=ROOT / "Editor", env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        status = root / "charts.status"
        if result.returncode or not status.is_file() or status.read_text() != "PASS\n":
            raise RuntimeError(f"{policy} diagnostic tests failed: {status.read_text() if status.exists() else 'no status'}; log: {log}")
        text = log.read_text(encoding="utf-8", errors="replace")
        if any(error in text for error in ("VUID-", "Assertion failed", "Verify Failed")):
            raise RuntimeError(f"Rendering or UI validation failure: {log}")
        print(f"PASS: {policy} timing history, missing data, legend, zoom/inspection, pause/resume and frozen passes", flush=True)
    print("Evidence:", output)


if __name__ == "__main__":
    main()
