#!/usr/bin/env python3
"""Test FMOD layouts and pinned Asterwave source validation without private dependencies."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import FetchAsterwave


def find_premake():
    names = ["premake5.exe", "premake5"] if os.name == "nt" else ["premake5"]
    for directory in (ROOT, ROOT / "vendor/bin"):
        for name in names:
            candidate = directory / name
            if candidate.is_file() and os.access(candidate, os.X_OK):
                return candidate
    raise SystemExit("premake5 not found in the repo root or vendor/bin; run scripts/Linux-Build.sh once "
                     "or use the committed vendor/bin/premake5.exe on Windows")


def expect_python_error(callback, message):
    try:
        callback()
    except RuntimeError as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError(f"Expected validation to reject: {message}")


def main():
    premake = find_premake()
    with tempfile.TemporaryDirectory(prefix="lux-audio-source-") as temporary:
        work = Path(temporary)
        fmod, source = work / "FMOD SDK", work / "Asterwave source"
        manifest = FetchAsterwave.load_manifest()
        # Public fixtures contain placeholder text only. A real local commit supplies a test pin,
        # so these tests exercise Git validation without accessing the private repository.
        for name in manifest["sources"] + manifest["required_files"]:
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n", encoding="utf-8")
        FetchAsterwave.git(source, "init")
        FetchAsterwave.git(source, "add", ".")
        FetchAsterwave.git(source, "-c", "user.name=Layout Test", "-c", "user.email=layout@example.invalid",
                           "-c", "commit.gpgsign=false", "commit", "-m", "Synthetic source layout")
        manifest["revision"] = FetchAsterwave.git(source, "rev-parse", "HEAD")

        shutil.copyfile(ROOT / "Dependencies.lua", work / "Dependencies.lua")
        (work / "vendor/premake_customization").mkdir(parents=True)
        shutil.copyfile(ROOT / "vendor/premake_customization/ordered_pairs.lua",
                        work / "vendor/premake_customization/ordered_pairs.lua")
        manifest_path = work / "scripts/dependencies/Asterwave.json"
        manifest_path.parent.mkdir(parents=True)
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        probe = work / "probe.lua"
        probe.write_text('include "Dependencies.lua"\n'
                         'newaction { trigger = "check-audio-sdk", description = "Validate audio sources",\n'
                         'execute = function() ValidateAudioSDK(); print("SDK_LAYOUT_OK"); end }\n', encoding="utf-8")
        common = [fmod / "api/core/inc/fmod.hpp", fmod / "api/studio/inc/fmod_studio.hpp"]
        platforms = {
            "windows": [fmod / "api/core/lib/x64/fmod_vc.lib", fmod / "api/core/lib/x64/fmod.dll",
                        fmod / "api/studio/lib/x64/fmodstudio_vc.lib", fmod / "api/studio/lib/x64/fmodstudio.dll"],
            "linux": [fmod / "api/core/lib/x86_64/libfmod.so", fmod / "api/core/lib/x86_64/libfmod.so.14",
                      fmod / "api/studio/lib/x86_64/libfmodstudio.so", fmod / "api/studio/lib/x86_64/libfmodstudio.so.14"]}
        for path in common + [path for files in platforms.values() for path in files]:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        environment = dict(os.environ, LUX_FMOD_SDK=str(fmod), LUX_ASTERWAVE_ROOT=str(source))

        for platform, files in platforms.items():
            command = [str(premake), "--file=" + str(probe), "--os=" + platform, "check-audio-sdk"]

            def check(expected_error=None):
                result = subprocess.run(command, cwd=work, env=environment, capture_output=True, text=True)
                output = (result.stdout + result.stderr).replace("\\", "/")
                if expected_error is None:
                    assert result.returncode == 0 and "SDK_LAYOUT_OK" in output, output
                else:
                    assert result.returncode != 0 and str(expected_error).replace("\\", "/") in output, output

            check()
            FetchAsterwave.validate_checkout(source, manifest)
            for path in common + files:
                path.unlink()
                check(path)
                path.touch()
            for name in manifest["sources"] + manifest["required_files"]:
                path = source / name
                path.unlink()
                check(path)
                expect_python_error(lambda: FetchAsterwave.validate_checkout(source, manifest), str(path))
                path.write_text("fixture\n", encoding="utf-8")

            wrong = dict(manifest, revision="0" * 40)
            manifest_path.write_text(json.dumps(wrong), encoding="utf-8")
            check("revision mismatch")
            expect_python_error(lambda: FetchAsterwave.validate_checkout(source, wrong), "revision mismatch")
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            changed = source / manifest["sources"][0]
            changed.write_text("modified fixture\n", encoding="utf-8")
            check("local changes")
            expect_python_error(lambda: FetchAsterwave.validate_checkout(source, manifest), "local changes")
            expect_python_error(lambda: FetchAsterwave.fetch(source, manifest), "dirty Asterwave checkout")
            changed.write_text("fixture\n", encoding="utf-8")

            untracked = source / "unexpected.cpp"
            untracked.touch()
            check("local changes")
            expect_python_error(lambda: FetchAsterwave.validate_checkout(source, manifest), "local changes")
            untracked.unlink()
            check()
            FetchAsterwave.fetch(source, manifest)  # An already pinned checkout needs no network.
            print(f"PASS: {platform}: FMOD and Asterwave files, spaced paths, exact pin, dirty/untracked refusal")

        expect_python_error(lambda: FetchAsterwave.fetch(ROOT, manifest), "Refusing to replace")
        expect_python_error(lambda: FetchAsterwave.fetch(ROOT / "Core/PrivateSourceNotIgnored", manifest), "gitignored")

        # Exercise a first-fetch failure and retry against the synthetic local Git repository.
        # Only the fetch URL is redirected; checkout, Git cleanliness and pin checks are real.
        retry = work / "Retry source"
        original_git = FetchAsterwave.git

        def fail_fetch(root, *arguments, **kwargs):
            if arguments[0] == "fetch":
                raise RuntimeError("simulated fetch failure")
            return original_git(root, *arguments, **kwargs)

        def local_fetch(root, *arguments, **kwargs):
            if arguments[0] == "fetch":
                arguments = ("-c", "protocol.file.allow=always", "fetch", "--depth=1", source.as_uri(), manifest["revision"])
            return original_git(root, *arguments, **kwargs)

        with patch.object(FetchAsterwave, "git", side_effect=fail_fetch):
            expect_python_error(lambda: FetchAsterwave.fetch(retry, manifest), "simulated fetch failure")
        with patch.object(FetchAsterwave, "git", side_effect=local_fetch):
            FetchAsterwave.fetch(retry, manifest)
        FetchAsterwave.validate_checkout(retry, manifest)
        print("PASS: fetch refuses public checkout/unignored destinations; incomplete initial fetch retries safely")
    return 0


if __name__ == "__main__":
    sys.exit(main())
