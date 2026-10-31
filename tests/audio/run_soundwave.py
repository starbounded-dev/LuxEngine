#!/usr/bin/env python3
"""Build and run Soundwave's independent C++20 contract checks without engine/audio SDKs."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("CXX"), help="C++ compiler (one executable path)")
    parser.add_argument("--output", type=Path, help="Directory for the executable and build log")
    args = parser.parse_args()
    compiler = args.compiler or next((path for name in ("cl", "clang++", "g++") if (path := shutil.which(name))), None)
    if not compiler:
        parser.error("No C++20 compiler found. On Windows, use an x64 MSVC developer prompt; otherwise set CXX or --compiler.")
    output = (args.output or Path(tempfile.mkdtemp(prefix="lux-soundwave-"))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    # A unique artifact prevents an old executable from satisfying a failed or no-op build.
    started = time.time_ns()
    executable = output / ("soundwave-" + str(started) + (".exe" if os.name == "nt" else ""))
    library = ROOT / "Core/vendor/soundwave"
    sources = [ROOT / "tests/audio/SoundwaveTests.cpp", library / "src/AcousticPath.cpp",
               library / "src/AcousticMaterialSettings.cpp"]
    msvc = Path(compiler).name.lower() in ("cl", "cl.exe")
    if msvc:
        command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", "/fp:precise", "/MD",
                   "/I" + str(library / "include"), *sources, "/Fe:" + str(executable)]
    else:
        command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off",
                   "-I" + str(library / "include"), *sources, "-o", executable]
    print("Soundwave test artifacts:", output, flush=True)
    with (output / "build.log").open("w", encoding="utf-8") as log:
        subprocess.run(list(map(str, command)), cwd=output, check=True, stdout=log, stderr=subprocess.STDOUT)
    artifact = executable.stat()
    if artifact.st_size == 0 or artifact.st_mtime_ns < started - 2_000_000_000:
        raise RuntimeError("Compiler did not produce a fresh Soundwave executable")
    subprocess.run([str(executable)], cwd=output, check=True, timeout=30)
    print("Verified fresh artifact:", executable, flush=True)


if __name__ == "__main__":
    main()
