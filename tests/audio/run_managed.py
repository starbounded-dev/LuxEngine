#!/usr/bin/env python3
"""Exercise production C# subtitle dispatch without a running editor or Coral native host."""
from pathlib import Path
import subprocess
import tempfile
from xml.sax.saxutils import escape

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="lux-dialogue-managed-") as temporary:
    directory = Path(temporary)
    (directory / "NuGet.Config").write_text('<configuration><packageSources><clear /></packageSources></configuration>')
    project = directory / "DialogueTests.csproj"
    project.write_text(f'''<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType><TargetFramework>net9.0</TargetFramework>
    <UseAppHost>false</UseAppHost><RollForward>Major</RollForward>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks><Nullable>enable</Nullable>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="{escape(str(root / 'ScriptCore/Source/**/*.cs'))}" />
    <Compile Include="{escape(str(root / 'tests/audio/DialogueManagedTests.cs'))}" />
    <Reference Include="Coral.Managed"><HintPath>{escape(str(root / 'Editor/DotNet/Coral.Managed.dll'))}</HintPath></Reference>
  </ItemGroup>
</Project>''')
    subprocess.run(["dotnet", "run", "--project", str(project), "--verbosity", "quiet"], check=True)
