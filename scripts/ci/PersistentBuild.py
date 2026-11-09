#!/usr/bin/env python3
"""Retain trusted-runner objects, invalidate toolchain changes, and relink current artifacts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

GENERATED = (
    "bin", "bin-int", "Core/vendor/JoltPhysics/bin", "Core/vendor/JoltPhysics/bin-int",
    "Core/vendor/Coral/Build", "Core/vendor/Coral/Intermediates",
    "Core/vendor/Coral/Coral.Managed/obj", "ScriptCore/obj",
    "Editor/LuxSampleProject/Assets/Scripts/obj", "Editor/LuxSampleProject/Assets/Scripts/Binaries",
    "Editor/DotNet", "Editor/Resources/Scripts",
)
VOLATILE = ("logs", "artifacts", "release")


def remove_generated(root, relative):
    root = root.resolve()
    path = root / relative
    resolved = path.resolve()
    if resolved == root or not resolved.is_relative_to(root):
        raise RuntimeError(f"Refusing to clean outside the checkout: {path}")
    # Junctions/symlinks must not redirect a cleanup to a different source tree.
    if os.path.normcase(str(resolved)) != os.path.normcase(str(path.absolute())):
        raise RuntimeError(f"Refusing redirected generated path: {path}")
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def prepare(root, platform, configuration, signature, metadata, cold=False):
    root = root.resolve()
    old = json.loads(metadata.read_text(encoding="utf-8")) if metadata.exists() else {}
    warm = not cold and old.get("signature") == signature
    if not warm:
        for relative in GENERATED:
            remove_generated(root, relative)
    for relative in VOLATILE:
        remove_generated(root, relative)
    suffix = f"{configuration}-{platform}-x86_64"
    if platform == "windows":
        targets = (f"bin/{suffix}/Core/Core.lib", f"bin/{suffix}/Editor/Editor.exe",
                   f"bin/{suffix}/Lux-Runtime/Lux-Runtime.exe")
    else:
        targets = (f"bin/{suffix}/Core/libCore.a", f"bin/{suffix}/Editor/Editor",
                   f"bin/{suffix}/Lux-Runtime/Lux-Runtime")
    # Retain objects/PCHs but force archives and executables to be freshly linked.
    # The workflow's existing timestamp checks therefore remain meaningful.
    for relative in targets:
        remove_generated(root, relative)
    metadata.parent.mkdir(parents=True, exist_ok=True)
    metadata.write_text(json.dumps({"signature": signature, "schema": 1}), encoding="utf-8")
    print("Persistent build cache: " + ("WARM (objects retained)" if warm else "COLD (toolchain/generator cache reset)"))
    return warm


def fingerprint(root, platform):
    digest = hashlib.sha256()
    digest.update(b"lux-persistent-build-v1\0")
    digest.update(platform.encode())
    paths = subprocess.check_output(
        ["git", "ls-files", "--recurse-submodules", "-z", "--", "*.lua", "scripts/*.py",
         "Directory.Build.props", "Directory.Build.targets"], cwd=root).decode().split("\0")
    for name in sorted(filter(None, paths)):
        path = root / name
        if path.is_file():
            digest.update(name.encode())
            digest.update(path.read_bytes())
    for name in ("VULKAN_SDK", "LUX_PREMAKE_OPTIONS"):
        digest.update((name + "=" + os.environ.get(name, "")).encode())
    sdk = Path(os.environ.get("VULKAN_SDK", ""))
    for path in (sdk / "Include/vulkan/vulkan_core.h", sdk / "include/vulkan/vulkan_core.h"):
        if path.is_file():
            digest.update(path.read_bytes())
    for program in (("dotnet", "--version"), ("cmake", "--version")):
        digest.update(subprocess.check_output(program, stderr=subprocess.STDOUT))
    if platform == "windows":
        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        installation = Path(subprocess.check_output(
            [str(vswhere), "-latest", "-products", "*", "-requires",
             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
            text=True).strip())
        version = (installation / "VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt").read_text().strip()
        compiler = installation / "VC/Tools/MSVC" / version / "bin/Hostx64/x64/cl.exe"
        digest.update(compiler.read_bytes())
    else:
        digest.update(subprocess.check_output(["clang++", "--version"]))
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "report"))
    parser.add_argument("--configuration", choices=("Debug", "Debug-AS", "Release", "Dist"), required=True)
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--cold", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if args.command == "prepare":
        git_path = Path(subprocess.check_output(["git", "rev-parse", "--git-path", "lux-build-cache.json"],
                                               cwd=root, text=True).strip())
        if not git_path.is_absolute():
            git_path = root / git_path
        prepare(root, args.platform, args.configuration, fingerprint(root, args.platform), git_path, args.cold)
    else:
        files = [p for directory in ("bin-int", "Core/vendor/JoltPhysics/bin-int")
                 for p in (root / directory).rglob("*") if p.is_file()]
        size = sum(p.stat().st_size for p in files)
        print(f"Persistent intermediate cache: {len(files)} files, {size / 1024**3:.2f} GiB")
        print("NuGet/pip user caches and .git/lfs remain outside checkout cleaning.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
