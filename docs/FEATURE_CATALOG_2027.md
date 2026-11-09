# LuxEngine — Feature Catalog for 2027.1 (or 2028.1)

Written 2026-11-09 on `feature/native-audio-system`. A planning document, like the other files in
`docs/`: it says what should and should not be in the engine, editor, runtime and a future launcher.
What is built is described in `.claude/docs/Architecture-LuxEngine.md`; the open work list is
`docs/NEXT_UP_BACKLOG.md`. This catalog sits above both and gives each feature a verdict.

## How to read it

**State** — how sure the "today" column is:

- ✔ seen in the source tree today (file listing or grep; nothing was built or run for this document)
- ◐ taken from the backlog, a plan or the README; re-check before starting
- ? not checked; the item says what to look for

**Verdict**

| Verdict | Meaning |
|---|---|
| KEEP | Built and in use. Leave it. |
| FINISH | Code exists but was never run in the editor or in Play. Run it, fix what breaks. |
| MODIFY | Exists, needs a change before a release. |
| ADD | Does not exist. |
| DEFER | Not for 2027.1. Goes to 2028.1 or later. |
| CUT | Remove it, or hide it from users. |
| NEVER | Should not be in LuxEngine. Standing rule or product principle. |

**Release**: `27` = needed for 2027.1 · `27?` = wanted for 2027.1, first to drop · `28` = 2028.1 · `—` = no release.
**Size**: S (hours), M (a day), L (several days), XL (a plan of its own) — same scale as the backlog.

Standing rules applied throughout: no temporal rendering; ray tracing, terrain and GI restart from
scratch; ImGui stays; the DX11/DX12 scaffolding stays; nothing may make game makers install extra
software; small and refined over broad.

---

## 1. The release bar

**2027.1 is ready when** a person who has never seen the engine can download one zip and, with only
the .NET SDK installed, make and export a small complete 3D game: an animated character walking a lit
level, a menu and a HUD, sound, saved progress, rebindable controls. The exported game runs on a PC
with nothing installed.

Measured against that bar, the engine today is strong in rendering, audio, physics and asset
packing, and has four holes: **no animation, no game UI, a thin C# API, and a large amount of code
that builds but was never run.**

### Must have for 2027.1

| # | Item | Why it blocks the release | Size |
|---|---|---|---|
| M1 | **Hardening pass** — run every backlog item marked "builds, not run" (material graph, prefab sync, character controller, fixed update, layouts, quality tiers, area-light shadows, asset-upload fix) | The backlog lists more than a dozen finished features nobody has driven. Each is a release bug waiting | L |
| M2 | **Skeletal animation** (§2.2) | No characters without it | XL |
| M3 | **Game UI** (§2.9) | No menus or HUDs without custom code | XL |
| M4 | **C# API completion** (§2.7): physics queries, scene loading, time, maths types, entity active | Scripts cannot raycast or change scene today | L |
| M5 | **Beam basics** (§3): never lose work, C# build errors inline, click an error to open the line | The product principle says no external IDE; then the built-in one must be enough | L |
| M6 | **Export that stands alone** (§4): works from the release zip with no source tree, lean layout, no .NET install for players, game name and icon | The whole point of a release | L |
| M7 | **.NET 10** (§2.7 SC9) | .NET 9 leaves support about now; a 2027 release cannot require it | M |
| M8 | **Project format version + upgrade path** (§6 X3) | Projects made in 2027.1 must open in 2027.2 | M |
| M9 | **Test on AMD and Intel GPUs** (§2.1 R26) | Every measurement in the notes is one RTX 4070 Ti | M |
| M10 | **One sample game made only with the release zip** (§6 X6) | It is the acceptance test for the bar above | L |

### Wanted for 2027.1, dropped first if time runs out

Input actions (I2), save-game API (G2), player settings (RT5), autosave (E15), basic particles (G1),
reflection probes (R9), simple fog (R7), decals (R11), string localisation (G3), physics joints (P4),
sprite animation (D2).

### The slip rule

