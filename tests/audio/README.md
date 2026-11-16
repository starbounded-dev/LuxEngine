# Headless audio regression tests

The independent Soundwave contract checks run on Windows (an x64 MSVC developer prompt)
and Linux with a C++20 compiler, without audio SDKs, banks or a renderer:

```sh
python tests/audio/run_soundwave.py
```

They check loss/gain compatibility with the original scalar formulas, ordered reduction,
caps, path reset/reuse and material validation. This is a narrow library check, not the
full audio-suite or engine-build gate below. `OcclusionTests.cpp` additionally compares
every authored default against the original engine accessor and checks the engine adapter.

Windows can run `python tests/audio/run_occlusion_windows.py` from an x64 MSVC developer prompt
(after project generation). It compiles the production occlusion/settings/geometry/material units
and executes the same integration assertions.

`scripts/Lux-BuildTest.ps1` regenerates and builds Debug/Release/Dist, checks Core/Runtime/Editor
artifact timestamps (Editor is intentionally excluded in Dist), then runs both Windows checks.
`scripts/Lux-GoldenRun.ps1 -Label soundwave-check` captures the audio demo through the existing
renderer runner and rejects missing/stale artifacts or Vulkan validation output.

On Linux, regenerate `Core/Makefile` and build Debug Jolt and ImGui,
then run from the repository root:

```sh
python tests/audio/run.py
```

For Linux AddressSanitizer lifetime coverage, use the existing Ubuntu 26.04 CI environment
or a matching development machine:

```sh
LUX_RUN_AUDIO_TESTS=1 scripts/Linux-Build.sh debug-as
# Or, after generating/building Debug-AS dependencies:
python tests/audio/run_soundwave.py --sanitize-address
python tests/audio/run.py --config debug-as
```

Debug-AS uses its own Jolt/ImGui archives and instruments both production objects and test
hosts. The full runner rejects a generated Core configuration without sanitizer compile flags
and verifies ASan memory-access symbols in `AudioAcoustics.o`. The ordinary Debug command
retains its previous behavior. CI uploads separate Debug/Debug-AS audio logs; no leak/error
suppression is added. The pure-library tests need no engine SDKs, including with sanitization.
The real-worker Soundwave ownership checks run before the occlusion case, so a later failure does
not hide their result.

`run.py` requires `clang++` and the generated Core flags; it needs no audio SDK. It compiles production
code with Debug assertions and covers file-stream failures, asset-pack error propagation, real Jolt
worker contacts, the Soundwave ownership checks (Debug-AS), occlusion, and the music director's YAML.
Logs and artifacts are retained in the printed temporary directory; `--output /tmp/my-audio-tests`
selects a fresh directory. Managed payload/ABI/subscription tests run separately with
`python3 tests/audio/run_managed.py`.

Playback is tested by `tests/audio/native` (CMake, every platform, no device): Sounds and their node
trees, parameters and curves, limits, streaming, Sound Banks and the clip cache, streaming from a pack,
the mixer (buses, effects, reverb, snapshots, VCAs), accessibility, meters, music timelines and
transitions, dialogue clips, the voice budget, physics audio, and the project audio settings
(accessibility, dialogue, performance and surface tables). See its README.

The FMOD-hosted suites (surfaces, music, dialogue, accessibility, geometry, performance, platform,
and portal/source serialization) were removed with FMOD in November 2026. The behaviours native has
are covered by `tests/audio/native`; portal and source YAML round trips and the shutter/portal geometry
checks have no Linux runner yet (`specs/002-fmod-replacement/report.md`).

The Linux CI Debug build runs this same complete suite after its dependencies and before
Core/Editor compilation. Engine builds and final artifact verification still run afterward.
For that ordering locally, use `LUX_RUN_AUDIO_TESTS=1 ./scripts/Linux-Build.sh debug`.
Each compiler invocation checks its output's size and timestamp before the suite uses it.

The Linux occlusion case links its production dependencies with the same minimal logger-host
scope as the Windows check; other suite cases retain the full playback host.

