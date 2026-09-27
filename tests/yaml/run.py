#!/usr/bin/env python3
"""YAML value oracle (docs/YAML_MIGRATION_PLAN.md): proves the engine's YAML layer (Lux::Yaml) reads the
repo's assets to the same values as the recorded reference (first recorded with yaml-cpp, before it
was removed).

    python tests/yaml/run.py            run every check (default)
    python tests/yaml/run.py --write    re-record the reference (only for intended data changes)
    python tests/yaml/run.py --dump DIR write one "path = value" file per asset, for diffing

Checks: Lux::Yaml against the reference; every asset parsed, re-written with Yaml::Writer and
re-parsed against it; and Lux::Yaml's self-tests.
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
CORE = ROOT / "Core"

# Engine YAML formats (AssetExtensions.h), plus the project file and asset registry.
EXTENSIONS = (".luxscene", ".luxproj", ".lmat", ".lzr", ".lprefab", ".ldialogue", ".lsurfaces")

TARGETS = {
    # Lux::Yaml over rapidyaml, built from the engine's own sources.
    "Oracle": {
        "sources": [TESTS / "Oracle.cpp", CORE / "Source/Lux/Serialization/Yaml.cpp", CORE / "vendor/rapidyaml/ryml.cpp",
                    CORE / "Source/Lux/Core/Ref.cpp", CORE / "Source/Lux/Core/UUID.cpp"],
        "includes": [TESTS / "shim", CORE / "Source", CORE / "vendor/glm", CORE / "vendor/rapidyaml"],
        "defines": ["LUX_PLATFORM_WINDOWS" if platform.system() == "Windows" else "LUX_PLATFORM_LINUX"],
    },
}


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


def build(name):
    target = TARGETS[name]
    out_dir = BUILD / name
    out_dir.mkdir(parents=True, exist_ok=True)
    sources = target["sources"]
    headers = [CORE / "Source/Lux/Serialization/Yaml.h"] if name == "Oracle" else []
    newest = max(path.stat().st_mtime for path in [*sources, *headers])

    if platform.system() == "Windows":
        exe = out_dir / f"{name}.exe"
        if exe.exists() and exe.stat().st_mtime >= newest:
            return exe
        flags = " ".join([*(f'/D{d}' for d in target["defines"]), *(f'/I "{i}"' for i in target["includes"])])
        command = (f'call "{vcvars()}" >nul 2>nul && cl /nologo /EHsc /O2 /MD /std:c++20 /utf-8 {flags} '
                   f'/Fo"{out_dir}\\\\" /Fe"{exe}" ' + " ".join(f'"{source}"' for source in sources))
        result = subprocess.run(command, shell=True, capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"build of {name} failed:\n{result.stdout[-4000:]}")
    else:
        exe = out_dir / name
        compiler = shutil.which("clang++") or shutil.which("g++")
        run([compiler, "-std=c++20", "-O2", *(f"-D{d}" for d in target["defines"]),
             *(f"-I{i}" for i in target["includes"]), *sources, "-o", exe])
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true", help="re-record tests/yaml/reference.txt")
    mode.add_argument("--dump", metavar="DIR", help="write per-file value dumps to DIR")
    args = parser.parse_args()

    listing = BUILD / "files.txt"
    BUILD.mkdir(parents=True, exist_ok=True)
    listing.write_text("\n".join(corpus()) + "\n", encoding="utf-8")
    common = ["--list", listing, "--root", ROOT]

    if args.write or args.dump:
        extra = ["--write", REFERENCE] if args.write else ["--dump", Path(args.dump).resolve()]
        sys.exit(subprocess.run(list(map(str, [build("Oracle"), *common, *extra]))).returncode)

    checks = [
        ("Lux::Yaml vs reference", [build("Oracle"), *common, "--check", REFERENCE]),
        ("Lux::Yaml write round-trip", [build("Oracle"), *common, "--check", REFERENCE, "--roundtrip"]),
        ("Lux::Yaml self-test", [build("Oracle"), "--selftest"]),
    ]
    failed = 0
    for label, command in checks:
        print(f"== {label}", flush=True)
        failed += subprocess.run(list(map(str, command))).returncode != 0
    print("ALL YAML CHECKS PASSED" if not failed else f"{failed} YAML CHECK(S) FAILED")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
