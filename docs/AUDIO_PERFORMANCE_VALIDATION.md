# Audio budgets and validation (Phase 14)

## Configure budgets

Open Project Settings → Audio → Voice and Performance Budgets. Save and reopen the project to
apply changes to the running audio system. New exports carry the saved settings.

| Setting | Default | Behavior |
| --- | --- | --- |
| Real voices | 64 | FMOD software-channel limit, configured before initialization; range 1–512 |
| CPU warning | 5% | Sum of FMOD DSP, streaming, Core update and Studio update CPU measurements |
| VA warning | 2 ms | Last completed VA raytracing batch |
| FMOD memory | 64 MiB | Total FMOD allocations, including banks, samples and mixer overhead |
| Bus voice warning | `bus:/`: 64 | Inclusive playing-channel demand under each configured bus |

The global real-voice limit is enforced by FMOD. Bus limits are warning thresholds: they do not
mute buses or implement separate stealing rules. Set event concurrency/stealing policies in FMOD
Studio when a category needs a hard event-instance limit. Higher-priority channels get preference
under the global cap. A single event can use several channels, so voice counts and event-instance
counts differ. Parent bus counts include child buses; do not add those readings together.

View → Audio Debugger → Budgets and Bus Meters shows real/virtual voices, culled components, CPU,
memory, VA timing and input peak/RMS meters for configured buses. Sampling runs four times per
second. Peak/RMS display the loudest channel in dBFS at each bus input, before its fader/effects;
they are not output loudness or LUFS readings. Budget warnings log once per bank session, while
the panel continues showing current values. Reloading banks resets meters and warning history.

## Source priority and distance culling

The Audio Source inspector exposes Priority (0 highest, 256 lowest, default 128) and Distance
Culling (off by default). Both persist through scenes, prefabs, duplication and runtime export.
They are also available to C#:

```csharp
var source = GetComponent<AudioSourceComponent>();
source.Priority = 24;
source.DistanceCulling = true;
source.Play();
bool suspendedByDistance = source.IsCulled;
```

Culling applies to 3D Audio Source components beyond their event's authored maximum distance
from every listener with positive weight. A listener's attenuation target takes precedence over
its camera position. Re-entry requires moving inside 95% of the maximum distance to prevent
allocation churn at the boundary. 2D events are not distance-culled.

An out-of-range source releases its FMOD instance and VA emitter. Continuous events retain play
intent, script parameters, timeline and pause state; on re-entry they recreate and resume from
the frozen timeline. A culled one-shot is discarded and never fires late on re-entry. Calling Stop
while culled cancels the pending loop. Play restarts the timeline. `IsPlaying` remains true for a
culled loop with play intent; inspect `IsCulled` to distinguish it from a physical instance.

Enable culling only where discarding distant audio is intended: an authored send or reverb tail
can remain audible beyond the direct sound's range. Standalone C# EventInstance objects, music,
dialogue and zone directors keep their existing ownership and playback behavior. Global voice
limits still apply to their channels.

## Validate before export

Use View → Audio Debugger → Audio Validation → Validate Project Audio after building banks.
The report is a snapshot; rerun after edits. Validation reads all registered scenes, prefabs,
surface/dialogue tables, accessibility metadata and configured buses, plus the debugger's current
scene, including unsaved edits. A separate no-sound FMOD system inspects the built bank files;
validation does not unload the playing project's banks.

Errors include unreadable banks/assets, missing referenced events or buses, incompatible event
types, invalid acoustic materials/priorities/budgets, and legacy audio sources needing migration.
Warnings include unassigned sources, default concrete collider materials, missing strings banks,
audio-source events with no `Occlusion` parameter (walls cannot muffle them), and bank events with
no serialized reference. Script-only references cannot be inferred, so an
unreferenced-event warning is advisory and never deletes anything. GUID-only playback can work
without strings banks; script path lookups need them.

The report lists bank file sizes and their total disk footprint. Runtime memory is measured
separately with FMOD allocator statistics (available with both release and logging SDK libraries); compressed file sizes do not predict resident sample memory.

Export runs validation after preparing FMOD banks and stops on errors. Warnings remain available
in the console. Projects without audio banks or serialized event references can still export.
The memory threshold retains the `BankMemoryMiB` project key and conservatively covers all FMOD
allocations rather than attributing memory to individual banks.
Runtime project format 23 adds a bounded performance-settings block; older formats retain defaults.

## Verification

`tests/audio/run.py` exercises real FMOD no-sound playback and VA, including source culling,
priority, virtualization, meters, independent bank validation and production serialization blocks.
`tests/audio/run_managed.py` checks C# controls and thread guards. Scene hierarchy copy/prefab
self-tests additionally require the full editor host. Listening tests and editor visual checks
remain useful when tuning project-specific limits and authored attenuation.
