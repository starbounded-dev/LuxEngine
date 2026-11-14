# LuxEngine Obfuscation Plan

Scramble what a Lux game ships, plus a game maker's project folder if they opt in, so that files
can't be opened in an image viewer, a text editor, a .NET decompiler or an AI scraper. This is
a planning document written 2026-11-09 from `feature/native-audio-system` at `cc1d7730`. It is not a
description of what exists: `.claude/docs/Architecture-LuxEngine.md` stays the authority for what is
built.

This is **obfuscation, not encryption**. The key travels with the game, so a determined person can
undo it. It stops casual copying and bulk scraping, which is what was asked for.

## Goal card

- **Goal:** exported games, and opted-in projects, stop shipping readable files.
- **Success:**
  1. In an obfuscated export, `AssetPack.lap`, `ShaderPack.lsp`, the runtime project file and the
     game's C# code are not readable as plain bytes. The game plays exactly as before.
  2. A decompiled game DLL shows meaningless names for everything the engine doesn't look up by name.
  3. A project with **Obfuscate project files** turned on keeps its scenes, prefabs and other YAML
     scrambled on disk. The editor opens, edits and saves them as usual. LuxSample keeps this off.
  4. Old exports and plain projects still load unchanged.
- **Non-goals:** real encryption, DRM or anti-tamper; anti-debugging; hiding the engine's own open
  source (engine shaders, ScriptCore); obfuscating `.cs` sources in the project folder (dotnet and
  IDEs must read them; see Open questions).
