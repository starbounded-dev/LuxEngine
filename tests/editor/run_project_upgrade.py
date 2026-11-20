#!/usr/bin/env python3
"""Opening a project made by another Lux: an older project is upgraded with a backup, a newer one is refused, a current one is left alone.

Four launches of a real editor on isolated user folders with LUX_STARTUP_SELFTEST (the report is written 30 frames after startup); the
disposable project is the saved 2026.1 project in tests/project-format. Unattended runs upgrade without asking (LUX_PROJECT_UPGRADE=yes, and
inside the test harnesses); LUX_PROJECT_UPGRADE=no refuses. The dialog itself is not driven here.
"""
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/project-format/old-2026.1"


def launch(editor, root, project, upgrade):
    user = root / "user"
    user.mkdir(parents=True)
    (user / "UserPreferences.yaml").write_text(
        f"UserPrefs:\n  ShowSplashScreen: false\n  StartupProject: {project.as_posix()}\n  RecentProjects: []\n", encoding="utf-8")
    status = root / "startup.status"
    env = dict(os.environ, LUX_USER_DATA_DIR=str(user), LUX_STARTUP_SELFTEST=str(status), LUX_NO_WATCHDOG="1")
    env.pop("LUX_PROJECT_UPGRADE", None)
    if upgrade:
        env["LUX_PROJECT_UPGRADE"] = upgrade
    result = subprocess.run([str(editor)], cwd=ROOT / "Editor", env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
    logs = "".join(log.read_text(encoding="utf-8", errors="replace") for log in (user / "logs").glob("*.log")) if (user / "logs").is_dir() else ""
    if result.returncode != 0 or not status.is_file() or status.read_text() != "PASS\n":
        raise RuntimeError(f"the editor did not run to the report (exit {result.returncode}); logs: {user / 'logs'}")
    return logs


def make_project(root):
    project = root / "project"
    shutil.copytree(FIXTURE, project)
    return project


def main():
    platform = "windows" if os.name == "nt" else "linux"
    editor = ROOT / f"bin/Debug-{platform}-x86_64/Editor" / ("Editor.exe" if os.name == "nt" else "Editor")
    editor = Path(os.environ.get("LUX_TEST_EDITOR", editor))
    output = ROOT / "bin/project-upgrade-smoke" / str(time.time_ns())
    scene = Path("Assets/Scenes/Main.luxscene")

    # 1. An older project is upgraded, and its originals are kept.
    root = output / "older"
    project = make_project(root)
    original = (project / scene).read_bytes()
    logs = launch(editor, root, project / "Old.luxproj", None)
    backups = sorted((project / ".luxbackup").glob("*"))
    assert len(backups) == 1, f"expected one backup folder, found {backups}"
    assert (backups[0] / scene).read_bytes() == original, "the backup holds the original scene, byte for byte"
    upgraded = (project / scene).read_bytes()
    assert upgraded.startswith(b"FormatVersion: 1") and upgraded.endswith(original), "the scene is stamped and otherwise unchanged"
    assert (project / "Old.luxproj").read_bytes().startswith(b"FormatVersion: 1"), "the project file is stamped"
    assert "Upgraded 4 file(s)" in logs, "the log says what was upgraded"
    # The fixture has no script module: the editor says so only once the project is the active one.
    assert "Script module not found" in logs, "the project was opened after the upgrade"
    print("PASS: an older project is upgraded with a backup and opened", flush=True)

    # 2. The same, and a second start finds nothing to do.
    root = output / "again"
    shutil.copytree(project, root / "project")
    before = {p: p.read_bytes() for p in (root / "project/Assets").rglob("*") if p.is_file()}
    launch(editor, root, root / "project/Old.luxproj", None)
    after = {p: p.read_bytes() for p in (root / "project/Assets").rglob("*") if p.is_file()}
    assert before == after, "a project that is up to date is not touched"
    assert len(list((root / "project/.luxbackup").glob("*"))) == 1, "no second backup"
    print("PASS: an upgraded project opens without another upgrade", flush=True)

    # 3. A newer project is refused and left alone.
    root = output / "newer"
    project = make_project(root)
    text = (project / "Old.luxproj").read_bytes()
    (project / "Old.luxproj").write_bytes(b'EngineVersion: "2099.3"\nFormatVersion: 42\n' + text)
    logs = launch(editor, root, project / "Old.luxproj", None)
    assert "2099.3" in logs and "update Lux to open it" in logs, "the log names the newer Lux and says to update"
    assert not (project / ".luxbackup").exists() and (project / scene).read_bytes() == (FIXTURE / scene).read_bytes(), "a newer project is not touched"
    print("PASS: a project saved by a newer Lux is refused, naming it", flush=True)

    # 4. LUX_PROJECT_UPGRADE=no leaves an older project alone and does not open it.
    root = output / "declined"
    project = make_project(root)
    logs = launch(editor, root, project / "Old.luxproj", "no")
    assert "LUX_PROJECT_UPGRADE=no" in logs and not (project / ".luxbackup").exists(), "declined: nothing upgraded, nothing backed up"
    assert (project / scene).read_bytes() == (FIXTURE / scene).read_bytes(), "declined: the scene is untouched"
    print("PASS: an older project is not upgraded when the upgrade is declined", flush=True)
    # 5. Asking: the dialog is up and nothing is opened or touched (the buttons are not clicked here).
    root = output / "asking"
    project = make_project(root)
    logs = launch(editor, root, project / "Old.luxproj", "ask")
    assert "Script module not found" not in logs and not (project / ".luxbackup").exists(), "asking: nothing is opened or backed up before the answer"
    assert (project / scene).read_bytes() == (FIXTURE / scene).read_bytes(), "asking: the scene is untouched"
    print("PASS: an older project waits for the answer, with the dialog drawing for 30 frames", flush=True)
    print("Evidence:", output)


if __name__ == "__main__":
    main()
