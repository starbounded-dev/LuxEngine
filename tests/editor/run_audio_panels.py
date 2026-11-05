#!/usr/bin/env python3
"""Run the production Sound Bank, Sound saves and Mixer controls in disposable editor projects."""
import os
from pathlib import Path
import subprocess
import time
import math
import struct
import wave

ROOT = Path(__file__).resolve().parents[2]


def main():
    platform = "windows" if os.name == "nt" else "linux"
    editor = ROOT / f"bin/Debug-{platform}-x86_64/Editor" / ("Editor.exe" if os.name == "nt" else "Editor")
    editor = Path(os.environ.get("LUX_TEST_EDITOR", editor))
    output = ROOT / "bin/audio-panels-smoke" / str(time.time_ns())
    for policy in ("Single", "Multi"):
        root = output / policy
        user, project = root / "user", root / "project"
        user.mkdir(parents=True)
        (project / "Assets").mkdir(parents=True)
        with wave.open(str(project / "Assets/Tone.wav"), "wb") as tone:
            tone.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
            tone.writeframes(b"".join(struct.pack("<h", int(1500 * math.sin(2 * math.pi * 440 * sample / 48000))) for sample in range(12000)))
        (project / "AudioPanelTest.luxproj").write_text("Project:\n  Name: AudioPanelTest\n  AssetDirectory: Assets\n  AssetRegistry: Assets/AssetRegistry.lzr\n  AutoSave: false\n")
        (project / "Assets/AssetRegistry.lzr").write_text("Assets: []\n")
        (user / "UserPreferences.yaml").write_text(f"UserPrefs:\n  ShowSplashScreen: false\n  StartupProject: {project.as_posix()}/AudioPanelTest.luxproj\n  RecentProjects: []\n")
        env = dict(os.environ, LUX_USER_DATA_DIR=str(user), LUX_AUDIO_PANELS_SELFTEST=str(project), LUX_THREADING_POLICY=policy, LUX_NO_WATCHDOG="1")
        log = root / "editor.log"
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(editor)], cwd=ROOT / "Editor", env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        status = project.with_suffix(".status")
        if result.returncode or not status.is_file() or status.read_text() != "PASS\n":
            raise RuntimeError(f"{policy} audio panels failed (exit {result.returncode}): {status.read_text() if status.exists() else 'no status'}; log: {log}")
        text = log.read_text(encoding="utf-8", errors="replace")
        if any(error in text for error in ("VUID-", "Validation Error", "Assertion failed", "Verify Failed")):
            raise RuntimeError(f"Rendering or UI validation failure: {log}")
        print(f"PASS: {policy} audio panels/save failures/undo, pack round-trips and Mixer Apply/preview", flush=True)
    print("Evidence:", output)


if __name__ == "__main__":
    main()