If animation and game UI are not both done **and used by the sample game** by about September 2027,
call the release 2028.1 and pull in the `28` items: navigation, terrain, probe GI, the launcher,
tilemaps, the sequencer.

---

## 2. Engine (`Core`, `ScriptCore`)

### 2.1 Rendering

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| R1 | Deferred clustered PBR, render graph, GPU culling (frustum + HZB), meshoptimizer LODs, geometry cache | ✔ | KEEP | 27 | — |
| R2 | Shadows: cascades, spot, point, area lights | ✔ shaders; ◐ area-light shadow phases "not yet run" | FINISH | 27 | S |
| R3 | Materials: Lux Standard, clearcoat/sheen/subsurface/anisotropy, instances, triplanar, node graph (46 nodes), material functions | ✔ code; ◐ "none of the material work has been run in the editor" | FINISH — the largest unverified surface | 27 | L |
| R4 | Foliage plan (alpha coverage, transmission, double-sided) | ◐ plan exists, status not recorded | FINISH what is started, DEFER the rest | 27? | M |
| R5 | Post: FFT bloom, DOF, GTAO, SSR, exposure/auto-exposure, AgX/ACES | ✔ shaders | KEEP. MODIFY: the README still says GTAO and SSR run "with temporal"; confirm the shipped defaults are spatial only | 27 | S |
| R6 | Anti-aliasing: SMAA 1x only | ✔ | ADD a render-scale slider above 1.0 (supersampling) and a spatial upscale + sharpen for weak GPUs | 27? | M |
| R6n | TAA, SMAA T2x, FSR 2+, DLSS, XeSS | — | NEVER (temporal) | — | — |
| R7 | Fog | ✔ none (no fog shader) | ADD analytic height/distance fog. Froxel volumetrics without history: DEFER | 27? / 28 | M / L |
| R8 | Sky: Preetham + HDR environment | ✔ | KEEP. Physical atmosphere and clouds (removed earlier): DEFER | 28 | L |
| R9 | Reflection probes | ✔ none | ADD baked, box-projected cube probes; SSR falls back to them | 27? | L |
| R10 | Global illumination | ✔ none beyond the sky light | DEFER: a baked SH probe grid, written from scratch, baked in the editor with the existing rasteriser. No extra software, no ghosting | 28 | XL |
| R11 | Decals | ✔ none | ADD deferred decals | 27? | M |
| R12 | Skinned mesh passes | ◐ shader variants existed, no runtime | ADD with M2 | 27 | (M2) |
| R13 | Terrain | ✔ not in tree | DEFER, restart from scratch | 28 | XL |
| R14 | Water, hair, cloth rendering | none | DEFER, no plan | — | — |
| R15 | Ray tracing, path tracer | ✔ `Shaders/PathTracing/` is an empty folder | DEFER, restart. Delete the empty `PathTracing/` and `Meshlets/` folders if nothing loads them | 28 | XL |
| R16 | NRD denoiser | ◐ linked, not wired | CUT. It is temporal by design and conflicts with the standing rule | — | S |
| R17 | Negative entity scale renders and collides inside-out (backlog 2.8) | ◐ | MODIFY | 27 | M |
| R18 | Bindless plan, async compute, retained-scene leftovers | ◐ | DEFER until a profile asks for them | 28 | — |
| R19 | DirectX 12 backend | ✔ scaffolding only | DEFER. Scaffolding stays | — | XL |
| R20 | HDR display output | none | DEFER | 28 | M |
| R21 | 2D renderer: sprites, circles, lines, MSDF text | ✔ | KEEP | 27 | — |
| R22 | Glass refraction, forward transparents | ✔ | KEEP | 27 | — |
| R23 | VR (LUX OPTIC) | ◐ Phase 1 coded, never run | KEEP behind `--vr`, labelled experimental. Must not block the release | 28 | — |
| R24 | `HazelPBR_*` shader names | ✔ | MODIFY: rename to Lux names before a public release | 27? | S |
| R25 | Depth-only pipelines declare unused vertex attributes (backlog 2.9) | ◐ | MODIFY | 27? | S |
| R26 | Hardware coverage and minimum spec | ? `RendererCapabilities.h` exists; all recorded tests are one NVIDIA card | MODIFY: run the golden scenes on an AMD and an Intel GPU; at start-up, a missing required feature shows a clear message instead of a crash; publish a minimum spec | 27 | M |

