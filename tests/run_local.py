#!/usr/bin/env python3
"""Run the suites CI cannot (they launch the editor, so they need a GPU) and print one summary.

    python tests/run_local.py [--build] [--only substring ...]

Sets up the MSVC environment once (scripts/run_with_msvc.py), optionally builds Debug and Release,
then runs every tests/editor/run_*.py (sorted: run_remaster_export reuses the panel runners' saved
fixtures, which sort before it), the Play checks in Debug and Release, and the managed audio checks.
Each runner isolates itself with its own LUX_USER_DATA_DIR. Logs: bin/local-tests/<time>/.
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from run_with_msvc import msvc_env  # noqa: E402


def suites():
    found = [[path.relative_to(ROOT).as_posix()] for path in sorted((ROOT / "tests/editor").glob("run_*.py"))]
    return found + [["tests/play/run_play.py"], ["tests/play/run_play.py", "--config", "release"],
                    ["tests/audio/run_managed.py"]]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", action="store_true", help="build Debug and Release with MSBuild first")
    parser.add_argument("--only", nargs="*", default=[], help="run only suites whose command contains one of these")
    args = parser.parse_args()

    env = msvc_env()
    output = ROOT / "bin/local-tests" / time.strftime("%Y%m%d-%H%M%S")
    output.mkdir(parents=True)
    results = []

    def run(name, command):
        log = output / (name.replace("/", "_").replace(" ", "_") + ".log")
        started = time.monotonic()
        with log.open("w", encoding="utf-8", errors="replace") as stream:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
        results.append((name, code, time.monotonic() - started, log))
        print(f"{'PASS' if code == 0 else 'FAIL'}  {name}  ({time.monotonic() - started:.0f} s)", flush=True)
        return code == 0

    if args.build:
        for config in ("Debug", "Release"):
            if not run(f"build {config}", ["msbuild", "Lux.sln", "-restore", "-m", f"-p:Configuration={config}",
                                           "-p:Platform=Mixed Platforms", "-v:m", "-nologo"]):
                break  # testing a stale binary would report the old code
    if all(code == 0 for _, code, _, _ in results):
        for suite in suites():
            name = " ".join(suite)
            if not args.only or any(word in name for word in args.only):
                run(name, [sys.executable, *suite])

    failed = [(name, log) for name, code, _, log in results if code != 0]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed in {sum(r[2] for r in results) / 60:.1f} min; logs in {output}")
    for name, log in failed:
        print(f"  FAIL {name}: {log}")
    return 1 if failed or not results else 0


if __name__ == "__main__":
    sys.exit(main())
