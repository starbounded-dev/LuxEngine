#!/usr/bin/env python3
"""Play-mode gameplay checks (docs/HARDENING_PLAN.md Phase 2).

Writes a throw-away Lux project (physics scene, a prefab and its instance, the C# checks in
tests/play/Scripts), builds the scripts with dotnet, then opens the Debug editor with
LUX_PLAY_SELFTEST: it plays the start scene for a fixed number of frames and closes. The scripts
log "PLAYTEST PASS <name>" or "PLAYTEST FAIL <name>: <reason>". Runs under both threading policies;
fails on a missing PASS, any FAIL, a managed exception or a graphics/validation failure.

    python tests/play/run_play.py [--config debug|release] [--policies Single,Multi] [--frames 900]

Needs the .NET 9 SDK and a built editor. Never touches the sample project or your preferences.
"""
import argparse
import math
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/editor"))
from native_checks import require_clean_graphics_log  # noqa: E402

CHECKS = ["FixedStep", "FixedBeforeStep", "FallAndLand", "Character", "SlopeClimb", "SlopeBlock",
          "Contacts", "Triggers", "Spawn", "Destroy", "Gizmos", "PrefabSync"]
NAMESPACE = "PlayTest"
SCENE_HANDLE = 7300000000000000001
PREFAB_HANDLE = 7300000000000000002


def fnv1a(text):
    """Lux::Hash::GenerateFNVHash: 32-bit FNV-1a over the text and a terminating zero byte."""
    value = 2166136261
    for byte in text.encode("utf-8") + bytes(1):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def vec(values):
    return "[" + ", ".join(f"{v:g}" for v in values) + "]"


def entity(eid, name, pos, rot=(0, 0, 0), parent=0, children=(), body="", script=None, fields="", prefab=""):
    lines = [f"  - Entity: {eid}", "    TagComponent:", f"      Tag: {name}", f"    Parent: {parent}"]
    lines.append("    Children:" + ("" if children else "\n      []"))
    lines += [f"      - Handle: {child}" for child in children]
    lines += [prefab.rstrip("\n")] if prefab else []
    lines += ["    TransformComponent:", f"      Position: {vec(pos)}", f"      Rotation: {vec(rot)}", "      Scale: [1, 1, 1]"]
    if script:
        full = f"{NAMESPACE}.{script}"
        lines += ["    ScriptComponent:", "      Enabled: true", f"      ClassName: {full}", f"      ScriptID: {fnv1a(full)}"]
        if fields:
            lines.append(fields.rstrip("\n"))
    if body:
        lines.append(body.rstrip("\n"))
    return "\n".join(lines) + "\n"


def rigid(kind, trigger=False, gravity=True):
    kinds = {"static": 0, "dynamic": 1, "kinematic": 2}
    return (f"    RigidBodyComponent:\n      BodyType: {kinds[kind]}\n      Mass: 1\n"
            f"      DisableGravity: {'false' if gravity else 'true'}\n      IsTrigger: {'true' if trigger else 'false'}\n")


def box(half):
    return f"    BoxColliderComponent:\n      HalfSize: {vec(half)}\n      Offset: [0, 0, 0]\n      Friction: 0.6\n      Restitution: 0\n"


def character():
    return ("    CharacterControllerComponent:\n      SlopeLimitDeg: 45\n      StepOffset: 0.5\n      DisableGravity: false\n"
            "    CapsuleColliderComponent:\n      Radius: 0.3\n      HalfHeight: 0.5\n      Offset: [0, 0, 0]\n")


def prefab_link(entity_id, tracked=False, removed=()):
    text = f"    PrefabComponent:\n      Prefab: {PREFAB_HANDLE}\n      Entity: {entity_id}\n"
    if tracked:
        text += "      Tracked: true\n"
    if removed:
        text += "      Removed:\n" + "".join(f"        - {r}\n" for r in removed)
    return text