### 2.2 Animation

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| A1 | Import skeletons and clips (Assimp, glTF first) | ✔ asset types only (`Skeleton`, `Animation`, `AnimationGraph`) | ADD | 27 | L |
| A2 | Playback, blending, cross-fade, GPU skinning, root motion, compressed clips (ACL, ◐ vendored) | none | ADD | 27 | XL |
| A3 | Animation graph: state machine with parameters and transitions | none | ADD | 27 | L |
| A4 | Animation events to C# and to audio (footsteps on the frame) | none | ADD | 27 | M |
| A5 | C# API: play, cross-fade, set parameter, read bone transform, attach entity to bone | none | ADD | 27 | M |
| A6 | Blend spaces, additive layers, masks | none | DEFER | 28 | L |
| A7 | IK, ragdoll, retargeting, morph targets | none | DEFER | 28 | XL |
| A8 | Sequencer / timeline for cutscenes and cameras | none | DEFER | 28 | XL |

### 2.3 Physics

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| P1 | Jolt: bodies, box/sphere/capsule/mesh/compound colliders, layers, `CharacterVirtual`, fixed step, contact and trigger events | ✔ code; ◐ "not yet tried in Play" | FINISH | 27 | M |
| P2 | Switched-off entities and a collider `Enabled` flag (backlog 5.1) | ◐ missing | MODIFY | 27 | M |
| P3 | Queries from C#: raycast, shape cast, overlap, with layer masks | ✔ C++ `SceneQueries.h` exists; ✔ no C# `Physics` class | ADD | 27 | M |
| P4 | Joints: fixed, hinge, slider, spring, distance | ✔ none | ADD | 27? | M |
| P5 | Vehicles, soft bodies, cloth, ragdoll | none | DEFER | 28+ | — |
| P6 | Box2D: box and circle only, steps per frame | ✔ | MODIFY if 2D is supported in 2027.1 (decision D1): fixed step, polygon and capsule colliders, 2D queries | 27? | M |

### 2.4 Audio

The most complete system in the engine. Almost nothing to add.

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| AU1 | Sounds, banks, mixer, Soundwave acoustics, zones, music director, dialogue, accessibility, budgets | ✔ | KEEP. Finish and merge the current branch | 27 | — |
| AU2 | Reading old FMOD projects (reported, never played) | ✔ | KEEP through 2027, CUT in 2028.1 | 28 | S |
| AU3 | Legacy asset types (`SoundConfig`, `SpatializationConfig`, `SoundGraphSound`, `AudioProject`, `AudioBank`) | ✔ in the enum | CUT from every menu and filter; keep the enum values so old registries load | 27 | S |
| AU4 | Vercidium EULA question (backlog 4.14) | ✔ no Vercidium or FMOD entry left in `Dependencies.lua` | Close it once a grep of `Core/` confirms nothing links it | 27 | S |
| AU5 | Per-submesh acoustic materials | ◐ | DEFER | 28 | M |
| AU6 | Voice chat, MIDI, a synthesis graph | none | NEVER for now | — | — |

### 2.5 Scene and entities

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| S1 | Entities, hierarchy, folders, prefabs, variants, structural sync | ✔ code; ◐ sync "not yet tried in the editor" | FINISH | 27 | M |
| S2 | Scene loading from scripts: load, unload, additive, entities that survive a load | ✔ C# has only `CreateEntity`, `DestroyEntity`, `InstantiatePrefab` | ADD | 27 | M |
| S3 | `Entity.Active` and component `Enabled` in C# (backlog 5.2) | ◐ missing | ADD | 27 | S |
| S4 | Find by name, tag, component; user tags | ? | ADD what is missing | 27 | S |
| S5 | `NativeScriptComponent` | ✔ exists, outside the switch-off rules | CUT from the user-facing surface. C# is the scripting path | 27 | S |
| S6 | Simulation thread | ◐ scaffolded, nothing constructs it | CUT the scaffolding, or leave it unreachable and undocumented | — | S |
| S7 | Scene streaming, world partition | none | DEFER | 28+ | XL |

