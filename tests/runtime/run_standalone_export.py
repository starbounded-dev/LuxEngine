#!/usr/bin/env python3
"""Export from the release zip, with no source tree (docs/FEATURE_CATALOG_2027.md M6).

Builds the layout of the release zip in a temp folder outside the repository (the Release editor, its
Resources and DotNet folders, and the prebuilt Dist player under Runtime/Dist, staged by
scripts/ci/StageRuntime.py), so the editor finds no premake5.lua above it and cannot build anything.
Then it exports the Play test project (tests/play) with that editor and runs the packaged game.

Checks:
  * the export succeeds with no source tree, using the shipped Dist player although the project asks
    for Release;
  * the package has no editor-only libraries, debug files or shader sources;
  * a game with scripts ships its own .NET runtime under DotNet/Runtime, and the running player uses
    it: coreclr.dll and System.Private.CoreLib.dll are loaded from there;
  * the executable carries the game's name, version and icon (Windows);
  * the player started from another working directory finds its files and passes every Play check.

    python tests/runtime/run_standalone_export.py [--editor-config release] [--keep]

Needs a built Release editor and Dist Lux-Runtime, the .NET 10 SDK and a GPU with a desktop session.
Windows only for now. Never touches the sample project or your preferences.
"""
import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/play"))
sys.path.insert(0, str(ROOT / "tests/editor"))
import run_play  # noqa: E402
from native_checks import require_clean_graphics_log  # noqa: E402

GAME_NAME = "Standalone Check"
GAME_VERSION = "3.1.4"
PLAYER_SECONDS = 600


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def stage_install(install, editor_config, runtime_config):
    """The release zip's layout: the editor build output, Resources, DotNet and Runtime/<Config>."""
    editor_out = ROOT / "bin" / f"{editor_config.capitalize()}-windows-x86_64" / "Editor"
    require((editor_out / "Editor.exe").is_file(), f"No editor at {editor_out}; build it first")
    shutil.copytree(editor_out, install, ignore=shutil.ignore_patterns("*.pdb"))
    for folder in ("Resources", "DotNet"):
        shutil.copytree(ROOT / "Editor" / folder, install / folder, ignore=shutil.ignore_patterns("Intermediates", "*.pdb"))
    subprocess.run([sys.executable, str(ROOT / "scripts/ci/StageRuntime.py"), "--config", runtime_config.capitalize(),
                    "--dest", str(install / "Runtime" / runtime_config.capitalize())], check=True, stdout=subprocess.DEVNULL)
    # Nothing above the install may look like a source checkout, or export would try to build.
    for parent in [install, *install.parents]:
        require(not (parent / "premake5.lua").exists(), f"{parent} looks like a source tree; stage elsewhere")


def brand_project(project):
    """The settings a game maker would set: a name, a version and an icon (the non-square test sprite)."""
    path = project / "PlayTest.luxproj"
    text = path.read_text(encoding="utf-8")
    text = text.replace("    GameName: PlayTest\n", f"    GameName: {GAME_NAME}\n    Version: {GAME_VERSION}\n    IconPath: Sprites/Blink.png\n")
    require(GAME_VERSION in text, "could not brand the test project")
    path.write_text(text, encoding="utf-8")


def run_export(install, project, root, timeout):
    """Exports through the staged editor with the golden capture hooks (what golden_run.py drives)."""
    user = root / "user"
    user.mkdir(parents=True, exist_ok=True)
    (user / "UserPreferences.yaml").write_text("UserPrefs:\n  ShowSplashScreen: false\n  RecentProjects: []\n", encoding="utf-8")
    golden = root / "golden"
    golden.mkdir()
    export_dir = root / "export"
    env = dict(os.environ)
    env.update({
        "LUX_USER_DATA_DIR": str(user), "LUX_NO_WATCHDOG": "1", "LUX_SKIP_BUILD": "1",
        "LUX_GOLDEN_DIR": str(golden), "LUX_GOLDEN_SCENE": "Scenes/PlayTest.luxscene", "LUX_GOLDEN_NAME": "PlayTest",
        "LUX_GOLDEN_PROJECT": str(project / "PlayTest.luxproj"), "LUX_GOLDEN_SIZE": "640x360",
        "LUX_GOLDEN_FRAME": "2", "LUX_GOLDEN_PERF_FRAMES": "1", "LUX_GOLDEN_PERF_WARMUP": "0",
        "LUX_GOLDEN_OPTIONS": "", "LUX_GOLDEN_SELFTEST": "0", "LUX_GOLDEN_PLAY": "0",
        "LUX_GOLDEN_EXPORT_DIR": str(export_dir),
    })
    log = root / "editor.log"
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(install / "Editor.exe")], cwd=install, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=timeout)
    text = log.read_text(encoding="utf-8", errors="replace")
    status = golden / "PlayTest.status"
    require(status.is_file() and status.read_text(encoding="utf-8").startswith("OK"),
            f"the staged editor did not finish the export (exit {result.returncode}); see {log}")
    metadata = (golden / "PlayTest.export.yaml").read_text(encoding="utf-8")
    fields = dict(line.split(": ", 1) for line in metadata.splitlines() if ": " in line)
    return (Path(fields["Root"].strip().strip("'\"")), Path(fields["Executable"].strip().strip("'\"")), text)


