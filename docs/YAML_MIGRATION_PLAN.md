# LuxEngine YAML Migration Plan (yaml-cpp → rapidyaml)

Replaces yaml-cpp with rapidyaml (ryml) as the engine's YAML library, to cut editor load times on
large scenes without changing what any file means. It is a planning document, not a description of
what exists — read `.claude/docs/Architecture-LuxEngine.md § 2.13 Serialization` for what is
actually built.

**Goal card**

- **Goal:** yaml-cpp is gone; every human-readable asset is read and written through rapidyaml.
- **Success:** the Benchmark scene opens measurably faster than the yaml-cpp baseline, **measured
  without Tracy**; every YAML asset in the repo loads to identical values; the scene round-trip
  self-test and undo/redo pass; Editor, `Lux-Runtime`, and the Linux audio tests build.
- **Output style — the user's decision "C":** same layout and key order, inline `[x, y, z]`
  vectors, block maps; floats written as the **shortest exact** form (`2.2`, not `2.20000005`). The
  one-time float reformat lands as its **own commit**.
- **Non-goals:** changing any file format or key; binary editor scenes; the other scene-open costs
  (entity creation pass, undo baseline, first-frame mesh build).
- **Constraints:** vendored permissive library through `Dependencies.lua`; no new install for game
  makers; project regeneration whenever files change; every phase builds, runs, and ships.

**Decisions this plan is built on**

| Decision | Choice | Consequence |
|---|---|---|
| Replace yaml-cpp | Yes (user, 2026-09-27) | One YAML library at the end; yaml-cpp deleted in Phase 6. |
| Saved-file style | C — hybrid (user) | Readable, exact floats; one reformat commit; old engine builds still read the output. |
| Call sites use ryml directly vs an engine wrapper | **Engine wrapper `Lux::Yaml`** | ~960 call sites port near-mechanically; conversions live in one place; ryml's pre-1.0 API churn (v0.16.0 deprecated its `<<`/`>>` serialization) stays in one file. Must remain a thin view over ryml's `ConstNodeRef` or the speed is lost. |
| Packaging | Single-header amalgamation + one implementation `.cpp` | Fewest vendored files; same `NoPCH` treatment yaml-cpp gets today. |
| Version | Pin v0.16.0, no `RYML_WITH_LEGACY_OPERATORS` | Current release; no deprecated APIs adopted. |
| Coexistence | Both libraries from Phase 1 through Phase 5 | Every phase shippable; each file ported and verified on its own. |
| Lenient reads | Wrapper `as<bool>` accepts yaml-cpp's spellings (`true/True/TRUE/yes/on/y`, …); writes `true`/`false` | Hand-edited files keep loading. |
| Parse errors | Wrapper installs ryml callbacks that report and throw a wrapper exception | A malformed file must produce an error, never a crash (see Open questions). |

---

## Part 0 — Where we are

