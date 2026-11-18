#!/usr/bin/env python3
"""Export from the release zip, with no source tree (docs/FEATURE_CATALOG_2027.md M6).

Builds the layout of the release zip in a temp folder outside the repository (the Dist editor, its
Resources and DotNet folders, and the prebuilt Dist player under Runtime/Dist, staged by
scripts/ci/StageRuntime.py), so the editor finds no premake5.lua above it and cannot build anything.
Then it exports the Play test project (tests/play) with that editor and runs the packaged game.

Checks:
  * the export succeeds with no source tree, using the shipped Dist player although the project asks
    for Release;
  * the package is the executable, its libraries and one Data folder, with no editor-only libraries,
    debug files or shader sources;
  * a game with scripts ships its own .NET runtime under DotNet/Runtime, and the running player uses
    it: coreclr.dll and System.Private.CoreLib.dll are loaded from there;
  * the executable carries the game's name, version and icon, and the game shows its splash while it loads (Windows);
  * the player started from another working directory finds its files and passes every Play check.

    python tests/runtime/run_standalone_export.py [--editor-config dist] [--keep]

Needs a built Dist editor and Dist Lux-Runtime, the .NET 10 SDK and a GPU with a desktop session.
Windows and Linux; the executable name, version, icon and splash checks are Windows only. Never touches the sample project or your preferences.
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

IS_WINDOWS = os.name == "nt"
PLATFORM = "windows" if IS_WINDOWS else "linux"
EDITOR_FILE = "Editor.exe" if IS_WINDOWS else "Editor"
GAME_FILE = "Standalone_Check.exe" if IS_WINDOWS else "Standalone_Check"
GAME_NAME = "Standalone Check"
GAME_VERSION = "3.1.4"
PLAYER_SECONDS = 600


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def bundle_linux_libraries(install):
    """What the Linux package step in main.yml does: the vendored shared libraries the editor resolves, in lib/
    (its RPATH is $ORIGIN/lib), so the install runs with no LD_LIBRARY_PATH."""
    vendor = [ROOT / "Core/vendor/assimp/bin/linux", ROOT / "Core/vendor/NvidiaAftermath/lib/x64/linux", ROOT / "Core/vendor/VulkanSDK/x86_64/lib"]
    vendor = [path.resolve() for path in vendor if path.is_dir()]
    env = dict(os.environ, LD_LIBRARY_PATH=":".join(str(path) for path in vendor))
    listing = subprocess.run(["ldd", str(install / EDITOR_FILE)], capture_output=True, text=True, env=env, check=True).stdout
    (install / "lib").mkdir(exist_ok=True)
    for line in listing.splitlines():
        parts = line.split()
        if "=>" in parts and parts[parts.index("=>") + 1].startswith("/"):
            library = Path(parts[parts.index("=>") + 1]).resolve()
            if any(library.is_relative_to(path) for path in vendor) and library.name != "libvulkan.so.1":
                shutil.copy2(library, install / "lib" / Path(parts[0]).name)


def stage_install(install, editor_config, runtime_config):
    """The release zip's layout: the editor build output, Resources, DotNet and Runtime/<Config>."""
    editor_out = ROOT / "bin" / f"{editor_config.capitalize()}-{PLATFORM}-x86_64" / "Editor"
    require((editor_out / EDITOR_FILE).is_file(), f"No editor at {editor_out}; build it first")
    shutil.copytree(editor_out, install, ignore=shutil.ignore_patterns("*.pdb"), symlinks=True)
    if not IS_WINDOWS:
        bundle_linux_libraries(install)
    for folder in ("Resources", "DotNet"):
        shutil.copytree(ROOT / "Editor" / folder, install / folder, ignore=shutil.ignore_patterns("Intermediates", "*.pdb"))
    subprocess.run([sys.executable, str(ROOT / "scripts/ci/StageRuntime.py"), "--config", runtime_config.capitalize(), "--platform", PLATFORM,
                    "--dest", str(install / "Runtime" / runtime_config.capitalize())], check=True, stdout=subprocess.DEVNULL)
    # Nothing above the install may look like a source checkout, or export would try to build.
    for parent in [install, *install.parents]:
        require(not (parent / "premake5.lua").exists(), f"{parent} looks like a source tree; stage elsewhere")


