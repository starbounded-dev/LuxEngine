# LuxEngine Audio System Plan

The complete design for LuxEngine's audio, from where it is today to a general-purpose,
shipping-quality system. It is a planning document, not a description of what exists — read
`.claude/docs/Architecture-LuxEngine.md § 2.10` for what is actually built.

**Decisions this plan is built on**, so the reasoning is inspectable rather than implicit:

| Decision | Choice | Consequence |
|---|---|---|
| Target | General-purpose engine | Every subsystem gets a genre-neutral foundation; none is specialised for one game type. |
| Ray-traced acoustics | Invest further in Vercidium | Acoustic materials, portals and dynamic geometry are planned on the assumption occlusion gets fixed upstream. |
| Depth | Music, dialogue, ambience, physics audio — all four | No stub subsystems. Each is designed as if it were the headline feature. |
| Scripting | Full runtime C# API | Anything a designer can author, a script can drive. |
| Legacy raw-file path | Delete once events work | One playback path. Every sound goes through an FMOD Studio event. |
| Networking | Leave a seam | Triggers separable from playback; no replication built now. |
| Accessibility | Comprehensive | First-class subsystem, not a subtitle checkbox. |
| Platforms | Linux, Windows, consoles eventually | Per-platform banks, memory budgets, certification behaviour. |

---

## Contents