| Capability | State | Evidence |
|---|---|---|
| yaml-cpp vendored and compiled into Core | ✅ builds (all configs, Windows) | `Core/vendor/yaml-cpp` (502 KB, plain directory, not a submodule); sources listed in `Core/premake5.lua:55-57`; `NoPCH` filter at `Core/premake5.lua:95`; `Dependencies.lua` `YAML_CPP` (include dir only) |
| Usage footprint | ✅ counted | 32 non-vendor files, ~960 `YAML::` uses. Largest: `Scene/SceneSerializer.cpp` (414), `Project/ProjectSerializer.cpp` (175), `Asset/MaterialSerializer.cpp` (46), `Asset/MeshSerializer.cpp` (37). Also: `Audio/AudioAccessibilitySettings`, `DialogueTable`, `AudioPerformanceSettings`, `AudioSurfaceTable`, `AcousticMaterial`, `AudioOcclusionSettings`, `AudioAccessibility`; `Tiering/TieringSerializer`; `Project/UserPreferences`; `Platform/Vulkan/ShaderCompiler/VulkanShaderCache`; `Asset/AssetManager/EditorAssetManager`; `Editor/PanelManager`; `Core/ApplicationSettings`; `Asset/PrefabSerializer`; `Lux-Runtime/src/RuntimeApplication.cpp`; `tests/audio/*` |
| yaml-cpp types in headers | ✅ read | `Scene/SceneSerializer.h` (`SerializeToYAML(YAML::Emitter&)`, forward `namespace YAML`); `Audio/AcousticMaterial.h:101-102`; `Audio/AudioAccessibilitySettings.h`, `AudioPerformanceSettings.h`, `AudioOcclusionSettings.h`; `Utilities/SerializationMacros.h` (`LUX_SERIALIZE_PROPERTY`, `LUX_DESERIALIZE_PROPERTY`) |
| Custom conversions | ⚠️ duplicated | `YAML::convert<glm::vec2/3/4>` and `convert<Lux::UUID>` + `operator<<` in `SceneSerializer.cpp:28-135`; **`convert<glm::vec3>` defined again** in `ProjectSerializer.cpp:20` (the same explicit specialization in two TUs); `convert<std::vector<uint32_t>>` in `MeshSerializer.cpp:26` |
| Current output format | ✅ sampled | `Assets/Scenes/FMODDemo.luxscene`: block maps, `[1, 1, 1]` flow vectors, floats padded to 9 significant digits (`0.00800000038`); 23,671 such floats across the sample scenes |
| Linux audio tests | ⚠️ fragile | `tests/audio/run.py:74-200` text-splices `SceneSerializer.cpp` by string markers and links yaml-cpp objects from `bin-int/Release-linux-x86_64/Core` |
| Scene round-trip self-test | ✅ exists | `SceneSerializer::RunRoundTripSelfTests`, run from Renderer Debugger (`RendererDebuggerPanel.cpp:1021`); requires snapshot → restore → snapshot byte-identical |
| Undo snapshots | ✅ working (verified in editor 2026-09-27) | `SerializeEntitySnapshots` (full + subset), `ApplyEntitySnapshots`, `DeserializeFromSnapshots`; undo diffs compare emitted strings, so the writer must be deterministic |
| Benchmark open cost | ✅ measured, **traced only** | Tracy, Release: 24.1 s → 11.1 s after the const-lookup fix (**uncommitted as of this plan**); of the 11.1 s, `YAML::Load` 3.6 s + YAML tree destruction 2.8 s. Standalone yaml-cpp parse of the 3.8 MB file: 513 ms. **No untraced number yet.** |

---

## Results so far

**Phase 0 — done (2026-09-27, `9d3bd66a`).** Untraced baseline, Release, `LUX_TRACK_MEMORY` on,
three cold opens of Benchmark: **3578 / 3571 / 3516 ms, median 3.57 s**. Re-opening an already
loaded scene asset takes ~430 ms, so ~3.1 s of a cold open is reading the file (parse + entity
build). The traced 11.1 s figure was mostly Tracy's per-allocation overhead. The oracle commits a
per-file hash manifest (`tests/yaml/reference.txt`) rather than full dumps (Benchmark alone would
be many MB); `run.py --dump` produces full dumps on demand. Verified to accept a value-neutral
reformat and to reject a value change, a string change, and a key-order swap. Fixing the scene-open
log line also exposed that tagged logs with an unconfigured tag were silently dropped (`c3de24e7`).

**Phase 1 spike — go (2026-09-27).** Standalone, v0.16.0 single header (1,751,073 bytes, 1.7 MB):
- **Correctness:** all 188 tracked YAML files read to identical values as yaml-cpp.
- **Speed:** Benchmark parse + free, 5 runs: yaml-cpp median **618 ms**, rapidyaml median **38 ms**
  (~16×). In-engine the gap should widen (yaml-cpp's allocations are tracked); confirm in Phase 3.
- **Errors:** the default callbacks `abort()` unless `RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS` is
  defined. With custom throwing `error_basic` / `error_parse` / `error_visit` callbacks, bad
  indentation, unclosed `[` and quotes, tabs, and stray colons all became catchable errors with a
  line number. `ErrorDataParse::ymlloc.line` is already 1-based.
- The spike was not vendored; Phase 1's in-repo vendoring was folded into Phase 2 (user's call).

