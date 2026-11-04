#!/usr/bin/env python3
"""Compile the production C# template renderer, then build its output against the actual Lux API.

Windows: run from an x64 MSVC developer prompt. Linux: set CXX if c++ is not on PATH.
"""
import os
from pathlib import Path
import shutil
import subprocess
import time
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]


def main():
    compiler = os.environ.get("CXX") or shutil.which("cl" if os.name == "nt" else "c++")
    if not compiler:
        raise RuntimeError("Use an x64 MSVC developer prompt or set CXX")
    output = ROOT / "bin/script-template-tests" / str(time.time_ns())
    output.mkdir(parents=True)
    executable = output / ("template-tests.exe" if os.name == "nt" else "template-tests")
    source = ROOT / "tests/editor/ScriptTemplateTests.cpp"
    include = ROOT / "Editor/Source"
    if Path(compiler).stem.lower() in ("cl", "clang-cl"):
        command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", "/I" + str(include), str(source), "/Fe:" + str(executable)]
    else:
        command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I" + str(include), str(source), "-o", str(executable)]
    started = time.time_ns()
    subprocess.run(command, cwd=output, check=True)
    assert executable.stat().st_size and executable.stat().st_mtime_ns >= started - 2_000_000_000
    generated = output / "Source"
    subprocess.run([str(executable), str(ROOT / "Editor/Resources/Templates/NewClassTemplate.cs"), str(generated)], cwd=output, check=True)
    # Compile the example too: removing its comment must yield valid Lux API usage.
    player = generated / "PlayerController.cs"
    player.write_text(player.read_text().replace("// Translation +=", "Translation +="))
    (output / "NuGet.Config").write_text('<configuration><packageSources><clear /></packageSources></configuration>')
    (output / "TemplateTests.csproj").write_text(f'''<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net9.0</TargetFramework><OutputType>Library</OutputType>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks><EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <Configurations>Debug;Release;Dist</Configurations>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="Source/**/*.cs" />
    <Reference Include="ScriptCore"><HintPath>{escape(str(ROOT / 'Editor/Resources/Scripts/ScriptCore.dll'))}</HintPath></Reference>
    <Reference Include="Coral.Managed"><HintPath>{escape(str(ROOT / 'Editor/DotNet/Coral.Managed.dll'))}</HintPath></Reference>
  </ItemGroup>
</Project>''')
    for config in ("Debug", "Release", "Dist"):
        subprocess.run(["dotnet", "build", str(output / "TemplateTests.csproj"), "-c", config, "--nologo", "--verbosity", "quiet"], cwd=output, check=True)
    print("PASS: generated starters and uncommented movement example compile against ScriptCore in Debug/Release/Dist")
    print("Evidence:", output)


if __name__ == "__main__":
    main()