### 2.6 Assets

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| AS1 | UUID asset manager, registry, asset pack with validation, shader pack, baked LODs | ✔ | KEEP | 27 | — |
| AS2 | GPU texture compression (BC7/BC5) at import or at pack time | ? compression is mentioned in `TextureImporter.cpp`; no encoder library seen in `Dependencies.lua` | Check. If textures ship uncompressed, ADD: it decides VRAM use and game size | 27 | M |
| AS3 | Per-asset import settings (sRGB, filter, max size, mesh scale and axes) and re-import when the source file changes | ? | Check, ADD what is missing | 27 | M |
| AS4 | Find references, safe rename, move and delete | ? | Check, ADD | 27? | M |
| AS5 | Async loading (backlog 4.8 fix) | ◐ builds, not run | FINISH | 27 | S |
| AS6 | Lean exported layout, one data folder, pack obfuscation (backlog 4.15) | ◐ planned | ADD. Call it obfuscation in the UI: a key the game can read stops nobody determined | 27 | L |
| AS7 | Patch and DLC packs (more than one pack) | none | DEFER | 28 | L |
| AS8 | Shareable asset packages (`.luxpackage`) | none | DEFER | 28 | M |
| AS9 | Asset store, online package manager | none | NEVER | — | — |

### 2.7 Scripting (C#, Coral)

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| SC1 | Hot reload, inspector attributes, `OnFixedUpdate`, collision and trigger callbacks, `Gizmos`, prefab spawn, generated API reference | ✔ | KEEP / FINISH (callbacks never run in Play) | 27 | S |
| SC2 | Basic types: `Quaternion`, `Mathf`, `Time` (delta, scale, unscaled), `Random`, `Color` | ✔ only `Vector2/3/4` found as types | ADD | 27 | M |
| SC3 | `Physics` class (P3), scene API (S2), animation (A5), UI (U6), particles | none | ADD with each system | 27 | — |
| SC4 | Components reachable from C#: lights, mesh and material parameters, music director, portals | ✔ 19 of 37 C++ components have a C# class | ADD the rest that gameplay needs | 27 | M |
| SC5 | Timers and coroutines | none | ADD | 27? | S |
| SC6 | Application events: focus, pause, quit, window resize | ? | ADD | 27 | S |
| SC7 | Exceptions shown in the console with a stack trace; a click opens Beam on the line | ? | ADD | 27 | M |
| SC8 | Step debugger for scripts | none | DEFER. Until then: logs, `Gizmos`, and attaching an external debugger as an optional path | 28 | XL |
| SC9 | .NET version | ✔ hard-coded to 9 (Coral, `net9.0`) | MODIFY: move to .NET 10 LTS. .NET 9 support ends around November 2026 (check the date); distro packages already default to 10 | 27 | M |
| SC10 | Dead slot per script instance per Play in `m_ManagedObjects` (backlog 3.12) | ◐ | MODIFY | 27? | S |
| SC11 | Visual scripting | none | NEVER for now. One scripting path, done well | — | — |
| SC12 | Native C++ game modules | none | DEFER | 28+ | L |

### 2.8 Input

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| I1 | Keyboard, mouse, gamepads, DualSense triggers, rumble, cursor modes | ✔ | KEEP | 27 | — |
| I2 | Input actions: named actions, key and gamepad bindings, rebinding at runtime, saved per player | ✔ none | ADD | 27? | M |
| I3 | Touch, Steam Input | none | DEFER | — | — |

