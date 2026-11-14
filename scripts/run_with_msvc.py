#!/usr/bin/env python3
"""Run a command inside the MSVC x64 environment: python scripts/run_with_msvc.py <command> [args...]

Finds vcvars64.bat (vswhere, then the Visual Studio 18 Community path), captures its environment
once, and runs the command with it. Use it for tests that compile a host with cl.exe.
"""
import os
import subprocess
import sys
from pathlib import Path

FALLBACK = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"


def find_vcvars():
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.is_file():
        out = subprocess.run([str(vswhere), "-latest", "-products", "*", "-find", r"VC\Auxiliary\Build\vcvars64.bat"],
                             capture_output=True, text=True).stdout.strip().splitlines()
        if out and Path(out[0]).is_file():
            return out[0]
    if Path(FALLBACK).is_file():
        return FALLBACK
    raise SystemExit("vcvars64.bat not found; install Visual Studio with the C++ workload")


def msvc_env():
    dump = subprocess.check_output(f'cmd /d /s /c ""{find_vcvars()}" >nul && set"', text=True, stderr=subprocess.DEVNULL)  # vcvars nags about vswhere on PATH
    env = {k: v for k, v in os.environ.items() if k.lower() != "path"}  # the var may be "PATH" or "Path"
    env.update(line.split("=", 1) for line in dump.splitlines() if "=" in line and not line.startswith("="))
    return env


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    sys.exit(subprocess.run(sys.argv[1:], env=msvc_env()).returncode)