**Phase 2 — done (2026-09-27).** Vendored `Core/vendor/rapidyaml/` (`ryml_all.hpp`, `ryml.cpp` —
the single implementation unit, `NoPCH` — and `LICENSE.txt` with the MIT/BSD-2 notices copied from
the header); `Dependencies.lua` `RapidYAML` entry; projects regenerated (`Win-GenProjects.py --last`).
**NEW** `Serialization/Yaml.h/.cpp` (`Lux::Yaml`): rapidyaml stays out of the header; a `Node` holds
a `Ref` to its document; yaml-cpp semantics kept (key-exists truthiness, lenient bools, integer
keys on maps, `Dump()` = value only); writer emits style C (`[1, 2.5, -3]` via `FLOW_SL|FLOW_SPC`,
shortest floats, null-like strings quoted); error handlers installed lazily via `std::call_once`.
`tests/yaml/run.py` now also builds the oracle over `Lux::Yaml`: all 188 files match the reference,
all 188 survive parse → `Yaml::Writer` → re-parse, and the self-test passes (semantics, conversions,
quoting, determinism, malformed input throws). Windows Debug/Release build; Linux not built here.
`Building.md` needed no change (it lists no vendored libraries). The in-editor parse measurement
moves to Phase 3, the first phase where engine code calls the wrapper.

**Phase 3 — code done, awaiting in-editor checks (2026-09-27).** `SceneSerializer` and
`PrefabSerializer` run on `Lux::Yaml`; `SceneAssetSerializer` needed no change. Untraced, same
protocol as Phase 0: **261 / 265 / 271 ms, median 0.27 s vs 3.57 s (~13×)**. The startup logs match
the baseline line for line (no new errors or warnings). Parsing was only ~0.6 s of the old open;
the rest of the saving is presumably yaml-cpp's node lookups and stringstream conversions during
entity build (inferred, not profiled). Deviations from the plan:
- `Node::SetScalar` added: prefab override detection edits parsed values in place (entity-reference
  remapping). A variant prefab now appends `BasePrefab:` as text instead of re-emitting a tree.
- Undo restore no longer re-emits and re-parses the snapshot blocks; the parsed blocks go straight
  to `DeserializeEntities`.
- `SerializationMacros.h` moves to Phase 4: its only user is `VulkanShaderCache` (batch 2).
- The `tests/audio/run.py` splice markers had been broken since `5c19e87e` (const reads); fixed
  here along with the port. All 14 markers verified against the source; not run on Linux here.
- **Found after the commit (fixed in Phase 4):** yaml-cpp wrote `uint8_t` as a character, so the 61
  mesh colliders in the sample scenes store `CollisionComplexity: ""`. `2e5c62ee` read that as
  invalid and fell back to `Default`. The value oracle could not see it (it compares generic YAML,
  not what a typed `as<T>` makes of it). `Lux::Yaml` now reads 8-bit values as a number or as the
  legacy character, and the Writer refuses 8-bit types so every caller casts explicitly.

## Part 1 — Design

- **`Lux::Yaml`** — **NEW** `Core/Source/Lux/Serialization/Yaml.h/.cpp`. Owns every ryml include.
  - `Yaml::Document` — owns a `ryml::Tree` parsed with `parse_in_arena` from a file or string.
  - `Yaml::Node` — non-owning view (`ConstNodeRef` + tree): `operator[](key)` (missing → invalid
    node, **never allocates**), `IsMap/IsSequence/IsDefined`, `explicit operator bool`, `size()`,
    iteration, `as<T>()` / `as<T>(fallback)`.
  - `Yaml::Writer` — builds a tree and emits: `BeginMap/EndMap/BeginSeq/EndSeq/Key/Value`, a
    `Flow` marker for vectors, `c_str()`/`str()`.
  - Conversions, in one header: bool (lenient read), integers incl. `uint64_t` UUIDs, float/double
    (shortest round-trip), `std::string`, `glm::vec2/3/4/quat`, `UUID`, `std::vector<uint32_t>`.
  - `Yaml::Exception` thrown from the installed ryml error callbacks.