The Phase 2 `SoundwaveSolveTests.cpp` independently exercises the CPU direct crossing walk and
analytic shutter clipping (including endpoints inside volumes), material/strength arithmetic,
invalid input, output aliasing and scratch reuse. `OcclusionTests.cpp` additionally compares the
new evaluator bit-for-bit with production AudioOcclusion on captured box triangles and transformed
portal opening states. It does not yet prove reflection/opening propagation or Scene integration.

`SoundwavePropagationTests.cpp` adds independent CPU image-source/edge-route checks: analytic
reflection gains, authored absorption/scattering, blocked legs, third-bounce traversal, thick
corners and off-axis doorways, sealed closed-door rejection, deterministic output and explicit
work/path limits. The independent runner builds all kernel sources and these checks without SDKs.
They do not replace Scene/Play/runtime verification for the completed Phase 2 integration.

The geometry checks also compare a static/moving `GeometryView` with flattened geometry,
including bitwise direct loss, combined BVH/edge indices, masks and moving-only scenes. The
propagation checks close an independently captured moving door against a static frame, remove
it through the exclusion list and replace its grid without rebuilding the room. Kernel settings
checks run the production YAML/runtime serializers: old blocks, nondefault values, all new
fields, strict type/range/version/key checks and transactional rejection. Scene/Play transport
and project authoring checks are described below; complete runtime/export and Scene corner/doorway
validation remain separate Phase 2 gates.


`AudioAcousticsTests.cpp` runs the production adapter with the real JobSystem, including blocked
worker publication/clear, detached scene lifetimes, latest-request coalescing, atomic geometry
layer migration, failed capture/retry and last-valid source results. It exercises source cadence,
budget fairness, smoothing, pruning and both worker/inline policies. The standard Windows and
Linux occlusion runners include these checks. Actual Scene render-mesh capture and audio output
are checked separately with `tests/rendering/golden_run.py --config debug --scenes AudioDemo --play`.
The optional demo Play artifact does not establish Sponza corner/doorway behavior or GPU parity.

The explicit native Scene cases use `python tests/audio/run_scene_acoustics.py --config debug`.
They load the actual Sponza render source, pin the listener/source, enable the existing physics-ray
oracle over all model submeshes and verify the gain Scene applies to the source. Cases cover a pillar
corner, an off-axis archway, its closed portal control and blocked/clear direct-only profiles.
Sponza has neighboring open arches: five authored partitions seal the tested bay at its sides,
back, floor and ceiling so the closed control tests a sealed doorway. The original Sponza arch
and pillar geometry remains present and determines the open route. Materials are explicitly
opaque and work/node limits are authored at their maximum; truncation stays visible in artifacts.
Without `--export-runtime`, these checks cover Play only. They do not prove performance,
all routes at default budgets or GPU parity.

Fixtures live under `bin/soundwave-scene-tests/<unique-run>/`; native capture evidence lives under
`bin/golden/<run>-<case>/`. The script copies the project/registry, scene, compiled script module
and Sponza/default model folders (the importer rewrites generated materials). Audio authoring
files are copied too, excluding generated Build directories; the committed regression banks
from `tests/audio/fixtures/banks` are copied into that fixture's Build/Desktop. Both desktop
profiles select that owned location. Other existing asset directories use links. The runner
checks original project/scene/registry/model and committed bank bytes on exit. Golden fixtures do
not enter recent-project history. Geometry counts come from the actual
glTF index accessors, including the sample cube’s 156 triangles; the portal uses the kernel’s
procedural twelve-triangle box. PyYAML is required only for this developer runner. Use `--cases`
to select named cases, or `--threading single` to exercise the inline policy. Native Editor
must already be built, along with the selected runtime when exporting. It works on a clean
checkout; each capture’s existing timestamp gate runs once.

Add `--audio-settings-ui` to run the shared Project Settings acoustic controls before the first
editor frame. The isolated ImGui context drives numeric text input and both comparison toggle
states, tests legacy Source independence, dirty results, all bounds, the direct-only profile and
YAML round-trip. The runner requires a fresh `<scene>.audio-settings-ui.txt` containing PASS.
No project file is edited. This exercises actual widgets in the native editor, but does not
claim a complete visual panel walkthrough. For example:

```sh
python tests/rendering/golden_run.py --config debug --label acoustic-authoring --scenes AudioDemo --play --audio-settings-ui
```

`SoundwaveInstanceTests.cpp` checks shared local meshes and virtual hierarchy/edge indices,
world-space trace/direct parity for mirrors, shear and nonuniform scale, deletion/source masks,
concurrent readers and transactional invalid transforms/material maps. Its large case retains one
6,144-triangle mesh and 359 top-level nodes for 1,105,920 logical triangles. Independent thick-corner,
translated reflection and open/closed moving-door checks exercise the propagation solver through
the instanced GeometryView. Scene now captures shared local meshes; broad scene/runtime/export validation remains a separate gate.

`SoundwaveReverbTests.cpp` checks the listener reverb measurement: deterministic repeats, a closed
marble room near Sabine's decay, a closed corridor shorter than that room, open corridor ends
shorter still, a carpeted room far drier, and open ground with no send at all.

The production adapter tests also detach a blocked job retaining a shared mesh owner, drop the
Scene/caller refs, and prove the owner survives until the worker finishes. Inline publication
checks two transformed instances share twelve stored triangles, reject an ownerless replacement
without losing the last valid snapshot, and release the mesh on a valid empty replacement.

The instance checks also preserve every render primitive ID under an explicit retention policy,
keep a very small valid face ray-visible, count exact local/world-zero faces, reject unsupported
policies transactionally, and assert that collapsed primitives produce neither false paths nor
invalid/divide-by-zero floating operations. The original strict geometry tests remain unchanged.
Linux compilation and focused occlusion linking derive kernel objects from the same source list.

To exercise the real export and packaged player as well as Play:

```sh
python tests/audio/run_scene_acoustics.py --config debug --export-runtime --cases corner doorway-open doorway-closed direct-blocked direct-clear
```

`--runtime-config` selects the exported Debug/Release/Dist binary (default: `--config`).
The fixture registry contains only its copied scene and no unrelated sample prefabs; export
still performs its full project audio validation. ScriptModulePath is empty because the fixture
has no script components. The ordinary exporter writes assets, binary project, runtime,
resources and shader pack into a unique empty test destination. It then launches that exact
package and checks the same applied gain and geometry/oracle controls, plus raw gain,
direct float-bit and opening-path parity with Play. Reports/logs live beside the golden artifacts.
The optional native report is inert without LUX_RUNTIME_AUDIO_REPORT and advances through
normal frames, never waiting on jobs or GPU. Native builds and fresh timestamp checks remain
separate gates. Passing this check establishes only the implemented reference path.

In Linux Debug-AS, `AudioAcousticsTestMain.cpp` also runs the existing production adapter
worker/inline ownership cases in an executable with no legacy SDK dependency. Its production
objects retain AddressSanitizer instrumentation. This runs before the complete instrumented
occlusion/audio suite, whose SDK leak failures remain failures with no suppression. A focused
lifetime pass alone does not establish a passing full Debug-AS engine/audio gate.

Runtime acoustic completion uses a staged `.status.tmp` file atomically replaced onto the
final `.status` path after the report is written. The runner checks fresh report/status files
and normal process exit; it does not require a console message. Dist omits the console sink,
and logging preferences cannot determine whether acoustic verification passed. Publication
failure remains a missing/stale-marker failure, with native errors reported through the normal
tagged logger.

Linux package launches set LD_LIBRARY_PATH to the package's `lib/` only. Rebuild Lux-Runtime
after project regeneration so its post-build shared-library staging has run; normal export must
carry that staged closure. Inherited Assimp or Vulkan-SDK paths cannot mask an incomplete package.
For Linux Debug package checks, the runner gives Vulkan an isolated copy of the installed SDK's
Khronos validation manifest with its DSO path made absolute. Validation remains enabled and all
other manifest settings are preserved; the SDK itself is unchanged. LD_LIBRARY_PATH still names
only the package's lib directory. Release/Dist use their normal layer-free launch configuration.
