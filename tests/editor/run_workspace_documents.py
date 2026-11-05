#!/usr/bin/env python3
"""Keep real dirty Beam/material/Sound documents through production workspace switches."""
import os
from pathlib import Path
import subprocess
import time
import wave

from native_checks import require_clean_graphics_log

ROOT = Path(__file__).resolve().parents[2]


def main():
    platform = "windows" if os.name == "nt" else "linux"
    editor = ROOT / f"bin/Debug-{platform}-x86_64/Editor" / ("Editor.exe" if os.name == "nt" else "Editor")
    editor = Path(os.environ.get("LUX_TEST_EDITOR", editor))
    output = ROOT / "bin/workspace-documents-smoke" / str(time.time_ns())
    for policy in ("Single", "Multi"):
        root = output / policy
        user, project = root / "user", root / "project"
        user.mkdir(parents=True)
        (project / "Assets").mkdir(parents=True)
        (project / "Documents.luxproj").write_text(
            "Project:\n  Name: WorkspaceDocuments\n  AssetDirectory: Assets\n"
            "  AssetRegistry: Assets/AssetRegistry.lzr\n  AutoSave: false\n", encoding="utf-8")
        (project / "Assets/Workspace.cs").write_text("// original document\n", encoding="utf-8")
        (project / "Assets/Surface.lmat").write_text(
            "Material:\n  AlbedoColor: [0.3, 0.6, 0.2]\n  Roughness: 0.45\n", encoding="utf-8")
        clip = 93048800000000001
        (project / "Assets/AssetRegistry.lzr").write_text(
            f"Assets:\n  - Handle: {clip}\n    FilePath: Tone.wav\n    Type: Audio\n", encoding="utf-8")
        with wave.open(str(project / "Assets/Tone.wav"), "wb") as tone:
            tone.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
            tone.writeframes(bytes(24000))
        (project / "Assets/Workspace.lsound").write_text(
            "Sound:\n  Version: 2\n  Bus: bus:/\n  Volume: 0\n  Pitch: 1\n"
            "  Spatial:\n    Enabled: false\n  Nodes:\n    - Type: Clip\n"
            f"      Clip: {clip}\n      Loop: true\n      Volume: 1\n      Pitch: 1\n", encoding="utf-8")
        (user / "UserPreferences.yaml").write_text(
            f"UserPrefs:\n  ShowSplashScreen: false\n  StartupProject: {project.as_posix()}/Documents.luxproj\n"
            "  RecentProjects: []\n", encoding="utf-8")
        env = dict(os.environ, LUX_USER_DATA_DIR=str(user), LUX_WORKSPACE_DOCUMENTS_SELFTEST=str(project),
                   LUX_THREADING_POLICY=policy, LUX_NO_WATCHDOG="1")
        log = root / "editor.log"
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(editor)], cwd=ROOT / "Editor", env=env,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=180)
        status = project.with_suffix(".status")
        if result.returncode or not status.is_file() or status.read_text() != "PASS\n":
            raise RuntimeError(f"{policy} workspace document checks failed (exit {result.returncode}): "
                               f"{status.read_text() if status.exists() else 'no status'}; log: {log}")
        require_clean_graphics_log(log)
        print(f"PASS: {policy} real dirty documents, workspace switches, audition cleanup and restored saves", flush=True)
    print("Evidence:", output)


if __name__ == "__main__":
    main()