def obfuscate_project(project):
    """Turns on Obfuscate Exported Game; the first export makes and keeps the key."""
    path = project / "PlayTest.luxproj"
    text = path.read_text(encoding="utf-8")
    anchor = "  RuntimeExport:" + chr(10)
    require(anchor in text, "could not find the project's export settings")
    path.write_text(text.replace(anchor, "  Obfuscation:" + chr(10) + "    Exports: true" + chr(10) + anchor, 1), encoding="utf-8")


def check_obfuscation(package, executable, project, obfuscated):
    """An obfuscated export holds no readable pack, shader pack or project file; a plain one is unchanged."""
    files = {"AssetPack.lap": b"LZAP", "ShaderPack.lsp": b"LXSP", "Project.luxruntime": None}
    for name, plain_magic in files.items():
        data = (package / "Data/Assets" / name).read_bytes()
        if obfuscated:
            require(data[:4] == b"LXOB", f"{name} is not scrambled")
            for needle in (b"This program cannot be run", b"PlayTest", b"RIFF", b"Scene:", b"ScriptComponent", b"glsl"):
                require(needle not in data, f"the scrambled {name} holds readable '{needle.decode()}'")
        else:
            require(data[:4] != b"LXOB", f"{name} is scrambled in a plain export")
            if plain_magic:
                require(data[:4] == plain_magic, f"{name} no longer starts with {plain_magic!r}")
    exe = executable.read_bytes()
    block = exe.find(b"LUXOBFKEYBLOCK01")
    require(block >= 0 and exe.find(b"LUXOBFKEYBLOCK01", block + 1) < 0, "the player has no single key block")
    patched = exe[block + 16:block + 32] == b"patched---KEY-ok"
    require(patched == obfuscated, "the key block is " + ("unpatched" if obfuscated else "patched") + " in a " + ("obfuscated" if obfuscated else "plain") + " export")
    if obfuscated:
        project_text = (project / "PlayTest.luxproj").read_text(encoding="utf-8")
        key = project_text.split("Key:", 1)[1].split()[0] if "Key:" in project_text else ""
        require(len(key) == 64, "the project did not keep the key it generated")
        require(bytes.fromhex(key) == exe[block + 32:block + 64], "the key in the executable is not the project's key")
        for path in (package / "Data").rglob("*"):
            if path.is_file() and path.suffix in (".lap", ".lsp", ".luxruntime"):
                require(path.read_bytes()[:4] == b"LXOB", f"{path.name} shipped plain")


def brand_project(project):
    """The settings a game maker would set: a name, a version and an icon (the non-square test sprite)."""
    path = project / "PlayTest.luxproj"
    text = path.read_text(encoding="utf-8")
    text = text.replace("    GameName: PlayTest\n", f"    GameName: {GAME_NAME}\n    Version: {GAME_VERSION}\n    IconPath: Sprites/Blink.png\n    SplashPath: Sprites/Blink.png\n")
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
        "LUX_GOLDEN_EXPORT_RUN": "1",  # Export and Run: the editor starts the game when the export finishes
    })
    log = root / "editor.log"
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(install / EDITOR_FILE)], cwd=install, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=timeout)
    text = log.read_text(encoding="utf-8", errors="replace")
    # A Dist editor has no console sink: its engine log is in the user folder.
    for session_log in sorted((user / "logs").glob("*.log")):
        text += session_log.read_text(encoding="utf-8", errors="replace")
    status = golden / "PlayTest.status"
    require(status.is_file() and status.read_text(encoding="utf-8").startswith("OK"),
            f"the staged editor did not finish the export (exit {result.returncode}); see {log}")
    metadata = (golden / "PlayTest.export.yaml").read_text(encoding="utf-8")
    fields = dict(line.split(": ", 1) for line in metadata.splitlines() if ": " in line)
    return (Path(fields["Root"].strip().strip("'\"")), Path(fields["Executable"].strip().strip("'\"")), text)


