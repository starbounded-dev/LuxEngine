"""Stage the FMOD Engine SDK files CI needs into a private-repository layout.

The SDK is licensed and must never be committed to the public LuxEngine repository. CI checks
out a separate private repository instead (see .github/workflows/main.yml). This script copies
only the headers, link libraries, runtime libraries and licence files the build reads, from the
SDK packages you downloaded yourself, into:

    <out>/FMOD/windows/api/{core,studio}/{inc,lib/x64}      LUX_FMOD_SDK on Windows runners
    <out>/FMOD/linux/api/{core,studio}/{inc,lib/x86_64}     LUX_FMOD_SDK on Linux runners

Example:

    python scripts/ci/StageAudioSDKs.py --out ../LuxEngine-AudioSDKs ^
        --fmod-windows "Core/vendor/FMOD/FMOD Studio API Windows" ^
        --fmod-linux fmodstudioapi20314linux.tar.gz

Then commit and push <out> to the private repository. Any platform argument may be omitted to
stage only the other; CI jobs for a missing platform fail at project generation. Asterwave source
is checked out separately at its tracked revision; use scripts/FetchAsterwave.py for local setup.
"""

import argparse
import shutil
import sys
import tarfile
from pathlib import Path

FMOD_WINDOWS_LIBRARIES = [
    "api/core/lib/x64/fmod_vc.lib",
    "api/core/lib/x64/fmod.dll",
    "api/studio/lib/x64/fmodstudio_vc.lib",
    "api/studio/lib/x64/fmodstudio.dll",
]

# libfmod.so is what the linker resolves; the .so.14 files are what the executables load at run
# time. Symlinks are copied as regular files so the layout survives Windows checkouts and git.
FMOD_LINUX_LIBRARIES = [
    "api/core/lib/x86_64/libfmod.so",
    "api/core/lib/x86_64/libfmod.so.14",
    "api/studio/lib/x86_64/libfmodstudio.so",
    "api/studio/lib/x86_64/libfmodstudio.so.14",
]

FMOD_HEADER_DIRS = ["api/core/inc", "api/studio/inc"]
FMOD_LICENCE = "doc/LICENSE.TXT"

def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def copy_file(source: Path, destination: Path):
    if not source.is_file():
        fail(f"required SDK file not found: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)  # follows symlinks: stores the real library bytes


def copy_tree(source: Path, destination: Path):
    if not source.is_dir():
        fail(f"required SDK directory not found: {source}")
    shutil.copytree(source, destination, dirs_exist_ok=True)


def find_fmod_root(directory: Path) -> Path:
    """Accept either the package root (containing api/) or a folder that contains it."""
    if (directory / "api" / "core" / "inc" / "fmod.hpp").is_file():
        return directory
    matches = [p.parents[3] for p in directory.glob("*/api/core/inc/fmod.hpp")]
    if len(matches) != 1:
        fail(f"expected exactly one FMOD Engine SDK under {directory}, found {len(matches)}")
    return matches[0]


def extract_fmod_archive(archive: Path, scratch: Path) -> Path:
    # filter="data" rejects absolute paths and links escaping the extraction directory.
    if not hasattr(tarfile, "data_filter"):
        fail("extracting the FMOD archive safely needs Python 3.11.4+ (tarfile data filter); extract it yourself and pass the folder")
    with tarfile.open(archive) as tar:
        tar.extractall(scratch, filter="data")
    return find_fmod_root(scratch)


def stage_fmod(source: Path, destination: Path, libraries):
    root = find_fmod_root(source)
    for header_dir in FMOD_HEADER_DIRS:
        copy_tree(root / header_dir, destination / header_dir)
    for library in libraries:
        copy_file(root / library, destination / library)
    licence = root / FMOD_LICENCE
    if licence.is_file():
        copy_file(licence, destination / FMOD_LICENCE)
    print(f"Staged FMOD Engine SDK from {root} -> {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", required=True, type=Path, help="Working tree of the private SDK repository.")
    parser.add_argument("--fmod-windows", type=Path, help="FMOD Engine SDK for Windows (package root).")
    parser.add_argument("--fmod-linux", type=Path, help="FMOD Engine SDK for Linux (package root or .tar.gz).")
    args = parser.parse_args()

    if not (args.fmod_windows or args.fmod_linux):
        fail("nothing to stage: pass at least one of --fmod-windows, --fmod-linux")

    out = args.out.resolve()
    repository_root = Path(__file__).resolve().parents[2]
    if (repository_root / ".git").exists() and out.is_relative_to(repository_root):
        fail(f"--out must be outside the public LuxEngine repository: {out}")
    out.mkdir(parents=True, exist_ok=True)

    if args.fmod_windows:
        stage_fmod(args.fmod_windows, out / "FMOD" / "windows", FMOD_WINDOWS_LIBRARIES)

    if args.fmod_linux:
        if args.fmod_linux.is_file():
            scratch = out / ".staging-fmod-linux"
            shutil.rmtree(scratch, ignore_errors=True)
            try:
                stage_fmod(extract_fmod_archive(args.fmod_linux, scratch), out / "FMOD" / "linux", FMOD_LINUX_LIBRARIES)
            finally:
                shutil.rmtree(scratch, ignore_errors=True)
        else:
            stage_fmod(args.fmod_linux, out / "FMOD" / "linux", FMOD_LINUX_LIBRARIES)

    print(f"Done. Commit and push {out} to the private SDK repository.")


if __name__ == "__main__":
    main()
