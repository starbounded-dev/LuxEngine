#!/usr/bin/env python3
"""Exercise production docking against the vendored ImGui without a GPU or user preferences."""
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    env = os.environ.copy()
    compiler = env.get("CXX") or shutil.which("cl" if os.name == "nt" else "c++")
    if not compiler and os.name == "nt":
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        install = subprocess.check_output([str(vswhere), "-latest", "-property", "installationPath"], text=True).strip()
        setup = Path(install) / "Common7/Tools/VsDevCmd.bat"
        settings = subprocess.check_output(f'cmd /d /s /c ""{setup}" -arch=x64 >nul && set"', text=True)
        env = {k: v for k, v in env.items() if k.lower() != "path"}  # the env var may be "PATH" or "Path"
        env.update(line.split("=", 1) for line in settings.splitlines() if "=" in line and not line.startswith("="))
        compiler = shutil.which("cl", path=next(v for k, v in env.items() if k.lower() == "path"))
    if not compiler:
        raise RuntimeError("Use an x64 developer prompt or set CXX")
    output = ROOT / "bin/workspace-tests" / str(time.time_ns())
    output.mkdir(parents=True)
    executable = output / ("workspaces.exe" if os.name == "nt" else "workspaces")
    sources = [ROOT / "tests/editor/WorkspaceDockingTests.cpp"]
    sources += [ROOT / "Core/vendor/imgui" / name for name in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")]
    includes = [ROOT / "Editor/Source", ROOT / "Core/Source", ROOT / "Core/vendor"]
    if Path(compiler).stem.lower() in ("cl", "clang-cl"):
        command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/D_CRT_SECURE_NO_WARNINGS"]
        command += ["/I" + str(path) for path in includes] + list(map(str, sources)) + ["/Fe:" + str(executable)]
    else:
        command = [compiler, "-std=c++20", "-Wall", "-Wextra"]
        command += ["-I" + str(path) for path in includes] + list(map(str, sources)) + ["-o", str(executable)]
    started = time.time_ns()
    subprocess.run(command, cwd=output, env=env, check=True)
    assert executable.stat().st_size and executable.stat().st_mtime_ns >= started - 2_000_000_000
    subprocess.run([str(executable)], cwd=output, check=True, timeout=30)
    print("Verified fresh artifact:", executable)


if __name__ == "__main__":
    main()
