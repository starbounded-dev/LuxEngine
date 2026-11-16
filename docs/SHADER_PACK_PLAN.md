# LuxEngine Shader Pack Plan

Written 2026-11-15 on `feature/lucky-merge`. Builds that cannot compile shaders (the Dist editor,
exported Dist games, the planned installer) load precompiled SPIR-V from a shader pack. This plan
makes those packs correct by design: an engine pack that no project can invalidate, exported packs
that stand alone, and a shipped editor that can still compile material graphs. It is a planning
document; what is built is described in `.claude/docs/Architecture-LuxEngine.md` § Shader packs.

**Goal card**

- **Goal:** shader packs work the same way every time: the Dist editor always starts, exported games
  always start, and nothing about either depends on which project a developer opened last.
- **Success:**
  1. The Dist editor starts after any Debug/Release editor session on any project (today it refuses:
     `Shader pack is stale for a build with no shader compiler`, reproduced 2026-11-15).
  2. Exported Dist and Release players pass `run_play.py --export` 12/12, as today.
  3. A forgotten engine-pack rebuild is caught by a check, not by a user.
  4. Graph materials work in the editor that ships to game makers (decision D1).
- **Non-goals:** a DX12 pack, shader hot reload in players, pipeline caches, compression or
  obfuscation of packs (that is `docs/OBFUSCATION_PLAN.md` Phase 3), Linux and macOS packaging.
- **Constraints:** existing exported games keep starting; `.lmat` / `.luxproj` files unchanged;
  no new install for game makers (CLAUDE.md § Product Principle); Windows first (Linux deferred).
- **User's words:** "properly implement shaderpacks … visit luckyengine to know how they did it and
  do it like how they do it [or] do it yourself."

**Decisions this plan is built on** (D1–D4 accepted as recommended by the user, 2026-11-15)

| # | Decision | Recommendation | Consequence |
|---|---|---|---|
| D1 | Does the editor that ships to game makers (catalog E30: "the editor that goes in the zip") compile shaders? | **Yes.** Keep the compiler out of the game player only | Graph materials and shader edits work in the shipped editor; it carries `shaderc_shared.dll` + `dxcompiler.dll` (26 MB, already copied into Dist today, unused) |
| D2 | How the engine pack is built | **Lucky's way:** `Editor.exe --build-shader-pack`, no project opened, output committed; plus a GPU-free CI check that it is current | Engine developers rebuild it after a shader edit; game makers never do |
| D3 | How an exported game checks its pack | **Against the build that wrote it, not against shipped sources.** Stop shipping `Resources/Shaders` and the editor pack in exports | Smaller games, no shader source in them, no source hashing at startup |
| D4 | Global macros (`__HZ_AO_METHOD`, bent normals, reflection occlusion) in compilerless builds | **Spike first** (Phase 0). If a quality change misrenders, pack the macro variants of the 4 affected shaders | Quality tiers that toggle GTAO behave the same in Dist as in Release |
| T1 | Which include the engine pack compiles against | The canonical **no-graph** include, never the live file (technical) | The engine pack cannot depend on a project |
| T2 | Stamp path spelling | `generic_string()` (forward slashes) (technical) | One identity on every OS; every stamp changes once |

## Part 0 — Where we are (2026-11-15, verified this session)

