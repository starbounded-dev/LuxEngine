#!/usr/bin/env python3
"""Validate bundled primitive geometry and exercise production project-template copying.

Run from an x64 MSVC developer prompt on Windows, or with CXX on Linux.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    compiler = os.environ.get("CXX") or shutil.which("cl" if os.name == "nt" else "c++")
    if not compiler:
        parser.error("No C++ compiler found; use an x64 MSVC developer prompt or set CXX")
    output = (args.output or ROOT / "bin/project-template-tests" / str(time.time_ns())).resolve()
    output.mkdir(parents=True, exist_ok=True)
    resources = ROOT / "Editor/Resources/Meshes/Default"
    primitives = ("Capsule", "Cone", "Cube", "Cylinder", "Plane", "Sphere", "Torus")
    for name in primitives:
        gltf = json.loads((resources / (name + ".gltf")).read_text())
        assert gltf["asset"]["version"] == "2.0", name
        assert gltf["meshes"] and gltf["nodes"] and gltf["scenes"], name
        for buffer in gltf["buffers"]:
            path = resources / buffer["uri"]
            assert path.parent == resources and path.is_file(), path
            assert path.stat().st_size == buffer["byteLength"], path
        for view in gltf["bufferViews"]:
            assert view.get("byteOffset", 0) + view["byteLength"] <= gltf["buffers"][view["buffer"]]["byteLength"], name
        for mesh in gltf["meshes"]:
            for primitive in mesh["primitives"]:
                positions = gltf["accessors"][primitive["attributes"]["POSITION"]]
                assert positions["count"] > 0 and positions["type"] == "VEC3", name
    print("PASS: seven glTF primitives with complete external buffers and bounded views", flush=True)
    executable = output / ("project-templates.exe" if os.name == "nt" else "project-templates")
    source = ROOT / "tests/editor/ProjectTemplateTests.cpp"
    include = ROOT / "Editor/Source"
    if Path(compiler).stem.lower() in ("cl", "clang-cl"):
        command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", "/I" + str(include), str(source), "/Fe:" + str(executable)]
    else:
        command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I" + str(include), str(source), "-o", str(executable)]
    started = time.time_ns()
    subprocess.run(command, cwd=output, check=True)
    artifact = executable.stat()
    if artifact.st_size == 0 or artifact.st_mtime_ns < started - 2_000_000_000:
        raise RuntimeError("Compiler did not produce a fresh test executable")
    subprocess.run([str(executable), str(resources), str(output / "fixtures")], cwd=output, check=True, timeout=30)
    print("Verified fresh artifact:", executable)


if __name__ == "__main__":
    main()
