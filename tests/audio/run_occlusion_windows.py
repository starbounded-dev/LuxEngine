#!/usr/bin/env python3
"""Run production occlusion regression tests from an x64 MSVC developer prompt."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    compiler = shutil.which("cl")
    if os.name != "nt" or not compiler:
        parser.error("Run from a Windows x64 MSVC developer prompt; use tests/audio/run.py on Linux.")
    output = (args.output or Path(tempfile.mkdtemp(prefix="lux-occlusion-"))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    core = ROOT / "Core"
    namespace = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
    project = ET.parse(core / "Core.vcxproj").getroot()
    condition = "'$(Configuration)|$(Platform)'=='Debug|x64'"
    properties = next(group for group in project.findall("ms:PropertyGroup", namespace)
                      if group.get("Condition") == condition and group.find("ms:ExternalIncludePath", namespace) is not None)
    includes = [core / "Source", core / "vendor"]
    for value in properties.find("ms:ExternalIncludePath", namespace).text.split(";"):
        if not value or "$" in value:
            continue
        path = Path(value)
        includes.append(path if path.is_absolute() else core / path)
    definitions = next(group for group in project.findall("ms:ItemDefinitionGroup", namespace)
                       if group.get("Condition") == condition)
    defines = definitions.find("ms:ClCompile/ms:PreprocessorDefinitions", namespace).text.split(";")
    defines = [value for value in defines if value and "%" not in value
               and not value.startswith(("TRACY", "LUX_TRACK_MEMORY"))]
    # Compile actual production units; function sections discard unrelated engine entry points.
    # No acoustic computation or material accessor is replaced by a test double.
    sources = [ROOT / "tests/audio/OcclusionTests.cpp"]
    sources += [core / "Source/Lux" / (value + ".cpp") for value in (
        "Audio/AudioOcclusion", "Audio/AudioOcclusionSettings", "Audio/AcousticMaterial",
        "Audio/AudioGeometrySystem", "Serialization/FileStream", "Serialization/StreamReader",
        "Serialization/StreamWriter", "Serialization/Yaml", "Core/UUID", "Core/Ref", "Audio/RaytracedAudioScene")]
    sources += [core / "vendor/rapidyaml/ryml.cpp", core / "vendor/soundwave/src/AcousticPath.cpp",
                core / "vendor/soundwave/src/AcousticMaterialSettings.cpp"]
    va_include = next(path for path in includes if path.as_posix().endswith("/3d/native/include"))
    native = va_include.parent
    libraries = [native / "production/windows/vaudionative.lib"]
    started = time.time_ns()
    executable = output / ("occlusion-" + str(started) + ".exe")
    command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/Zc:preprocessor", "/Zc:__cplusplus",
               "/Od", "/MDd", "/MP4", "/Gy", "/Gw", "/fp:precise", "/Y-",
               *["/D" + value for value in defines], *["/I" + str(path) for path in includes],
               *sources, "/Fe:" + str(executable), "/link", *libraries, "/OPT:REF", "/INCREMENTAL:NO"]
    print("Occlusion test artifacts:", output, flush=True)
    with (output / "build.log").open("w", encoding="utf-8") as log:
        subprocess.run(list(map(str, command)), cwd=output, check=True, stdout=log, stderr=subprocess.STDOUT)
    artifact = executable.stat()
    if artifact.st_size == 0 or artifact.st_mtime_ns < started - 2_000_000_000:
        raise RuntimeError("Compiler did not produce a fresh occlusion executable")
    environment = dict(os.environ)
    environment["PATH"] = os.pathsep.join([str(path.parent) for path in libraries] + [environment["PATH"]])
    subprocess.run([str(executable), str(output)], cwd=output, env=environment, check=True, timeout=30)
    print("Verified fresh artifact:", executable, flush=True)


if __name__ == "__main__":
    main()