- **No behaviour moves between threads.** Parsing happens where it happens today (main thread for
  scene open and undo, asset thread for asset loads). Trees are per-call locals; the ryml callbacks
  are installed once at startup, before any asset thread starts.
- **Lifetime:** a `Yaml::Node` must not outlive its `Document`. Call sites that parse then iterate
  (e.g. `ApplyEntitySnapshots`) keep the `Document` in scope for the whole iteration.
- **Serialization compatibility:** reads are value-equivalent (proved by the corpus oracle, Part 3);
  writes change float spelling once; key names and order are unchanged; old engine builds still read
  the new output.
- **Runtime / Dist:** `Lux-Runtime` only reads a settings file through YAML
  (`RuntimeApplication.cpp:45-56`); asset packs are binary and untouched. Dist has no special YAML
  path.
- **Linux:** ryml is portable C++11; `tests/audio/run.py` must be updated in the phase that ports
  `SceneSerializer.cpp`.
- **Build:** new files → regenerate projects (`scripts\Win-GenProjects.bat`,
  `scripts/Linux-Build.sh`). New `Dependencies.lua` entry. ryml's implementation `.cpp` joins the
  `NoPCH` filter like yaml-cpp's sources.
- **Size:** yaml-cpp is 502 KB of source; ryml's amalgamated header size is recorded in Phase 1.

---

## Part 2 — Phases

### Phase 0 — Baseline and value oracle

**Goal:** a trustworthy "before" number, and a machine check that two parsers read identical values.

**Changes**
- Commit the pending const-lookup fix in `SceneSerializer.cpp` (`DeserializeEntities`) after the
  user's save-without-changes check shows no diff in `Benchmark.luxscene`.
- `Editor/Source/EditorLayer.cpp` — `OpenScene`: log elapsed time (`LUX_CORE_INFO_TAG("Editor", …)`).
- **NEW** `tests/yaml/Oracle.cpp` (+ a small build script): loads every YAML asset in the repo
  (`*.luxscene`, `*.luxproj`, `*.lmat`, prefabs, `AssetRegistry.lzr`, the shader cache registry,
  audio tables) and writes a normalized dump — scalars parsed to typed values, key order kept.

**Verification**
- Build: Release. Run: open Benchmark three times **without Tracy**, `LUX_TRACK_MEMORY` on; record
  the median. Oracle dump of the current tree committed as the reference.

**Exit criteria:** baseline median recorded in this document; reference dump committed.
**Rollback:** revert the timer and oracle; no engine behaviour changed.

### Phase 1 — Spike: vendor ryml, go / no-go

**Goal:** know, with numbers, whether ryml delivers before any call site changes.

**Changes**
- **NEW** `Core/vendor/rapidyaml/` — `ryml_all.hpp` (v0.16.0), `LICENSE.txt`, `ryml.cpp`
  (`#define RYML_SINGLE_HDR_DEFINE_NOW` + include).
- `Dependencies.lua` — `RapidYAML = { IncludeDir = "%{wks.location}/Core/vendor/rapidyaml" }`.
- `Core/premake5.lua` — add `vendor/rapidyaml/**.hpp` and `vendor/rapidyaml/ryml.cpp` to `files`;
  add `files:vendor/rapidyaml/ryml.cpp` to the `NoPCH` filter at line 95.

**Playbook:** Architecture Part 4 *Add a new dependency* — `Dependencies.lua` entry, no manual
`links`/`includedirs`, then regenerate.

**Verification**
- Build: regenerate projects; Debug + Release on Windows; Linux build.
- Oracle run through ryml (a throwaway reader) → dump identical to the Phase 0 reference.
- Numbers: Benchmark parse standalone and in-engine, untraced.
- Error path: parse a deliberately malformed `.luxscene`; the process must report, not abort.

