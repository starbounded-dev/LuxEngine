#!/usr/bin/env python3
"""Check the Premake SDK file contract using disposable layouts; does not compile a Windows binary."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def find_premake():
    """Same lookup as scripts/Linux-Build.sh: repo root first, then vendor/bin (.exe on Windows)."""
    names = ["premake5.exe", "premake5"] if os.name == "nt" else ["premake5"]
    for directory in (ROOT, ROOT / "vendor" / "bin"):
        for name in names:
            candidate = directory / name
            if candidate.is_file() and os.access(candidate, os.X_OK):
                return candidate
    raise SystemExit("premake5 not found in the repo root or vendor/bin; run scripts/Linux-Build.sh once "
                     "(it downloads a pinned Linux build) or use the committed vendor/bin/premake5.exe on Windows")


PREMAKE = find_premake()
with tempfile.TemporaryDirectory(prefix="lux-audio-sdk-") as temporary:
    work = Path(temporary)
    fmod, va = work / "FMOD SDK", work / "VA SDK"
    probe = work / "probe.lua"
    probe.write_text('include(' + repr(str(ROOT / "Dependencies.lua")) + ')\n'
                     'newaction { trigger = "check-audio-sdk", description = "Validate audio SDK inputs",\n'
                     'execute = function() ValidateAudioSDK(); print("SDK_LAYOUT_OK"); print(FMODSDKRoot); end }\n')
    common = [fmod / "api/core/inc/fmod.hpp", fmod / "api/studio/inc/fmod_studio.hpp", va / "3d/native/include/vaudio.h"]
    platforms = {
        "windows": [fmod / "api/core/lib/x64/fmod_vc.lib", fmod / "api/core/lib/x64/fmod.dll",
                    fmod / "api/studio/lib/x64/fmodstudio_vc.lib", fmod / "api/studio/lib/x64/fmodstudio.dll",
                    va / "3d/native/production/windows/vaudionative.lib", va / "3d/native/production/windows/vaudionative.dll"],
        "linux": [fmod / "api/core/lib/x86_64/libfmod.so", fmod / "api/core/lib/x86_64/libfmod.so.14",
                  fmod / "api/studio/lib/x86_64/libfmodstudio.so", fmod / "api/studio/lib/x86_64/libfmodstudio.so.14",
                  va / "3d/native/production/linux/libvaudionative.so"]}
    for path in common + [path for files in platforms.values() for path in files]:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    environment = dict(os.environ, LUX_FMOD_SDK=str(fmod), LUX_VA_SDK=str(va))
    for platform, files in platforms.items():
        command = [str(PREMAKE), "--file=" + str(probe), "--os=" + platform, "check-audio-sdk"]
        result = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True, text=True)
        assert result.returncode == 0 and "SDK_LAYOUT_OK" in result.stdout, result.stdout + result.stderr
        for path in common + files:
            path.unlink()
            result = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True, text=True)
            output = result.stdout + result.stderr
            # Premake reports paths with forward slashes; Windows paths print with backslashes.
            assert result.returncode != 0 and (str(path) in output or path.as_posix() in output), output
            path.touch()
        print("PASS:", platform, "SDK overrides, paths with spaces, and each missing header/link/runtime file")
