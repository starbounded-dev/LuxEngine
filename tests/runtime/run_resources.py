#!/usr/bin/env python3
"""Check generated Linux post-build resource copies on clean and existing output directories."""
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
makefile = (ROOT / "Lux-Runtime/Makefile").read_text()
font_paths = sorted(set(re.findall(r'\.FilePath = "(Resources/Fonts/[^"]+)"',
                                  (ROOT / "Core/Source/Lux/ImGui/ImGuiLayer.cpp").read_text())))
assert font_paths

with tempfile.TemporaryDirectory(prefix="lux-runtime-resources-") as temporary:
    work = Path(temporary)
    source = work / "Source files"
    shutil.copytree(ROOT / "Editor/Resources/Fonts", source / "Resources/Fonts")
    (source / "DotNet").mkdir()
    (source / "DotNet/Coral.Managed.dll").write_bytes(b"first version")
    for config in ("debug", "debug-as", "release", "dist"):
        block = makefile.split("ifeq ($(config)," + config + ")", 1)[1].split("endif", 1)[0]
        postbuild = block.split("define POSTBUILDCMDS", 1)[1].split("endef", 1)[0]
        copies = [shlex.split(line.strip()) for line in postbuild.splitlines()
                  if '"../Editor/Resources"' in line or '"../Editor/DotNet"' in line]
        assert len(copies) == 2, "Regenerate Linux projects before running this test"
        output = work / (config + " output with spaces")
        output.mkdir()
        for repeat in range(2):
            marker = b"first version" if repeat == 0 else b"updated version"
            (source / "DotNet/Coral.Managed.dll").write_bytes(marker)
            if repeat:
                # Reproduce an incremental build introducing a newly required font.
                (output / font_paths[0]).unlink()
            for template in copies:
                command = template.copy()
                component = Path(command[-2]).name
                original_target = Path(command[-1])
                runtime_parent = Path("../bin") / (config.title().replace("Debug-As", "Debug-AS") + "-linux-x86_64") / "Lux-Runtime"
                command[-2] = str(source / component)
                command[-1] = str(output / original_target.relative_to(runtime_parent))
                subprocess.run(command, check=True)
            for font in font_paths:
                assert (output / font).read_bytes() == (ROOT / "Editor" / font).read_bytes(), font
            assert (output / "DotNet/Coral.Managed.dll").read_bytes() == marker
            assert not (output / "Resources/Resources").exists()
            assert not (output / "DotNet/DotNet").exists()
        print("PASS:", config, "fresh/repeated copies, new fonts and updated managed host")