### 2.9 Game UI

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| U1 | Retained UI on `Renderer2D` + MSDF text: canvas, anchors, stack/grid layout, resolution scaling | ✔ none (`Renderer/UI/` holds fonts only) | ADD | 27 | XL |
| U2 | Widgets: image (9-slice), text, button, toggle, slider, progress bar, text input, scroll list | none | ADD | 27 | (U1) |
| U3 | Keyboard and gamepad focus navigation | none | ADD. Without it a menu fails on a controller | 27 | (U1) |
| U4 | Captions and subtitles drawn by it | ◐ accessibility emits them | MODIFY | 27 | S |
| U5 | UI editing inside the Game View (§3 E9) | none | ADD | 27 | L |
| U6 | C# API: find widget, set text, callbacks | none | ADD | 27 | M |
| U7 | World-space UI, rich text, tweens | none | DEFER | 28 | L |
| U8 | ImGui as the game's UI; an HTML/CSS engine | — | NEVER (ImGui is the editor's; a web UI stack breaks "small") | — | — |

### 2.10 2D

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| D1 | Sprites, circles, lines, text, Box2D bodies | ✔ | KEEP | 27 | — |
| D2 | Sprite sheets (slicing) and sprite animation | none | ADD | 27? | M |
| D3 | Tilemaps and a tile editor | none | DEFER | 28 | L |
| D4 | 2D lights, 2D particles | none | DEFER | 28+ | — |

### 2.11 Gameplay services

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| G1 | Particles: emitters, curves over life, sub-UV, soft particles, instanced draw; an editor panel | ✔ none | ADD a basic CPU-simulated system. GPU simulation, trails, mesh particles, collisions: DEFER | 27? / 28 | XL |
| G2 | Save-game API: key/value and typed blobs in the user folder, slots | none | ADD | 27? | M |
| G3 | String localisation tables | ✔ only dialogue is localised | ADD, reusing the dialogue table's language handling | 27? | M |
| G4 | Navigation: navmesh bake and agents (Recast/Detour, zlib licence, vendored) | none | DEFER | 28 | L |
| G5 | Quality and settings API for games (ties to RT5) | ◐ quality categories exist in the project | ADD the runtime side | 27? | M |
| G6 | Networking | none | DEFER, and promise nothing. Decide in 2028 whether the engine does it at all | 28+ | XL |
| G7 | Video playback | none | DEFER | 28 | M |
| G8 | Behaviour trees, dialogue trees, inventory, quests | — | NEVER in the engine. They are game code | — | — |
| G9 | Discord Social SDK | ◐ optional flag, 1.3 GB SDK, never verified | CUT from release builds. Keep the flag for source builds | — | — |
| G10 | Steam and other store services | none | DEFER | 28+ | M |

### 2.12 Platform and core

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| C1 | Windows 10/11 x64 | ✔ | KEEP, first-class | 27 | — |
| C2 | Linux x64 | ◐ builds in CI; render thread off; no watchdog | Decision D3. Suggested: ship as "supported, single-threaded renderer" and verify once before the release | 27 | M |
| C3 | macOS, consoles, mobile, web | none | Not present. Say so plainly | — | — |
| C4 | Render thread, job system, async transfer queue | ✔ | KEEP | 27 | — |
| C5 | Crash watchdog | ◐ Windows editor only | MODIFY: add to `Lux-Runtime`; Linux later | 27? | M |
| C6 | Tracy, memory tracking, Aftermath | ✔ off in Dist | KEEP | 27 | — |
| C7 | Telemetry, analytics, online accounts | none | NEVER | — | — |

---

