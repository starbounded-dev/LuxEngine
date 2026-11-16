#!/usr/bin/env python3
"""Compile Soundwave's engine compute shaders with glslc and check them against the CPU reference
on a real Vulkan device (SoundwaveGpuParity.cpp). Needs the Vulkan SDK (VULKAN_SDK) and a C++20
compiler; it does not need the engine or an editor."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "Editor/Resources/Shaders"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Directory for SPIR-V, the executable and logs")
    args = parser.parse_args()
    sdk = os.environ.get("VULKAN_SDK")
    if not sdk:
        parser.error("VULKAN_SDK is not set")
    sdk = Path(sdk)
    glslc = shutil.which("glslc") or str(sdk / ("Bin/glslc.exe" if os.name == "nt" else "bin/glslc"))
    compiler = os.environ.get("CXX") or shutil.which("cl") or shutil.which("clang++")
    if not compiler:
        parser.error("No C++20 compiler; use an x64 MSVC developer prompt or set CXX")
    output = (args.output or Path(tempfile.mkdtemp(prefix="lux-soundwave-gpu-"))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    spirv = output / "SoundwaveReverb.spv"
    subprocess.run([glslc, "-fshader-stage=comp", "--target-env=vulkan1.2", "-I", str(SHADERS / "Include/GLSL"),
                    str(SHADERS / "SoundwaveReverb.glsl"), "-o", str(spirv)], check=True)
    library = ROOT / "Core/vendor/soundwave"
    sources = [ROOT / "tests/audio/SoundwaveGpuParity.cpp", *sorted((library / "src").glob("*.cpp"))]
    executable = output / ("SoundwaveGpuParity" + (".exe" if os.name == "nt" else ""))
    if Path(compiler).name.lower() in ("cl", "cl.exe"):
        command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/O2", "/MD", "/I" + str(library / "include"),
                   "/I" + str(sdk / "Include"), *map(str, sources), "/Fe:" + str(executable), "/link", str(sdk / "Lib/vulkan-1.lib")]
    else:
        command = [compiler, "-std=c++20", "-O2", "-I" + str(library / "include"), "-I" + str(sdk / "include"),
                   *map(str, sources), "-o", str(executable), "-L" + str(sdk / "lib"), "-lvulkan"]
    with (output / "build.log").open("w", encoding="utf-8") as log:
        subprocess.run(command, cwd=output, check=True, stdout=log, stderr=subprocess.STDOUT)
    subprocess.run([str(executable), str(spirv)], cwd=output, check=True, timeout=120)


if __name__ == "__main__":
    main()
