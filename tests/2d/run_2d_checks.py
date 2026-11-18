#!/usr/bin/env python3
"""Build and run the headless 2D checks (tests/2d/Checks2D.cpp). Windows only.

The checks link Core.lib and run the engine's real project/scene serializers, editor camera and Box2D
with no window and no GPU. They need the solution built in the same configuration first
(`MSBuild Lux.sln ... -p:Configuration=Debug "-p:Platform=Mixed Platforms"`).

The test project is derived from Editor/Editor.vcxproj so it gets every include path and library
unchanged: one source file, no resources, no post-build steps. Scene() always builds a Renderer2D,
which needs a GPU device, so the source replaces its constructor and /FORCE:MULTIPLE lets that win
(expect LNK4006).
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BACKSLASH = "\\"
MSBUILD_FALLBACK = r"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"


def find_msbuild():
    found = shutil.which("msbuild")
    if found:
        return found
    if Path(MSBUILD_FALLBACK).is_file():
        return MSBUILD_FALLBACK
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.is_file():
        out = subprocess.run([str(vswhere), "-latest", "-requires", "Microsoft.Component.MSBuild", "-find", r"MSBuild\**\Bin\MSBuild.exe"],
                             capture_output=True, text=True).stdout.splitlines()
        if out:
            return out[0]
    sys.exit("MSBuild not found")


def derive_project(config, output):
    source = (ROOT / "Editor" / "Editor.vcxproj").read_text(encoding="utf-8")
    item = '<ItemGroup>\n    <ClCompile Include="' + str(ROOT / "tests/2d/Checks2D.cpp") + '" />\n  </ItemGroup>'
    project = re.sub(r"<ItemGroup>\s*(<ClCompile Include=[^>]*/>\s*)+</ItemGroup>", lambda m: item, source, count=1)
    project = re.sub(r"\s*<ClInclude Include=[^>]*/>", "", project)
    project = re.sub(r"\s*<ResourceCompile Include=[^>]*/>", "", project)
    project = re.sub(r"<PostBuildEvent>.*?</PostBuildEvent>", "", project, flags=re.S)

    out_dir = "<OutDir>$(ProjectDir).." + BACKSLASH + "bin" + BACKSLASH + config + "-windows-x86_64" + BACKSLASH + "Editor" + BACKSLASH + "</OutDir>"
    int_dir = "<IntDir>$(ProjectDir).." + BACKSLASH + "bin-int" + BACKSLASH + config + "-windows-x86_64" + BACKSLASH + "Editor" + BACKSLASH + "</IntDir>"
    if out_dir not in project or int_dir not in project:
        sys.exit("Editor.vcxproj has no " + config + " OutDir/IntDir")
    project = project.replace(out_dir, "<OutDir>" + str(output / "bin") + BACKSLASH + "</OutDir>")
    project = project.replace(int_dir, "<IntDir>" + str(output / "obj") + BACKSLASH + "</IntDir>")
    project = project.replace("<TargetName>Editor</TargetName>", "<TargetName>Checks2D</TargetName>")
    link = "<AdditionalOptions>/ignore:4099 /DELAYLOAD:vulkan-1.dll"
    if link not in project:
        sys.exit("Editor.vcxproj link options changed; update tests/2d/run_2d_checks.py")
    project = project.replace(link, "<AdditionalOptions>/ignore:4099 /FORCE:MULTIPLE /DELAYLOAD:vulkan-1.dll")
    project = re.sub(r"<ProjectGuid>\{[^}]*\}</ProjectGuid>", lambda m: "<ProjectGuid>{5E2D0C41-7B1A-4D55-9C21-2D4A6F8B1E07}</ProjectGuid>", project)
    path = ROOT / "Editor" / "Checks2D.vcxproj"
    path.write_text(project, encoding="utf-8")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="Debug", choices=["Debug", "Release"])
    parser.add_argument("--no-build", action="store_true", help="run the last build")
    args = parser.parse_args()

    core = ROOT / "bin" / (args.config + "-windows-x86_64") / "Core" / "Core.lib"
    if not core.is_file():
        sys.exit("Build Lux.sln (" + args.config + ") first: " + str(core) + " is missing")

    output = ROOT / "bin" / "2d-checks" / args.config
    exe = output / "bin" / "Checks2D.exe"
    if not args.no_build:
        project = derive_project(args.config, output)
        command = [find_msbuild(), str(project), "-nologo", "-v:minimal", "-p:Configuration=" + args.config, "-p:Platform=x64",
                   "-p:BuildProjectReferences=false", "-p:SolutionDir=" + str(ROOT) + BACKSLASH, "-clp:ErrorsOnly;Summary"]
        subprocess.run(command, check=True)
    if not exe.is_file():
        sys.exit("Missing " + str(exe))

    env = dict(os.environ)
    env["PATH"] = str(ROOT / "bin" / (args.config + "-windows-x86_64") / "Editor") + os.pathsep + env["PATH"]
    run_dir = output / "run"
    shutil.rmtree(run_dir, ignore_errors=True)
    result = subprocess.run([str(exe), str(run_dir)], cwd=ROOT / "Editor", env=env, timeout=300)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