## 3. Editor

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| E1 | Shell: docking, workspaces, named layouts, panel memory, command palette, theme, Help panel and manual, undo history | ✔ code; ◐ layouts and panel memory "not run in the editor" | FINISH | 27 | M |
| E2 | Viewport: gizmos, selection outline, debug views, camera bookmarks, grid | ✔ | KEEP. Check: grid and angle snapping, multi-select transform | 27 | S |
| E3 | Hierarchy and Properties | ✔ | KEEP. Check, ADD if missing: multi-entity editing, copy/paste component values | 27 | M |
| E4 | Content Browser, thumbnails, asset pickers | ✔ | KEEP (+ AS3, AS4) | 27 | — |
| E5 | Material Editor and node canvas | ✔ code, never run | FINISH (R3) | 27 | L |
| E6 | Sound Editor, Mixer, Sound Banks, Audio Debugger, See the Sound | ✔ | KEEP | 27 | — |
| E7 | **Beam** code editor | ✔ multi-tab editor; ◐ 12-phase plan not started | FINISH the phases behind M5: upstream core, never lose work, search, C# build errors inline, jump to line. Roslyn completion: `27?`. Vim mode, local history: `28` | 27 | L |
| E8 | Animation tools: clip preview, skeleton view, state-machine graph (reuse the node canvas) | none | ADD with M2 | 27 | L |
| E9 | UI editor in the Game View | none | ADD with M3 | 27 | L |
| E10 | Particle editor | none | ADD with G1 | 27? | M |
| E11 | Prefab edit mode (open a prefab alone) | ? | Check, ADD | 27? | M |
| E12 | Play controls: Play, Simulate, Stop, Pause, Step one frame | ✔ Play/Simulate/Stop; ? Pause and Step | ADD what is missing | 27 | S |
| E13 | Export window: preflight, target folder; game name, icon, version, start scene, "Export and Run" | ✔ export steps and preflight; ? branding fields | MODIFY (RT3, RT4) | 27 | M |
| E14 | Project Settings: physics layers, audio, quality tiers; input actions and tags once they exist | ✔ | MODIFY as systems land | 27 | S |
| E15 | Autosave and recovery after a crash | ✔ no autosave found in `EditorLayer.cpp` | ADD. The watchdog reports the crash; the work is still lost | 27? | M |
| E16 | Undo by full snapshot | ✔ | MODIFY only after measuring it on a 10,000-entity scene. `EditorToolHost` commands are the base if it must change | 27? | S–L |
| E17 | Profiler, Renderer Debugger, render stats | ✔ | KEEP | 27 | — |
| E18 | Golden capture and other engine-development tools | ✔ | CUT from release editor menus (keep in source builds) | 27 | S |
| E19 | Keybinding editor | ? `Keybindings.md` exists | ADD a rebind screen if bindings are fixed | 27? | M |
| E20 | Start screen: recent projects, new from template, open, open the sample | ✔ recent/new/open exist | MODIFY into one clear first screen (see §5 L1) | 27 | M |
| E21 | Long tasks show progress and can be cancelled (import, export, script build, bake) | ? | Check, ADD | 27? | M |
| E22 | Level-building aids: primitives, snapping, duplicate along axis | ✔ `MeshFactory` primitives | KEEP. Box-modelling tools: DEFER | 28 | L |
| E23 | Probe placement and Bake (R9, R10) | none | ADD with R9 | 27? | M |
| E24 | Terrain and foliage painting | none | DEFER with R13 | 28 | XL |
| E25 | Editor extensions in C# (menu items, custom inspectors) | ◐ `EditorToolHost` is the base | DEFER | 28 | L |
| E26 | External automation bridge (MCP) | ◐ decided against for now | DEFER | — | — |
| E27 | A built-in git client | none | NEVER. ADD only a `.gitignore` in the project template; assets are already text | 27 | S |
| E28 | Translating the editor's own UI | none | NEVER for now | — | — |
| E29 | "Open every panel" smoke run in CI | ◐ per-panel Python runners exist under `tests/editor/` | ADD one run that opens each panel and each workspace and fails on an ImGui error | 27 | M |
| E30 | Dist Editor with a stamped shader pack | ✔ recent commits | FINISH: this is the editor that goes in the zip | 27 | S |

---

## 4. Runtime and the exported game