| Capability | State | Evidence |
|---|---|---|
| Pack format | ✅ Working | `Serialization/ShaderPackFile.h`: `LXSP`, `Version = 1`, 16-byte header, no spare field; one program per shader name hash, one variant each. Reader accepts legacy `HZSP` (`ShaderPack.cpp` constructor) |
| What a pack holds | ✅ Complete for today's renderer | `ShaderPack::CreateFromLibrary` dumps `Renderer::GetShaderLibrary()`. `Renderer.cpp` loads 56 shaders at init and nothing loads one later (grep: no other `GetShaderLibrary()->Load`); the packs hold 56 programs |
| Stamp | ✅ Built, ⚠️ wrong input | `ShaderPack::ComputeShaderInputIdentity` hashes every `Resources/Shaders` source **and the live** `Resources/Cache/Shader/Generated/MaterialGraphs.glslh`. Paths use native separators (`lexically_normal().string()`) |
| Generated include | ⚠️ Shared mutable state | `MaterialGraphRegistry::Regenerate` rewrites it for the open project (one function per graph, compiled into 6 surface shaders: `GBuffer_Static`, `HazelPBR_Transparent`, `PreDepth_Masked`, `PreDepth_Meshlet`, `DirShadowMap_Masked`, `SpotShadowMap_Masked`). Any project switch changes it |
| Dist editor start | ❌ Broken | Launched 2026-11-15 on an empty isolated project: `Verify Failed: Shader pack is stale…`. Committed stamp `ShaderInputs: 1655602983`; current inputs `2206879063`; no shader source changed since the pack (git log) |
| Dist editor and graphs | ❌ Cannot work | `MaterialGraphRegistry::Update` returns under `LUX_DIST`; `.lmat` files store `GraphIndex` only inside asset packs (`MaterialSerializer.cpp`, `packed`), so editor-loaded graph materials get index 0 |
| Engine pack refresh | ✅ Working, ⚠️ project-dependent | Application Settings → Refresh shader pack (`ApplicationSettingsPanel.cpp`) writes `Resources/ShaderPack.lsp` from the live library, so with a graph project open it bakes that project's graphs into the engine pack |
| Export pack | ✅ Verified at runtime | `EditorLayer.cpp` `RuntimeExportStep::ShaderPack`: `ScanProject`, copy the live include, `WriteRuntimeShaderPack` → `<export>/Assets/ShaderPack.lsp` (+ `.stamp`). `run_play.py --export` 12/12 Dist (2026-11-14) and Release (2026-11-15), both policies |
| What exports ship | ⚠️ Wasteful | `RuntimeExportStep::Resources` copies all of `Resources/` (shader sources and the editor's 1.4 MB pack included, seen in `bin/play-tests/…/Resources/ShaderPack.lsp`); the player only reads `Assets/ShaderPack.lsp` (`RuntimeApplication.cpp` `ApplyRuntimeShaderPack`) and hashes the shipped sources at startup |
| Compiler DLLs | ⚠️ Shipped unused | `Editor/premake5.lua` copies `shaderc_shared.dll` (4.9 MB) and `dxcompiler.dll` (21.3 MB) in Release **and Dist**; "Runtime export ships every DLL in this folder" |
| Compiler in Dist Core | ❌ Removed | `Core/premake5.lua` Dist `removefiles { "Source/Lux/Platform/Vulkan/ShaderCompiler/**.cpp" }`; `Shader.h`: `LUX_HAS_SHADER_COMPILER = !LUX_DIST` (7 files test it) |
| Precedent for "Dist editor has it, player doesn't" | ✅ Working | `AssimpMeshImporter.cpp` is compiled into Core Dist and reached only through `MeshSourceSerializer::SetSourceImporter`, which the editor calls and the player does not (`Core/premake5.lua` comment) |
| Macros in compilerless builds | ⚠️ Unproven | `SceneRenderer.cpp` sets `__HZ_AO_METHOD` / bent normals from GTAO settings; `RHIShader::RT_Reload` does nothing without a compiler, so Dist keeps the packed variant of `AO-Composite`, `GTAO`, `GTAO-Denoise`, `SSR` |
| Dependants | — | `docs/INSTALLER_PLAN.md` (installer is Dist, consumes a pack); `docs/OBFUSCATION_PLAN.md` Phase 3 (rewrites `WriteRuntimeShaderPack`) |

## Part 1 — How Lucky Engine does it

Read in `M:\LuckyEngine\MAC-Source` (Apache-2.0):

- **One engine pack, no project shaders.** `LuckyEditor/Resources/ShaderPack.hsp`, used by the Dist
  editor (`LuckyEditor.cpp`: VERIFY it exists) and by the runtime (`RuntimeApplication.cpp`). Lucky
  has no material graphs, so nothing per project goes into shaders.
- **Built on purpose, then committed.** `EditorLayer::BuildShaderPack`: menu Build → Build Shader
  Pack, part of Build All, and `LuckyEditor --build-shader-pack`, which builds and exits before
  opening a project (`Building.md` § Rebuilding the distributable shader pack).
- **Contents independent of the build machine.** It force-loads lazily loaded and
  capability-gated shaders (SR, meshlets) and every registered module's `ShaderManifest`, dormant
  ones included: "this machine's GPU must not decide the pack's contents."
- **Versioned, not stamped.** `ShaderPackFile::FileHeader::Version = 3`; a pack of another
  version is ignored with a warning. No source hash: freshness is a discipline ("any new .glsl
  requires regenerating the pack … and committing it").
- **Packages don't ship sources** on Windows/Linux, "whose ShaderPack is self-sufficient"
  (`scripts/Mac-Package.py`).

**What Lux takes:** the explicit `--build-shader-pack` entry point, the committed engine pack,
"contents never depend on the machine", a format version, and self-sufficient packages.
**What Lux does differently:** Lux has per-project graph code inside six engine shaders, so it needs
two kinds of pack (engine and per-export), and it keeps a freshness stamp, because Lucky's
discipline-only approach is exactly how today's bug would go unnoticed.

## Part 2 — Design

```
                 engine developer                          game maker
  Resources/Shaders ──► Editor --build-shader-pack     Editor (compiles, D1) ──► Export
       │                 (no project; no-graph include)       │ graphs in the 6 surface shaders
       ▼                                                      ▼
  Editor/Resources/ShaderPack.lsp (+.stamp, committed)   <export>/Assets/ShaderPack.lsp (+.stamp)
       │ consumed by                                          │ consumed by
       ▼                                                      ▼
  Dist editor first-launch seed (D1) / Installer        Lux-Runtime (Dist/Release)
  check: sources vs stamp, no-graph include             check: format + engine build + programs (D3)
```

- **Engine pack** (T1): compiled with `MaterialGraphRegistry`'s no-graph include. Its identity
  hashes the sources plus that canonical text, never the live file. Built only by
  `--build-shader-pack`; the Application Settings row becomes a status line plus the command to
  run (it can't build an engine pack while a project's graphs are compiled in).
- **Export pack**: unchanged in content (the live library after `ScanProject`, graphs included).
  Its stamp records the engine build (D3); the player checks format version, engine build, and that
  every shader `Renderer::Init` loads is present, then loads. No sources ship.
- **Shipped editor** (D1): compiles like Release. The compiler is compiled into Core Dist and
  reached through a hook the editor registers (the Assimp precedent), so the Dist player links no
  `shaderc`. A current engine pack seeds the first launch so it doesn't compile 56 shaders.

**Engine fit (Step 5)**

| Concern | Answer |
|---|---|
| System | Renderer owns packs (`Renderer/ShaderPack.*`); MaterialGraphRegistry supplies the canonical include text; EditorLayer owns export; the editor app owns the CLI flag |
| Thread | Stamp, evaluate and write: main thread (file I/O, as today). `--build-shader-pack` runs on main after `Renderer::Init`, before any project opens; shader compiles keep running where `ShaderLibrary::Load` runs them today. Same under both threading policies (no new thread) |
| Ownership | No new GPU objects; SPIR-V stays owned by the `RHIShader`s (`WriteStampedPack` comment). Phase 3 variants are extra `RHIShader`s held by the library, freed with it |
| Renderer data flow / invariants | No new bindings, passes or pipelines. Phase 3 swaps one program for another variant: the existing `m_AOPassInputsDirty` rebind path must run, as it does after a recompile |
| Serialization | Pack: LXSP v1 stays readable; Phase 3 writes v2. Stamp YAML gains keys; old stamps still parse (missing keys default). No scene/project/asset change |
| Editor UI | Application Settings shader-pack row (text changes only); ImGui audit in that phase |
| Scripting | None |
| Runtime and Dist | The point of the plan; every phase verifies the Dist editor and an exported Dist player |
| Linux | T2 makes stamps portable; nothing else platform-specific. Linux packs untested (deferred) |
| Build | Phase 4 changes `Core/premake5.lua` (Dist keeps `ShaderCompiler/**.cpp`) and needs `scripts\Win-GenProjects.bat` |
| Docs | Architecture § Shader packs (each phase), `Building.md` (rebuilding the pack, Dist DLLs), `Rendering.md` if Phase 3 adds a variant rule |

## Part 3 — Phases

### Phase 0 — Spikes: macros and first-launch cost

**Goal:** settle D4's scope and D1's cost with numbers, changing nothing.

**Changes:** none in engine code. Two throwaway checks, results written into this plan.
- *Macro spike:* export the sample's `MaterialShowcase` as Dist with GTAO on; in the player switch
  the AO quality tier off (and back). Record whether AO changes, goes black or wrong, or logs errors.
- *Compile cost:* Release editor, `Resources/Cache/Shader` emptied, isolated user dir: seconds from
  launch to first frame (that's what a pack-less shipped editor would pay once).

**Verification:** screenshots and log excerpts for each case; the time, three runs.
**Exit:** D4 is "not needed" or "Phase 3 needed"; D1's first-launch cost is a number.
**Rollback:** nothing to roll back.

**Results (2026-11-15):**
- *Compile cost:* Release editor, isolated, quick-exit start: warm cache 2.3–2.4 s, emptied cache
  5.3–5.4 s (3 runs; the cold run rebuilt all 232 cache files). A shipped editor that compiles pays
  about 3 s once. A seed pack in Phase 4 is a nicety, not a need.
- *Macros:* settled by reading, not run. No player code changes the AO macros after startup (no C#
  quality API; `SceneRenderer::ApplyProjectSettings` sets them once from the project), so an export's
  packed variant matches its startup settings. The live risk was the Dist editor's quality panel,
  which Phase 4 removes. **D4: Phase 3 not needed now**; it becomes needed when games can change
  quality at run time. Added instead (Phase 1): a compilerless build warns once when asked for a
  macro value its pack doesn't hold (`s_PackedShaderMacros`, `Renderer.cpp`).

### Phase 1 — An engine pack no project can break

**Goal:** success criteria 1 and 3: the Dist editor starts after any session on any project, and a
forgotten rebuild fails a check.

**Changes**
- `Renderer/MaterialGraph/MaterialGraphRegistry.h/.cpp` — **NEW** `static std::string GetEmptyIncludeText()` (exposes today's `EmptyInclude()`); `EnsureGeneratedInclude` uses it.
- `Renderer/ShaderPack.h/.cpp` — `ComputeShaderInputIdentity(std::string_view generatedInclude)`: hashes the given include text instead of reading the live file; paths spelled `generic_string()` (T2). Stamp gains `Kind: Engine|Export`.
- `Renderer/Renderer.cpp` — `CaptureShaderPackEnvironment(kind)`: an engine pack is evaluated against the no-graph text, an export pack against the include it shipped with (Phase 2 replaces this).
- `Editor/Source/LuxEditorApp.cpp` — **NEW** `--build-shader-pack`: writes the no-graph include, lets `Renderer::Init` compile the 56 shaders, calls `ShaderPack::WriteStampedPack` to `Resources/ShaderPack.lsp`, logs the identity, exits with 0/1 before opening any project (Lucky's flow). Rejected in Dist (no compiler) with a clear message.
- `Editor/Source/Panels/ApplicationSettingsPanel.cpp` — the row shows the engine pack's state and the command; the button that baked the open project's graphs into the engine pack goes.
- `tests/shaders/check_engine_pack.py` — **NEW**, GPU-free: recomputes the identity (FNV-1a chain, same rules) and compares it with the committed stamp. Runs in the CI Release job and in `run_local.py`.
- `tests/editor/run_dist_editor_start.py` — **NEW**: opens a graph project in the Release editor (rewrites the include), then starts the Dist editor on an isolated empty project and requires no `Verify Failed` in its log. Today's repro, kept.
- `Editor/Resources/ShaderPack.lsp` + `.stamp` — rebuilt with the new command and committed (**needs your OK**: you asked me to leave these files alone).

**Playbook:** none applies (no component, asset type, pass or panel).
**Thread and lifetime:** all main thread; no GPU object created or freed.
**Verification:** build Debug, Release, Dist editor. `Editor.exe --build-shader-pack` logs the
identity and exits 0; `check_engine_pack.py` prints the same identity; `run_dist_editor_start.py`
passes; open `MaterialShowcase` in Release, then rerun it (still passes); edit any `.glsl` and
`check_engine_pack.py` fails naming the command. `run_local.py` 19/19 plus the new runner.
**ImGui:** the settings row: one Begin/End path, unique labels, both states (current, stale) seen.
**Docs:** Architecture § Shader packs (two kinds of pack, canonical include, CLI); `Building.md`
§ new "Rebuilding the engine shader pack".
**Exit:** all of the above; CI green with the new check.
**Rollback:** revert the commit; the old pack and stamp come back with it.

**Status (2026-11-15):** code done. Found while building it: the stamp hashed raw bytes, but a
Windows checkout is CRLF where git stores LF, so the identity now drops carriage returns (C++ and
Python). `Hash::GenerateFNVHash` hashes a trailing NUL, which the Python mirror copies; both give
`23729104` on this tree. `run_dist_editor_start.py` fails on the committed pack with today's
"Shader pack is stale" and passes with a pack from `--build-shader-pack`. The rebuilt engine pack
(`Kind: Engine`, inputs `23729104`, 56 programs) is committed with your OK; Phase 1 is done.

### Phase 2 — Exported games stand alone

**Goal:** success criterion 2 with smaller, source-free exports (D3).

**Changes**
- `Renderer/ShaderPack.*` — stamp records `EngineBuild` (the editor's build identity, **NEW** if no such id exists yet; Phase 2 starts by grepping for one) and `Kind: Export`. **NEW** `ShaderPackState::MissingPrograms` naming the absent shaders.
- `Renderer/Renderer.cpp` — an export pack is checked by format version, engine build, and that every shader `Renderer::Init` loads is present; no source hashing. Unstamped packs keep the `NoStamp` warning (old games).
- `Editor/Source/EditorLayer.cpp` — `RuntimeExportStep::Resources` skips `Shaders/` and `ShaderPack.lsp*`; `RuntimeExportStep::ShaderPack` no longer copies the generated include.
- `Editor/premake5.lua` / export DLL copy — the player export leaves out `shaderc_shared.dll` and `dxcompiler.dll` once `dumpbin /dependents` shows the Dist player doesn't import them.

**Verification:** `run_play.py --export` (Dist and Release) 12/12 both policies; `run_remaster_export.py`
passes; the export folder has no `Resources/Shaders`, no `Resources/ShaderPack.lsp`, no compiler
DLLs; export size before/after recorded; a pack from Phase 1's export started by Phase 2's player
reports the engine-build mismatch by name.
**Docs:** Architecture § Shader packs (export check); `Building.md` (what an export contains).
**Exit / Rollback:** as above; revert restores today's export.
**Coordinate:** `OBFUSCATION_PLAN.md` Phase 3 edits the same `WriteRuntimeShaderPack`; land this first.

**Status (2026-11-15): done.** As built, with one change from the plan: only **Dist** exports drop
`Resources/Shaders`. A Debug/Release player recompiles at startup when a project macro (GTAO bent
normals, say) differs from `Renderer::Init`'s defaults, and a missing source is `LUX_CORE_FATAL` there.
The engine-build id is `LUX_VERSION` (CI appends the commit; local builds share the series). Also found:
the Release player doesn't import Assimp either, so no export ships it; and `ShaderLibrary::Load` used
to log and carry on when a pack lacked a shader, even in builds that could compile it. Results: Dist
export 126 → 94 MB; `run_play.py --export` (Dist and Release) 12/12 with the new contents checks; a
stamp faked to `2025.9-0000000` stops the player with "written by Lux 2025.9-0000000 and this is Lux
2026.1: export the game again". Not done: `Resources/` still carries editor-only folders (`Editor`,
`Docs`, `AgentDocs`, `Templates`, `Robots`…), a follow-up outside this plan.

### Phase 3 — Macro variants in packs (only if Phase 0 shows a problem)

**Goal:** a quality change that flips a global macro renders the same in Dist as in Release.

**Changes**
- `Serialization/ShaderPackFile.h` — LXSP **v2**: programs keyed by name hash plus macro-set hash; v1 still read.
- `Renderer/ShaderPack.cpp` — writer compiles and stores each value set of the macros a shader declares (4 shaders × at most 4 sets); reader indexes them.
- `Renderer/Renderer.cpp` `SetGlobalMacroInShaders` / `UpdateDirtyShaders` — without a compiler, swap to the packed variant instead of reloading; the existing AO rebind (`m_AOPassInputsDirty`) runs as after a recompile.

**Verification:** Phase 0's case passes in a Dist player and the Dist editor; pack size delta
recorded; Release unchanged (golden `hp3` identical).
**Docs:** Architecture § Shader packs; `Rendering.md` if a variant rule is added.
**Rollback:** revert; v1 packs are still written and read.

### Phase 4 — The shipped editor compiles shaders (D1)

**Goal:** success criterion 4: material graphs work in the Dist editor; the Dist player stays compiler-free.

**Changes**
- `Core/premake5.lua` — Dist stops removing `Platform/Vulkan/ShaderCompiler/**.cpp`; run `scripts\Win-GenProjects.bat`.
- `Renderer/Shader.h` and the 7 files that test `LUX_HAS_SHADER_COMPILER` — a runtime check, `Renderer::HasShaderCompiler()` (**NEW**), true once the editor registers the compiler (**NEW** hook, set in `CreateApplication` beside `SetSourceImporter`); the player never registers it, so its link drops `shaderc` (the Assimp pattern).
- `MaterialGraphRegistry::Update` — runs whenever a compiler is registered, not "not Dist".
- `Editor/Source/LuxEditorApp.cpp` — the Dist editor uses the engine pack as a first-launch seed when it is `Current`, and compiles otherwise.

**Verification:** Dist editor opens `MaterialShowcase`; `MS_Graph` shows its graph; a graph edit
recompiles; `dumpbin /dependents` on the Dist player shows no `shaderc_shared.dll`; first launch
time recorded against Phase 0; `run_local.py` passes; Dist export 12/12.
**Docs:** Architecture (compiler availability is a runtime capability), `Building.md` (Dist editor
DLLs), `CLAUDE.md` § Configuration Macros if `LUX_HAS_SHADER_COMPILER` goes.
**Rollback:** revert; the Dist editor goes back to pack-only.

**Phase 4 status (2026-11-15): done.** `ShaderCompiler` hook table in `Renderer/Shader.h` (the
`LUX_HAS_SHADER_COMPILER` define is gone); `VulkanShaderCompiler::InstallAsShaderCompiler` fills it, from
non-Dist `Renderer::Init` and the Dist editor's `CreateApplication`. Core Dist compiles the compiler;
only the Dist editor links it. `dumpbin`: the Dist editor imports `shaderc_shared.dll` + `dxcompiler.dll`,
the Dist player neither. With a compiler, a bad pack is skipped with a warning (checked with a faked
stale stamp: the Dist editor starts and compiles). `run_dist_editor_start.py` now also opens a project
with `MS_Graph.lmat` in the Dist editor and requires "Material graphs: 1 compiled";
`LUX_STARTUP_SELFTEST` reports after 30 frames for that.

**Phase 4 ledger (2026-11-15, read before starting; no code changed yet):**
- Only four call sites reach the compiler: `ShaderLibrary::Load` (`VulkanShaderCompiler::Compile`),
  `RHIShader::RT_Reload` (`TryRecompile`), `MaterialGraphRegistry` `TestCompile` (`TryCompileGLSL`), and
  `Renderer` shutdown (`ClearUniformBuffers`). Compile-time gates: `Shader.h` define plus `Renderer.cpp`
  (3), `RHIShader.cpp` (2), `Shader.cpp` (2), `LuxEditorApp.cpp` (1); `MaterialGraphRegistry::Update`
  returns under `LUX_DIST`. The compiler sources have no Dist code of their own.
- Design: a hook table (Compile, Recompile, TryCompileGLSL, ClearUniformBuffers) behind
  `Renderer::HasShaderCompiler()`; non-Dist `Renderer::Init` registers `VulkanShaderCompiler`, the Dist
  editor registers it in `CreateApplication`, the Dist player never does. With a compiler present, a
  stale or unreadable pack is skipped with a warning instead of VERIFY-failing (the engine pack becomes
  a first-launch seed).
- Linking: `ShaderC`, `ShaderCUtil`, `SPIRVCrossCore`, `SPIRVCrossGLSL`, `SPIRVTools` are
  `Configurations = "Debug,Release"` in `Dependencies.lua`. Follow the Assimp precedent: Core Dist adds
  their include dirs and stops removing `ShaderCompiler/**.cpp`; only `Editor/premake5.lua`'s Dist filter
  links them (Release libs); `Lux-Runtime` Dist links nothing new. `DirectXCompiler` already links in
  every config (an unreferenced import lib adds no import). Exit check: `dumpbin /dependents` on the Dist
  player shows no `shaderc_shared.dll`.

## Part 4 — Verification of the whole

After Phase 4, on a clean checkout: build all configs; `Editor.exe --build-shader-pack`;
`check_engine_pack.py`; `run_local.py` (with the new runner); `run_play.py --export` Dist and
Release; Dist editor opens three different projects in turn and starts every time; one Dist export
played on a second machine with another GPU (device drift is a warning, never a refusal).

## Part 5 — Risks

| Risk | Likelihood | Caught by |
|---|---|---|
| The Python identity drifts from the C++ one | Medium | Phase 1 compares both on the same tree; a mismatch fails the check |
| Compiling the engine pack without a project still pulls something project-specific (macros from project settings) | Medium | `--build-shader-pack` opens no project; macros are recorded in the stamp and diffed in review |
| An "engine build" id doesn't exist and is costly to add | Low | Phase 2 starts by finding one (git hash at build, or Core build time); fallback: hash of the pack writer's own binary |
| Phase 4 linker fallout: Dist player starts importing `shaderc` | Medium | `dumpbin /dependents` is an exit criterion |
| Every stamp changes once (T2, canonical include) | Certain | Expected: Phase 1 rebuilds the engine pack; exported games already in the wild carry no or old stamps and keep the warning path |

## Part 6 — Open questions and your decisions

- **D1–D4:** accepted as recommended (2026-11-15): D1 yes, D2 Lucky's CLI plus CI check, D3 yes, D4 spike first.
- **Permission:** Phase 1 rebuilds and commits `Editor/Resources/ShaderPack.lsp` and `.stamp`, which
  you asked me to leave alone.
- **Unverified:** whether the Dist player imports `shaderc_shared.dll` today (Phase 2 checks); the
  macro behaviour in Dist (Phase 0); first-launch compile time (Phase 0).

## Part 7 — Research notes

- **Lucky Engine** (read directly, Apache-2.0): one committed engine pack built by
  `--build-shader-pack`; contents never depend on the build machine's GPU; header version gate;
  packages ship no shader sources. See Part 1.
- **Unreal:** shaders split into *global* (content-independent, one global shader map) and
  *material* shaders (per material, which dominate cook time). Lux's engine pack / export pack split
  is the same idea at small scale. [Epic: shader development](https://dev.epicgames.com/documentation/en-us/unreal-engine/shader-development-in-unreal-engine), [Epic: adding global shaders](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine), [FMaterialShaderMap](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FMaterialShaderMap)
- **Godot 4.5:** an export-time *shader baker* scans resources and scenes and precompiles what they
  use; the editor keeps its compiler. Same shape as Lux's export pack plus D1. [Godot 4.5 release](https://godotengine.org/releases/4.5), [Godot: pipeline compilations](https://docs.godotengine.org/es/4.x/tutorials/performance/pipeline_compilations.html)
- **Unity:** keyword-driven variants are compiled at build and stripped when unused; a missing
  variant falls back to a similar one. Lux's macro set is tiny (4 shaders), so Phase 3 packs all
  variants rather than adding stripping. [Unity: shader variant stripping](https://docs.unity3d.com/Manual/shader-variant-stripping.html)
- **Pitfall avoided:** Lucky has no freshness check, so a forgotten rebuild reaches users; Lux keeps
  the stamp and adds a CI check (Phase 1).
- **Rejected:** shipping shader sources so players can verify packs (today's design): costs size and
  exposes source for a check the build identity does better.
