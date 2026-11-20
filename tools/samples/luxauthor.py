"""Writes a LuxEngine project the way the editor saves one (docs/SAMPLE_GAMES_PLAN.md).

The sample games are built by the generators next to this file with the same files the editor writes: a .luxproj, scenes, materials, an
asset registry, a script project. Nothing here reaches into the engine's source tree; the default meshes come from the editor's own
Resources/Meshes/Default folder, which every release zip holds. Run a generator from the repository root; it rewrites the sample folder.
"""
import hashlib
import pathlib
import shutil
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
EDITOR = ROOT / "Editor"
DEFAULT_MESHES = EDITOR / "Resources" / "Meshes" / "Default"
SCRIPT_PROPS = EDITOR / "LuxSampleProject" / "Assets" / "Scripts" / "Directory.Build.props"


def fnv1a(text):
    """Lux::Hash::GenerateFNVHash: 32-bit FNV-1a over the text and a terminating zero byte (a script's ScriptID)."""
    value = 2166136261
    for byte in text.encode("utf-8") + bytes(1):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def handle_for(path):
    """A stable asset handle for a project path: the same path always gets the same handle."""
    value = int.from_bytes(hashlib.sha1(path.encode("utf-8")).digest()[:8], "big") & 0x7FFFFFFFFFFFFFFF
    return value or 1


def vec(values):
    return "[" + ", ".join(f"{v:g}" for v in values) + "]"


def write_png(path, width, height, rows):
    """rows: height lists of width (r, g, b, a) tuples."""
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + b"".join(bytes(pixel) for pixel in row) for row in rows)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


class Ids:
    def __init__(self, base):
        self.next = base

    def __call__(self):
        self.next += 1
        return self.next


def entity(eid, name, pos=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1), parent=0, children=(), body="", script=None, namespace=None, fields="", tag=""):
    lines = [f"  - Entity: {eid}", "    TagComponent:", f"      Tag: {name}"]
    if tag:
        lines.append(f"      UserTag: {tag}")
    lines.append(f"    Parent: {parent}")
    lines.append("    Children:" + ("" if children else "\n      []"))
    lines += [f"      - Handle: {child}" for child in children]
    lines += ["    TransformComponent:", f"      Position: {vec(pos)}", f"      Rotation: {vec(rot)}", f"      Scale: {vec(scale)}"]
    if script:
        full = f"{namespace}.{script}"
        lines += ["    ScriptComponent:", "      Enabled: true", f"      ClassName: {full}", f"      ScriptID: {fnv1a(full)}"]
        if fields:
            lines.append(fields.rstrip("\n"))
    if body:
        lines.append(body.rstrip("\n"))
    return "\n".join(lines) + "\n"


# ---- components -----------------------------------------------------------------------------------------------------------

def mesh_component(mesh, material):
    return f"    StaticMeshComponent:\n      AssetID: Meshes/Source/Default/{mesh}.gltf\n      MaterialTable:\n        0: {material}\n      Visible: true\n"


def rigid(kind="static", trigger=False, gravity=True):
    kinds = {"static": 0, "dynamic": 1, "kinematic": 2}
    return (f"    RigidBodyComponent:\n      BodyType: {kinds[kind]}\n      LayerID: 0\n      Mass: 1\n"
            f"      DisableGravity: {'false' if gravity else 'true'}\n      IsTrigger: {'true' if trigger else 'false'}\n")


def box_collider(half, friction=0.6):
    return f"    BoxColliderComponent:\n      HalfSize: {vec(half)}\n      Offset: [0, 0, 0]\n      Friction: {friction:g}\n      Restitution: 0\n"


def sphere_collider(radius):
    return f"    SphereColliderComponent:\n      Radius: {radius:g}\n      Offset: [0, 0, 0]\n      Friction: 0.6\n      Restitution: 0\n"


def character_controller(radius, half_height):
    return ("    CharacterControllerComponent:\n      SlopeLimitDeg: 50\n      StepOffset: 0.4\n      DisableGravity: false\n"
            f"    CapsuleColliderComponent:\n      Radius: {radius:g}\n      HalfHeight: {half_height:g}\n      Offset: [0, 0, 0]\n")