- **User's decisions (2026-11-09):** obfuscate the asset pack, the shader pack and the game DLL;
  project-folder obfuscation is a per-project opt-in ("LuxSample is the sample so I don't want it
  encrypted, but someone making a game should be protected"); the DLL is both hidden and
  symbol-renamed with Obfuscar; one key per project, baked into that game's exported runtime;
  plan first.
- **Constraints:** game makers install nothing new beyond what the editor already needs (.NET SDK).
  Obfuscar arrives as a dotnet tool, only for projects that turn it on. Windows first; Linux exports
  follow the same code (CI builds Linux; the native Linux runtime is unverified today).

## Decisions this plan is built on

| Decision | Choice | Consequence |
|---|---|---|
| What gets obfuscated | Asset pack, exported shader pack, runtime project file, game DLL; project YAML when opted in (user) | The editor's own `Editor/Resources/ShaderPack.lsp` stays plain: it holds engine shaders and no project key exists for it. |
| Scramble | XOR with a keystream computed from (key, absolute byte position), in 64-byte blocks (technical) | Any byte range can be read alone, which the audio VFS and pack random access need. It is a pure function, so it's safe on the asset, audio-streaming and job threads. Not a cipher, by design. |
| File format | One shared container: a plain 32-byte `LXOB` header (magic, version, flags, 8-byte key fingerprint), then the scrambled original bytes (technical) | A single detection point. Files without `LXOB` read as today, so old exports and plain projects need no migration. A wrong key is reported by its fingerprint instead of producing garbage. |
| Key | 32 random bytes per project, stored in `.luxproj` under `Obfuscation:` (user: per project; technical: where) | Anyone holding the project folder can decode it. That's inherent, and it lets teammates and other machines open the project. `.luxproj` itself stays plain because it holds the key. |
| Key in exports | The export patches a reserved, marked byte block inside the copied `Lux-Runtime` executable (user: "baked into the runtime"; technical: patching) | No per-game rebuild of the runtime. Phase 0 proves the block survives Dist LTO on both compilers. Godot does the same job by compiling the key into a custom export template, which needs a rebuild. |
| Symbol renaming | Obfuscar run at export, with its config generated from the script metadata the engine already collects (user: Obfuscar; technical: generated config) | Obfuscar has no "skip subclasses of X" rule, only name, regex and visibility filters ([config docs](https://docs.lextudio.com/obfuscar/getting-started/configuration)). Generating explicit skips for each script class, lifecycle method and editor field keeps the by-name lookups working. |
| Obfuscar unavailable (offline, tool restore fails) | The export fails with a clear message when the project asked for renaming (recommended) | Never ships a game unprotected while claiming it is protected. The user can turn renaming off for that export. |
| `.cs` sources | Left readable (scope) | dotnet build and IDEs read them directly; see Open questions. |

## Part 0 — Where we are

| Capability | State | Evidence |
|---|---|---|
| Any encryption or obfuscation | ❌ None | `grep -i "encrypt\|cipher\|obfuscat"` over Core, Editor, Runtime and ScriptCore finds only a comment in `Social/DiscordSocial.cpp:97`. |
| Asset pack format | ✅ Working (export verified in spec 002) | `Serialization/AssetPackFile.h`: `FileHeader{ 'L','Z','A','P', Version = 4, BuildVersion }`, `IndexTable` with absolute `PackedOffset`s. Written by `AssetPackSerializer.cpp:34` (`FileStreamWriter`), read by `AssetPackSerializer.cpp:135` and `AssetPack.cpp:139/152/180` (`FileStreamReader`). |
| Game DLL inside the pack | ✅ Working | `IndexTable::PackedAppBinaryOffset/Size`, written at `AssetPackSerializer.cpp:56`, read at `AssetPack.cpp:175-183`. The export copies only engine and Coral libraries loose (`EditorLayer.cpp:4825-4834`, `RuntimeExport::ShipsLibrary`). |
| Game DLL at runtime | ⚠️ Leaks a plain copy | `ScriptEngine::LoadProjectAssemblyRuntime` writes it to `%TEMP%/LuxScriptShadow/<n>/App.dll` before loading it, because `LoadAssemblyFromMemory` "yields zero types": Coral enumerates types by `Assembly.Location` (comment at `ScriptEngine.cpp:252`). |
| Audio clips read in place from the pack | ✅ Working | `Audio/Native/NativePackVFS.cpp`: `RegisterPackedClip(name, pack, offset, size)` (line 186), `Open` uses `fopen` + `entry.Offset` (68-74), and `OnRead`/`OnSeek`/`OnTell` work on that window. |
| Shader pack format | ✅ Working | `Serialization/ShaderPackFile.h:69-72`: `FileHeader{ 'L','X','S','P', Version = 1 }`, no spare field. Read via `FileStreamReader` at `ShaderPack.cpp:53/97`, written at `:173`. The exported pack is written by `EditorLayer::WriteRuntimeShaderPack` (`RuntimeExportStep::ShaderPack`). The runtime finds it at `<export>/Assets/ShaderPack.lsp` (`RuntimeApplication.cpp:88-95`). |
| Runtime project file | ✅ Working | Binary, written by `ProjectSerializer.cpp:857` (`FileStreamWriter`), read at `:1132`. |
| File streams | ✅ Working | `Serialization/FileStream.h`: `FileStreamReader/Writer` with `ReadData/WriteData` and `Get/SetStreamPosition`. They are the one choke point for every pack read and write above. |
| Project YAML reads | ✅ Working | `Yaml::LoadFile` (`Serialization/Yaml.h:89`) is used across 33 files (72 calls). Some readers read text themselves and call `Yaml::Load(text)`. |
| Project YAML writes | ✅ Working, scattered | `FileSystem::WriteFileAtomically` in Scene, Prefab, Material, Sound, Project and the asset registry. Plain `std::ofstream` in `AudioSurfaceTableSerializer.cpp:35`, `DialogueTableSerializer.cpp:33` and `MeshSerializer.cpp:189/258/327`. |
| Script lookups by name | ✅ Working | `ScriptEngine::BuildAssemblyCache` keeps every `Lux.Entity` subclass by full name (scenes store these), all method names, and fields (public, or `Lux.ShowInEditorAttribute`). Lifecycle calls by string: `OnCreate`, `OnUpdate`, `OnFixedUpdate`, `OnDestroy`, `OnCollisionBegin/End`, plus `On…Internal` and static `Dispatch*` in ScriptCore. The starter template declares `OnCreate`/`OnUpdate` **private**. |
| Threads touching packs | ✅ Known | Runtime assets load on the asset thread (`RuntimeAssetManager::SyncWithAssetThread`); clips stream on the `AudioStreaming` thread through the VFS (`Threading.md`). |

Nothing here has been runtime-tested for obfuscation, since none exists.

## Part 1 — Design

**`Serialization/Obfuscation.h/.cpp` (NEW, Core):**
- `ObfuscationKey { std::array<uint8_t, 32> }`, with `Generate()` (OS random), `Fingerprint()`
  (8 bytes) and to/from hex for `.luxproj`.
- `void Apply(const ObfuscationKey&, uint64_t position, void* data, size_t size)`: XOR in place
  with the keystream at that absolute position. The keystream block is SplitMix64/xxHash-style over
  (key words, block index); this is mixing, not a cipher. Applying it twice gives back the input.
- The container header: `struct ObfuscatedHeader { char Magic[4] = {'L','X','O','B'}; uint16_t
  Version = 1; uint16_t Flags; uint64_t KeyFingerprint; uint8_t Reserved[16]; }`, 32 bytes. Also
  `bool Peek(path, header&)`.

**Streams:** `FileStreamReader` checks for `LXOB` when it opens. If the header is there, it needs
the key from a process-wide key ring, `Obfuscation::SetActiveKey`, which the runtime sets from its
patched block and the editor sets from the active project. It then hides the 32-byte header, so
positions stay what the file's own format expects, and descrambles each `ReadData` at
`32 + position`. `FileStreamWriter` gets an opt-in constructor flag that writes the header and
scrambles. Every format inside (LZAP, LXSP, the runtime project file) keeps its current offsets, so
no index math changes.

**Audio VFS:** `RegisterPackedClip` carries the pack's data-start (0 or 32) and whether it is
scrambled. `OnRead` descrambles at `dataStart + entry.Offset + cursor`. The work is per read call,
on the audio streaming thread, and pure.

**Text:** NEW `FileSystem::ReadText(path)` returns plain text whether or not the file is `LXOB`, and
`Yaml::LoadFile` uses it. NEW `ProjectFiles::WriteText(path, text)` writes through
`WriteFileAtomically`, scrambled when the active project has `Obfuscation.ProjectFiles` on. The
project write sites listed in Part 0 move to it.

**Key in the runtime (NEW):** `Lux-Runtime` holds a `static volatile` 64-byte block: a 16-byte magic,
a 16-byte "patched" marker and the 32-byte key, referenced at startup so LTO keeps it. The export
finds the magic in the copied executable and writes the key. A block left unpatched means no key,
which means only plain packs load.

**Symbol renaming (NEW export step):** after the DLL builds and before it is packed, the export
writes `obfuscar.xml` from `ScriptEngine`'s metadata:
- `SkipType` for each script class, with names kept;
- `SkipMethod` matching `^On(Create|Update|FixedUpdate|Destroy|CollisionBegin|CollisionEnd|TriggerBegin|TriggerEnd)$`;
- `SkipField` for each public or `[ShowInEditor]` field of a script class;
- `KeepPublicApi` on.

It runs Obfuscar as a dotnet tool restored into the project (`dotnet tool restore` from a NEW
tool manifest the editor writes only when renaming is on), then packs the renamed DLL. The fix for
the `%TEMP%` copy is a Coral change, so memory loads enumerate types; Phase 5 says how.

**Ownership and threads:** keys are value types. The key ring is written on the main thread at
project open or runtime start, before any asset or audio thread reads it, and is read-only after
that (`std::atomic` publish). No GPU resources or ECS state are involved.

**Serialization and compatibility:**
- `.luxproj` gains `Obfuscation: { Exports: bool, ProjectFiles: bool, RenameScripts: bool, Key: hex }`.
  Missing means all off. LuxSample stays off and the YAML oracle is unaffected.
- LZAP and LXSP versions don't change, because the container wraps them.
- Old exports (no `LXOB`) and plain projects read through the unchanged path.

## Part 2 — Phases

### Phase 0 — Prove the three risky assumptions
**Goal:** know that the transform is fast enough, that the patched block survives Dist, and that
Obfuscar plus Coral work on a real game DLL, before building on them.
**Changes:** throwaway, under `bin/`:
- a benchmark of `Apply` on 1 GiB;
- a Dist `Lux-Runtime` build, with a script that finds and patches the magic;
- Obfuscar on LuxSample's DLL with a hand-written generated-style config, packed and played in an export.
**Verification:**
- `Apply` runs at ≥ 2 GB/s on one core, so streaming and loading cost is negligible.
- The patched Dist exe reads back the key: Windows MSVC LTO, and Linux clang LTO in CI.
- The renamed DLL loads, every script registers (count matches the unrenamed DLL), and AudioDemo
  plays with the same behaviour.
**Exit:** all three pass, or the plan changes before Phase 1. Ideas if one fails: a key file
scrambled with an engine constant instead of patching; a different renamer.
**Rollback:** nothing is committed.

### Phase 1 — The transform and container exist and are tested
**Changes:** `Core/Source/Lux/Serialization/Obfuscation.h/.cpp` **NEW**; `Project.h`
`ProjectConfig::Obfuscation` **NEW**, serialized in `ProjectSerializer` (both directions, with
defaults when absent); a test executable **NEW** (`tests/obfuscation/`, CMake like
`tests/audio/native`): round trip, random-access equality, wrong-key fingerprint, header parse.
**Playbook:** none applies (no component, asset type or pass). New files → regenerate projects.
**Verification:** the new test passes in CI's Test phase (added as a job); Debug/Release/Dist build;
LuxSample's `.luxproj` loads and saves byte-identical.
**Docs:** Architecture § Serialization: the container and the key ring.

### Phase 2 — Obfuscated exports: asset pack and runtime project file
**Changes:**
- `FileStream.h/.cpp`: `LXOB` detection on read, opt-in scrambled write.
- `AssetPackSerializer`: write scrambled when `Obfuscation.Exports` is on.
- `NativePackVFS.cpp`: data-start plus descramble in `OnRead`.
- The runtime project write in `ProjectSerializer.cpp:857`.
- Export (`EditorLayer` runtime export): generate the key if missing; Project Settings › Build gets
  an **Obfuscate exported game** toggle.
- `Lux-Runtime`: the patched key block, and `Obfuscation::SetActiveKey` at start.
**Thread:** export on main. Reads happen on the asset and audio threads after the key is published.
**Verification:**
- Export AudioDemo with the toggle on. `AssetPack.lap` starts with `LXOB`, and `strings` finds no
  YAML keys, no `.lsound` text and no `RIFF`/`OggS` headers.
- The runtime plays for 30 s with music and streamed clips, with no load errors.
- An export with the toggle off is byte-identical to today's.
- An old export from before this phase still runs.
**Docs:** Architecture § Asset packs and § Audio (the VFS); `Building.md` § export.

### Phase 3 — Obfuscated exported shader pack
**Changes:** `WriteRuntimeShaderPack` writes through the scrambling writer when exports are
obfuscated. `ShaderPack` reads through `FileStreamReader`, so it detects `LXOB` with no change.
The editor's `Editor/Resources/ShaderPack.lsp` and its stamp stay plain.
**Verification:** the exported `ShaderPack.lsp` starts with `LXOB`; the Dist runtime starts and
renders with no shader errors; the Dist Editor still loads its plain pack.

### Phase 4 — Script symbols renamed at export
**Changes:**
- Export: the `obfuscar.xml` generator from `ScriptEngine` metadata, the tool manifest and
  `dotnet tool restore`, the run, and a clear failure message.
- Project Settings: **Rename script symbols** (needs exports obfuscated).
- The export report lists what was renamed and what was kept, with counts.
**Verification:**
- Export LuxSample with renaming on. A decompiler listing (`ilspycmd`, run in CI only) shows script
  class names and lifecycle names kept, and private helpers and locals renamed.
- In the runtime, every script registers and behaves the same.
- Offline, the export fails with the message and ships nothing.
**Docs:** the help page on exporting; `Building.md` (Obfuscar is optional, with its licence and size).

### Phase 5 — No plain DLL on disk at runtime
**Changes:** make `LoadAssemblyFromMemory` expose types. Coral.Managed is vendored source, so its
type enumeration changes to use the loaded `Assembly` object instead of `Location`. Then
`ScriptEngine::LoadProjectAssemblyRuntime` drops the `%TEMP%` staging when the pack is obfuscated.
**Verification:** the runtime with an obfuscated export writes nothing to `%TEMP%/LuxScriptShadow`
and scripts register. The editor's hot reload still works through its path-based load.
**Risk:** this is a vendored-library change, so record it in `premake5.lua`/`Building.md` like the
other Coral workarounds.

### Phase 6 — Opt-in project-folder obfuscation
**Changes:**
- `FileSystem::ReadText` and `Yaml::LoadFile` read containers transparently.
- `ProjectFiles::WriteText`, adopted by the write sites in Part 0 (Scene, Prefab, Material, Sound,
  SoundBank, DialogueTable, AudioSurfaceTable, Mesh asset YAML, asset registry).
- Project Settings: **Obfuscate project files**, with an apply/revert pass that rewrites every
  project YAML file atomically. Reuse the scan/apply pattern of `Audio/AudioLegacyCleanup`, with
  content checks before writing.
- `.luxproj` and `.cs` stay plain.
**Verification:**
- On a copy of LuxSample: turn it on; every `.luxscene`/`.lprefab`/`.lmat`/`.lsound` starts with
  `LXOB`; reopen, edit, save, undo; Play; export; turn it off and confirm the files are
  byte-identical to the start.
- LuxSample itself stays off and its YAML oracle (`tests/yaml/run.py`) is unchanged.
- ImGui: the toggle and the apply/revert confirmation have unique IDs, closed scopes and every
  state driven.
**Docs:** Architecture § Project; the help page on project settings.

### Phase 7 — Hardening
Linux runtime export (patch the ELF block, run in CI), Debug-AS on the transform tests, docs pass,
and the honest limits stated in the editor's help.

## Part 3 — Verification of the whole

- **Readable-bytes check:** on an obfuscated export, a CI script scans every shipped file for YAML
  keys, `RIFF`/`OggS`/`PNG`/`DDS` magics and the game's script class names. Each one found must be
  on an allow list (the engine's own DLLs, `Resources/`).
- **Behaviour check:** AudioDemo and Benchmark run the same obfuscated as plain (spec 002's runtime
  checks).
- **Compatibility:** a frozen pre-obfuscation export runs; LuxSample's files don't change.

## Part 4 — Risks

| Risk | Likelihood | Early detection |
|---|---|---|
| LTO strips or merges the key block | Medium | Phase 0 patches a real Dist build on both compilers. |
| Obfuscar breaks a by-name lookup we missed (reflection in game code, `nameof`, serialized events) | Medium | Phase 4 compares the registered-script count and runs the demos. The generated config is inspectable in the export folder. |
| Obfuscar has no stable net9 support (3.0 is beta; 2.2.50 is the latest stable) | Medium | Phase 0. Fall back to the stable tool if it reads net9 IL, or pin the beta. |
| Scrambling costs streaming time | Low | Phase 0 benchmark; profile streamed music in Phase 2. |
| A project file written by a path that skips `ProjectFiles::WriteText` stays plain | Medium | The Phase 6 apply pass reports plain files left after a save cycle; a grep check in CI for `std::ofstream` under project serializers. |
| Users treat it as security | High | The toggle and help say "stops casual copying; not encryption". |

## Part 5 — Open questions

- **`.cs` sources** (the main thing AI scrapers read): leave plain, or keep only scrambled copies
  and unscramble into an ignored build folder for dotnet? The second breaks IDE editing. User decision.
- **Git:** scrambled project files diff as noise. Should the editor offer a `.gitattributes`
  textconv helper? Follow-up.
- **Asset sources** (`.png`, `.wav`, `.gltf` in the project folder): out of scope as written. They
  are already protected in exports by the pack.
- **Key rotation:** changing the key re-scrambles the project (Phase 6 pass) and needs a new export.
  Is it needed at all?

## Part 6 — Research notes

- **Obfuscar config:**
  - `KeepPublicApi=true` with `HidePrivateApi=true` is the default since 2.2.0.
  - `SkipType` takes `name`, as a wildcard or a `^` regex, plus `skipMethods`/`skipFields`/….
  - `SkipMethod`/`SkipField` take `type`, `name`, `rx` and `attrib` (public/protected).
  - It honours `System.Reflection.ObfuscationAttribute`, and `MarkedOnly`.
  - It has no inheritance-based skip, which is why this plan generates per-class skips.
  - Source: [docs](https://docs.lextudio.com/obfuscar/getting-started/configuration).
- **Obfuscar distribution:** the `Obfuscar.GlobalTool` NuGet package. 2.2.50 is the latest stable;
  3.0.0-beta.19 deprecates relative paths and environment variables in configs, so the generated
  config uses absolute paths. Source: [NuGet](https://www.nuget.org/packages/Obfuscar.GlobalTool/3.0.0-beta.19).
  The project is MIT licensed.
- **Prior art, not re-verified this session:**
  - Godot compiles an export key into custom export templates, which needs a template rebuild.
    That is the reason for patching here instead.
  - Unreal encrypts `.pak` files with an AES key in the binary. That is real encryption, which
    the user ruled out.
- **Rejected:** shipping the key as a loose file next to the game. It is no harder to undo than
  the patched block, but it is trivially discoverable, and the user asked for it in the runtime.

## Handoff

Start with Phase 0. Implement each phase with `/dev`, review with `/cr`, and ship with `/send-pr`.
