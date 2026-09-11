# LuxEngine Architecture Reference

Authoritative structural reference for agents and developers extending LuxEngine.

**This doc is structural, not API-level.** For function signatures, fields, and method lists, read
the header — it is the source of truth and never goes stale. Use this doc for system boundaries,
ownership, lifecycle, threading, extension hooks, and non-obvious invariants.

Companions: `.claude/docs/Conventions.md` (style + helper reuse), `.claude/docs/Building.md` (build
and regeneration), `.claude/docs/Threading.md` (thread contexts), `.claude/docs/Rendering.md`
(renderer invariants).

---

## Table of Contents

- [Part 1: System Overview](#part-1-system-overview)
- [Part 2: Per-System Notes](#part-2-per-system-notes)
- [Part 3: Cross-Cutting Concerns](#part-3-cross-cutting-concerns)
- [Part 4: Implementation Playbook](#part-4-implementation-playbook)
- [Part 5: Directory Map](#part-5-directory-map)

---

## Part 1: System Overview

LuxEngine is a C++20, Vulkan-only 3D game engine and editor. It is a solo project. It builds as a
static library (`Core`), a C# scripting assembly (`ScriptCore`), an editor application (`Editor`),
and a standalone runtime player (`Lux-Runtime`). Everything lives in the `Lux` C++ namespace.

```mermaid
graph TB
    subgraph Entry["Entry"]
        EP[EntryPoint.h] --> APP[Application]
    end

    subgraph Platform["Core / Platform"]
        APP --> WIN[Window / GLFW]
        APP --> INPUT[Input]
        APP --> RT[RenderThread]
        APP --> JOBS[JobSystem]
    end

    subgraph Rendering["Renderer"]
        APP --> REN[Renderer]
        REN --> SR[SceneRenderer]
        SR --> RG[RenderGraph]
        SR --> R2D[Renderer2D]
        SR --> DBG[DebugRenderer]
        REN --> NVRHI[nvrhi / Vulkan]
    end

    subgraph SceneGraph["Scene / ECS"]
        APP --> SCENE[Scene]
        SCENE --> ENT[Entity]
        ENT --> REG[entt::registry]
        SCENE --> PKT[FrameRenderPacket]
        PKT --> SR
        SCENE --> RS[RenderScene / GPUScene]
        RS --> SR
    end

    subgraph PhysicsAll["Physics"]
        SCENE --> PS[PhysicsScene]
        PS --> JOLT[JoltPhysics]
        SCENE --> B2D[PhysicsScene2D / Box2D]
    end

    subgraph Script["Scripting"]
        SCENE --> SE[ScriptEngine]
        SE --> CORAL[Coral / .NET 9]
        SE --> SG[ScriptGlue]
    end

    subgraph Assets["Asset System"]
        AM[AssetManager] --> EAM[EditorAssetManager]
        AM --> RAM[RuntimeAssetManager]
        EAM --> EAS[EditorAssetSystem worker]
    end

    subgraph EditorSys["Editor"]
        EDLAYER[EditorLayer] --> PM[PanelManager]
        EDLAYER --> ECAM[EditorCamera]
        EDLAYER --> SEL[SelectionManager]
    end

    APP --> AM
    EDLAYER --> SCENE
```

### System summary

| System | Location | Key headers |
|---|---|---|
| Application / Core | `Core/Source/Lux/Core/` | `Application.h`, `Layer.h`, `RenderThread.h`, `JobSystem.h`, `Ref.h`, `Base.h` |
| Window / Platform | `Core/Source/Lux/Core/`, `Core/Platform/{Windows,Linux}/` | `Window.h`, `Thread.h` |
| Renderer | `Core/Source/Lux/Renderer/` | `Renderer.h`, `SceneRenderer.h`, `RenderGraph.h`, `FrameRenderPacket.h` |
| Vulkan backend | `Core/Source/Lux/Platform/Vulkan/` | `VulkanDeviceManager.h`, `DescriptorSetManager.h`, `ShaderCompiler/` |
| Scene / ECS | `Core/Source/Lux/Scene/` | `Scene.h`, `Entity.h`, `Components.h`, `SceneSerializer.h` |
| Physics 3D | `Core/Source/Lux/Physics/` (+ `JoltPhysics/`) | `PhysicsSystem.h`, `PhysicsScene.h`, `PhysicsShapes.h` |
| Physics 2D | `Core/Source/Lux/Physics2D/` | via `Scene.h` |
| Scripting | `Core/Source/Lux/Scripting/` | `ScriptEngine.h`, `ScriptGlue.h`, `ScriptBuilder.h` |
| Assets | `Core/Source/Lux/Asset/` | `AssetManager.h`, `Asset.h`, `AssetTypes.h` |
| Audio | `Core/Source/Lux/Audio/` | `AudioEngine.h`, `AudioSource.h`, `RaytracedAudioScene.h` |
| Editor framework | `Core/Source/Lux/Editor/` | `EditorPanel.h`, `PanelManager.h`, `EditorCamera.h`, `SelectionManager.h` |
| Editor app | `Editor/Source/` | `EditorLayer.h`, `Panels/` |
| ImGui | `Core/Source/Lux/ImGui/` | `ImGuiEx.h`, `ImGuiUtilities.h`, `Colors.h` |
| Project | `Core/Source/Lux/Project/` | `Project.h`, `ProjectSerializer.h`, `UserPreferences.h` |
| Serialization | `Core/Source/Lux/Serialization/` | `AssetPack.h`, `StreamReader/Writer.h` |
| Social | `Core/Source/Lux/Social/` | `DiscordSocial.h` |
| Build | repo root | `premake5.lua`, `Dependencies.lua`, `scripts/` |

### System dependency rules

Violations block the editor/runtime split and are rejected in review.

| System | May depend on | Must NOT depend on |
|---|---|---|
| Scene / ECS | Asset, Physics, Core, Renderer *types* | `Editor/Source/**` |
| Physics | Scene (read), Core, Math | Renderer, Editor, ScriptEngine |
| Renderer / SceneRenderer | Scene (read, via packet), Asset, Core | `Editor/Source/**`, Physics, ScriptEngine |
| ScriptEngine | Scene, Asset, Core (via ScriptGlue) | Renderer, `Editor/Source/**` |
| Asset system | Core, Project | Renderer internals, `Editor/Source/**` |
| Editor panels | everything in `Core` | — (engine code never includes editor-app headers) |

Two notes specific to LuxEngine:

- `Scene.h` includes `Lux/Editor/EditorCamera.h` and `Renderer2D.h`. That is allowed: those are
  `Core`-owned editor *framework* types, not the editor application. The prohibition is on
  `Editor/Source/**`.
- `Core` must remain buildable and usable without `Editor`. `Lux-Runtime` is the proof — if a change
  breaks the runtime build, the dependency direction was violated.

---

## Part 2: Per-System Notes

### 2.1 Application lifecycle

`Application` (`Core/Source/Lux/Core/Application.h`) owns the window, layer stack, render thread,
event queue, application settings, and performance profiler. The client implements
`CreateApplication(argc, argv)` (see `EntryPoint.h`).

`ApplicationSpecification` carries name/size/vsync/fullscreen, `RendererConfig`,
`CoreThreadingPolicy`, `EnableSimulationThread`, `EnableImGui`, `EnableDiscordRichPresence`, and
`IconPath`.

**Construction order matters** and is not obvious: `s_MainThreadID` is captured, settings are
deserialized, `JobSystem::Init` runs, then `m_RenderThread.Run()`, and only *then* is the `Window`
created. The threading policy is therefore read from `App.lsettings` in `LuxEditorApp.cpp` *before*
the `Application` object exists, because `RenderThread` is constructed with it.

**Layers** (`Layer.h`, `LayerStack.h`): `OnAttach` / `OnDetach` / `OnUpdate(Timestep)` /
`OnImGuiRender` / `OnEvent`. `PushLayer` for regular, `PushOverlay` for top. The editor and the
runtime each supply one.

**Frame loop** — see `.claude/docs/Threading.md § The frame loop` for the exact ordering. Do not add
per-frame work directly to `Application::Run`; add it to a layer's `OnUpdate`.

**Events** are two-stage: `QueueEvent` / `DispatchEvent<T>` are thread-safe and deferred until
`SyncEvents()`; `DispatchEvent<T, true>` dispatches immediately (main thread only).

### 2.2 Window / Platform

`Window` (`Core/Source/Lux/Core/Window.h`) is created via `Window::Create(WindowSpecification)` and
owns the GLFW window, the `RendererContext`, and the `DeviceManager`.
`Application::GetGraphicsDeviceManager()` / `GetGraphicsDevice()` are the shortcuts to the nvrhi
device.

Platform-specific implementations are separate translation units under
`Core/Platform/Windows/` and `Core/Platform/Linux/` (`*FileSystem.cpp`, `*Thread.cpp`,
`*RenderThread.cpp`), selected by a premake glob on `os.target()`. Add a platform behaviour by adding
the file to **both** folders — not with `#ifdef` in shared code.

### 2.3 Renderer

Three layers (`Renderer` facade → `SceneRenderer` → `Renderer2D`/`DebugRenderer`) over NVRHI/Vulkan.

**Read `.claude/docs/Rendering.md` before changing anything here.** The invariants that are easy to
break and hard to see: the global `(set, binding)` namespace, pipeline caching, frame-indexed
resource release, and `RenderGraph::ComputeStructureHash` completeness.

Structurally:

- `SceneRenderer` owns a `RenderGraph` and rebuilds its description each frame, caching the compile
  behind a structure hash. The pipeline is deferred PBR: G-buffer, clustered (froxel) light culling,
  HZB + GPU mesh culling, GTAO, SSR, volumetric clouds, sky atmosphere, transparent forward, then
  post (TAA, auto-exposure, bloom, composite, SMAA, DOF).
- `RenderScene` / `GPUScene` / `MaterialScene` / `TextureScene` hold the persistent render-side
  mirror of the ECS, with `StaticMeshRenderProxy` entries and dirty flags. `Scene::SyncRenderScene`
  maintains them.
- `FrameRenderPacket` is the per-frame snapshot that decouples submission from the live registry.
- `RendererConfig::FramesInFlight` defaults to 3.
- Selection outline jump-flood inputs are rebound inside the render queue for each iteration,
  because the ping-pong pass is reused. Mask/distance data uses point sampling. Selection wireframes
  use the on-top pass; collider wireframes use a cached depth-tested variant unless On Top is enabled.
  Both share the scene color target, and the graph declares the collider depth read. Collider colors
  are captured per frame and written to material storage in render-queue order.

### 2.4 Scene / ECS

`Scene` (`Core/Source/Lux/Scene/Scene.h`) derives `Asset` and owns the `entt::registry`, the
`Renderer2D`, the physics scenes (`PhysicsScene` 3D, `PhysicsScene2D`), the `ScriptStorage` and live
`CSharpObject` instances, runtime audio sources, and the UUID→entity map.

`Entity` (`Entity.h`) is a thin wrapper over `entt::entity` + `Scene*` with
`AddComponent<T>` / `GetComponent<T>` / `TryGetComponent<T>` / `HasComponent<T...>` /
`RemoveComponent<T>`. Templates live in `EntityTemplates.h`, included at the bottom of `Scene.h`.

**Components** (`Components.h`): `IDComponent`, `TagComponent`, `TransformComponent`,
`RelationshipComponent`; rendering (`MeshComponent`, `StaticMeshComponent`, `SubmeshComponent`,
`MeshTagComponent`, `SpriteRendererComponent`, `CircleRendererComponent`, `TextComponent`,
`CameraComponent`); lighting (`DirectionalLightComponent`, `PointLightComponent`,
`SpotLightComponent`, `SkyLightComponent`, `SkyAtmosphereComponent`, `VolumetricCloudComponent`,
`ExponentialHeightFogComponent`); physics 3D (`RigidBodyComponent`, `CharacterControllerComponent`,
`Box`/`Sphere`/`Capsule`/`Mesh`/`CompoundColliderComponent`); physics 2D (`RigidBody2DComponent`,
`BoxCollider2DComponent`, `CircleCollider2DComponent`); scripting (`ScriptComponent`,
`NativeScriptComponent`); audio (`AudioSourceComponent`, `AudioListenerComponent`);
`FolderComponent` (a purely organizational hierarchy grouping node — non-empty marker, kept out of
the transform/render paths); and `PrefabComponent`.

**Prefab instances:** an instantiated (or freshly created) prefab hierarchy carries a
`PrefabComponent{PrefabID, EntityID}` on every entity, linking each to its source entity inside the
prefab asset's scene. `SceneSerializer::GetOverriddenComponentKeys(instance, source)` diffs the two
entities' serialized component blocks to surface per-component overrides in the inspector, and
`Scene::ReconcilePrefabComponents(dst, src)` makes one entity's prefab-tracked components match the
other exactly (replace/add/remove) — together backing the editor's per-component and all-at-once
**Revert** (source → instance) / **Apply** (instance → source, then re-serialize the asset). True
**Variant prefabs** are self-contained prefabs that carry a `BasePrefab` handle (`Prefab::GetBasePrefab`),
serialized as an optional top-level `BasePrefab` key by `PrefabSerializer::WritePrefabFile` (the single
prefab-write path, so the base link survives every save). A variant instantiates like any prefab; on
saving a base, `EditorLayer::PropagateToVariants` refreshes each derived variant asset via
`Scene::AdoptPrefabBaseEdits` (UUID-matched, un-overridden entities adopt the base edit). **Prefab edit mode**
(EditorLayer) swaps the editing context to a copy of the prefab's scene (`ApplyEditorScene`, shared
with `OpenScene`); on save it writes the asset and calls `Scene::PropagatePrefabEdits`, which
refreshes un-overridden instances in the returned scene to the edited prefab's values.

**Lifecycle:** `OnRuntimeStart` / `OnRuntimeStop` (physics + scripts), `OnSimulationStart` /
`OnSimulationStop` (physics only), and the per-mode updates `OnUpdateRuntime` /
`OnUpdateSimulation` / `OnUpdateEditor`.

**Rendering entry points:** `OnRenderEditor` / `OnRenderSimulation` / `OnRenderRuntime`, built on
`BuildRenderPacket*` + `SubmitRenderPacket`. `Render3D` / `Render3DRuntime` are the higher-level
orchestrators.

**Conventions:**

- `UUID` is stable across save/load and scene copies; `entt::entity` handles are not. Identify by
  UUID anywhere that crosses a frame, a file, or a duplication.
- The registry is **not** thread-safe; mutation is main-thread only.
- Destroying an entity mid-iteration invalidates views — use `SubmitToDestroyEntity`, which defers
  into `m_PostUpdateQueue`.
- `Scene::Copy` / `CopyTo` back play-mode duplication; a component that isn't copied there silently
  vanishes on Play.

### 2.5 Physics (3D — Jolt)

Layered so the backend can be swapped:

- `PhysicsAPI` (`PhysicsAPI.h`) — backend interface; `JoltAPI` is the only implementation.
- `PhysicsSystem` (`PhysicsSystem.h`) — static facade: init/shutdown, mesh cooking, scene factory.
- `PhysicsScene` / `PhysicsBody` — the world and its bodies (`JoltScene`-equivalent logic in
  `PhysicsScene.cpp`, `JoltBody`).
- `PhysicsShapes.h` — box, sphere, capsule, convex mesh, triangle mesh (static only), compound.
- `CharacterController.h` / `JoltCharacterController`.
- `PhysicsLayer` / `PhysicsLayerManager` — collision filtering.
- `SceneQueries.h` — raycasts, shape casts, overlaps.
- `MeshCookingFactory` / `MeshColliderCache` — mesh colliders are cooked and cached, not rebuilt.
- `PhysicsCaptureManager`, `PhysicsContactCallback`, `PhysicsSettings`.

Stepping is driven by `Scene::StepPhysics(ts)` from the scene update — there is **no** fixed-phase
scheduler in LuxEngine.

### 2.6 Physics (2D — Box2D)

`Core/Source/Lux/Physics2D/`, driven directly by `Scene` (`OnPhysics2DStart` / `OnPhysics2DStop`).
No abstraction layer — Box2D is small enough to use directly.

### 2.7 Scripting (C# / Coral)

`ScriptEngine` (`Scripting/ScriptEngine.h`) hosts .NET 9 through Coral (`Core/vendor/Coral/`).

- Host lifecycle: `InitializeHost` / `ShutdownHost`, then `Initialize(project)` / `Shutdown`.
- Assemblies: `LoadProjectAssembly` (editor, from disk), `LoadProjectAssemblyRuntime(Buffer)`
  (runtime, from an asset pack), `ReloadAppAssembly` (hot reload), `BuildAssemblyCache`.
- `ScriptGlue.cpp` registers every internal call. **All new internal calls go there** — never in
  `ScriptEngine.{h,cpp}`.
- `ScriptEntityStorage.hpp` holds per-entity field values (`ScriptStorage`, serialized with the
  scene); live objects are `CSharpObject` instances on the `Scene`.
- `ScriptBuilder` shells out to build the project's C# assembly.
- `ScriptFieldMetadata::HasMethod(name)` gates lifecycle invocation so the engine doesn't call hooks
  a script doesn't define.

The Coral host assembly is deployed to `Editor/DotNet/` by `Core`'s premake post-build step (see
`.claude/docs/Building.md`). Managed references are invalidated on reload — never cache them across
frames.

### 2.8 Asset system

`AssetManager` (`Asset/AssetManager.h`) is a **static facade** over `AssetManagerBase`, resolved
through `Project::GetAssetManager()`. Two implementations:

- `EditorAssetManager` — file-backed, owns the `AssetRegistry` (`AssetHandle` → `AssetMetadata`).
- `RuntimeAssetManager` — loads from a packed `AssetPack`.

Asset types (`AssetTypes.h`): `Scene`, `Prefab`, `Mesh`, `StaticMesh`, `MeshSource`, `Material`,
`Texture`, `EnvMap`, `Audio`, `SoundConfig`, `SpatializationConfig`, `Font`, `Script`, `ScriptFile`,
`MeshCollider`, `SoundGraphSound`, `Skeleton`, `Animation`, `AnimationGraph`.

Serializers live beside the importers (`MeshSerializer`, `TextureSerializer`, `MaterialSerializer`,
`SceneAssetSerializer`, `AudioAssetSerializer`, plus `*RuntimeSerializer` variants) and are wired up
in `AssetImporter.cpp`. Extension → type mapping is in `AssetExtensions.h`.

Rules:

- Reference assets by `AssetHandle`, never by path after import.
- Memory-only assets (procedural meshes, runtime textures) are registered with
  `AssetManager::AddMemoryOnlyAsset` and live in a **separate map** (`m_MemoryAssets`, guarded by a
  `std::shared_mutex`), queried via `IsMemoryAsset`. They are *not* marked with a flag —
  `AssetFlag` has only `None`, `Missing`, and `Invalid` (`AssetTypes.h`). Don't look for a
  `MemoryOnly` flag; there isn't one.
- Engine code must work against **both** managers — no editor-only assumptions.
- Async: `GetAssetAsync` + `SyncWithAssetThread()`; the worker is `EditorAssetSystem` /
  `RuntimeAssetSystem` (see `.claude/docs/Threading.md`).
- Dependencies: `RegisterDependency(dep, handle)` so a reloaded texture notifies its materials.

### 2.9 Editor

Split between engine-owned framework (`Core/Source/Lux/Editor/`) and the editor application
(`Editor/Source/`).

- `EditorPanel` (`Core/.../Editor/EditorPanel.h`) — `RefCounted` base with `OnImGuiRender(bool&
  isOpen)`, plus optional `OnEvent`, `OnProjectChanged`, `SetSceneContext`, `OnClose`.
- `PanelManager` — `AddPanel<T>(category, strID, isOpenByDefault, args...)`, `GetPanel<T>(strID)`,
  `RemovePanel`, and `Serialize` / `Deserialize` of open state. Panels are grouped by
  `PanelCategory`.
- `SelectionManager`, `EditorCamera`, `EditorConsolePanel` + `EditorConsole/`,
  `SceneHierarchyPanel`, `EditorResources`, `FontAwesome.h`.
- `EditorStack` (`Core/.../Editor/EditorStack.h`, header-only, main-thread singleton) — undo/redo
  *signal*. It carries a "scene edited" flag; `ImGuiEx::Property` raises it on every field edit (gated
  by the `UndoDo` macro), and non-widget edits call `MarkSceneEdited("label")`. `EditorLayer` turns the
  flag into a **labelled, per-entity diff** step: it splits the scene via
  `SceneSerializer::SerializeEntitySnapshots` (per-entity YAML + meta) and stores only the changed
  entities, so history is O(change). Restore reassembles the full scene and runs the whole-scene
  deserialize (`DeserializeFromSnapshots`), which keeps it safe against the two-way parent/child links.
  Value-based, so nothing dangles. Non-scene edits (renderer/project settings) push closure commands
  (`CustomUndo`/`CustomRedo`, via `EditorLayer::PushUndoCommand`) onto the same stack, so one `Ctrl+Z`
  covers everything. Selection is restored per step; a `UndoHistoryPanel` (View → History) shows the
  stack. Play/Simulate get a separate transient history (discarded on Stop; undo there rebuilds and
  restarts the runtime). Resets on scene load. Full design + phased plan: `docs/Editor/Undo-Redo.md`.
- Editor app panels (`Editor/Source/Panels/`): ContentBrowser (+ `ContentBrowser/`),
  ApplicationSettings, ProjectSettings, AssetManager, Materials, MaterialEditor, LightSettings,
  SceneRenderer, RenderStats, RendererDebugger, AudioDebug, TextEditor, ThumbnailCache.
- `Editor/Source/EditorLayer.{h,cpp}` is the orchestrator. Prefer adding a **panel** over adding code
  to `EditorLayer`.
- `Editor/Source/RuntimeExportUtils.{h,cpp}` builds the standalone runtime package.
- Viewport transform gizmos operate on world matrices and convert edits back through the parent
  transform. Translation, rotation, and scale have separate snap increments (also available with Ctrl).
  The six-axis view widget uses `EditorCamera::SetOrbitState`; camera view construction uses the
  orientation's up vector so top/bottom views remain valid. Icons use world positions and selected
  mesh bounds use only that mesh's submeshes. 2D collider overlays match Box2D's radius/offset
  convention and share the 3D collider scope, color, and On Top controls.

UI style: use `ImGuiEx` scopes and widgets and `Colors::Theme` constants — see
`.claude/docs/Conventions.md`.

### 2.10 Audio

FMOD Studio playback with required Vercidium Audio acoustics. `AudioEngine`, `AudioSource`, `AudioListener`, `AudioFileUtils`. `Scene` owns
runtime sources (`GetOrCreateRuntimeAudioSource`, playlists via
`GetOrCreateRuntimePlaylistSource`) and releases them on stop (`ReleaseAllRuntimeAudio`). Components:
`AudioSourceComponent`, `AudioListenerComponent`.

**Ray-traced acoustics:** `RaytracedAudioScene` (`Audio/RaytracedAudioScene.h`) wraps the
required Vercidium Audio SDK (`Core/vendor/VA_RAY/`) behind a Pimpl. FMOD and VA are mandatory
build dependencies; generation fails if the target SDK is missing. No fallback backend is compiled.

`Scene` owns one `Ref<RaytracedAudioScene> m_RaytracedAudioScene` (`GetRaytracedAudioScene()`).
`OnRaytracedAudioStart()` creates the simulation for runtime scenes. The simulation joins its work
before scene teardown and geometry replacement.

1. `WaitForResults()` (`vaWorldWait`) — joins the batch the *previous* frame kicked.
2. Sync the active `AudioListenerComponent`'s position/forward, create/position one VA emitter per
   `AudioSourceComponent` entity with a valid `Audio` handle, and read `GetResult` / `GetAmbience`.
3. `OnUpdate(ts)` (`vaWorldUpdate`) — kicks the next batch and returns immediately.

Step 2 is the only window in which no Vercidium worker is touching the world, so both emitter
mutation and result reads belong there. Reading results without step 1 races the workers still
writing them, and — because `vaWorldUpdate` without a matching wait can leave a batch queued but not
yet picked up — `Stop()` performs an unconditional `vaWorldWait` *before* testing
`vaWorldGetThreadsRunning`, since that predicate reads false for work that has not started and would
otherwise skip the shutdown drain entirely.

**Emitter topology — the listener is the only ray caster.** It casts all five ray types (reverb,
occlusion, permeation, ambient occlusion, ambient permeation, at Vercidium's documented counts) and
every audio source is registered as one of its targets via `vaEmitterAddTarget(listener, source)`.
Sources keep the SDK's zero ray-count defaults and cast nothing. This is the arrangement Vercidium's
documentation describes, and it is what makes the cost scale: adding a source adds a *target* to the
listener's existing ray budget, not a second budget of its own. Note the argument order — the caster
comes first in both `vaEmitterAddTarget` and `vaEmitterGetTargetFilter`, and swapping them silently
yields no occlusion rather than an error.

Two getters split the output along the axis the simulation itself uses:

- `GetResult(entityID)` → `RaytracedAudioResult`, **per source**: two-band direct-path occlusion
  gain (`OcclusionGainLF/HF`).
- `GetAmbience()` → `RaytracedAudioAmbience`, **per listener**: the ambient (enclosure) filter, the
  measured echogram energy split, and `RaytracedAudioReverb` — the complete EAX/I3DL2 parameter set
  (`vaEmitterGetEAX`), in the SDK's own units of seconds, Hz and linear gain.

Reverb lives on the ambience rather than on each source because it is a property of the *space*, not
of any one emitter; sources contribute to it only through their individual send levels. Multi-zone
reverb (several rooms at once) is what VA's grouped EAX is for — `RaytracedAudioStats::GroupedEAXCount`
surfaces the zone count, but nothing maps those zones onto separate backend reverb units yet.

**Visualisation.** `SetVisualisationEnabled(...)` turns on the SDK's visualisation rays and
`GetVisualisation(...)` returns a `RaytracedAudioVisualisation` snapshot of world-space bounce
positions and normals, laid out ray-major. These rays are debug-only: they cost real raytracing work
and feed nothing back into the audio, so they default to off. **The SDK delivers bounces on its own
worker threads** through `vaEmitterSetVisualisationCallback`, and the delivered array is valid only
for the duration of the callback — a file-local `VisualisationState` (reached through
`vaEmitterSetUserData`, since the callback is a plain C function pointer with no user-data parameter)
copies it out under a mutex, and readers copy it again. Anything else races the worker threads.

VA computes acoustic parameters only — it does not play audio itself, so this is a separate system
from the playback backend below.

**Playback backend:** FMOD Studio owns the Core mixer. `AudioSource` retains raw-file asset
compatibility using FMOD Core; events use Studio. Audio-file metadata also comes from FMOD via
`FMOD_OPENONLY`, so the miniaudio dependency and bundled decoders have been removed. FMOD and
VA shared libraries are copied beside both Linux applications, with `$ORIGIN/lib` lookup.

Under FMOD, `AudioSource` owns an `FMOD::Sound` + a lazily-created `FMOD::Channel` kept paused
rather than stopped between plays (`Channel::stop()` permanently invalidates an FMOD channel, which
doesn't fit this API's "replay in place" contract).

**Where VA and FMOD connect** — `Scene::OnUpdateRuntime`'s acoustics block, in two steps:

1. Once per frame, `AudioEngine::SetReverb(ambience.Reverb)` converts the full EAX set to
   `FMOD_REVERB_PROPERTIES` (seconds → ms, linear gain → dB, ratios → percent) and applies it to the
   one ambient, effectively unbounded `FMOD::Reverb3D` created in `AudioEngine::Init()`. Every field
   is clamped to FMOD's documented range: **FMOD rejects the whole struct if any single field is out
   of range**, which would silently leave the previous reverb in place. `GetReverbSnapshot()` returns
   what was actually applied, in FMOD's units, for the editor to show beside the simulation's output.
2. Per source, `AudioSource::SetAcoustics(AudioSourceAcoustics)` applies the two-band result.

The two-band mapping is the part worth understanding. FMOD's `set3DOcclusion` is a single scalar that
attenuates *and* low-passes together, so the two measured bands are split across the two controls
that can carry them independently: the **LF gain scales the channel's volume** (how much gets through
at all) and **the HF-relative loss, `1 − gainHF/gainLF`, drives the occlusion filter** (how much
*further* the highs are attenuated). Feeding the HF gain straight into `set3DOcclusion` would
double-count the loss — attenuating by the HF amount as well as filtering by it — and a source behind
a wall would go inaudible instead of muffled. Because occlusion now scales volume, `AudioSource`
keeps `m_ConfiguredVolume` and `m_OcclusionVolumeScale` apart and multiplies them, so a `SetConfig`
doesn't wipe out occlusion and occlusion doesn't overwrite the authored volume.

Only the Linux FMOD package has been fetched — the Windows paths in `Dependencies.lua` are unverified
placeholders (see the comment there).

**FMOD Studio (in progress — authoring pipeline).** The engine is migrating from Core-only playback
of raw audio files to Studio events authored in an `.fspro` project. `Dependencies.lua` has two FMOD
entries because Studio is a *separate library layered on* Core (`FMODStudio` + `FMOD`); Studio owns
events and banks and creates a Core system internally, so both must be linked and both `.so`s copied.

Project-level configuration lives in `ProjectAudioSettings` (`Project.h`): `StudioProjectPath` (the
`.fspro`, relative to the asset directory), `StudioBankOutputPath` (relative to the `.fspro`, since
FMOD writes `Build/` next to the project file), `RebuildBanksOnPlay`, and `EnableLiveUpdate`.
`Project::GetStudioProjectPath()` / `GetStudioBankDirectory()` resolve these to absolute paths and
return empty when the project has no authored audio — callers must check, not assume.

`AudioBankBuilder` (`Audio/AudioBankBuilder.{h,cpp}`) shells out to FMOD's CLI, the same way
`ScriptBuilder` shells out to `dotnet`, and deliberately does **not** sit behind `LUX_ENABLE_FMOD` —
building banks needs the Studio *application*, not the SDK. It locates `fmodstudiocl` via
`LUX_FMOD_STUDIO_CL`, then the usual install paths, then executable files on `PATH`; an unsuccessful
lookup returns empty. Discovery refreshes every two seconds so the settings UI avoids per-frame I/O
and picks up later installations. Tool arguments are quoted literally on Linux; Windows rejects
names containing shell variable expansion or quote characters. `NeedsRebuild` compares the newest
file under the `.fspro`'s directory against the *oldest* bank (a project that gained a bank since the
last full build has one current file and one stale one, and the stale one is what matters), skipping
the build directory (including sibling `GUIDs.txt` and platform outputs), `.cache`, `.user`, and `.git`
so generated files and workspace changes do not invalidate a build. `EditorLayer::RebuildAudioBanksIfNeeded()`
runs this on entering Play; a failed build logs and lets Play continue, because a stale bank still
plays something.

Builds always pass `-export-guids`, which writes `Build/GUIDs.txt` as `{guid} event:/Path` lines.
**Events must be referenced by GUID, not by path**: a rename in Studio leaves a path-referencing
scene silently mute with no error, while GUIDs survive renames and moves.

`.fspro` and `.bank` are registered as `AssetType::AudioProject` / `AssetType::AudioBank` so the
Content Browser can show and *activate* them — activation hands off to the FMOD Studio GUI
(`AudioBankBuilder::OpenInStudio`), since neither is edited in this editor. They have no
`AssetImporter` serializer on purpose: they are never loaded as engine assets.
`ContentBrowserPanel::ProcessDirectory` treats a directory containing a `.fspro` as **opaque** and
does not recurse into it — a Studio project is dozens of GUID-named XML files under `Metadata/`,
plus its cache and build output, none of which is engine content. Dot-directories are skipped for
the same reason. (`Metadata/` *is* tracked in git: it is the authored source, stored one file per
object so audio work merges. `Build/`, `.cache/` and `.user/` are ignored.)

**Runtime ownership: Studio owns the Core system.** `AudioEngine::Init` calls
`FMOD::Studio::System::create` and then `getCoreSystem()` — it must **not** also call
`FMOD::System_Create`, which would leave a second core system that nothing mixes through.
Consequences that are easy to get wrong:

- `AudioEngine` owns its initialized flag; callers cannot set it. Failed startup
  leaves it false and clears the backend pointers. Repeated initialization and shutdown are safe.
  `Application` shuts audio down after destroying layers and their scene instances.
- `Shutdown` releases **only** the Studio system. It owns the core system, so releasing or closing
  `s_Engine` as well is a double free.
- `Update` must call **both** `Studio::System::update()` and then `System::update()`. Studio's update
  does **not** recompute the Core system's 3D attenuation: measured with `Channel::getAudibility` on
  a fixed source with the listener walked away from it, Studio's update alone leaves audibility
  frozen at the geometry the channel started with — distance has no effect, which is indistinguishable
  from spatialisation being switched off — while adding the Core update makes it track the rolloff
  curve exactly. Studio goes first so event state resolves before the core mixer consumes it.
- `GetEngine()` still returns the core system, and remains the right handle for what Studio does not
  cover: the legacy 3D listener, the ray-traced `Reverb3D`, and CPU/memory stats. Core-API playback
  (`createSound`/`playSound`, as `AudioSource` still uses) works on it unchanged, which is what lets
  the event migration land incrementally instead of as one breaking change.

Live update (`FMOD_STUDIO_INIT_LIVEUPDATE`) is gated on `ProjectAudioSettings::EnableLiveUpdate` and
compiled out of `dist` — it opens a listening socket so the FMOD Studio app can attach to the running
editor and remix in place.

`LoadBanks(directory)` validates the directory before replacing loaded banks, then loads every
`.bank` in the built-bank directory, **strings bank first**: it
carries the event path table, and loading it late makes every `getEvent("event:/…")` issued in
between fail with `EVENT_NOTFOUND`, which does not hint at the cause. It flushes pending Studio
commands before collecting event metadata and enumerates `AudioEventInfo` (path, GUID, 3D, oneshot)
from the banks themselves rather than from
`GUIDs.txt`, so the list cannot disagree with what is actually loaded. `EditorLayer` loads banks on
project open (so events are listable in Edit mode, not just in Play) and reloads them after a
successful rebuild on Play. A partial load reports failure while retaining the banks it could load;
an invalid or empty directory preserves the previous banks. Reload invalidates existing event
instances; scenes recreate them on their next update.

**Runtime exports (format 17):** `AudioBankManifest` is an explicit, bounded stream block after
`ProjectInfo`'s unchanged fixed-size header. It carries an asset-relative directory, the exact bank
filenames, and the live-update setting (disabled for Dist exports). Never put owning strings or
vectors into the raw `ProjectInfo` block. Older formats load without Studio configuration and warn
that a re-export is needed for events; authoring paths and rebuild-on-play are disabled at runtime.

`RuntimeExport::PrepareAudioBanks` runs once during the existing synchronous export operation on
the main thread. It rejects missing authoring input, missing/empty banks, a missing master/strings
pair, and stale output using `AudioBankBuilder::NeedsRebuild`. The selected bank-output directory
is the configured FMOD platform output (Desktop for current Linux/Windows exports); export does
not invoke Studio or guess a platform. An empty Studio project setting means no authored banks.
Bank paths inside Assets retain their relative location for scripts; external output is packaged
under `Assets/Audio/Banks`. Absolute authoring paths are not portable script paths. The `.fspro`
and source audio are not copied. FMOD Core/Studio and VA shared libraries are required copies,
including Linux's `lib` subdirectory. Copy failures abort export.

`Project::LoadRuntime` initializes FMOD and loads only the manifest's banks, strings first, before
loading scenes or starting scripts. Failure clears partial loads and rejects the project. The exact
manifest prevents obsolete banks left in a reused export directory from being auto-loaded. Scripts
can still load additional banks explicitly. Bank load paths are relative to the packaged Assets,
and the normal idempotent `Audio.LoadBank` behavior applies to banks already loaded at startup.
This does not implement acoustic materials or cross-platform bank compilation.

**Listeners:** `AudioListenerComponent` stores authored `Active`, `ListenerIndex` (0–7), `Weight`
(0–1), `UseAttenuationTarget`, and `AttenuationTarget` (entity UUID). The obsolete listener cone
fields and component-owned runtime `Ref` are removed. Old YAML without the new keys retains an
active listener at index 0 with weight 1; old cone keys are ignored. Listener data is copied through
scenes, duplication, and prefabs. References inside cloned hierarchies are remapped after all
entities exist; duplication preserves external scene targets, while prefab creation clears them.
Prefab apply/revert maps references within the correct instance root, and override comparisons
compare targets in instance UUID space.

`Scene::SyncAudioListeners` runs on the main thread before initial playback and after scripts/physics
on runtime updates, including paused updates. It submits a complete fixed-size snapshot to
`AudioListener::Apply`; adding/removing a component during Play requires no backend object allocation.
World-transform columns provide local -Z forward and +Y up; the bridge orthonormalizes scaled/sheared
bases and supplies a stable orientation for collapsed axes. Scene-owned previous positions provide
velocity, reset on start, slot reassignment, and paused frames. Non-finite transforms and invalid
indices/weights are rejected with rate-limited diagnostics. Duplicate active indices choose the
lowest UUID deterministically and report the conflict.

Studio receives every populated slot, zero weights for holes, and weights normalized to sum to 1.
A missing/deleted attenuation target falls back to the listener position and reports the problem.
No positive-weight listeners resets Studio and Core to one neutral listener at the world origin
(Studio requires at least one nonzero weight); this fallback is reported while playing. Stopping
Play also resets listener state. Studio also publishes listener counts and camera attributes to Core during its asynchronous update;
`AudioListener::Apply` must mirror identical slots in both APIs. Empty/zero-weight slots receive the
dominant camera's attributes (with Studio weight zero) to avoid phantom Core listeners at the origin.
Core raw-file playback uses FMOD's nearest-listener behavior across active cameras, without Studio
weighting or independent attenuation positions. Miniaudio uses one dominant camera (highest weight,
lowest index on ties). The current single-listener Vercidium simulation uses the dominant listener but traces from its attenuation target when
present, after joining the previous worker batch. The Audio Debugger reports distances from that
acoustic position. These are explicit legacy/acoustics limitations, not blended multi-listener
acoustic simulation.

**Event playback:** `Scene` owns one `AudioEventInstance` per entity. Its cache is keyed by entity
UUID and checked against the assigned event GUID and `AudioEngine::GetEventGeneration()`; failed
lookups are cached until the assignment or bank generation changes. Bank unload/system shutdown
advance the generation before invalidating handles. Wrappers check it before any FMOD call, including
destruction, so externally held references cannot touch a released system. These APIs are main-thread
only. Component removal, entity destruction, and scene stop explicitly stop instances before dropping
the scene's references.

An assigned event takes precedence over legacy file playback at startup and on later updates.
Creation applies serialized parameter overrides, world position/orientation, volume, and pitch before
PlayOnAwake; a completed one-shot is not restarted on the next frame. Emitters face local -Z, with
their basis orthonormalized for scaled/sheared transforms. Scene pause is layered over the caller's
pause state, so resuming the editor does not unpause a gameplay-paused event. Event-only sources also
participate in ray-traced acoustics without needing a legacy audio asset.

`AudioEventRef` persists GUID, advisory path and bank name; `ParameterOverrides` persists name/value
pairs through scene/prefab serialization and undo snapshots. Runtime instances are never serialized
or shared by scene copies. The picker follows a new selection/assignment, then preserves the chosen
bank filter while browsing. Events without a strings-bank label display their GUID; path buffers are
sized from FMOD's reported length rather than truncating long event paths.

`SetBusVolume` / `GetBusVolume` drive the mixer buses the sound designer authored (`bus:/`,
`bus:/SFX`). The engine never invents the bus hierarchy — an unknown path returns false/0, which is
the normal answer for a project that has not authored that bus.

**Gameplay scripting (Phase 4):** `AudioScriptBindings` registers the managed `Audio`,
`EventInstance`, `AudioSourceComponent`, and `AudioListenerComponent` APIs. All calls run on the
main thread. Component state belongs to the scene; standalone events belong to a native registry
with monotonically allocated handles, never managed raw pointers. `Dispose` releases an event;
scene stop and assembly reload release the registry and invalidate managed wrappers. One-shots
must be authored as finite events and are collected after playback. An event path requires loaded
strings-bank metadata; GUID references do not. Existing raw-file source controls also use FMOD.

FMOD callbacks copy handle/marker notifications into a mutex-protected bounded queue. The scene
drains it before script updates, dispatching managed callbacks on the main thread; late callbacks
for disposed handles are discarded. Script pause is separate from scene pause, and explicit Play
or Stop consumes pending PlayOnAwake. Managed strings are scoped and freed after internal calls.
Snapshot convenience methods, music, and dialogue remain in their later roadmap
phases. New binding/managed files require Premake regeneration for both native and C# projects.

`Audio.LoadBank(bankFile)` synchronously loads an additional bank during scene setup. Relative
paths resolve beneath the active project's Assets directory; absolute paths are accepted. Load the
master and strings bank before calling event paths. Repeated loads of the same canonical file are
idempotent; adding banks preserves existing event handles. The bank catalog revision retries failed
component lookups without restarting existing playback. Directory reload still invalidates all old
handles. Banks remain engine-owned until bank reload or engine shutdown.

**Editor observability:** `AudioDebugPanel` (`Editor/Source/Panels/AudioDebugPanel.{h,cpp}`, View →
Audio Debugger, closed by default) renders both halves of the stack. It is pure visualization over
read-only accessors — `AudioEngine::GetStats()` / `GetReverbSnapshot()` and
`RaytracedAudioScene::GetStats()` / `GetResult()` / `GetAmbience()` / `GetVisualisation()` — and adds
no instrumentation of its own. Its Reverb section deliberately shows the simulation's EAX output and
the values FMOD actually received side by side, because when reverb sounds wrong the useful question
is which of the two is surprising (a clamp that flattened a value shows up there).

The panel owns the `AudioVisualisationSettings` but does not draw: `EditorLayer::DrawAudioVisualisation()`,
called from `OnOverlayRender()`, reads them and draws ray paths, bounce points, surface normals,
emitter gizmos and world bounds with the `Renderer2D` that function has already set up for the frame.
Splitting it this way keeps the settings next to their UI while the drawing stays where a camera is
already bound — a panel has no scene camera of its own. Note `EditorLayer` holds a `Ref<AudioDebugPanel>`
*only* for this; `PanelManager` still owns the panel and drives its render and scene context.

Both stats structs expose SDK status as data, so editor panels use read-only Core accessors.
`AudioEngineStats::HasMixerStats` distinguishes unavailable measurements from a real zero.

`RaytracedAudioScene::Impl` tracks `StaticTriangleCount` alongside its primitives purely so the panel
can tell "no geometry mirrored" apart from "geometry the simulation is ignoring" — the SDK offers no
way to read a primitive's triangle count back.

### 2.11 Input

`Core/Source/Lux/Core/Input.h` — static, with `KeyCodes.h` / `MouseCodes.h`. Frame-accurate state is
updated by `Application`.

### 2.12 Project

`Project` (`Project/Project.h`) is `Ref`-counted with a static active-project slot. Path accessors:
`GetActiveProjectDirectory`, `GetActiveAssetDirectory`, `GetActiveAssetRegistryPath`,
`GetActiveCacheDirectory`, `GetActiveMeshPath` / `MeshSourcePath` / `AnimationPath`,
`GetActiveScriptModuleFilePath` / `ScriptProjectPath`, `GetActiveAudioCommandsRegistryPath`,
`GetActiveAssetFileSystemPath(path)`.

`SetActive` (editor) vs `SetActiveRuntime(project, assetPack)` (runtime) choose which asset manager
is installed. `ProjectSerializer` handles `.luxproj`; `UserPreferences` holds machine-local state;
`TieringSettings` / `TieringSerializer` hold quality tiers.

> **Regression trap:** the editor persists renderer quality settings into the project file. When a
> visual regression appears "from nowhere", diff the `.luxproj` before diffing code.

### 2.13 Serialization

YAML (yaml-cpp) for human-readable assets — scenes, prefabs, materials, project settings, tiering.
`Utilities/SerializationMacros.h` provides `LUX_SERIALIZE_PROPERTY`.

Binary for distribution: `Serialization/AssetPack.{h,cpp}` + `AssetPackFile.h` +
`AssetPackSerializer`, `ShaderPackFile.h`, and the stream layer (`FileStream`, `MemoryStream`,
`StreamReader`, `StreamWriter`, `Serialization.h` / `SerializationImpl.h`).

Missing keys must deserialize to the struct default. Never hard-fail a load on an absent optional
field, and never silently drop data on save.

### 2.14 Social (Discord)

`Social/DiscordSocial.{h,cpp}` + `DiscordppImpl.cpp`. Double opt-in: the `--discord` premake flag
(defines `LUX_ENABLE_DISCORD`, requires the manually-fetched, gitignored
`Core/vendor/discord_social_sdk/`) **and** the runtime `Discord.RichPresenceEnabled` setting, which
defaults to off. `DiscordSocial::Update()` is pumped once per frame from `Application::Run`, so all
SDK callbacks land on the main thread and presence state needs no locking.

### 2.15 Reflection

`Reflection/` — `TypeDescriptor.h`, `TypeName.h`, `TypeStructures.h`, `TypeUtils.h`,
`MetaHelpers.h`. Compile-time type-name and structure helpers used by the script and serialization
layers.

---

## Part 3: Cross-Cutting Concerns

### Smart pointers

`Ref<T>` (intrusive, atomic, `RefCounted`), `WeakRef<T>` (liveness-checked, **no** `Lock()`),
`Scope<T>` (`std::unique_ptr` alias). Forbidden: raw `new`/`delete`, `std::shared_ptr`,
`std::make_shared`. Full rules and the double-free hazard in `Ref::DecRef` are in
`.claude/docs/Conventions.md`.

### Events

`Core/Events/` — `Event.h` base plus `ApplicationEvent.h`, `KeyEvent.h`, `MouseEvent.h`,
`SceneEvents.h`, `EditorEvents.h`. Dispatch with `EventDispatcher::Dispatch<T>(fn)`. New event:
add the type, declare the class with the event macros, handle it in the relevant layer's `OnEvent`.
`LUX_BIND_EVENT_FN(fn)` (in `Base.h`) is the binding helper.

### Logging, asserts, profiling

`LUX_CORE_*_TAG` / `LUX_*_TAG` (always tag), `LUX_CORE_ASSERT` (Debug) vs `LUX_CORE_VERIFY` (all
configs), `LUX_PROFILE_*` (Tracy, off in Dist / `--no-tracy`). Details in
`.claude/docs/Conventions.md`.

GPU work is profiled separately: `VulkanDeviceManager` owns a `TracyVkCtx` (created in
`CreateDevice`, destroyed in `DestroyDevice`, exposed by `GetGPUProfilerContext()`), and
`RenderCommandBuffer::RT_BeginTimerQuery` / `RT_EndTimerQuery` emit a Tracy GPU zone alongside the
engine's own nvrhi timer query. This is the only place Tracy's Vulkan header is used outside
`Platform/Vulkan/` — it is deliberately kept out of `Debug/Profiler.h`, which reaches nearly every
translation unit through the PCH. See `.claude/docs/Rendering.md § GPU timing has two consumers`.

### Math

GLM, with `GLM_FORCE_DEPTH_ZERO_TO_ONE` defined for `Core` (Vulkan clip space). Engine helpers under
`Core/Source/Lux/Core/Math/` (frustum, sphere, …) and `Core/Source/Lux/Math/`.

### Error handling

Validate at boundaries — file I/O, user input, deserialization, script interop. Internal call sites
are trusted; don't sprinkle defensive checks for impossible states. Prefer RAII over manual cleanup.
`LUX_CORE_VERIFY` is the assert that survives into Dist; use it for invariants that must hold in a
shipped build.

---

## Part 4: Implementation Playbook

### Add a new component

1. Define the struct in `Scene/Components.h`.
2. Handle copying — `Scene::Copy` / `CopyTo` / `DuplicateEntity` / prefab instantiation. A component
   missed here vanishes on Play or on duplicate.
3. Serialize in `Scene/SceneSerializer.cpp` — **both** serialize and deserialize.
4. Editor UI in `Core/Source/Lux/Editor/SceneHierarchyPanel.cpp` — a collapsing header in
   `DrawComponents` plus an "Add Component" menu entry.
5. If the renderer consumes it, add it to `FrameRenderPacket` and the `BuildRenderPacket*` capture —
   not to a direct ECS read during submission.
6. Optional: C# mirror in `ScriptCore` + internal calls in `ScriptGlue.cpp`.
7. Regenerate projects if you added files (`scripts\Win-GenProjects.bat`).

Skipping any step fails silently: invisible in the editor, lost on save, or dropped on Play.

### Add a new asset type

1. Add to the `AssetType` enum and its to/from-string helpers in `Asset/AssetTypes.h`.
2. Create the asset class deriving `Asset`, with `GetStaticType()` / `GetAssetType()`.
3. Create a serializer deriving `AssetSerializer` (plus a runtime serializer if it ships in an
   asset pack).
4. Register in `Asset/AssetImporter.cpp`.
5. Add the extension mapping in `Asset/AssetExtensions.h`.

### Add a new render pass

See `.claude/docs/Rendering.md § Adding a pass` — the short version: shader in
`Editor/Resources/Shaders/`, pipeline + material created once in `SceneRenderer::Init()`, shader
dependency registered, transient targets via `AddTransientTexture`, pass added with **accurate**
reads/writes, feature-gated so it costs nothing when off. Anything affecting compilation must be
folded into `ComputeStructureHash()`.

### Add a new editor panel

1. Derive `EditorPanel` in `Editor/Source/Panels/` (or `Core/Source/Lux/Editor/` if the engine owns
   it). Implement `OnImGuiRender(bool& isOpen)`; override `SetSceneContext` / `OnProjectChanged` as
   needed.
2. Register in `EditorLayer` via `m_PanelManager->AddPanel<MyPanel>(category, "MyPanelID", true)`.
3. Add the menubar toggle.
4. Regenerate projects.

Prefer a new panel over new code in `EditorLayer`.

### Add a new C# internal call

1. Implement in `Scripting/ScriptGlue.cpp` (naming: `ClassName_MethodName`).
2. Register it in the internal-call registration block in the same file.
3. Add the matching C# declaration in `ScriptCore/Source/Lux/`.
4. Add the C# wrapper that calls it.

Names must match exactly. Validate entity liveness before touching components.

### Add a new thread or background job

Read `.claude/docs/Threading.md` first. Use `Lux::Thread` (named) rather than a bare `std::thread`,
call `LUX_PROFILE_THREAD` at the top of the body, and route any GPU work through `Renderer::Submit`
(which will defer it correctly). Never mutate the ECS or the asset registry off the main thread.

For data-parallel work, prefer `JobSystem::ParallelFor` over spawning a thread.

### Add a new dependency

Edit `Dependencies.lua` — one entry in the `Dependencies` table, with platform-specific library
names under `Windows = { … }` / `Linux = { … }`. `ProcessDependencies()` / `IncludeDependencies()`
iterate it automatically; no manual `links {}` / `includedirs {}` in project files. Then regenerate.

### Add a build toggle

One entry in `scripts/BuildOptions.py`'s `OPTIONS`, plus a matching `newoption` in `premake5.lua`
for a `premake`-kind option. See `.claude/docs/Building.md`.

---

## Part 5: Directory Map

```
luxengine/
├── Core/                          # The engine (StaticLib)
│   ├── Source/
│   │   ├── lpch.h / lpch.cpp      # Precompiled header
│   │   └── Lux/
│   │       ├── Core/              # Application, Window, Layer, Ref, Events, Input,
│   │       │                      #   RenderThread, JobSystem, SimulationThread, Log, UUID, Math
│   │       ├── Renderer/          # Renderer, SceneRenderer, RenderGraph, Renderer2D,
│   │       │                      #   RenderScene/GPUScene, Material, Shader, Pipeline, Mesh, UI/
│   │       ├── Scene/             # Scene, Entity, Components, SceneSerializer, Prefab
│   │       ├── Physics/           # PhysicsSystem/Scene/Body/Shapes + JoltPhysics/
│   │       ├── Physics2D/         # Box2D
│   │       ├── Scripting/         # ScriptEngine, ScriptGlue, ScriptBuilder, ScriptEntityStorage
│   │       ├── Asset/             # AssetManager facade, AssetManager/, AssetSystem/, serializers
│   │       ├── Audio/             # AudioEngine, AudioSource, AudioListener, RaytracedAudioScene
│   │       ├── Editor/            # EditorPanel, PanelManager, EditorCamera, SelectionManager,
│   │       │                      #   SceneHierarchyPanel, EditorConsole/
│   │       ├── ImGui/             # ImGuiLayer, ImGuiEx, ImGuiUtilities, Colors, Fonts, ImGuizmo
│   │       ├── Project/           # Project, ProjectSerializer, UserPreferences, TieringSettings
│   │       ├── Serialization/     # AssetPack, streams, runtime serializers
│   │       ├── Platform/Vulkan/   # nvrhi/Vulkan backend, DescriptorSetManager, ShaderCompiler/, Debug/
│   │       ├── Utilities/         # FileSystem, StringUtils, FileDialogs, CommandLineParser
│   │       ├── Reflection/        # TypeDescriptor / TypeName / TypeUtils
│   │       ├── Debug/             # Profiler.h (Tracy wrappers)
│   │       ├── Social/            # DiscordSocial
│   │       ├── Tiering/           # TieringSerializer
│   │       └── Embed/             # LuxIcon.embed
│   ├── Platform/{Windows,Linux}/  # Per-platform FileSystem / Thread / RenderThread
│   └── vendor/                    # Box2D, JoltPhysics, GLFW, imgui, nvrhi, Coral, tracy,
│                                  #   msdf-atlas-gen, NFD-Extended, yaml-cpp, VMA, FastNoise, …
├── ScriptCore/                    # C# scripting assembly (.NET 9)
├── Editor/
│   ├── Source/                    # EditorLayer, LuxEditorApp, Panels/, Viewport/
│   ├── Resources/Shaders/         # GLSL shader corpus (+ Include/, PostProcessing/)
│   ├── DotNet/                    # Coral host assembly (populated by Core's post-build)
│   └── LuxSampleProject/          # Sample project incl. its C# script solution
├── Lux-Runtime/                   # Standalone runtime player
├── scripts/                       # Setup / Win-GenProjects / Configure / BuildOptions / Linux-*
├── vendor/bin/premake5.exe        # Windows premake (Linux binary is fetched, gitignored)
├── premake5.lua                   # Workspace definition
├── Dependencies.lua               # Centralized dependency table
└── .github/workflows/main.yml     # CI (windows-2025, Debug/Release/Dist)
```

---

## Maintaining this document

When a change alters a system boundary, an interface, an ownership rule, or an integration point,
update the matching section here in the **same** change. When a fact here turns out to be wrong,
fix it rather than working around it — a stale architecture doc is worse than none, because it gets
trusted.

Keep it structural. Function-by-function detail belongs in the header.