def camera(fov=60, near=0.1, far=300, primary=True):
    return (f"    CameraComponent:\n      Camera:\n        ProjectionType: 0\n        PerspectiveFOV: {fov:g}\n        PerspectiveNear: {near:g}\n"
            f"        PerspectiveFar: {far:g}\n        OrthographicSize: 10\n        OrthographicNear: -1\n        OrthographicFar: 1\n"
            f"      Primary: {'true' if primary else 'false'}\n      FixedAspectRatio: false\n")


def sun(intensity=6, radiance=(1, 0.95, 0.85)):
    return (f"    DirectionalLightComponent:\n      Intensity: {intensity:g}\n      Radiance: {vec(radiance)}\n      Unit: 0\n      ColorTemperature: 6500\n"
            "      UseColorTemperature: false\n      CastShadows: true\n      SoftShadows: true\n      LightSize: 0.5\n      ShadowAmount: 1\n"
            "      ShadowDistance: 0\n      ShadowResolutionTier: 2\n")


def sky(intensity=0.5):
    return (f"    SkyLightComponent:\n      EnvironmentMap: 0\n      Intensity: {intensity:g}\n      Lod: 0\n      DynamicSky: true\n"
            "      TurbidityAzimuthInclination: [2, 0, 0.4]\n")


def canvas(reference=(1920, 1080)):
    return f"    UICanvasComponent:\n      ScaleMode: 1\n      ReferenceResolution: {vec(reference)}\n"


def ui_text(text, size=48, color=(1, 1, 1, 1), anchor_min=(0, 0), anchor_max=(1, 1), offset_min=(0, 0), offset_max=(0, 0), align_h=0, align_v=1):
    escaped = text.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n")
    return (f"    UIElementComponent:\n      AnchorMin: {vec(anchor_min)}\n      AnchorMax: {vec(anchor_max)}\n      OffsetMin: {vec(offset_min)}\n      OffsetMax: {vec(offset_max)}\n"
            f"    UITextComponent:\n      Text: \"{escaped}\"\n      Font: 0\n      Color: {vec(color)}\n      Size: {size:g}\n      AlignH: {align_h}\n      AlignV: {align_v}\n")


def particles(**settings):
    """A ParticleSystemComponent; every keyword is a key of the component or of its settings."""
    top = {"Texture": 0, "Seed": 1, "PreviewInEditor": "false", "PlayOnAwake": "true"}
    text = "    ParticleSystemComponent:\n"
    own = {"Texture", "Seed", "PreviewInEditor", "PlayOnAwake", "SortingLayer", "OrderInLayer", "Enabled"}
    merged = dict(top)
    merged.update({k: v for k, v in settings.items() if k in own})
    for key, value in merged.items():
        text += f"      {key}: {value}\n"
    for key, value in settings.items():
        if key in own:
            continue
        if isinstance(value, (list, tuple)) and value and isinstance(value[0], (list, tuple)):
            text += f"      {key}:\n" + "".join(f"        - {vec(item)}\n" for item in value)
        elif isinstance(value, (list, tuple)):
            text += f"      {key}: {vec(value)}\n"
        else:
            text += f"      {key}: {value}\n"
    return text


# ---- the project ----------------------------------------------------------------------------------------------------------

