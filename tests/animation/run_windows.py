#!/usr/bin/env python3
"""Build and run the skeletal import tests from an x64 MSVC developer prompt (after project generation).

    python scripts/run_with_msvc.py python tests/animation/run_windows.py
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    compiler = shutil.which("cl")
    if os.name != "nt" or not compiler:
        parser.error("Run from a Windows x64 MSVC developer prompt.")
    output = (args.output or Path(tempfile.mkdtemp(prefix="lux-skeletal-"))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    core = ROOT / "Core"
    ns = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
    project = ET.parse(core / "Core.vcxproj").getroot()
    condition = "'$(Configuration)|$(Platform)'=='Debug|x64'"
    properties = next(g for g in project.findall("ms:PropertyGroup", ns)
                      if g.get("Condition") == condition and g.find("ms:ExternalIncludePath", ns) is not None)
    includes = [core / "Source", core / "vendor"]
    for value in properties.find("ms:ExternalIncludePath", ns).text.split(";"):
        if not value or "$" in value:
            continue
        path = Path(value)
        includes.append(path if path.is_absolute() else core / path)
    definitions = next(g for g in project.findall("ms:ItemDefinitionGroup", ns) if g.get("Condition") == condition)
    defines = definitions.find("ms:ClCompile/ms:PreprocessorDefinitions", ns).text.split(";")
    defines = [v for v in defines if v and "%" not in v and not v.startswith(("TRACY", "LUX_TRACK_MEMORY"))]
    # Production units, compiled directly: nothing here is a test double.
    sources = [ROOT / "tests/animation/SkeletalImportTests.cpp"]
    sources += [core / "Source/Lux" / (v + ".cpp") for v in (
        "Animation/Skeleton", "Animation/Animation", "Animation/AnimationPose", "Animation/SkinPalette", "Asset/SkeletalImporter",
        "Serialization/FileStream", "Serialization/StreamReader", "Serialization/StreamWriter",
        "Core/UUID", "Core/Ref")]
    assimp = core / "vendor/assimp/bin/windows/Debug/assimp-vc143-mtd.lib"
    executable = output / "skeletal-tests.exe"
    command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/Zc:preprocessor", "/Zc:__cplusplus",
               "/Od", "/MDd", "/MP4", "/Gy", "/Gw", "/fp:precise", "/Y-",
               *["/D" + v for v in defines], *["/I" + str(p) for p in includes],
               *sources, "/Fe:" + str(executable), "/link", str(assimp), "/OPT:REF", "/INCREMENTAL:NO"]
    print("Skeletal test artifacts:", output, flush=True)
    with (output / "build.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(list(map(str, command)), cwd=output, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        print((output / "build.log").read_text(encoding="utf-8", errors="replace")[-6000:])
        raise SystemExit("build failed")
    shutil.copy2(assimp.with_suffix(".dll"), output)  # assimp links as an import library
    subprocess.run([str(executable), str(ROOT / "tests/animation/assets/RiggedSimple.gltf"), str(output)],
                   cwd=output, check=True, timeout=60)


if __name__ == "__main__":
    main()