**Exit criteria / kill switch:** oracle mismatches that the wrapper cannot correct, or parse time
not meaningfully below yaml-cpp's in-engine → stop, delete `vendor/rapidyaml`, keep yaml-cpp.
**Docs:** `Building.md` — the new dependency. **Rollback:** delete the vendor dir and the three
build-file edits.

### Phase 2 — The `Lux::Yaml` wrapper

**Goal:** a tested YAML API the engine owns, emitting style C, with no call site ported yet.

**Changes:** **NEW** `Core/Source/Lux/Serialization/Yaml.h/.cpp` as in Part 1; installs the ryml
error callbacks from `Application` init, before the asset thread starts.

**Verification:** oracle through the wrapper = reference; emit a sample of every value kind and
check style C (inline vectors, shortest floats, block maps); emitting the same tree twice is
byte-identical; malformed input throws `Yaml::Exception`.
**Docs:** `Conventions.md` — `Lux::Yaml` as the helper to use for YAML (replaces the line 317
mention of the yaml-cpp helpers). **Rollback:** delete the two files.

### Phase 3 — The scene path

**Goal:** scenes, prefabs, and undo run on ryml; the load-time win is real and measured.

**Changes**
- `Scene/SceneSerializer.h/.cpp` — every `YAML::` use → `Lux::Yaml`; `SerializeToYAML` signature
  takes `Yaml::Writer&`; the local `convert<>` / `operator<<` blocks (lines 28-135) deleted in
  favour of the wrapper's conversions.
- `Asset/PrefabSerializer.cpp`, `Asset/SceneAssetSerializer.cpp` — follow the new signatures.
- `Utilities/SerializationMacros.h` — macros expand to `Lux::Yaml` calls (same names, same
  semantics).
- `tests/audio/run.py` — update the text-splice markers and link lists for the ported serializer.

**Verification**
- Build: Windows Debug/Release, Linux; `run.py` audio tests pass.
- Run: `RunRoundTripSelfTests` passes; open Benchmark and FMODDemo; material change + undo/redo,
  gizmo move + undo/redo, delete-with-children + undo/redo; Debug build shows no "outside its
  declared scope" or "diverged" errors.
- Numbers: Benchmark open, untraced, median of three vs the Phase 0 baseline.
- Oracle: all scenes and prefabs equal the reference **after load**.

**Exit criteria:** all of the above; saved scenes use style C and reload to identical values.
**Docs:** `Architecture-LuxEngine.md § 2.13` — "YAML via `Lux::Yaml` (rapidyaml)" for scenes.
**Rollback:** revert the phase commit; yaml-cpp is still present.

### Phase 4 — Every other serializer

**Goal:** no engine code outside `Lux::Yaml` includes yaml-cpp.

Three batches, each built, oracle-checked, and committed on its own:
1. `ProjectSerializer.cpp` (drops its duplicate `convert<glm::vec3>`), `TieringSerializer.cpp`,
   `UserPreferences.cpp`, `ApplicationSettings.cpp`, `Editor/PanelManager.cpp` — plus the audio
   settings the project file embeds (`AcousticMaterial`, `AudioAccessibility*`,
   `AudioPerformanceSettings`, `AudioOcclusionSettings`) and their `tests/audio` users, since
   `ProjectSerializer` passes its writer and nodes straight into them.
2. `MaterialSerializer.cpp`, `MeshSerializer.cpp` (its `convert<std::vector<uint32_t>>` moves to
   the wrapper), `EditorAssetManager.cpp` (asset registry), `VulkanShaderCache.cpp`.
3. The audio tables (`AudioSurfaceTable`, `DialogueTable`), `Lux-Runtime/src/RuntimeApplication.cpp`,
   and the rest of `tests/audio/*`.

