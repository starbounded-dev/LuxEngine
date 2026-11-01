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
and executes the same integration assertions without the full FMOD playback test host.

`scripts/Lux-BuildTest.ps1` regenerates and builds Debug/Release/Dist, checks Core/Runtime/Editor
artifact timestamps (Editor is intentionally excluded in Dist), then runs both Windows checks.
`scripts/Validate-AudioBanks.ps1` uses the production validator's separate NOSOUND bank catalog host.
It does not collect project references; editor/export validation remains responsible for those.
`scripts/Lux-GoldenRun.ps1 -Label soundwave-check` captures the audio demo through the existing
renderer runner and rejects missing/stale artifacts or Vulkan validation output.

On Linux, regenerate `Core/Makefile` and build Debug Jolt and ImGui,
then run from the repository root:

```sh
python tests/audio/run.py
```

Requires `clang++` and the repository's FMOD and VA SDKs (including the `LUX_FMOD_SDK` and
`LUX_VA_SDK` overrides used by CI). `fmodstudiocl` is required only when generating banks;
`FMODSTUDIOCLI` may select its executable path. YAML implementation objects are compiled afresh. The runner compiles production
code with Debug assertions, creates a disposable copy of the sample FMOD project in `/tmp`, and
uses FMOD's no-sound output. It does not alter the sample project or require a renderer/audio device.
Logs and artifacts are retained in the printed temporary directory. `--output /tmp/my-audio-tests`
selects a fresh directory. Use `--banks /path/to/Build/Desktop` to reuse fixture banks instead of
running FMOD Studio; those banks must contain `event:/SurfaceHit` (one-shot) and
`event:/SurfaceMotion` (continuous), with local `Speed`, `Weight`, `Surface`, and `Impulse`
parameters ranging from 0 to 10000. SurfaceMotion also needs a local LocalState parameter with Quiet/Loud labels. `Master.bank` and `Master.strings.bank` are loaded. Music tests additionally require 2D
`MusicBed`/`MusicOther` continuous events and `MusicStinger` one-shot, as authored by
`AuthorFixture.js`: State labels Explore/Combat, Intensity and Layer_Drums (0–1), tempo 240,
Cue and Section:Verse markers, and a two-second timeline loop. Use a fresh fixture after changing
that script.

Checks cover surface-table YAML/defaults/invalid input, impact cooldowns, compound-contact
coalescing, footstep parameters, pause, event-type validation, bank reload, fade completion,
entity cleanup, real Jolt worker contacts, masses/impulse estimates, solid/sensor transitions in both directions, body removal/sleep,
file-stream failures, packed surface-table truncation and asset-pack error propagation. Only application host setup and failing
asset serializers are substituted; playback/contact implementations and SDK calls are real.

Music checks cover state/intensity/layers without restart, stinger type validation, all queued
transition boundaries, main-thread callbacks, stale notification batches, callback reentrancy,
pause/resume, bank invalidation/recreation, fades, and destruction. `AudioTestHost.h` supplies the
shared minimal FMOD host; the production director and event wrapper are linked unchanged.

Accessibility checks use Music/SFX/Dialogue buses created by `AuthorFixture.js`; regenerate older
fixtures. They cover default/invalid/bounded configuration, player preference replacement/reload,
mono/compressor DSPs, category gain independent of authored volume, narration ducking, localized
captions, moving cue direction, opt-in descriptions, bank/system recreation and teardown. The production ImGui overlay/menu is
also exercised headlessly for draw output, clipping and explicit newline limits; build Debug ImGui
alongside Debug Jolt before running. Managed payload/ABI/subscription
tests run separately with `python3 tests/audio/run_managed.py`.

