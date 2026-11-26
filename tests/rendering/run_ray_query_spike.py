#!/usr/bin/env python3
"""GI0: compile RayQuerySpike.comp with glslc and run RayQuerySpike.cpp on a real Vulkan device
(acceleration structures + ray query, no engine, no editor, no window). Prints SKIP on GPUs without
ray query. Needs the Vulkan SDK (VULKAN_SDK) and a C++20 compiler.

    python tests/rendering/run_ray_query_spike.py   (from an x64 MSVC developer prompt)
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


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
    output = (args.output or Path(tempfile.mkdtemp(prefix="lux-ray-query-"))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    spirv = output / "RayQuerySpike.spv"
    subprocess.run([glslc, "-fshader-stage=comp", "--target-env=vulkan1.2", str(ROOT / "tests/rendering/RayQuerySpike.comp"), "-o", str(spirv)], check=True)
    source = ROOT / "tests/rendering/RayQuerySpike.cpp"
    executable = output / ("RayQuerySpike" + (".exe" if os.name == "nt" else ""))
    if Path(compiler).name.lower() in ("cl", "cl.exe"):
        command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/O2", "/MD", "/I" + str(sdk / "Include"),
                   str(source), "/Fe:" + str(executable), "/link", str(sdk / "Lib/vulkan-1.lib")]
    else:
        command = [compiler, "-std=c++20", "-O2", "-I" + str(sdk / "include"), str(source),
                   "-o", str(executable), "-L" + str(sdk / "lib"), "-lvulkan"]
    with (output / "build.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=output, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        print((output / "build.log").read_text(encoding="utf-8", errors="replace")[-6000:])
        raise SystemExit("build failed")
    subprocess.run([str(executable), str(spirv)], cwd=output, check=True, timeout=120)


if __name__ == "__main__":
    main()