- [Part 0 — Where we are](#part-0--where-we-are)
- [Part 1 — Principles](#part-1--principles)
- [Part 2 — Architecture](#part-2--architecture)
- [Part 3 — Core runtime](#part-3--core-runtime)
- [Part 4 — Components](#part-4--components)
- [Part 5 — Ray-traced acoustics](#part-5--ray-traced-acoustics)
- [Part 6 — Ambience and reverb zones](#part-6--ambience-and-reverb-zones)
- [Part 7 — Surfaces and physics audio](#part-7--surfaces-and-physics-audio)
- [Part 8 — Interactive music](#part-8--interactive-music)
- [Part 9 — Dialogue and subtitles](#part-9--dialogue-and-subtitles)
- [Part 10 — Accessibility](#part-10--accessibility)
- [Part 11 — The C# API](#part-11--the-c-api)
- [Part 12 — Editor tooling](#part-12--editor-tooling)
- [Part 13 — Performance and budgets](#part-13--performance-and-budgets)
- [Part 14 — Platforms and shipping](#part-14--platforms-and-shipping)
- [Part 15 — Roadmap](#part-15--roadmap)
- [Part 16 — Open questions](#part-16--open-questions)

---

## Part 0 — Where we are

Honest ledger. Everything below is measured or observed, not assumed.

| Capability | State | Note |
|---|---|---|
| FMOD Studio system, banks, live update | ✅ Working | Verified at runtime. |
| Bank build + auto-rebuild on Play | ✅ Working | `fmodstudiocl`, timestamp-gated. |
| Event enumeration, GUID refs, picker | ✅ Working | Two-stage bank → event with search. |
| Event playback from a component | ⚠️ Built, barely proven | Chain verified; audible playback only just possible. |
| VA reverb → FMOD | ✅ Working | Measured: 0.10 s open field → 1.39 s large hall. |
| VA occlusion → FMOD | ❌ Blocked upstream | Identical values with and without geometry. Engine side is correct. |
| Buses | ✅ Get/set volume | No snapshots, no VCAs, no metering. |
| C# API | ❌ Nothing | `AudioSourceComponent` in C# is an empty class. |
| Listener | ⚠️ Minimal | Single, index 0, still carries dead miniaudio cone fields. |
| Runtime export | ❌ Broken | Ships no banks and no Studio config. Exported games are silent. |

**The three things that most need fixing** are, in order: no C# API at all, runtime export shipping
silent, and the listener still being miniaudio-shaped.

---

## Part 1 — Principles

Five rules that decide arguments later.

**1. The mix lives in FMOD Studio, not in C++.**
The engine reports *what is happening*; the event decides what it sounds like. Any time a design
puts a curve, a filter or a randomisation in engine code, it is wrong — that belongs to whoever is
authoring sound, and they should be able to change it without a rebuild.

**2. References are GUIDs. Paths are labels.**
Every asset reference — events, buses, banks, snapshots, parameters where possible — is stored by
GUID and displayed by path. A rename in Studio must never silently mute a scene.

**3. Failures are loud.**
Audio fails by being absent, which is the hardest failure to notice. Every unresolvable reference,
missing bank, exhausted voice budget and rejected parameter logs once, with the identifier needed to
find it. Never log per frame.

**4. Everything is scriptable.**
If a designer can author it, a script can drive it. No capability is editor-only.

**5. The engine owns placement and state; the event owns sound.**
Engine responsibilities: where a sound is, what the world is doing, when things happen, how many
voices are affordable. Everything else belongs to the event.

---

## Part 2 — Architecture

```
                        ┌────────────────────────────┐
   authoring            │      FMOD Studio (.fspro)  │
                        └──────────────┬─────────────┘
                                       │ fmodstudiocl
                                       ▼
                        ┌────────────────────────────┐
   build output         │   banks (+ GUIDs.txt)      │
                        └──────────────┬─────────────┘
                                       ▼
 ┌─────────────────────────────────────────────────────────────────┐
 │                         AudioEngine                             │
 │  Studio::System (banks, events, buses, VCAs, snapshots)         │
 │  Core::System  (listener, geometry, DSP, metering)              │
 └───┬─────────────┬──────────────┬───────────────┬────────────────┘
     │             │              │               │
     ▼             ▼              ▼               ▼
 AudioEvent   AudioZone      MusicDirector   DialogueDirector
 Instance     System         (Part 7)        (Part 8)
     ▲             ▲              ▲               ▲
     │             │              │               │
 ┌───┴─────────────┴──────────────┴───────────────┴────────────────┐
 │   Scene  ·  components, per-frame sync, lifetime                │
 └───┬─────────────────────────────────────────────────────────────┘
     │
     ├── RaytracedAudioScene (VA) ──► acoustic parameters
     ├── PhysicsScene ─────────────► impacts, footsteps (Part 9)
     └── ScriptEngine ─────────────► the C# API (Part 11)
```

**Ownership rules.**

- `AudioEngine` is a static facade over one `Studio::System`. It owns banks, buses, VCAs, snapshots
  and the global listener set. It never knows about entities.
- `Scene` owns per-entity instances and their lifetime. Nothing outside `Scene` may hold a raw
  instance across frames without a handle.
- Directors (music, dialogue, ambience) are `Scene`-owned services with an explicit update order.
- Nothing in `Core/Source/Lux/Audio/` may include an editor header.

**Threading.** All audio calls happen on the main thread, inside the scene update. FMOD is
thread-safe but the ordering is not: an instance created and 3D-positioned in the same frame must
have both applied before `Studio::System::update()`. VA delivers results on its own worker threads
and is joined explicitly — see Part 5.

---

## Part 3 — Core runtime

### 3.1 AudioEngine

```cpp
class AudioEngine
{
public:
    static void Init();
    static void Shutdown();
    static void Update();                        // Studio::update() then System::update()

    // ── Banks ──────────────────────────────────────────────────────────────
    static bool  LoadBanks(const std::filesystem::path& directory);
    static bool  LoadBank(std::string_view bankName);         // on demand, by name
    static void  UnloadBank(std::string_view bankName);
    static void  UnloadAllBanks();
    static bool  IsBankLoaded(std::string_view bankName);
    static float GetBankLoadProgress(std::string_view bankName);   // async loads
    static const std::vector<AudioBankInfo>& GetLoadedBanks();

    // ── Events ─────────────────────────────────────────────────────────────
    static const std::vector<AudioEventInfo>& GetEvents();
    static const AudioEventInfo* FindEvent(std::string_view guid);
    static Ref<AudioEventInstance> CreateInstance(std::string_view guid);
    static void PlayOneShot(std::string_view guid);
    static void PlayOneShot(std::string_view guid, const glm::vec3& position);

    // ── Mixing ─────────────────────────────────────────────────────────────
    static bool  SetBusVolume(std::string_view busPath, float volume);
    static float GetBusVolume(std::string_view busPath);
    static bool  SetBusMuted(std::string_view busPath, bool muted);
    static bool  SetBusPaused(std::string_view busPath, bool paused);
    static bool  StopBus(std::string_view busPath, bool allowFadeOut = true);
    static bool  SetVCAVolume(std::string_view vcaPath, float volume);
    static float GetVCAVolume(std::string_view vcaPath);

    // ── Snapshots ──────────────────────────────────────────────────────────
    static Ref<AudioEventInstance> StartSnapshot(std::string_view guid);
    static void StopSnapshot(std::string_view guid);

    // ── Global parameters ──────────────────────────────────────────────────
    static bool  SetGlobalParameter(std::string_view name, float value);
    static float GetGlobalParameter(std::string_view name);
    static bool  SetGlobalParameterLabel(std::string_view name, std::string_view label);

    // ── Listeners ──────────────────────────────────────────────────────────
    static void SetListenerCount(int count);       // 1..8
    static int  GetListenerCount();

    static AudioEngineStats GetStats();
    static FMOD::Studio::System* GetStudioSystem();
    static FMOD::System* GetCoreSystem();
};
```

**Notes that are easy to get wrong.**

- `Update()` **must** call Studio then Core. Studio's update does not recompute Core's 3D
  attenuation; measured with `Channel::getAudibility`, Studio alone leaves audibility frozen at the
  geometry a voice started with.
- `Shutdown()` releases **only** the Studio system. It owns the Core system; releasing both is a
  double free.
- Banks load strings-first. The strings bank carries the path table, and loading it late makes every
  `event:/…` lookup fail with an unhelpful `EVENT_NOTFOUND`.

### 3.2 Bank strategy

Three loading modes, chosen per bank in project settings:

| Mode | When | Behaviour |
|---|---|---|
| Preload | Master, UI, core SFX | Loaded at project/runtime start, never unloaded. |
| On demand | Level-specific content | `LoadBank` by name; refcounted so two levels sharing a bank behave. |
| Streaming | Music, dialogue | Sample data stays on disk; FMOD streams it. |

Bank metadata (mode, platform, whether sample data is preloaded) lives in project settings, keyed by
bank name, since FMOD does not carry it.

### 3.3 Voice management

FMOD virtualises voices past its channel limit, but silently. The engine adds:

- A **voice budget** per bus, configured in project settings.
- **Priority** on the source component, forwarded to the event instance.
- **Distance culling**: instances beyond an event's max distance are released rather than
  virtualised, since a virtual voice still costs bookkeeping.
- A **stats readout** of real vs virtual vs culled voices, surfaced in the Audio Debugger and logged
  once when a budget is first exceeded.

---

## Part 4 — Components

### 4.1 AudioSourceComponent

```cpp
struct AudioSourceComponent
{
    AudioEventRef Event;                 // GUID + advisory path/bank
    float  Volume        = 1.0f;
    float  Pitch         = 1.0f;
    bool   PlayOnAwake   = true;
    bool   FollowEntity  = true;         // false = fire at spawn position, don't track
    int    Priority      = 128;          // 0 = highest
    AudioStopMode StopMode = AudioStopMode::AllowFadeOut;   // on destroy/disable

    std::vector<AudioParameterOverride> ParameterOverrides;

    // Runtime-only, not serialized
    bool Started = false;
};
```

`AudioParameterOverride` is `{ std::string Name; float Value; bool Continuous; }` — `Continuous`
distinguishes a value applied once at creation from one re-applied each frame (useful for a
parameter bound to a component field).

### 4.2 AudioListenerComponent

```cpp
struct AudioListenerComponent
{
    bool  Active         = true;
    int   ListenerIndex  = 0;            // 0..7, Studio supports up to eight
    float Weight         = 1.0f;         // blend across listeners for split-screen

    // Studio can hear from one point and attenuate from another. This is the
    // third-person fix: pan at the camera, attenuate at the character, so distant
    // sounds are not artificially loud because the camera sits behind the player.
    bool  UseAttenuationTarget = false;
    UUID  AttenuationTarget    = 0;
};
```

The cone fields go — FMOD has no listener cone, so they were dead miniaudio vocabulary.

Both the Studio listener and the Core listener are driven while any Core-API playback remains; after
the legacy path is deleted, only Studio's.

### 4.3 New components

| Component | Purpose | Part |
|---|---|---|
| `AudioZoneComponent` | Reverb/ambience volume with blend distance | 6 |
| `AudioSurfaceComponent` | Tags a collider with a surface material | 7 |
| `MusicDirectorComponent` | Scene-level interactive music state | 8 |
| `DialogueSpeakerComponent` | Marks an entity as a dialogue source | 9 |
| `AudioOccluderComponent` | Explicit occlusion volume, independent of colliders | 5 |

---

## Part 5 — Ray-traced acoustics

The differentiator. Everything here assumes the occlusion defect gets fixed upstream; the reverb
half works today and is genuinely good.

### 5.1 Acoustic materials

The largest missing piece. Today every mirrored surface is `VAMaterialConcrete`, so a curtained room
sounds like a car park.

```cpp
enum class AcousticMaterial : uint8_t
{
    Default = 0, Brick, Carpet, Cloth, Concrete, ConcretePolished,
    Dirt, Glass, Grass, Gravel, Marble, Metal, Plaster, Plastic,
    Rock, Snow, Soil, Water, Wood, WoodThin, Ceramic, Rubber, Foliage,
};
```

- Added to `MeshColliderComponent` alongside the physics `ColliderMaterial`, and to
  `AudioSurfaceComponent` for finer control.
- Mapped to `VAMaterialType` when mirroring geometry.
- Per-material absorption/scattering/transmission overridable in project settings, via
  `vaWorldSetMaterial*`.
- **Shared with Part 7**: the same tag drives footsteps. One authored fact, two consumers — a wooden
  floor sounds wooden underfoot *and* reflects like wood.

### 5.2 Dynamic geometry

Geometry is currently mirrored once at runtime start, so a closing door changes nothing.

- Dirty-tracking on collider transforms; re-mirror changed primitives, not the whole world.
- A `Static`/`Dynamic` flag on colliders so the common case stays cheap.
- Rebuild budgeted per frame — VA's BVH rebuild is not free.
- Doors, destructible walls and moving platforms become acoustically real.

### 5.3 Portals and rooms

Explicit acoustic topology, which raytracing alone cannot infer cheaply:

- `AudioPortalComponent` — a doorway or window with an open/closed factor driven by gameplay.
- Rooms derived from `AudioZoneComponent` volumes.
- Portal state feeds VA and the zone blend, so an opening door smoothly reveals the space beyond.

### 5.4 Occlusion, with a fallback

Since VA's occlusion is unusable today, the plan carries a **fallback strategy** even while
investing in VA:

- `OcclusionMode::{ Raytraced, PhysicsRaycast, Off }` in project settings.
- `PhysicsRaycast` casts a small number of Jolt rays from listener to source, weighted by material
  transmission, and writes the same `Occlusion` parameter.
- Identical output contract, so events do not care which produced it. When VA is fixed, flipping one
  enum restores it with no content changes.

### 5.5 Debug visualisation

Already built: ray paths, bounce points, normals, emitter gizmos, world bounds. Planned additions:

- Colour rays by ray type, matching VA's own colour conventions.
- Draw mirrored geometry as wireframe, tinted by acoustic material — the fastest way to spot an
  untagged wall.
- Echogram plot per emitter.
- A heatmap mode: sample reverb decay over a grid at listener height.

---

## Part 6 — Ambience and reverb zones

```cpp
struct AudioZoneComponent
{
    AudioEventRef AmbienceEvent;      // bed that plays while inside
    AudioSnapshotRef Snapshot;        // mix state applied while inside
    float Priority      = 0.0f;       // higher wins where zones overlap
    float BlendDistance = 2.0f;       // metres of crossfade at the boundary
    ZoneShape Shape     = ZoneShape::Box;   // Box | Sphere | Collider
};
```

**How it works.** The zone system evaluates every zone containing the listener each frame, sorts by
priority, and computes a blend weight per zone from the listener's distance to its boundary.
Snapshot intensities are set to those weights, so crossing a threshold crossfades rather than snaps.
Ambience beds start on entry and stop with a fade on exit.

**Where VA fits.** Zones do not replace ray-traced reverb; they layer with it. VA measures the room
you are physically in; a zone expresses authored intent ("this cave should feel oppressive
regardless"). Project settings choose which dominates, or blends them.

Also planned: wind and weather layers driven by global parameters, time-of-day ambience blending,
and one-shot ambient emitters (distant birds, drips) scattered in a zone with randomised interval
and position.

---

## Part 7 — Surfaces and physics audio

```cpp
struct AudioSurfaceComponent
{
    AcousticMaterial Material = AcousticMaterial::Default;
    AudioEventRef FootstepOverride;    // optional, else the global surface table
    AudioEventRef ImpactOverride;
};
```

**Surface table.** A project asset mapping `AcousticMaterial` → `{ footstep, impact, scrape, roll }`
events. One table, authored once, used by every character and every physics body.

**Footsteps.** Driven either from animation notifies (once an animation system exposes them) or from
a character-controller ground query. The surface comes from the collider underfoot; the event gets
`Speed`, `Weight` and `Surface` parameters.

**Impacts.** `PhysicsScene` contact callbacks produce impact events with the collision impulse mapped
to a parameter, so a light tap and a heavy crash are the same event at different intensities.
Requires a per-body cooldown, or a bouncing object machine-guns the mixer.

**Rolling and scraping.** Continuous events on persistent contacts, with relative velocity driving a
parameter, started and stopped by contact begin/end.

**Threading note.** Jolt contact callbacks arrive on physics threads. They must be queued and drained
on the main thread — calling FMOD from a Jolt job is out of contract.

---

## Part 8 — Interactive music

```cpp
class MusicDirector
{
public:
    void Play(std::string_view eventGuid);
    void Stop(bool allowFadeOut = true);

    void SetState(std::string_view stateName);        // "explore", "combat", "stealth"
    void SetIntensity(float intensity);               // 0..1, continuous
    void SetLayerEnabled(std::string_view layer, bool enabled);

    void PlayStinger(std::string_view eventGuid);     // one-shot over the bed
    void QueueTransition(std::string_view eventGuid, MusicSync sync);

    float GetCurrentBeat() const;
    float GetCurrentBar() const;
    void  SetTempoCallback(std::function<void(int bar, int beat)> callback);
};
```

`MusicSync` is `{ Immediate, NextBeat, NextBar, NextMarker, NextSection }`. Transitions synced to
musical time are the difference between a music system that feels composed and one that feels like a
crossfade.

**Design.** The director owns exactly one music instance, since two competing music beds is always a
bug. State changes write parameters on that instance rather than restarting it — the whole point of
an interactive score is that FMOD handles the transition internally.

Beat and bar callbacks come from FMOD's timeline marker and beat callbacks, which fire on FMOD's
mixer thread — they are queued and dispatched on the main thread, so gameplay can safely sync to
musical time (a rhythm mechanic, a beat-synced UI pulse).

Also planned: ducking music under dialogue via a snapshot or sidechain, a separate music bus with its
own accessibility volume, and persistence of musical position across scene loads.

---

## Part 9 — Dialogue and subtitles

The most involved subsystem, because it is three problems: audio, text, and localisation.

```cpp
class DialogueDirector
{
public:
    DialogueHandle Speak(const DialogueLine& line, UUID speakerEntity);
    void Stop(DialogueHandle handle, bool allowFadeOut = true);
    void StopAll();

    void   SetQueueMode(DialogueQueueMode mode);   // Interrupt | Queue | DropIfBusy
    bool   IsSpeaking(UUID speakerEntity) const;
    size_t GetQueueLength() const;

    void SetSubtitleCallback(std::function<void(const SubtitleEvent&)> callback);
};

struct DialogueLine
{
    std::string Key;              // localisation key, e.g. "npc.guard.greeting_01"
    AudioEventRef Event;          // usually a programmer-sound event
    DialoguePriority Priority = DialoguePriority::Normal;
    bool Interruptible = true;
};
```

**Programmer sounds** are the FMOD mechanism here: one event whose audio is supplied at runtime from
an audio table, keyed by string. That means one authored event serves ten thousand lines, and
localisation is a matter of loading a different audio table bank.

**Subtitles** are emitted as events, not drawn by the audio system:

```cpp
struct SubtitleEvent
{
    std::string Text;             // already localised
    std::string SpeakerName;
    float Duration = 0.0f;
    UUID SpeakerEntity = 0;
    glm::vec3 SpeakerPosition{ 0.0f };
    bool IsOffScreen = false;     // for directional indicators
};
```

The engine tells the game *what was said, by whom, from where*. Rendering is the game's business —
which is also what makes the accessibility work in Part 10 possible without touching the audio code.

**Barks** (short, positional, interruptible, deduplicated across nearby speakers) get their own path,
because running them through the full queue makes crowds sound like a call centre.

---

## Part 10 — Accessibility

First-class, not a checkbox. Every item is a real feature with an owner.

**Subtitles and captions**
- Speaker names, with per-speaker colour.
- Directional indicators for off-screen speakers, driven by `SubtitleEvent::SpeakerPosition`.
- **Closed captions** for non-speech: `[door creaks]`, `[footsteps behind you]`. Authored as an
  optional caption string on any event, emitted through the same subtitle channel.
- Size, background opacity, max lines, display duration multiplier.

**Mixing**
- Per-bus volume in a standard options menu: master, music, SFX, dialogue, UI, ambience.
- **Mono downmix** for single-sided hearing (`Studio::System::setSpeakerPosition`, or a mono bus
  fold).
- **Dynamic range compression** preset for playing quietly or on poor speakers.
- Dialogue boost independent of the SFX bus.

**Visual sound cues**
- A `SoundEvent` notification stream the game can render as an on-screen indicator: direction,
  intensity, category. Gives deaf players positional information hearing players get for free.

**Audio description**
- A hook for a description track that ducks other audio, using the same queue as dialogue.

**Why this is cheap here.** Because subtitles are already events and the mix is already buses, most
of this is surfacing what exists rather than building new machinery. The one genuinely new piece is
the visual cue stream.

---

## Part 11 — The C# API

Nothing exists today. This is the full surface.

### 11.1 Component

```csharp
public class AudioSourceComponent : Component
{
    public bool  IsPlaying { get; }
    public bool  IsPaused  { get; set; }
    public float Volume    { get; set; }
    public float Pitch     { get; set; }

    public void Play();
    public void Stop(bool allowFadeOut = true);
    public void Restart();

    public void  SetParameter(string name, float value);
    public float GetParameter(string name);
    public void  SetParameterLabel(string name, string label);

    public void SetEvent(string guidOrPath);       // swap at runtime
    public int  TimelinePosition { get; set; }     // milliseconds
}
```

### 11.2 Static facade

```csharp
public static class Audio
{
    // One-shots
    public static void PlayOneShot(string eventRef);
    public static void PlayOneShot(string eventRef, Vector3 position);

    // Instances
    public static EventInstance CreateInstance(string eventRef);

    // Mixing
    public static void  SetBusVolume(string busPath, float volume);
    public static float GetBusVolume(string busPath);
    public static void  SetBusMuted(string busPath, bool muted);
    public static void  SetVCAVolume(string vcaPath, float volume);

    // Global parameters
    public static void  SetGlobalParameter(string name, float value);
    public static float GetGlobalParameter(string name);

    // Snapshots
    public static EventInstance StartSnapshot(string snapshotRef);

    // Banks
    public static bool LoadBank(string bankName);
    public static void UnloadBank(string bankName);
    public static bool IsBankLoaded(string bankName);

    // Subsystems
    public static Music    Music    { get; }
    public static Dialogue Dialogue { get; }
}

public class EventInstance : IDisposable
{
    public bool IsPlaying { get; }
    public void Start();
    public void Stop(bool allowFadeOut = true);
    public void SetParameter(string name, float value);
    public void Set3DAttributes(Vector3 position, Vector3 velocity, Vector3 forward, Vector3 up);
    public void SetVolume(float volume);
    public void SetPitch(float pitch);
    public void Dispose();

    public event Action Stopped;      // fires when playback ends
    public event Action<string> Marker;
}

public static class Music
{
    public static void  Play(string eventRef);
    public static void  Stop(bool allowFadeOut = true);
    public static void  SetState(string state);
    public static float Intensity { get; set; }
    public static void  PlayStinger(string eventRef);
    public static event Action<int, int> Beat;    // (bar, beat)
}

public static class Dialogue
{
    public static DialogueHandle Speak(string key, Entity speaker = default);
    public static void Stop(DialogueHandle handle);
    public static bool IsSpeaking(Entity speaker);
    public static event Action<Subtitle> SubtitleShown;
    public static event Action<Subtitle> SubtitleHidden;
}
```

### 11.3 Implementation notes

- Internal calls follow the existing `LUX_ADD_INTERNAL_CALL` pattern; `Coral::String` marshals both
  ways, so paths and GUIDs as strings are fine.
- `EventInstance` is a **handle**, not a pointer: C# holds a `uint64` id into an engine-side
  registry. Passing raw pointers across the managed boundary invites a crash the moment a scene
  unloads underneath a script.
- Every handle is swept when the runtime stops. `IDisposable` is the polite path; the sweep is the
  safety net.
- Callbacks (`Stopped`, `Marker`, `Beat`) originate on FMOD's mixer thread and are **queued**,
  dispatched on the main thread before script updates. Invoking managed code from FMOD's thread is
  not survivable.

---

## Part 12 — Editor tooling

**Audio Debugger** (exists; to extend)
- Live bus metering with peak/RMS bars.
- Voice list: what is playing, where, on which bus, real or virtual.
- Per-event instance inspector with live parameter values.
- Event auditioning: play any event from the browser without entering Play.

**Content browser**
- Waveform thumbnails for audio assets.
- Event preview on hover.

**Scene view**
- Audio source gizmos showing min/max distance spheres, taken from the event.
- Zone volumes drawn with their blend margin.
- Listener gizmo with orientation and attenuation target.
- A "what can I hear from here?" mode: click a point, list audible sources with computed audibility.

**Validation** — a checkable list, run on demand and before export:
- Sources with no event assigned.
- Events referenced by scenes but absent from banks.
- Events in banks referenced by nothing (dead content).
- Colliders with no acoustic material.
- Bank size and memory report.

---

## Part 13 — Performance and budgets

**Measurement first.** Extend the existing Tracy zones to cover the audio update, VA join, zone
evaluation and director updates. Nothing here should be optimised before it is measured — the same
rule the renderer plan uses.

**Budgets**, surfaced in the debugger and warned once when exceeded:

| Budget | Default | Rationale |
|---|---|---|
| Voices (real) | 64 | Beyond this, virtualisation and mud, not loudness. |
| Audio CPU | 5% | FMOD's own `getCPUUsage`. |
| VA raytracing | 2 ms | Its own thread pool, but it competes for cores. |
| Bank memory | 64 MB | Platform-dependent; console values are lower. |

**Known costs.** VA's ray budget is the dominant one and scales with the listener, not source count —
one reason the listener-casts topology matters. Zone evaluation is per-listener per-frame and trivial
until zone counts reach the hundreds, at which point it wants a spatial index.

---

## Part 14 — Platforms and shipping

### 14.1 Runtime export — currently broken

An exported game ships **no banks and no Studio configuration**, so it is silent. The fix:

- `ProjectInfo::Audio` in the binary runtime format carries the Studio settings (format version bump).
- `RuntimeExportUtils` copies the built banks for the target platform into the package.
- Bank paths resolve relative to the packaged assets, not the `.fspro`, which does not ship.
- Export fails loudly if banks are missing or stale, rather than producing a silent build.

### 14.2 Windows

The `Dependencies.lua` Windows paths for both FMOD and VA are **unverified placeholders**. Verifying
them is a prerequisite for any Windows work.

### 14.3 Consoles

Not imminent, but cheap to design for now:
- Per-platform bank builds (`fmodstudiocl -platforms`), selected at export.
- Platform memory budgets in project settings.
- Certification behaviour: mute on focus loss, correct pause semantics, background audio rules.

### 14.4 Build ergonomics

`--fmod` and `--raytraced-audio` are silently dropped whenever premake regenerates, producing
~90 undefined symbols that read like a broken linker. **Auto-enable each feature when its vendor
directory exists**, preserving the current contract (no SDK, no feature) while removing the footgun.

---

## Part 15 — Roadmap

Ordered so each phase is independently useful and leaves the engine working.

| # | Phase | Contents | Unblocks |
|---|---|---|---|
| 1 | ✅ Foundation | Studio system, banks, live update, bank build | — |
| 2 | ✅ Events | Event refs, instances, picker, bank-aware selection | — |
| 3 | **Listener rework** | Index, weight, attenuation target; delete cone fields | Split-screen, third-person |
| 4 | **C# API** | Components, one-shots, buses, instances, handles | Everything gameplay-driven |
| 5 | **Runtime export** | Ship banks, carry config, fail loudly | Shipping at all |
| 6 | **Delete legacy path** | Remove `AudioSource`, raw-file playback, miniaudio | One code path |
| 7 | **Acoustic materials** | Material enum, collider tagging, VA mapping | Parts 5 and 7 both |
| 8 | **Zones and snapshots — implemented** | Box/sphere/primitive-collider zones, priority/listener blending, FMOD intensity control, C# and editor integration; see `AUDIO_ZONES.md` | Ambience, environment |
| 9 | **Surfaces and physics audio — implemented** | Shared table asset, ground-query/C# footsteps, Jolt impacts, scrape/roll lifetimes; see `AUDIO_SURFACES.md` | — |
| 10 | **Interactive music — implemented** | Scene-owned director, startup component, states/layers/intensity, stingers, queued boundaries and C# beat/marker callbacks; see `AUDIO_MUSIC.md` | Rhythm-adjacent gameplay |
| 11 | **Dialogue and subtitles** | Programmer sounds, queue, subtitle events, localisation | Accessibility |
| 12 | **Accessibility** | Captions, mono downmix, visual cues, dialogue boost | — |
| 13 | **Dynamic geometry, portals** | Moving colliders re-mirrored, portal components | Doors that matter |
| 14 | **Voice budgets, validation** | Priority, culling, validation pass, metering | Shipping quality |
| 15 | **Platform work** | Windows verification, per-platform banks, console budgets | Ports |

**Dependencies worth noting.** Phase 6 must follow 4 and 5, or scripts and exports lose their only
working path. Phase 7 gates both 5 and 9. Phase 12 mostly surfaces what 11 already produces.

---

## Part 16 — Open questions

Genuinely undecided, listed so they are not silently decided by whoever implements first.

1. **Zones versus ray-traced reverb.** When both have an opinion, which wins? Options: authored zone
   always wins, VA always wins, blend by zone priority. Leaning toward blend, defaulting to VA.
2. **Should acoustic material live on the collider or the render mesh?** The collider is what VA
   mirrors, but the visual material is what an artist thinks in. A mapping from render material to
   acoustic material would be less work to author and more likely to be wrong.
3. **Networking seam shape.** "Separable triggers" needs a concrete form — probably an audio event
   queue that a replication layer can intercept, but it is unspecified.
4. **Dialogue localisation storage.** FMOD audio tables, or an engine-side string table with FMOD
   only supplying audio? The latter is more work but keeps subtitles usable without loading banks.
5. **How much does the engine assume about the bus layout?** Accessibility volume controls need
   named buses (`bus:/Music`). Enforce a convention, or make it configurable?
6. **Whether to keep `AssetType::SoundConfig` and `SpatializationConfig`.** Both are inherited stubs
   with no implementation, and both describe things an event now owns.

---

*Written against `sound-fmod-va`, FMOD Studio 2.03.14, Vercidium Audio 1.8.0.*
