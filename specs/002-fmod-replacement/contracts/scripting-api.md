# Contract: C# audio API after FMOD (D4)

Today's surface (`ScriptCore/Source/Lux/Audio.cs`, `Scene/Components.cs`) and its fate under each
D4 option. **Recommended: a, keep `EventInstance`.**

| API today | a. Remove (recommended) | b. Warning stubs | c. Sound asset paths |
|---|---|---|---|
| `Audio.LoadBank(string)` | `[Obsolete("Banks are gone: Sounds load with their scenes. Delete this call.", true)]`, then deleted after one release | returns `true` and logs once | as a |
| `Audio.PlaysSoundAssets` | obsolete, then deleted (always true) | always `true` | as a |
| `Audio.PlayOneShot(string)` / `(string, Vector3)` | obsolete (error): "use `PlayOneShot(Sound)`" | resolves `event:/` → error log; an asset path → Sound | the string is an Assets-relative Sound path (`"Audio/Sounds/Fart"`), resolved through the pack's path table |
| `Audio.CreateInstance(string)` | as above | as above | as above |
| `AudioSourceComponent.SetEvent(string)` | replaced by `Sound Sound { get; set; }` | logs and does nothing | the string is a Sound path |
| `AudioZoneComponent.SetAmbience(string)` | replaced by `SetAmbience(Sound)` | logs | Sound path |
| `AudioZoneComponent.SetSnapshot(string)` | **kept**: the string names a mixer snapshot (`Audio.Mixer`), not FMOD | kept | kept |
| `Audio.SetBusVolume(string)`, `ClearBusVolume`, VCAs | **kept**: `bus:/` paths name mixer buses | kept | kept |
| `Audio.PlayOneShot(Sound…)`, `CreateInstance(Sound)` | kept: the main API | kept | kept |
| `EventInstance` (type name) | kept (sub-choice: rename it to `SoundInstance` with an `[Obsolete]` alias for one release) | kept | kept |
| `Marker` / `Stopped` events, timeline position | kept: the native timeline drives them | kept | kept |

Error text names the replacement, so a broken build tells the user what to write instead. The API
docs (`Editor/Resources/Docs/api`) are regenerated and `--check` passes in CI.
