# Headless audio regression tests

On Linux, regenerate `Core/Makefile`, build the Release Core objects (for yaml-cpp) and Debug Jolt,
then run from the repository root:

```sh
python tests/audio/run.py
```

Requires `clang++`, the repository's FMOD and VA SDKs, and `fmodstudiocl`. The runner compiles production
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