def check_package(package, executable, editor_log):
    require("exporting with the Dist player this install ships" in editor_log,
            "the export did not fall back to the shipped Dist player (is the project asking for Release?)")
    require(executable.name == "Standalone_Check.exe", f"the executable is {executable.name}, not the game's name")
    run_play.check_export_contents(package, "dist")
    for name in ("Assets/Project.luxruntime", "Assets/AssetPack.lap", "Assets/ShaderPack.lsp", "DotNet/Coral.Managed.dll"):
        require((package / name).is_file(), f"missing from the package: {name}")
    for path in package.rglob("*"):
        require(path.suffix.lower() != ".pdb", f"a debug file shipped: {path.relative_to(package)}")
    hostfxr = list((package / "DotNet/Runtime/host/fxr").glob("*/hostfxr.dll"))
    corelib = list((package / "DotNet/Runtime/shared/Microsoft.NETCore.App").glob("*/System.Private.CoreLib.dll"))
    require(hostfxr and corelib, "the game has scripts but ships no .NET runtime under DotNet/Runtime")
    return corelib[0].parent


def loaded_modules(pid):
    """Paths of the DLLs a process has loaded, through PowerShell (no extra Python packages)."""
    script = f"(Get-Process -Id {pid}).Modules | ForEach-Object {{ $_.FileName }}"
    output = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True)
    return [Path(line.strip()) for line in output.stdout.splitlines() if line.strip()]


def check_brand(executable):
    info = subprocess.run(["powershell", "-NoProfile", "-Command",
                           f"$v = (Get-Item -LiteralPath '{executable}').VersionInfo; "
                           "ConvertTo-Json @{Name=$v.ProductName; Description=$v.FileDescription; File=$v.FileVersion; Product=$v.ProductVersion; Original=$v.OriginalFilename}"],
                          capture_output=True, text=True, check=True)
    fields = json.loads(info.stdout)
    require(fields["Name"] == GAME_NAME and fields["Description"] == GAME_NAME, f"the executable's product name is {fields['Name']!r}")
    require(fields["File"] == GAME_VERSION and fields["Product"] == GAME_VERSION, f"the executable's version is {fields['File']!r}")
    require(fields["Original"] == executable.name, f"the original filename is {fields['Original']!r}")

    # The icon: the group resource lists the sizes built from the 32 x 16 sprite (16, 32 and 48; never upscaled further).
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LoadLibraryExW.restype = ctypes.c_void_p
    kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
    kernel.FindResourceW.restype = ctypes.c_void_p
    kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    kernel.LoadResource.restype = ctypes.c_void_p
    kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.LockResource.argtypes = [ctypes.c_void_p]
    kernel.SizeofResource.restype = ctypes.c_uint32
    kernel.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
    module = kernel.LoadLibraryExW(str(executable), None, 0x2)  # LOAD_LIBRARY_AS_DATAFILE
    require(module, "could not open the executable's resources")
    try:
        icon_type = 3
        # Named GLFW_ICON in Lux-Runtime.rc: the window class icon the task bar shows.
        group = kernel.FindResourceW(module, ctypes.cast(ctypes.c_wchar_p("GLFW_ICON"), ctypes.c_void_p), 14)
        require(group, "the executable has no GLFW_ICON group resource")
        data = ctypes.string_at(kernel.LockResource(kernel.LoadResource(module, group)), kernel.SizeofResource(module, group))
        count = int.from_bytes(data[4:6], "little")
        sizes, ids = [], []
        for index in range(count):
            entry = data[6 + index * 14:6 + (index + 1) * 14]
            sizes.append(entry[0] or 256)
            ids.append(int.from_bytes(entry[12:14], "little"))
        require(sorted(sizes) == [16, 32, 48], f"the icon has sizes {sorted(sizes)}, expected [16, 32, 48]")
        for icon_id, size in zip(ids, sizes):
            icon = kernel.FindResourceW(module, icon_id, icon_type)
            require(icon, f"icon image {icon_id} is missing")
            payload = ctypes.string_at(kernel.LockResource(kernel.LoadResource(module, icon)), kernel.SizeofResource(module, icon))
            require(int.from_bytes(payload[4:8], "little") == size and int.from_bytes(payload[14:16], "little") == 32,
                    f"icon image {icon_id} is not a {size}px 32-bit bitmap")
        # The sprite is red on the left half and green on the right half, centred on a transparent square:
        # the 32px image's pixel row 16 (bottom-up row 15) starts transparent, then red, then green.
        index = sizes.index(32)
        payload = ctypes.string_at(kernel.LockResource(kernel.LoadResource(module, kernel.FindResourceW(module, ids[index], icon_type))),
                                   kernel.SizeofResource(module, kernel.FindResourceW(module, ids[index], icon_type)))
        pixels = payload[40:40 + 32 * 32 * 4]
        row = lambda y: pixels[(31 - y) * 128:(32 - y) * 128]
        bgra = lambda y, x: tuple(row(y)[x * 4:x * 4 + 4])
        require(bgra(0, 0)[3] == 0, "the top of the centred icon is not transparent")
        require(bgra(16, 4) == (0, 0, 255, 255), f"left half is not red: {bgra(16, 4)}")
        require(bgra(16, 28) == (0, 255, 0, 255), f"right half is not green: {bgra(16, 28)}")
    finally:
        kernel.FreeLibrary(module)


