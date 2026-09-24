# Interactive music (Phase 10)

Lux owns one music bed per running scene. FMOD Studio owns the arrangement, transition regions,
parameter automation, instruments, and mix. Music uses 2D Studio events; spatial emitters still
use Audio Source components. Stingers are separate finite one-shots over the bed.

## Author in FMOD Studio

1. Create a **2D** event such as `event:/Music/Score`. Add your music instruments and a loop
   region or enable persistence so the event is continuous. Do not add a spatializer.
2. Add a local continuous `Intensity` parameter, range **0–1**. Automate the score with it.
3. For named states, add a local labeled `State` parameter, with labels such as `Explore`,
   `Combat`, and `Stealth`. Match spelling and case in scripts and the inspector.
4. For switchable layers, add local `Layer_<name>` parameters, range **0–1**. For example,
   `Layer_Drums` controls the drum track; scripts pass `"Drums"` to `SetLayerEnabled`.
5. Put tempo/time-signature markers on the timeline for beat callbacks. Named destination markers
   produce marker callbacks. Prefix section boundaries with `Section:`, for example `Section:Verse`.
6. Make stingers separate 2D one-shot events, for example `event:/Music/Victory`.
7. Route music and stingers to an authored `bus:/Music` bus if you want separate volume control.
   Use the existing `Audio.SetBusVolume("bus:/Music", volume)` API. The engine does not create buses.
8. Assign the events to banks and build. Configure the Studio project and bank output in Lux's
   Project Settings → Audio using the existing FMOD setup, then build/load banks before Play.

## Configure the scene

1. Create one entity named `Music` and add **Music Director** from the Audio component menu.
2. Pick the continuous 2D **Music Event**. The picker stores its GUID, path, and bank name.
3. Enable **Play On Awake**, set **Intensity**, and optionally set **Initial State**.
4. Save and press Play. Startup settings apply before script `OnCreate`. They are saved with the
   scene, copied into Play, duplicated into prefabs, and included in runtime scene packs.

The component configures startup; it does not own a second event instance. Multiple director
components log a conflict and the lowest entity UUID supplies startup settings. Runtime edits to
startup fields take effect on the next Play. Removing/destroying the selected owner clears music.
Scripts can use `Music` without any director component and explicitly start the score.

## Drive it from C#

```csharp
// Call from the engine main thread, in a running scene.
Music.SetState("Explore");
Music.Intensity = 0.2f;
Music.Play("event:/Music/Score"); // Omit when the component already starts it.

Music.SetState("Combat");       // Changes the parameter without restarting playback.
Music.Intensity = 0.85f;
Music.SetLayerEnabled("Drums", true);
Music.PlayStinger("event:/Music/Victory");

Music.Beat += OnBeat;
Music.Marker += OnMarker;

void OnBeat(int bar, int beat) { /* One-based FMOD bar and beat, on the main thread. */ }
void OnMarker(string marker) { /* e.g. "Section:Verse" */ }

Music.QueueTransition("event:/Music/NextScore", MusicSync.NextBar);
// Unsubscribe in the script's OnDestroy; scene/assembly reset also clears subscriptions.
Music.Beat -= OnBeat;
Music.Marker -= OnMarker;
Music.Stop();                    // Allows authored fadeout on the bed and stingers.
```

`Music.IsPlaying`, `Music.CurrentBar`, and `Music.CurrentBeat` expose current state. Bar/beat are
zero before the first callback or after Stop. Invalid events, missing parameters/labels, and failed
requests report an Audio error; managed mutating calls throw `InvalidOperationException` on failure.
Intensity rejects nonfinite/out-of-range values. A failed replacement validation preserves the
current bed. Each replacement must support the currently selected state, intensity, and layers.

## Timing and lifetime

- `Immediate` replaces the bed now. `NextBeat`, `NextBar` (beat 1), `NextMarker`, and `NextSection`
  replace it when a matching **future** notification reaches the main thread. The latest valid
  queued request replaces the previous one. Notifications already received when a request is
  made cannot satisfy it, including requests made from inside a callback.
- Cross-event replacement is frame-quantized and stops the old bed immediately before starting the
  next. It is **not sample-accurate**, and it does not overlap two beds. For seamless musical
  transitions, author Studio transition regions/quantization inside one event and drive its State
  or Intensity. Missing tempo/markers leave the corresponding request pending until Stop or a
  replacement request; the engine does not invent a clock or silently fall back to immediate play.
- FMOD callback threads copy immutable data into bounded, mutex-protected mailboxes. The main
  thread dispatches music callbacks before script `OnUpdate`. No ECS, managed code, or director
  pointer is accessed by a callback thread. Tokens are never reused, so disposed-instance callbacks
  cannot target a new instance. Queue overflow is reported on the main thread.
- Scene pause pauses the bed and stingers and suspends music callback delivery. Resume preserves
  musical position. Stop retains instances through authored fadeout; scene teardown clears them
  immediately. Up to 32 simultaneous stingers are allowed; exceeding this reports an error.
- Bank reload invalidates pending transitions and stingers. The active bed is recreated once banks
  are available, with its selected parameters, starting at the beginning. Explicitly stopped music
  is not restarted. Scene changes end music; cross-scene musical-position persistence is not part
  of Phase 10. Dialogue ducking is authored via FMOD sidechains/snapshots; dialogue integration is
  Phase 11 and accessibility options are Phase 12.

## Verification

`python3 tests/audio/run.py` builds a disposable FMOD project and runs production event/director
code against the real SDK with no-sound output. Music checks cover parameters, stingers, every
queued boundary, callback thread delivery, stale batches, reentrant callbacks, pause, bank reload,
and teardown. It also retains the Phase 9 physics and stream regression checks. Scene round-trip
self-tests cover music startup fields through duplication and prefab operations.

Native Linux builds and headless SDK tests do not verify audible mix quality, live editor UI,
or Windows execution. Those require interactive/platform checks with the authored project.