def check_dist_contents(package, executable):
    """RT9: a Dist export holds no editor code, Tracy, Aftermath or debug console."""
    banned_libraries = ("gfsdk", "llvm_", "tracy", "assimp", "shaderc", "dxcompiler", "imgui")
    for path in package.rglob("*"):
        name = path.name.lower().removeprefix("lib")
        library = path.suffix.lower() == ".dll" or ".so" in path.name.lower()
        require(not name.startswith(banned_libraries) or not library, f"an editor-only library shipped: {path.relative_to(package)}")
    for name in ("Docs", "Templates"):
        require(not (package / "Data/Resources" / name).exists(), f"the editor's {name} folder shipped in Resources")
    data = executable.read_bytes()
    for needle in (b"tracy", b"gfsdk", b"assimp-vc", b"shaderc", b"editorlayer", b"scenehierarchypanel", b"contentbrowserpanel", b"allocconsole"):
        require(needle not in data.lower(), f"the player executable contains '{needle.decode()}'")
    if not IS_WINDOWS:
        return
    # No console window: the PE subsystem is Windows GUI (2), not Console (3).
    pe = int.from_bytes(data[0x3C:0x40], "little")
    require(data[pe:pe + 4] == b"PE\x00\x00", "not a PE executable")
    subsystem = int.from_bytes(data[pe + 24 + 68:pe + 24 + 70], "little")
    require(subsystem == 2, f"the player's subsystem is {subsystem}, not Windows GUI (a console window would show)")