**Verification per batch:** build Windows + Linux; oracle equal for the batch's file types; launch
the editor and open, edit, save, and reopen a project, a material, and the audio settings; Lux-Runtime
starts. **Rollback:** revert the batch commit.

### Phase 5 — The reformat commit

**Goal:** every committed sample asset is in style C, in one commit that contains nothing else.

**Changes:** re-save all YAML assets under `Editor/LuxSampleProject` through the new writer.
**Verification:** oracle dump before and after is identical; the diff contains only float spellings
and whitespace. **Rollback:** revert the single commit.

### Phase 6 — Remove yaml-cpp

**Goal:** one YAML library.

**Changes:** delete `Core/vendor/yaml-cpp`; remove `YAML_CPP` from `Dependencies.lua`; remove
`Core/premake5.lua:55-57` and the yaml-cpp term of the `NoPCH` filter; remove the yaml objects
from `tests/audio/run.py`; regenerate projects.
**Verification:** grep finds no `yaml-cpp`/`YAML::`; Windows Debug/Release/Dist and Linux build;
editor and runtime smoke test; audio tests pass.
**Docs:** `Architecture-LuxEngine.md § 2.13`, `Conventions.md:112` (the `NoPCH` list), and
`Building.md` no longer mention yaml-cpp. **Rollback:** restore the directory and build entries
from the previous commit.

---

## Part 3 — Verification of the whole

- **Corpus oracle** (Phase 0) is the correctness gate for every phase: same values, same key order.
- **Round-trip self-test** and **undo/redo** in the editor gate the scene path.
- **Untraced median scene-open time** (Phase 0 protocol) is the success number for the goal.

## Part 4 — Risks

| Risk | Likelihood | Detection |
|---|---|---|
| ryml aborts on malformed input | Plausible | Phase 1 malformed-file test before any port |
| Lenient-read differences (bools, numbers) change loaded values | Medium | Oracle, every phase |
| Writer non-determinism breaks undo diffs | Low | Round-trip self-test; emit-twice check in Phase 2 |
| `run.py` text splices break | Certain | Fixed in Phase 3; markers checked against the source |
| ryml pre-1.0 API churn on upgrade | Likely over time | Confined to `Lux::Yaml` |
| Speed-up smaller than expected untraced | Unknown | Phase 0 + Phase 1 numbers, kill switch |

## Part 5 — Open questions

- **Untraced baseline** — the whole payoff rests on it; Phase 0 measures it.
- **ryml error handling** — whether the default callback aborts, and whether throwing from the
  callback is supported as documented; Phase 1 settles it with the malformed-file test.
- **Float formatting** — confirm ryml's shortest round-trip output for `float` under C++20 matches
  what the oracle reads back; Phase 2.
- **Amalgamated header size and compile time** — recorded in Phase 1.

**What would invalidate the plan:** an untraced baseline where YAML parsing is a small share of
scene-open time. Then the migration buys little, and the plan should stop after Phase 1.

## Part 6 — Research notes

- ryml: MIT, C++11, single-header amalgamation available; README claims ~30× faster parse and
  ~150× faster emit than yaml-cpp — to be confirmed on Benchmark in Phase 1.
  [README](https://github.com/biojppm/rapidyaml)
- v0.16.0 (2026-07-22) deprecated `.to_val()` tree building and the `<<`/`>>` serialization
  operators; custom `read()` returns a result type. Informs the wrapper and version-pin rows.
  [releases](https://github.com/biojppm/rapidyaml/releases)
- Float precision depends on `to_chars` / `c4::fmt::real`; full precision needs C++17 or later
  (engine is C++20). [docs](https://rapidyaml.readthedocs.io/v0.9.0/doxygen/group__doc__serialization.html)
- Parsed flow/block style is preserved on re-emit; the writer must set flow style explicitly for
  vectors. [DeepWiki](https://deepwiki.com/biojppm/rapidyaml/5-serialization-and-formatting)
- yaml-cpp forces high float precision on output, the source of `2.20000005`-style values.
  [yaml-cpp #106](https://github.com/jbeder/yaml-cpp/issues/106)