def write_project(project, keep=None, runtime_config="dist"):
    assets = project / "Assets"
    (assets / "Scenes").mkdir(parents=True)
    (assets / "Prefabs").mkdir()
    (project / "PlayTest.luxproj").write_text(
        "Project:\n  Name: PlayTest\n  AssetDirectory: Assets\n  AssetRegistry: Assets/AssetRegistry.lzr\n"
        f"  ScriptModulePath: Scripts/Binaries/PlayTest.dll\n  DefaultNamespace: {NAMESPACE}\n"
        f"  StartScene: {SCENE_HANDLE}\n  AutomaticallyReloadAssembly: false\n  AutoSave: false\n"
        f"  RuntimeExport:\n    GameName: PlayTest\n    WindowWidth: 1280\n    WindowHeight: 720\n    Fullscreen: false\n"
        f"    VSync: true\n    TargetConfig: {runtime_config.capitalize()}\n", encoding="utf-8")
    (assets / "AssetRegistry.lzr").write_text(
        f"Assets:\n  - Handle: {SCENE_HANDLE}\n    FilePath: Scenes/PlayTest.luxscene\n    Type: Scene\n"
        f"  - Handle: {PREFAB_HANDLE}\n    FilePath: Prefabs/Crate.lprefab\n    Type: Prefab\n", encoding="utf-8")

    # The prefab: a dynamic crate with three children. OldChild existed when the scene instance was
    # saved, PrefabAdded was added later, Removed is recorded as deleted in the instance.
    crate, old, added, removed = 7310000000000000001, 7310000000000000002, 7310000000000000003, 7310000000000000004
    prefab = "Scene: Crate\nEntities:\n"
    prefab += entity(crate, "Crate", (0, 0, 0), children=(old, added, removed), body=rigid("dynamic") + box((0.5, 0.5, 0.5)),
                     script="CrateScript", prefab=prefab_link(crate))
    for eid, name in ((old, "OldChild"), (added, "PrefabAdded"), (removed, "Removed")):
        prefab += entity(eid, name, (0, 1, 0), parent=crate, prefab=prefab_link(eid))
    (assets / "Prefabs/Crate.lprefab").write_text(prefab, encoding="utf-8")

    ids = iter(range(7320000000000000001, 7320000000000001000))
    e = []
    e.append(entity(next(ids), "Floor", (0, -0.5, 0), body=rigid("static") + box((60, 0.5, 60))))
    e.append(entity(next(ids), "FixedProbe", (-20, 5, -20), body=rigid("dynamic", gravity=False) + box((0.5, 0.5, 0.5)), script="FixedStep"))
    e.append(entity(next(ids), "Faller", (-10, 5, -20), body=rigid("dynamic") + box((0.5, 0.5, 0.5)), script="FallAndLand"))
    e.append(entity(next(ids), "Mover", (0, 3, -20), body=rigid("dynamic", gravity=False) + box((0.5, 0.5, 0.5)), script="ContactMover"))
    e.append(entity(next(ids), "Target", (3, 3, -20), body=rigid("static") + box((0.5, 0.5, 0.5)), script="ContactTarget"))
    e.append(entity(next(ids), "TriggerZone", (10, 3, -20), body=rigid("static", trigger=True) + box((1, 1, 1)), script="TriggerZone"))
    e.append(entity(next(ids), "TriggerRunner", (6, 3, -20), body=rigid("dynamic", gravity=False) + box((0.25, 0.25, 0.25)), script="TriggerRunner"))
    # The character walks +X from x=-20 through a second zone with the same script.
    e.append(entity(next(ids), "Hero", (-20, 1.5, 0), body=character(), script="Hero"))
    e.append(entity(next(ids), "HeroTriggerZone", (-17, 1, 0), body=rigid("static", trigger=True) + box((0.5, 1, 1)), script="TriggerZone"))
    # Ramps: 8 m boards rotated about Z, lower end at the floor near x=-17.
    for name, z, degrees in (("Climber30", 10, 30), ("Climber60", 20, 60)):
        angle = math.radians(degrees)
        half_length, half_thick = 4.0, 0.2
        cx = -17 + half_length * math.cos(angle)
        cy = half_length * math.sin(angle) - half_thick * math.cos(angle)
        e.append(entity(next(ids), f"Ramp{degrees}", (cx, cy, z), rot=(0, 0, angle), body=rigid("static") + box((half_length, half_thick, 2))))
        e.append(entity(next(ids), name, (-20, 1.5, z), body=character(), script="Climber"))
    spawner_fields = (f"      StoredFields:\n        - ID: {fnv1a(f'{NAMESPACE}.Spawner.Crate')}\n          Name: Crate\n"
                      f"          Type: Prefab\n          Data: {PREFAB_HANDLE}\n")
    e.append(entity(next(ids), "Spawner", (20, 0, 10), script="Spawner", fields=spawner_fields))
    e.append(entity(next(ids), "SelfDestruct", (0, 1, 10), script="SelfDestruct"))
    e.append(entity(next(ids), "DestroyWatcher", (0, 1, 12), script="DestroyWatcher"))
    e.append(entity(next(ids), "GizmoDrawer", (5, 1, 10), script="GizmoDrawer"))
    e.append(entity(next(ids), "PrefabSyncCheck", (10, 1, 10), script="PrefabSyncCheck"))
    # Ends the exported game once every check reported; ignored in the editor.
    e.append(entity(next(ids), "Finisher", (0, 1, 30), script="Finisher"))
    # The instance, as saved before the prefab gained PrefabAdded.
    instance, instance_old, instance_own = next(ids), next(ids), next(ids)
    e.append(entity(instance, "Crate", (30, 0.5, 0), children=(instance_old, instance_own), body=rigid("dynamic") + box((0.5, 0.5, 0.5)),
                    script="CrateScript", prefab=prefab_link(crate, tracked=True, removed=(removed,))))
    e.append(entity(instance_old, "OldChild", (0, 1, 0), parent=instance, prefab=prefab_link(old)))
    e.append(entity(instance_own, "InstanceAdded", (0, 1, 0), parent=instance))
    if keep:
        # Bisecting: only the named top-level entities (children follow their parent), and the floor.
        def field(text, key):
            return text.split(key, 1)[1].split("\n", 1)[0].strip()
        kept, parents = [], set()
        for text in e:
            if field(text, "Tag: ") in keep | {"Floor", "Finisher"} or field(text, "Parent: ") in parents:
                kept.append(text)
                parents.add(field(text, "Entity: "))
        e = kept
    (assets / "Scenes/PlayTest.luxscene").write_text("Scene: PlayTest\nEntities:\n" + "".join(e), encoding="utf-8")