def check_export_and_run(package, executable):
    """Export and Run started the game: it is running and wrote its log beside itself. Stops it."""
    deadline = time.time() + 60
    started = False
    while time.time() < deadline and not started:
        if IS_WINDOWS:
            listing = subprocess.run(["tasklist", "/fi", f"imagename eq {executable.name}", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout
            started = executable.name.lower() in listing.lower()
        else:
            started = subprocess.run(["pgrep", "-f", str(executable)], capture_output=True).returncode == 0
        if not started:
            time.sleep(0.5)
    require(started, "Export and Run did not start the exported game")
    if IS_WINDOWS:
        subprocess.run(["taskkill", "/f", "/im", executable.name], capture_output=True)
    else:
        subprocess.run(["pkill", "-9", "-f", str(executable)], capture_output=True)
    time.sleep(1)
    shutil.rmtree(package / "Data/logs", ignore_errors=True)  # the Play checks below judge their own run's log


def check_package(package, executable, editor_log):
    require("exporting with the Dist player this install ships" in editor_log,
            "the export did not fall back to the shipped Dist player (is the project asking for Release?)")
    require(executable.name == GAME_FILE, f"the executable is {executable.name}, not the game's name")
    run_play.check_export_contents(package, "dist")
    for name in ("Data/Assets/Project.luxruntime", "Data/Assets/AssetPack.lap", "Data/Assets/ShaderPack.lsp", "Data/DotNet/Coral.Managed.dll"):
        require((package / name).is_file(), f"missing from the package: {name}")
    for path in package.rglob("*"):
        require(path.suffix.lower() != ".pdb", f"a debug file shipped: {path.relative_to(package)}")
    # The lean layout: the executable, the libraries it loads, and one Data folder.
    entries = sorted(path.name for path in package.iterdir())
    # Linux also has the launcher and desktop entry, and lib/ when the player loads vendored libraries.
    expected = ["Data", GAME_FILE, "nethost.dll"] if IS_WINDOWS else ["Data", GAME_FILE, "Standalone_Check.desktop", "run-Standalone_Check.sh"] + (["lib"] if (package / "lib").exists() else [])
    require(entries == sorted(expected), f"the package top level is {entries}, expected {sorted(expected)}")
    hostfxr = list((package / "Data/DotNet/Runtime/host/fxr").glob("*/*hostfxr*"))
    corelib = list((package / "Data/DotNet/Runtime/shared/Microsoft.NETCore.App").glob("*/System.Private.CoreLib.dll"))
    require(hostfxr and corelib, "the game has scripts but ships no .NET runtime under DotNet/Runtime")
    return corelib[0].parent


def loaded_modules(pid):
    """Paths of the libraries a process has loaded: PowerShell on Windows, /proc/<pid>/maps on Linux."""
    if not IS_WINDOWS:
        try:
            lines = Path(f"/proc/{pid}/maps").read_text().splitlines()
        except OSError:
            return []
        return [Path(line.split(None, 5)[5]) for line in lines if len(line.split(None, 5)) == 6 and line.split(None, 5)[5].startswith("/")]
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
    if IS_WINDOWS:
        check_splash(process)
    core_names = ("coreclr.dll", "system.private.corelib.dll") if IS_WINDOWS else ("libcoreclr.so", "system.private.corelib.dll")
    try:
        deadline = time.time() + 120
        while time.time() < deadline and process.poll() is None:
            modules = loaded_modules(process.pid)
            if all(any(path.name.lower() == name for path in modules) for name in core_names):
                break
            time.sleep(0.5)
        process.wait(timeout=PLAYER_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        raise RuntimeError("the player never finished its Play checks")
    finally:
        stdout.close()
    require(process.returncode == 0, f"the player exited with {process.returncode}")

    runtime_modules = {path.name.lower(): path for path in modules if path.name.lower() in core_names}
    for name in core_names:
        require(name in runtime_modules, f"the player never loaded {name} (did scripts start?)")
        require(Path(os.path.normcase(runtime_modules[name])).is_relative_to(os.path.normcase(package / "Data/DotNet/Runtime")),
                f"the player loaded {name} from {runtime_modules[name]}, not from its own DotNet/Runtime")
    return finish_player(package, root, started)


def check_splash(process):
    """The splash window (Project Settings > Splash): up while the game loads, gone once it draws."""
    user32 = ctypes.WinDLL("user32")
    user32.FindWindowW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
    user32.FindWindowW.restype = ctypes.c_void_p
    splash_seen = False
    deadline = time.time() + 60
    while time.time() < deadline and process.poll() is None and not splash_seen:
        splash_seen = bool(user32.FindWindowW("LuxGameSplash", None))
        time.sleep(0.02)
    require(splash_seen, "the game never showed its splash window")
    deadline = time.time() + 60
    while time.time() < deadline and process.poll() is None and user32.FindWindowW("LuxGameSplash", None):
        time.sleep(0.05)
    require(process.poll() is not None or not user32.FindWindowW("LuxGameSplash", None), "the splash window never closed")


def finish_player(package, root, started):
    """The player's own log beside the game: bundled .NET reported, every Play check passed, no graphics failures."""
    logs = sorted((package / "Data/logs").glob("LUX_*.log"), key=lambda p: p.stat().st_mtime)
    require(logs and logs[-1].stat().st_mtime >= started - 2, "the player wrote no log beside the game")
    session = logs[-1].name[len("LUX_"):]
    text = ""
    for name in ("LUX_" + session, "APP_" + session):
        if (package / "Data/logs" / name).is_file():
            text += (package / "Data/logs" / name).read_text(encoding="utf-8", errors="replace")
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
    parser.add_argument("--editor-config", default="dist", choices=["debug", "release", "dist"])
    parser.add_argument("--runtime-config", default="dist", choices=["debug", "release", "dist"])
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--obfuscate", action="store_true", help="turn on Obfuscate Exported Game and check nothing readable ships")
    parser.add_argument("--keep", action="store_true", help="keep the staged install and export for inspection")
    args = parser.parse_args()

    root = Path(tempfile.mkdtemp(prefix="lux-standalone-"))
    print("Working in", root, flush=True)
    try:
        install = root / "LuxEngine"
        stage_install(install, args.editor_config, args.runtime_config)
        project = root / "project"
        run_play.write_project(project, None, "release")  # asks for Release; the install ships Dist
        run_play.build_scripts(project, root)
        brand_project(project)
        if args.obfuscate:
            obfuscate_project(project)
        package, executable, editor_log = run_export(install, project, root, args.timeout)
        print("Exported", package, flush=True)
        check_export_and_run(package, executable)
        check_dist_contents(package, executable)
        runtime_folder = check_package(package, executable, editor_log)
        check_obfuscation(package, executable, project, args.obfuscate)
        if IS_WINDOWS:
            check_brand(executable)
        passed = run_player(package, executable, root, runtime_folder)
        print(f"PASS: exported with no source tree, bundled {runtime_folder.name}, {'branded the executable, ' if IS_WINDOWS else ''}{'scrambled' if args.obfuscate else 'plain'} packs, {passed} Play checks passed", flush=True)
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
