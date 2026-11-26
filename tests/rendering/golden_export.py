"""Shared export and standalone-player capture for rendering checks."""

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def export_and_capture(project, scene, label, config):
    golden = ROOT / "bin/golden" / label
    subprocess.run([
        sys.executable, str(ROOT / "tests/rendering/golden_run.py"),
        "--config", config, "--project", str(project), "--label", label, "--scenes", scene,
        "--size", "961x541", "--frame", "100", "--perf-frames", "10", "--export-dir", str(project.parent / "Export"),
    ], cwd=ROOT, check=True)
    metadata = (golden / f"{scene}.export.yaml").read_text(encoding="utf-8")
    fields = dict(line.split(": ", 1) for line in metadata.splitlines() if ": " in line)
    package, executable = (Path(fields[name].strip().strip("'\"")) for name in ("Root", "Executable"))
    image = golden / "Player.lximg"
    image.unlink(missing_ok=True)
    logfile = golden / "Player.log"
    env = dict(os.environ, LUX_NO_WATCHDOG="1", LUX_RUNTIME_CAPTURE=str(image), LUX_RUNTIME_CAPTURE_FRAME="120")
    with logfile.open("w", encoding="utf-8") as log:
        subprocess.run([str(executable), "--project=" + str(package)], cwd=package, env=env,
                       stdout=log, stderr=subprocess.STDOUT, timeout=120, check=True)
    return package, image, logfile.read_text(encoding="utf-8", errors="replace")