def build_scripts(project, work):
    """Builds tests/play/Scripts into the project's ScriptModulePath, as the editor would load it."""
    build = work / "scripts"
    build.mkdir()
    (build / "NuGet.Config").write_text('<configuration><packageSources><clear /></packageSources></configuration>')
    output = project / "Assets/Scripts/Binaries"
    (build / "PlayTest.csproj").write_text(f'''<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net9.0</TargetFramework><OutputType>Library</OutputType><AssemblyName>PlayTest</AssemblyName>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks><EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <Nullable>disable</Nullable><OutDir>{escape(str(output))}/</OutDir>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="{escape(str(ROOT / 'tests/play/Scripts'))}/**/*.cs" />
    <Reference Include="ScriptCore"><HintPath>{escape(str(ROOT / 'Editor/Resources/Scripts/ScriptCore.dll'))}</HintPath><Private>false</Private></Reference>
    <Reference Include="Coral.Managed"><HintPath>{escape(str(ROOT / 'Editor/DotNet/Coral.Managed.dll'))}</HintPath><Private>false</Private></Reference>
  </ItemGroup>
</Project>''')
    subprocess.run(["dotnet", "build", str(build / "PlayTest.csproj"), "-c", "Debug", "--nologo", "--verbosity", "quiet"], cwd=build, check=True)
    if not (output / "PlayTest.dll").is_file():
        raise RuntimeError(f"dotnet produced no {output / 'PlayTest.dll'}")


def check_log(text):
    passed = {line.split("PLAYTEST PASS ", 1)[1].strip() for line in text.splitlines() if "PLAYTEST PASS " in line}
    failures = [line.split("PLAYTEST FAIL ", 1)[1].strip() for line in text.splitlines() if "PLAYTEST FAIL " in line]
    exceptions = [line.strip() for line in text.splitlines() if "Exception" in line and ("Scripting" in line or "at PlayTest." in line)]
    missing = [name for name in CHECKS if name not in passed]
    return passed, failures, exceptions, missing


def run_editor(editor, project, root, policy, args):
    """Plays the project in the editor through LUX_PLAY_SELFTEST. Returns (exit code, log path, played)."""
    user = root / "user"
    (user / "UserPreferences.yaml").write_text(
        f"UserPrefs:\n  ShowSplashScreen: false\n  StartupProject: {(project / 'PlayTest.luxproj').as_posix()}\n  RecentProjects: []\n", encoding="utf-8")
    env = dict(os.environ, LUX_USER_DATA_DIR=str(user), LUX_PLAY_SELFTEST=str(args.frames),
               LUX_THREADING_POLICY=policy, LUX_NO_WATCHDOG="1")
    log = root / "editor.log"
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(editor)], cwd=ROOT / "Editor", env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=args.timeout)
    text = log.read_text(encoding="utf-8", errors="replace")
    return result.returncode, log, "[PlayTest] playing" in text and "[PlayTest] done" in text