Geometry checks use the real VA world and worker completion, including transform/material updates,
world-bound growth, static capture, queue budgets/coalescing, removal, nearly open shutters, invalid
transform recovery, and normalized portal room leakage. Production portal and mesh YAML blocks
are exercised for round-trip fields, room IDs, legacy defaults and malformed values. Managed tests
also cover portal dispatch, acoustic mode enum values, invalid inputs and main-thread guards.
Scene/prefab hierarchy remapping lives in `SceneSerializer::RunRoundTripSelfTests`; it requires a
full engine host and is not executed by this headless runner.

Performance checks cover real FMOD voice priority and a four-real-voice cap with virtualization,
distance culling/re-entry, multi-listener attenuation targets, discarded one-shots, pause/stop and
parameter/timeline preservation, actual bus input peak/RMS and voice warnings, memory availability,
strict/bounded budget persistence and independent catalog validation (missing events/buses, wrong
event kinds, corrupt banks). Source YAML checks execute the production serialization blocks for
priority/culling defaults, bounds and runtime-state exclusion. Managed checks cover source budget
controls and main-thread guards. These tests use NOSOUND and do not replace listening/visual checks.

Desktop platform checks cover enabled/disabled host selection, old-project defaults, invalid
profiles, YAML round trips, effective runtime budgets, explicit Studio target argument quoting,
and real FMOD output mute without changing authored bus mute or scripted event pause.

Run `python3 tests/audio/run_sdk_layout.py` to exercise Premake's Windows/Linux SDK overrides,
paths with spaces, and each missing header/link/runtime file. It uses disposable empty files to
validate discovery rules, not a Windows compilation or hardware test. Console checks are on hold.

CI uses `--banks tests/audio/fixtures/banks` to run the complete suite without installing FMOD Studio.
See that fixture directory's README for provenance and regeneration.

Dialogue programmer-sound tests use `event:/Character/Dialogue` and the `welcome`/`goodbye`
audio-table keys authored into the fixture Master bank. They exercise real FMOD callbacks and
source duration with existing sample audio, without depending on optional FMOD SDK example banks.

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
Linux occlusion runners include these checks. Actual Scene render-mesh capture and FMOD output
are checked separately with `tests/rendering/golden_run.py --config debug --scenes FMODDemo --play`.
The optional Play artifact does not establish Sponza corner/doorway behavior or GPU parity.

Add `--audio-settings-ui` to run the shared Project Settings acoustic controls before the first
editor frame. The isolated ImGui context drives numeric text input and both comparison toggle
states, tests legacy Source independence, dirty results, all bounds, the direct-only profile and
YAML round-trip. The runner requires a fresh `<scene>.audio-settings-ui.txt` containing PASS.
No project file is edited. This exercises actual widgets in the native editor, but does not
claim a complete visual panel walkthrough. For example:

```sh
python tests/rendering/golden_run.py --config debug --label acoustic-authoring --scenes FMODDemo --play --audio-settings-ui
```

`SoundwaveInstanceTests.cpp` checks shared local meshes and virtual hierarchy/edge indices,
world-space trace/direct parity for mirrors, shear and nonuniform scale, deletion/source masks,
concurrent readers and transactional invalid transforms/material maps. Its large case retains one
6,144-triangle mesh and 359 top-level nodes for 1,105,920 logical triangles. Independent thick-corner,
translated reflection and open/closed moving-door checks exercise the propagation solver through
the instanced GeometryView. Scene now captures shared local meshes; broad scene/runtime/export validation remains a separate gate.

The production adapter tests also detach a blocked job retaining a shared mesh owner, drop the
Scene/caller refs, and prove the owner survives until the worker finishes. Inline publication
checks two transformed instances share twelve stored triangles, reject an ownerless replacement
without losing the last valid snapshot, and release the mesh on a valid empty replacement.

The instance checks also preserve every render primitive ID under an explicit retention policy,
keep a very small valid face ray-visible, count exact local/world-zero faces, reject unsupported
policies transactionally, and assert that collapsed primitives produce neither false paths nor
invalid/divide-by-zero floating operations. The original strict geometry tests remain unchanged.
Linux compilation and focused occlusion linking derive kernel objects from the same source list.