def run_player(package, executable, root, bundled_runtime):
    """Starts the player from another working directory, samples its modules, and waits for the checks."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("LUX_GOLDEN_")}
    env.update(LUX_THREADING_POLICY="Multi", LUX_NO_WATCHDOG="1")
    elsewhere = root / "elsewhere"
    elsewhere.mkdir()
    started = time.time()
    stdout = (root / "player.stdout.log").open("w", encoding="utf-8")
    process = subprocess.Popen([str(executable)], cwd=elsewhere, env=env, stdout=stdout, stderr=subprocess.STDOUT)
    modules = []
    try:
        deadline = time.time() + 120
        while time.time() < deadline and process.poll() is None:
            modules = loaded_modules(process.pid)
            if any(path.name.lower() == "coreclr.dll" for path in modules) and any(path.name.lower() == "system.private.corelib.dll" for path in modules):
                break
            time.sleep(0.5)
        process.wait(timeout=PLAYER_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        raise RuntimeError("the player never finished its Play checks")
    finally:
        stdout.close()
    require(process.returncode == 0, f"the player exited with {process.returncode}")

    runtime_modules = {path.name.lower(): path for path in modules if path.name.lower() in ("coreclr.dll", "system.private.corelib.dll", "hostfxr.dll")}
    for name in ("coreclr.dll", "system.private.corelib.dll"):
        require(name in runtime_modules, f"the player never loaded {name} (did scripts start?)")
        require(Path(os.path.normcase(runtime_modules[name])).is_relative_to(os.path.normcase(package / "DotNet/Runtime")),
                f"the player loaded {name} from {runtime_modules[name]}, not from its own DotNet/Runtime")

    logs = sorted((package / "logs").glob("LUX_*.log"), key=lambda p: p.stat().st_mtime)
    require(logs and logs[-1].stat().st_mtime >= started - 2, "the player wrote no log beside the game")
    session = logs[-1].name[len("LUX_"):]
    text = ""
    for name in ("LUX_" + session, "APP_" + session):
        if (package / "logs" / name).is_file():
            text += (package / "logs" / name).read_text(encoding="utf-8", errors="replace")
    log = root / "player.log"
    log.write_text(text, encoding="utf-8")
    require("Using the .NET runtime bundled with the game" in text, "the player did not report using its bundled .NET runtime")
    passed, failures, exceptions, missing = run_play.check_log(text)
    problems = [f"FAIL {f}" for f in failures] + [f"exception: {x}" for x in exceptions[:5]] + [f"no result from {n}" for n in missing]
    require("PLAYTEST END" in text, "the player never reported PLAYTEST END")
    require(not problems, "; ".join(problems))
    require_clean_graphics_log(log)
    return len(passed)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor-config", default="release", choices=["debug", "release"])
    parser.add_argument("--runtime-config", default="dist", choices=["debug", "release", "dist"])
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--keep", action="store_true", help="keep the staged install and export for inspection")
    args = parser.parse_args()
    if os.name != "nt":
        raise SystemExit("Windows only for now: Linux export-from-zip is verified by hand (docs/FEATURE_CATALOG_2027.md M12)")

    root = Path(tempfile.mkdtemp(prefix="lux-standalone-"))
    print("Working in", root, flush=True)
    try:
        install = root / "LuxEngine"
        stage_install(install, args.editor_config, args.runtime_config)
        project = root / "project"
        run_play.write_project(project, None, "release")  # asks for Release; the install ships Dist
        run_play.build_scripts(project, root)
        brand_project(project)
        package, executable, editor_log = run_export(install, project, root, args.timeout)
        print("Exported", package, flush=True)
        runtime_folder = check_package(package, executable, editor_log)
        check_brand(executable)
        passed = run_player(package, executable, root, runtime_folder)
        print(f"PASS: exported with no source tree, bundled {runtime_folder.name}, branded the executable, {passed} Play checks passed", flush=True)
    except Exception:
        print("Evidence kept in", root, flush=True)
        raise
    else:
        if args.keep:
            print("Evidence:", root)
        else:
            shutil.rmtree(root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
