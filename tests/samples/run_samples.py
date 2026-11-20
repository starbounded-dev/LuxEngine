#!/usr/bin/env python3
"""The sample games, from the release zip's layout with no source tree (docs/SAMPLE_GAMES_PLAN.md, M10).

For Coin Run (3D) and Pixel Hop (2D): stage the zip layout (the Dist editor, Resources, DotNet, the Dist player), copy the sample to Samples/<name> as the zip
ships it, build its scripts there (the project's own csproj finds ScriptCore and Coral in the install), open it and export it with the staged editor, then
run the exported game with LUX_SAMPLE_AUTOPLAY=1: the game plays itself (its character walks and jumps through the same code a player uses) and logs
`SAMPLE COMPLETE <name>` when it reaches its end. Windows and Linux. Needs a built Dist editor and Dist Lux-Runtime, the .NET SDK and a GPU with a desktop session.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/runtime"))
sys.path.insert(0, str(ROOT / "tests/editor"))
import run_standalone_export as rse  # noqa: E402

SAMPLES = {"CoinRun": "Scenes/Level.luxscene", "PixelHop": "Scenes/Level1.luxscene"}


def export(install, project_file, scene, name, root, timeout):
    user = root / "user"
    user.mkdir(parents=True, exist_ok=True)
    (user / "UserPreferences.yaml").write_text("UserPrefs:\n  ShowSplashScreen: false\n  RecentProjects: []\n", encoding="utf-8")
    golden = root / "golden"
    golden.mkdir()
    export_dir = root / "export"
    env = dict(os.environ)
    env.update({
        "LUX_USER_DATA_DIR": str(user), "LUX_NO_WATCHDOG": "1", "LUX_SKIP_BUILD": "1",
        "LUX_GOLDEN_DIR": str(golden), "LUX_GOLDEN_SCENE": scene, "LUX_GOLDEN_NAME": name,
        "LUX_GOLDEN_PROJECT": str(project_file), "LUX_GOLDEN_SIZE": "640x360",
        "LUX_GOLDEN_FRAME": "2", "LUX_GOLDEN_PERF_FRAMES": "1", "LUX_GOLDEN_PERF_WARMUP": "0",
        "LUX_GOLDEN_OPTIONS": "", "LUX_GOLDEN_SELFTEST": "0", "LUX_GOLDEN_PLAY": "0",
        "LUX_GOLDEN_EXPORT_DIR": str(export_dir),
    })
    log = root / "editor.log"
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(install / rse.EDITOR_FILE)], cwd=install, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    status = golden / f"{name}.status"
    rse.require(status.is_file() and status.read_text(encoding="utf-8").startswith("OK"), f"the staged editor did not export {name} (exit {result.returncode}); see {log}")
    fields = dict(line.split(": ", 1) for line in (golden / f"{name}.export.yaml").read_text(encoding="utf-8").splitlines() if ": " in line)
    return Path(fields["Root"].strip().strip("'\"")), Path(fields["Executable"].strip().strip("'\""))


def play(package, executable, root, name, seconds):
    env = {k: v for k, v in os.environ.items() if not k.startswith("LUX_GOLDEN_")}
    env.update(LUX_SAMPLE_AUTOPLAY="1", LUX_NO_WATCHDOG="1")
    elsewhere = root / "elsewhere"
    elsewhere.mkdir()
    shutil.rmtree(package / "Data/logs", ignore_errors=True)
    process = subprocess.Popen([str(executable)], cwd=elsewhere, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        process.wait(timeout=seconds)
    except subprocess.TimeoutExpired:
        process.kill()
        raise RuntimeError(f"{name} never finished playing itself in {seconds} s")
    text = "".join(log.read_text(encoding="utf-8", errors="replace") for log in sorted((package / "Data/logs").glob("*.log")))
    failed = [line for line in text.splitlines() if "SAMPLE FAILED" in line]
    rse.require(not failed, f"{name} gave up: {failed[0] if failed else ''}")
    done = [line for line in text.splitlines() if f"SAMPLE COMPLETE {name}" in line]
    rse.require(done, f"{name} never reported SAMPLE COMPLETE (exit {process.returncode})")
    return done[-1].split("SAMPLE COMPLETE", 1)[1].strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", default="", help="one sample: CoinRun or PixelHop")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    root = Path(tempfile.mkdtemp(prefix="lux-samples-"))
    print("Working in", root, flush=True)
    try:
        install = root / "LuxEngine"
        rse.stage_install(install, "dist", "dist")
        for name, scene in SAMPLES.items():
            if args.only and args.only != name:
                continue
            sample = install / "Samples" / name
            shutil.copytree(ROOT / "Editor/Samples" / name, sample, ignore=shutil.ignore_patterns("Binaries", "obj", "Cache"))
            subprocess.run(["dotnet", "build", str(sample / "Assets/Scripts" / f"{name}.csproj"), "-c", "Dist", "--nologo", "-v:q"], check=True, stdout=subprocess.DEVNULL)
            work = root / name
            work.mkdir()
            package, executable = export(install, sample / f"{name}.luxproj", scene, name, work, args.timeout)
            result = play(package, executable, work, name, 240)
            print(f"PASS: {name} exported from the zip layout and played itself: {result}", flush=True)
    except Exception:
        print("Evidence kept in", root, flush=True)
        raise
    else:
        if not args.keep:
            shutil.rmtree(root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
