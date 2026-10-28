# Headless audio regression tests

On Linux, regenerate the makefiles, build the Release Core objects (for `Lux::Yaml` and rapidyaml),
Debug Jolt and, when generated with `--raytraced-audio`, Debug Asterwave
(`bin/Debug-linux-x86_64/Asterwave/libAsterwave.a`), then run from the repository root:

```sh
python tests/audio/run.py
```

Requires `clang++`, the FMOD SDK and `fmodstudiocl` (plus the pinned Asterwave checkout when the makefiles
opted into it; without it the stub backend is compiled and the Asterwave geometry test is skipped). The runner compiles production
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
entity cleanup, real Jolt worker contacts, masses/impulse estimates, sensors, body removal/sleep,
file-stream failures, packed surface-table truncation and asset-pack error propagation. Only application host setup and failing
asset serializers are substituted; playback/contact implementations and SDK calls are real.

Music checks cover state/intensity/layers without restart, stinger type validation, all queued
transition boundaries, main-thread callbacks, stale notification batches, callback reentrancy,
pause/resume, bank invalidation/recreation, fades, and destruction. `AudioTestHost.h` supplies the
shared minimal FMOD host: it defines the `FMODInternal` system getters and bus locks in place of
`Audio/FMOD/FMODAudioEngine.cpp`; the production director and event wrapper are linked unchanged.

Accessibility checks use Music/SFX/Dialogue buses created by `AuthorFixture.js`; regenerate older
fixtures. They cover default/invalid/bounded configuration, player preference replacement/reload,
mono/compressor DSPs, category gain independent of authored volume, narration ducking, localized
captions, moving cue direction, opt-in descriptions, bank/system recreation and teardown. The production ImGui overlay/menu is
also exercised headlessly for draw output, clipping and explicit newline limits; build Debug ImGui
alongside Debug Jolt before running. Managed payload/ABI/subscription
tests run separately with `python3 tests/audio/run_managed.py`.

Geometry checks use the native Asterwave world and worker completion, including transform/material updates,
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
paths with spaces, each missing FMOD header/link/runtime file, and the Asterwave source
layout/revision. It uses disposable fixtures to validate discovery rules, not a Windows compilation or hardware test. Console checks are on hold.

## Acoustics checks without FMOD

`tests/audio/acoustics` needs a C++20 compiler, CMake and the glm and spdlog submodules; it does not
require FMOD, a renderer or audio hardware. It always builds the stub backend and engine material
defaults test (`AcousticsStubTests.cpp`) and engine occlusion (`OcclusionTests.cpp`, which includes only
`AudioTestLog.h`). When the pinned Asterwave checkout is present it also builds the production
`RaytracedAudioScene` adapter test against native Asterwave:

```sh
cmake -S tests/audio/acoustics -B bin/acoustics-tests
cmake --build bin/acoustics-tests --config Debug
ctest --test-dir bin/acoustics-tests -C Debug --output-on-failure
```

`tests/audio/native` holds the Phase 0 miniaudio checks for the planned native backend; see its
README.

These commands describe the verification path; a passing run establishes only the exercised
adapter/geometry contracts, not listening quality or full editor/runtime behavior. The complete
FMOD suite above remains a separate check.