def run_exported(project, root, policy, args):
    """Exports the project through the editor's normal pipeline (golden_run --export-dir), then runs the
    packaged player. Finisher calls Application.Quit once the checks reported. Returns (exit code, log, played)."""
    user = root / "user"
    (user / "UserPreferences.yaml").write_text("UserPrefs:\n  ShowSplashScreen: false\n  RecentProjects: []\n", encoding="utf-8")
    label = f"play-export-{time.time_ns()}"
    export_dir = root / "export"
    command = [sys.executable, str(ROOT / "tests/rendering/golden_run.py"), "--config", args.config,
               "--project", str(project / "PlayTest.luxproj"), "--label", label, "--scenes", "PlayTest",
               "--frame", "2", "--perf-frames", "1", "--perf-warmup", "0", "--timeout", str(args.timeout),
               "--export-dir", str(export_dir)]
    subprocess.run(command, cwd=ROOT, env=dict(os.environ, LUX_USER_DATA_DIR=str(user)), check=True)
    metadata = (ROOT / "bin/golden" / label / "PlayTest.export.yaml").read_text(encoding="utf-8")
    fields = dict(line.split(": ", 1) for line in metadata.splitlines() if ": " in line)
    package, executable = Path(fields["Root"].strip().strip("'\"")), Path(fields["Executable"].strip().strip("'\""))
    env = {k: v for k, v in os.environ.items() if not k.startswith("LUX_GOLDEN_")}
    env.update(LUX_THREADING_POLICY=policy, LUX_NO_WATCHDOG="1")
    started = time.time()
    with (root / "player.stdout.log").open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(executable), "--project=" + str(package)], cwd=package, env=env,
                                stdout=stream, stderr=subprocess.STDOUT, timeout=args.timeout)
    # Shipped games log next to the game, not to stdout (Dist has no console sink): engine lines in
    # LUX_<session>.log, script Log.* lines in APP_<session>.log. Both, engine first.
    log = root / "player.log"
    text = ""
    engine_logs = sorted((package / "logs").glob("LUX_*.log"), key=lambda p: p.stat().st_mtime)
    if engine_logs and engine_logs[-1].stat().st_mtime >= started - 2:
        session = engine_logs[-1].name[len("LUX_"):]
        for name in ("LUX_" + session, "APP_" + session):
            if (package / "logs" / name).is_file():
                text += (package / "logs" / name).read_text(encoding="utf-8", errors="replace")
    log.write_text(text, encoding="utf-8")
    return result.returncode, log, "PLAYTEST END" in text


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="debug", choices=["debug", "release"])
    parser.add_argument("--policies", default="Single,Multi")
    parser.add_argument("--frames", type=int, default=0, help="frames of Play (default 900 Debug, 6000 Release); the slowest check ends at 7 s of physics")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--only", default="", help="bisecting: keep only these top-level entities (comma list)")
    parser.add_argument("--export", action="store_true", help="export the project and run the checks in the packaged player")
    parser.add_argument("--runtime-config", default="dist", choices=["debug", "release", "dist"], help="player configuration for --export")
    args = parser.parse_args()
    # shortcut: a frame budget scales with frame rate (Release runs ~500 fps); count Play time in the editor hook if a faster GPU outruns 6000.
    args.frames = args.frames or (6000 if args.config == "release" else 900)

    platform = "windows" if os.name == "nt" else "linux"
    editor = ROOT / f"bin/{args.config.capitalize()}-{platform}-x86_64/Editor" / ("Editor.exe" if os.name == "nt" else "Editor")
    editor = Path(os.environ.get("LUX_TEST_EDITOR", editor))
    if not editor.is_file():
        raise RuntimeError(f"No editor at {editor}; build it first")

    output = ROOT / "bin/play-tests" / str(time.time_ns())
    ok = True
    for policy in [p for p in args.policies.split(",") if p]:
        root = output / policy
        project, user = root / "project", root / "user"
        user.mkdir(parents=True)
        write_project(project, set(filter(None, args.only.split(","))), args.runtime_config)
        build_scripts(project, root)
        user.mkdir(parents=True, exist_ok=True)
        if args.export:
            code, log, played = run_exported(project, root, policy, args)
        else:
            code, log, played = run_editor(editor, project, root, policy, args)

        passed, failures, exceptions, missing = check_log(log.read_text(encoding="utf-8", errors="replace"))
        problems = []
        if code:
            problems.append(f"{'player' if args.export else 'editor'} exit code {code}")
        if not played:
            problems.append("the player never reported PLAYTEST END" if args.export else "the editor never played the scene (no [PlayTest] playing/done lines)")
        problems += [f"FAIL {f}" for f in failures]
        problems += [f"exception: {x}" for x in exceptions[:5]]
        problems += [f"no result from {name}" for name in missing]
        try:
            require_clean_graphics_log(log)
        except RuntimeError as error:
            problems.append(str(error))

        print(f"{policy} ({'exported ' + args.runtime_config + ' player' if args.export else args.config + ' editor'}): {len(passed)}/{len(CHECKS)} passed", flush=True)
        for name in CHECKS:
            print(f"  {'PASS' if name in passed else 'FAIL'}  {name}")
        for problem in problems:
            print(f"  ! {problem}")
        ok = ok and not problems
    print("Evidence:", output)
    print("PASS: every Play check passed under every policy" if ok else "FAIL: see above")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
