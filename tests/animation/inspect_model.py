#!/usr/bin/env python3
"""Builds ModelInspect.cpp from the engine's own skeletal import units and runs it on a model file.

    python scripts/run_with_msvc.py python tests/animation/inspect_model.py "path/to/model.fbx"

It prints the skeleton, clips and skin weights the importer produces (FBX binary included), which is the
quickest way to find out how a new rig imports. Debug build of the units, no editor or GPU.
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    compiler = shutil.which("cl")
    if os.name != "nt" or not compiler:
        raise SystemExit("Run from a Windows x64 MSVC developer prompt (scripts/run_with_msvc.py).")
    output = Path(tempfile.mkdtemp(prefix="lux-inspect-"))
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
    sources = [ROOT / "tests/animation/ModelInspect.cpp"]
    sources += [core / "Source/Lux" / (v + ".cpp") for v in (
        "Animation/Skeleton", "Animation/Animation", "Animation/Animator", "Animation/AnimationGraph", "Animation/AnimationGraphYaml", "Animation/AnimationEvents", "Animation/AnimationPose", "Serialization/Yaml", "Animation/SkinPalette", "Asset/SkeletalImporter",
        "Serialization/FileStream", "Serialization/Obfuscation", "Serialization/StreamReader", "Serialization/StreamWriter", "Core/UUID", "Core/Ref")]
    sources.append(core / "vendor/rapidyaml/ryml.cpp")  # the YAML reader the graph and events files use
    assimp = core / "vendor/assimp/bin/windows/Debug/assimp-vc143-mtd.lib"
    executable = output / "ModelInspect.exe"
    command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/Zc:preprocessor", "/Zc:__cplusplus", "/Od", "/MDd", "/MP4", "/Gy", "/Gw",
               "/fp:precise", "/Y-", *["/D" + v for v in defines], *["/I" + str(p) for p in includes],
               *map(str, sources), "/Fe:" + str(executable), "/link", str(assimp), "/OPT:REF", "/INCREMENTAL:NO"]
    with (output / "build.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=output, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        print((output / "build.log").read_text(encoding="utf-8", errors="replace")[-6000:])
        raise SystemExit("build failed")
    shutil.copy2(assimp.with_suffix(".dll"), output)
    subprocess.run([str(executable), *sys.argv[1:]], cwd=output, check=True, timeout=300)


if __name__ == "__main__":
    main()