| ID | Feature | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| RT1 | `Lux-Runtime` plays a packed project; `--project`, `--vr`, fullscreen, VSync | ✔ | KEEP | 27 | — |
| RT2 | **Export from the release zip** | ✔ export code looks for the repository root and can build the runtime with MSBuild (`RuntimeExportUtils`) | MODIFY: the zip ships a prebuilt Dist runtime and export only copies it. Test on a machine with no Visual Studio | 27 | M |
| RT3 | **No .NET install for players** | ? the export copies a `DotNet/` folder; whether it holds a whole runtime was not checked | Check on a clean PC. If it needs an installed runtime, bundle one | 27 | M |
| RT4 | Lean layout: executable + one data folder (AS6) | ◐ planned | ADD | 27 | L |
| RT5 | Player settings: resolution, window mode, VSync, frame cap, quality tier, volumes, key bindings; stored in the user folder; C# API | ◐ fullscreen and VSync flags only | ADD | 27? | M |
| RT6 | Branding: executable name, icon, window title, version info, optional splash | ? | ADD | 27 | M |
| RT7 | No hitches on first use: pipelines and shaders warmed while loading; a loading-screen hook | ? | Check with a cold start of an exported game; ADD warm-up if it hitches | 27? | M |
| RT8 | Crash in a shipped game: watchdog, plain dialog, log path (C5) | ◐ editor only | ADD | 27? | M |
| RT9 | Dist contains no editor code, Tracy, Aftermath or debug console | ◐ by build flags | Check the exported file list once | 27 | S |
| RT10 | Size and start-up budget: publish the size and cold-start time of an empty exported game, keep them in CI | none | ADD | 27? | S |
| RT11 | Cross-export (Linux game from Windows) | ✔ export target is a build config, not a platform | DEFER. In 2027.1 you export for the OS you are on | 28 | L |
| RT12 | Dedicated server / headless mode | ◐ a headless test harness exists | DEFER with networking | 28+ | M |
| RT13 | DRM, anti-cheat, real encryption | — | NEVER | — | — |

---

## 5. Launcher

**Recommendation: no separate launcher in 2027.1.** With one engine version there is nothing to
manage; the editor's own start screen does the job. A launcher earns its place when a second
version exists, so it belongs to 2028.1. Two things it will need must be built in 2027.1 anyway.

| ID | Feature | Verdict | Rel | Size |
|---|---|---|---|---|
| L1 | Editor start screen (E20): recent projects, new from template, open, sample, link to the manual | MODIFY | 27 | M |
| L2 | Engine version written into `.luxproj`; opening an older project offers an upgrade with a backup; opening a newer one refuses clearly | ADD — the launcher's foundation | 27 | M |
| L3 | "A newer version exists" notice: one optional request to GitHub Releases, shows the changelog, never installs, works offline | ADD | 27? | S |
| L4 | **Lux Hub**: install engine versions side by side, verify downloads by checksum, update, remove | ADD | 28 | L |
| L5 | Project list across versions, version pinned per project, "open with…" | ADD | 28 | M |
| L6 | Download samples and templates; download runtime binaries for other platforms (the route to RT11) | ADD | 28 | M |
| L7 | Installer, `.luxproj` double-click association, Start-menu entry; AppImage on Linux | ADD | 28 | M |
| L8 | Release notes per version; open the crash-report folder | ADD | 28 | S |
| L9 | Built on `Core`'s window and ImGui with the editor theme: one small native executable, updates itself | Constraint | 28 | — |
| L10 | Accounts and sign-in, a store, cloud saves, a news feed that needs the network, telemetry, Electron or a web runtime | NEVER | — | — |

---

## 6. Release engineering

| ID | Item | State today | Verdict | Rel | Size |
|---|---|---|---|---|---|
| X1 | Version series | ✔ `LUX_VERSION_SERIES "2026.1"` | Bump when the release branch is cut | 27 | S |
| X2 | Stability promise: the C# API and file formats do not break inside a series; a changelog per release | none | ADD | 27 | S |
| X3 | Format versions on project, scene, prefab, material, sound files, with upgrade tests against saved old projects | ? | Check, ADD | 27 | M |
| X4 | README | ✔ out of date: lists the removed volume system, says there are no physics bindings, old "Next up" | MODIFY at release time | 27 | S |
| X5 | Manual, Help panel, generated API reference | ✔ | KEEP; add a page per new system | 27 | M |
| X6 | Samples | ✔ one project that doubles as the engine test bed (benchmarks, a `DDGI` folder) | Split: a clean small 3D game for users (M10), the test scenes kept for development. A 2D sample if D1 says yes | 27 | L |
| X7 | Test matrix: NVIDIA, AMD, Intel; Windows 10 and 11; one Linux distro; golden runs on each | ◐ one machine | ADD (M9) | 27 | M |
| X8 | Release zips per platform from CI | ✔ pre-releases on `dev` | KEEP. Code signing: decide (cost against the SmartScreen warning) | 27 | S |
| X9 | Licence audit: NOTICE, Aftermath redistribution terms, fonts and icons, sample assets | ◐ | ADD a pass before release | 27 | M |
| X10 | Bug-report path: issue template that asks for the watchdog report folder | none | ADD | 27 | S |

