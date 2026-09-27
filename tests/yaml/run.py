#!/usr/bin/env python3
"""YAML value oracle (docs/YAML_MIGRATION_PLAN.md): proves a YAML library reads the repo's assets
to the same values as the recorded reference.

    python tests/yaml/run.py            check against tests/yaml/reference.txt (default)
    python tests/yaml/run.py --write    re-record the reference (only for intended data changes)
    python tests/yaml/run.py --dump DIR write one "path = value" file per asset, for diffing
"""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent
BUILD = ROOT / "bin-int/YamlOracle"
REFERENCE = TESTS / "reference.txt"
YAML_CPP = ROOT / "Core/vendor/yaml-cpp"

# Engine YAML formats (AssetExtensions.h), plus the project file and asset registry.
EXTENSIONS = (".luxscene", ".luxproj", ".lmat", ".lzr", ".lprefab", ".ldialogue", ".lsurfaces")


def run(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def corpus():
    tracked = run(["git", "-C", ROOT, "ls-files"], capture_output=True, text=True).stdout.splitlines()
    return sorted(path for path in tracked if path.endswith(EXTENSIONS) and "/vendor/" not in path)


def vcvars():
    """Path to vcvars64.bat of the newest Visual Studio with the C++ toolset."""
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.exists():
        install = run([vswhere, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                       "-property", "installationPath"], capture_output=True, text=True).stdout.strip()
        if install:
            return Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
    for candidate in sorted(Path(r"C:\Program Files\Microsoft Visual Studio").glob("*/*/VC/Auxiliary/Build/vcvars64.bat"), reverse=True):
        return candidate
    sys.exit("error: no Visual Studio C++ toolset found")


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    sources = [TESTS / "Oracle.cpp", *sorted((YAML_CPP / "src").glob("*.cpp"))]
    if platform.system() == "Windows":
        exe = BUILD / "Oracle.exe"
        newest = max(source.stat().st_mtime for source in sources)
        if exe.exists() and exe.stat().st_mtime >= newest:
            return exe
        command = (f'call "{vcvars()}" >nul 2>nul && cl /nologo /EHsc /O2 /MD /std:c++20 /DYAML_CPP_STATIC_DEFINE '
                   f'/I "{YAML_CPP / "include"}" /Fo"{BUILD}\\\\" /Fe"{exe}" '
                   + " ".join(f'"{source}"' for source in sources))
        subprocess.run(command, shell=True, check=True, stdout=subprocess.DEVNULL)
    else:
        exe = BUILD / "Oracle"
        compiler = shutil.which("clang++") or shutil.which("g++")
        run([compiler, "-std=c++20", "-O2", "-DYAML_CPP_STATIC_DEFINE", "-I", YAML_CPP / "include", *sources, "-o", exe])
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true", help="re-record tests/yaml/reference.txt")
    mode.add_argument("--dump", metavar="DIR", help="write per-file value dumps to DIR")
    args = parser.parse_args()

    exe = build()
    files = corpus()
    listing = BUILD / "files.txt"
    listing.write_text("\n".join(files) + "\n", encoding="utf-8")

    command = [exe, "--list", listing, "--root", ROOT]
    if args.write:
        command += ["--write", REFERENCE]
    elif args.dump:
        command += ["--dump", Path(args.dump).resolve()]
    else:
        command += ["--check", REFERENCE]
    sys.exit(subprocess.run(list(map(str, command))).returncode)


if __name__ == "__main__":
    main()