class Project:
    def __init__(self, folder, name, namespace):
        self.folder = pathlib.Path(folder)
        self.name = name
        self.namespace = namespace
        self.assets = {}  # path under Assets -> asset type name
        shutil.rmtree(self.folder / "Assets" / "Scenes", ignore_errors=True)
        shutil.rmtree(self.folder / "Assets" / "Materials", ignore_errors=True)
        shutil.rmtree(self.folder / "Assets" / "Meshes", ignore_errors=True)
        shutil.rmtree(self.folder / "Assets" / "Sprites", ignore_errors=True)
        (self.folder / "Assets").mkdir(parents=True, exist_ok=True)

    @property
    def assets_dir(self):
        return self.folder / "Assets"

    def write_asset(self, relative, text, kind):
        path = self.assets_dir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if kind in ("Scene", "Material"):
            text = "FormatVersion: 1\n" + text  # docs/PROJECT_FORMAT_PLAN.md: a shipped sample must not need an upgrade
        path.write_text(text, encoding="utf-8", newline="\n")
        self.assets[relative] = kind

    def add_binary_asset(self, relative, kind):
        self.assets[relative] = kind

    def material(self, name, color, roughness=0.8, metalness=0.0, emission=0.0):
        relative = f"Materials/{name}.lmat"
        text = ("Material:\n  Transparent: false\n"
                f"  AlbedoColor: {vec(color)}\n  Emission: {emission:g}\n  UseNormalMap: false\n  Metalness: {metalness:g}\n  Roughness: {roughness:g}\n"
                "  AlbedoMap: 0\n  NormalMap: 0\n  MetalnessMap: 0\n  RoughnessMap: 0\n  UVTiling: [1, 1]\n")
        self.write_asset(relative, text, "Material")
        return relative

    def default_mesh(self, name):
        target = self.assets_dir / "Meshes" / "Source" / "Default"
        target.mkdir(parents=True, exist_ok=True)
        for suffix in (".gltf", ".bin"):
            shutil.copy2(DEFAULT_MESHES / (name + suffix), target / (name + suffix))
        self.assets[f"Meshes/Source/Default/{name}.gltf"] = "MeshSource"

    def scene(self, name, entities):
        relative = f"Scenes/{name}.luxscene"
        self.write_asset(relative, f"Scene: {name}\nEntities:\n" + "".join(entities), "Scene")
        return relative

    def script_project(self, sources):
        """The script project, with the editor's and engine's assemblies found relative to the zip layout (Samples/<Name>/Assets/Scripts)."""
        scripts = self.assets_dir / "Scripts"
        scripts.mkdir(parents=True, exist_ok=True)
        compile_items = "\n".join(f'    <Compile Include="Source\\{name}" />' for name in sources)
        (scripts / f"{self.name}.csproj").write_text(f"""<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <OutputType>Library</OutputType>
    <AssemblyName>{self.name}</AssemblyName>
    <RootNamespace>{self.namespace}</RootNamespace>
    <Configurations>Debug;Release;Dist</Configurations>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <Nullable>disable</Nullable>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <EnableDynamicLoading>true</EnableDynamicLoading>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    <OutputPath>Binaries\\</OutputPath>
    <IntermediateOutputPath>..\\..\\Cache\\ScriptBuild\\$(Configuration)\\</IntermediateOutputPath>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)' != 'Debug'">
    <Optimize>true</Optimize>
  </PropertyGroup>
  <ItemGroup>
{compile_items}
  </ItemGroup>
  <ItemGroup>
    <Reference Include="ScriptCore">
      <HintPath>..\\..\\..\\..\\Resources\\Scripts\\ScriptCore.dll</HintPath>
      <Private>false</Private>
    </Reference>
    <Reference Include="Coral.Managed">
      <HintPath>..\\..\\..\\..\\DotNet\\Coral.Managed.dll</HintPath>
      <Private>false</Private>
    </Reference>
  </ItemGroup>
</Project>
""", encoding="utf-8", newline="\n")
        shutil.copy2(SCRIPT_PROPS, scripts / "Directory.Build.props")

    def finish(self, start_scene, window=(1280, 720), extra_project=""):
        registry = "Assets:\n" + "".join(f"  - Handle: {handle_for(path)}\n    FilePath: {path}\n    Type: {kind}\n" for path, kind in sorted(self.assets.items()))
        (self.assets_dir / "AssetRegistry.lzr").write_text(registry, encoding="utf-8", newline="\n")
        project = ("FormatVersion: 1\n" f"Project:\n  Name: {self.name}\n  AssetDirectory: Assets\n  AssetRegistry: Assets/AssetRegistry.lzr\n"
                   f"  ScriptModulePath: Scripts/Binaries/{self.name}.dll\n  DefaultNamespace: {self.namespace}\n  StartScene: {start_scene}\n"
                   "  AutomaticallyReloadAssembly: true\n  AutoSave: false\n" + extra_project +
                   f"  RuntimeExport:\n    GameName: {self.name}\n    WindowWidth: {window[0]}\n    WindowHeight: {window[1]}\n"
                   "    Fullscreen: false\n    VSync: true\n    TargetConfig: Dist\n")
        (self.folder / f"{self.name}.luxproj").write_text(project, encoding="utf-8", newline="\n")