---

## 7. What should not be present

One list of everything above marked CUT or NEVER, so it can be checked in one place.

**Cut before 2027.1**

- NRD denoiser (R16) and the empty `PathTracing/` and `Meshlets/` shader folders (R15)
- Legacy audio asset types in menus (AU3); FMOD readers go in 2028.1 (AU2)
- `NativeScriptComponent` as a user feature (S5); simulation-thread scaffolding (S6)
- Discord integration in release builds (G9)
- Golden capture and other engine-development tools in the release editor's menus (E18)
- The `DDGI` folder and benchmark scenes in the sample that users receive (X6)

**Never (unless you change a standing rule)**

- Temporal rendering of any kind: TAA, temporal upscalers, temporal denoisers (R6n, R16)
- Anything a game maker must install besides the .NET SDK: language servers, external IDEs, Node, Python
- ImGui replaced, or used as the game's UI; an HTML/CSS UI engine (U8)
- Visual scripting next to C# (SC11); gameplay frameworks in the engine (G8)
- Asset store, accounts, telemetry, DRM (AS9, C7, RT13, L10)
- A built-in git client (E27)

---

## 8. Decisions that are yours

| # | Decision | Suggested answer |
|---|---|---|
| D1 | Is 2D a supported workflow in 2027.1, or "basic, 3D first"? | 3D first. Sprite animation only (D2); tilemaps in 2028.1 |
| D2 | Move to .NET 10 before or after the release? | Before (SC9) |
| D3 | Linux in 2027.1: first-class, or supported with a single-threaded renderer? | The second |
| D4 | Particles in 2027.1? | Yes, basic only, and the first big item to drop |
| D5 | Game UI: own retained system on `Renderer2D`? | Yes (U1) |
| D6 | Pack "encryption": is deterring casual extraction enough? | Yes, and name it obfuscation |
| D7 | Code signing for Windows releases? | Not for 2027.1; say so in the download notes |

## 9. Suggested order

1. **Now:** merge the audio branch; M1 hardening pass; M7 .NET 10; R17, P2, S3 (small correctness gaps).
2. **Scripting breadth** (M4): SC2, P3, S2, SC6, then I2, G2, RT5. Cheap, and every later system needs it.
3. **Animation** (M2) with its editor tools (E8).
4. **Game UI** (M3) with its editor (E9).
5. **Export and runtime** (M6): RT2, RT3, RT4, RT6; then L2 and M8.
6. **Beam** (M5) and autosave (E15).
7. **Wanted list**, in this order: reflection probes + fog, particles, decals, joints, localisation.
8. **Sample game** made from the release zip on a clean PC (M10), M9 GPU matrix, licence audit, freeze.

Each XL item starts with `/plan-le`.

## 10. What this catalog did not check

- Nothing was built or run. "✔" means the file or symbol is in the tree today.
- Every row marked **?**: texture compression, import settings, find-references, prefab edit mode,
  Pause/Step, keybinding editing, format versioning, .NET bundling in exports, branding fields.
- The state of `FOLIAGE_MATERIALS_PLAN.md`, `BINDLESS_PLAN.md` and `AUDIO_VISUALISATION_PLAN.md`.
- The .NET 9 end-of-support date is from memory; confirm it on Microsoft's support page.
- Linux: not looked at (Windows only, by your instruction).
