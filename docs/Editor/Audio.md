# Audio in the Editor

Everything the editor exposes for the audio stack: the **Audio Debugger** panel, the **Audio**
section of Project Settings, the five audio components in the Inspector, and the acoustics
visualisation drawn into the viewport.

> Scope: the editor-facing surface. The runtime design — `AudioEngine`, `AudioEventInstance`,
> `AudioAcoustics` / Soundwave, bank lifetime, zones, music, dialogue, budgets — is documented in
> `.claude/docs/Architecture-LuxEngine.md` §2.10. Authoring workflow for the sample project lives in
> `Editor/LuxSampleProject/FMOD_SETUP.md`.

The stack is **FMOD Studio** for playback and **Soundwave**, the engine's own acoustic kernel, for
occlusion, sound around corners and through doorways, and room reverb. FMOD is a mandatory SDK;
Soundwave is built in and needs nothing installed.

---

## The map

| Where | What it covers |
|---|---|
| **View → Audio Debugger** | Read-only runtime state: backends, banks, events, voices, acoustics, budgets, validation |
| **Project Settings → Audio** | Studio project wiring, budgets, desktop profiles, surface/dialogue tables, accessibility |
| **Inspector → Add Component → Audio** | Audio Source, Portal, Zone, Surface, Listener |
| **Viewport** | Sound paths, emitters, zone/portal wireframes, room readout |

---

## Audio Debugger

### What it is

A read-only window onto the live audio stack. It is **pure visualisation** — it reads data the two
backends already publish (`AudioEngine::GetStats` / `GetReverbSnapshot`, and the scene's
`AudioAcoustics` diagnostics, results and reverb) and adds no instrumentation of its own. Nothing you
do here changes what you hear.

Registered in `EditorLayer.cpp:393` under `PanelCategory::View` as `"AudioDebugPanel"`. It is
**closed by default**; the default layout docks it to the bottom region (`EditorLayer.cpp:1025`).

### How to use it

Open it with **View → Audio Debugger**. Most of it only has content in Play mode — outside Play the
acoustics sections report `Idle (enter Play)`.

Sections, in draw order:

| Section | What it tells you |
|---|---|
| **Backends** | Whether FMOD initialised, its version and sample rate; the acoustics backend (Soundwave) and whether it is running |
| **Playback** | Channels playing, DSP CPU, event instance count |
| **FMOD Studio** | Studio system state and live-update status |
| **Banks** | Every loaded bank — name, event count, kind |
| **Events** | Searchable table of events the loaded banks describe — path, kind, GUID |
| **Soundwave Acoustics** | Static/moving triangle and mesh-instance counts, pending geometry, source solves and failures; the listener's reverb — 60 dB decay, send, energy outside, mean free path |
| **Sources** | Per-source table: entity, event, distance, playback state, and the acoustics Soundwave resolved for it — occlusion, gain LF/HF, muffle, and *Paths* (`3R 1O x2`: reflections, openings, deepest bounce; `!` when a work or path budget ran out) |
| **Budgets and Bus Meters** | Real/virtual voices against budget, CPU, memory, per-bus input peak/RMS in dBFS |
| **Audio Validation** | The project's validation results — missing events, wrong event kinds, corrupt banks |

**Audio Validation is the one to check before exporting.** It reports the same failures that make
an exported game silent, and it is much easier to read here than in the log.

### Visualisation — "See the Sound"

The **3D Visualisation** section draws the acoustics into the viewport. The quickest way in is
**See the Sound**, also in the viewport's options popup (the gear), which works during Play. It
turns on the showcase set below and restores your previous settings when you turn it off.

| Control | Default | What it shows |
|---|---|---|
| Show Occlusion Paths | off | Each source's line from the listener, green (clear) to red (blocked), with every wall it crosses marked and labelled `Brick 0.30 m -9.0 dB`, plus the source's total, and the reflection and doorway routes Soundwave found, fainter. Drawn through walls so the blocked part stays visible. *Wall Labels* and *Selected Source Only* refine it. Reuses the solve's results; costs nothing extra |
| All Zones and Portals | off | Every zone and portal, brightened by its live weight (zones) or how open it is (portals), labelled with the number |
| Room Acoustics Readout | off | Top-left of the viewport: the listener room's 60 dB decay, reverb send, escaping energy and mean free path, as Soundwave measures it |
| Draw Emitters | off | Rings at the listener and every source (radius 0.35) |

**Acoustic Materials** (viewport options popup, edit mode and Play) tints every surface the acoustics
see — non-Disabled mesh colliders and closed portal shutters — by its effective acoustic material,
with a legend of the materials in the scene. It replaces the physics collider view while on.

### How it was built

Two decisions in `AudioDebugPanel.h` are worth knowing before you change anything:

**It does not branch on `LUX_ENABLE_FMOD`.** That define is set only for the Core project (`Core/premake5.lua`), so the Editor cannot see them. Each backend
identifies itself through its stats struct instead. Adding an `#ifdef LUX_ENABLE_FMOD` here
compiles to nothing and silently removes the section.

**The visualisation settings live in the panel, but the panel does not draw them.**
`AudioVisualisationSettings` is owned by `AudioDebugPanel` because that is where it is edited, and
read by `EditorLayer::DrawAudioVisualisation`, called from `OnOverlayRender`, which owns the only
`Renderer2D` with a camera already set up for the frame. The panel is the editing half; the
overlay is the drawing half.

**Everything the overlay reads was published on the main thread.** Soundwave's worker jobs hand
finished snapshots and measurements back to main, so the panel and overlay never see work in
progress.

### How to modify

- **Add a readout:** extend the relevant backend's stats struct first, then add a row. Do not reach
  into FMOD or Soundwave from the panel — that would put instrumentation in the editor, where it cannot be
  reused by the runtime or the tests.
- **Add a visualisation mode:** add the field to `AudioVisualisationSettings`, a control in the
  panel's visualisation table, and the drawing in `EditorLayer::OnOverlayRender`.

---

## Project Settings → Audio

Open with **View → Project Settings**, then the **Audio** section. Settings are saved with the
project and most take effect on reopen.

**FMOD Studio**
- *Studio Project (.fspro)* — relative to the project's `Assets` directory.
- *Bank Output* — the **directory** Studio builds into (e.g. `Build/Desktop`), relative to the
  `.fspro`. Not a `.bank` file; this is the single most common misconfiguration.
- *Rebuild Banks On Play* — requires Studio's command-line tool `fmodstudiocl` on PATH, or
  `LUX_FMOD_STUDIO_CL` pointing at it.
- *Live Update* — lets Studio connect to the running editor.

**Voice and Performance Budgets** — Real Voices, CPU warning %, Acoustics warning (ms of Soundwave's
main-thread update), FMOD memory (MiB), per-bus voice limits, and *Mute When Unfocused*. These drive the warnings
the Audio Debugger's budget section reports.

**Desktop Audio Profiles** — optional per-OS overrides (*Windows*, *Linux*). Each has *Override
defaults*, a *Studio Platform* name that must match a platform configured in your Studio project,
a *Bank Output* directory, and its own budgets. *Copy Default Budgets* seeds a profile from the
project defaults. The host OS selects its enabled profile for bank builds, Play, validation and
export.

**Zone Reverb** — how zone reverb and Soundwave's reverb combine: *Layer zones and Soundwave*,
*Prefer zones*, or *Prefer Soundwave*.

**Soundwave acoustics** — what muffles a source behind a wall (its event's `Occlusion` parameter)
and how wet the room sounds (`ReverbSend`). Soundwave walks the direct path through acoustic geometry
and closed portals, costing each wall 30 dB per its material's transmission distance (a 0.3 m brick
wall is about 18 dB), and adds reflection and doorway routes so a source around a corner stays
audible. *Transmission Strength* scales every wall; *Acoustic Rate* and *Sources per Frame* bound the
cost; *Maximum Bounces*, *Reflection Depth*, *Opening Samples* and the work budgets bound the search.
*Reverb Rays*, *Reverb Bounces* and *Reverb Reference Decay* control the listener reverb
measurement. *Air Absorption LF/HF* (dB per 100 m) darken reflected and doorway sound over their
extra length and shorten the reverb; FMOD's distance fade still owns how a source fades with
distance, so nothing is faded twice. *Compare Engine Ray* also runs the old physics-ray occlusion as a diagnostic. Applies on
the next Play. Projects saved with the old *Engine raycasts / VA (raytraced)* choice load unchanged
and play on Soundwave. The Audio Debugger's validation warns about source events with no
`Occlusion` or `ReverbSend` parameter.

**Surface Sounds** — the `.lsurfaces` *Surface Table* plus impact cooldown, minimum impulse (in
Jolt's estimated kg·m/s) and minimum motion speed, with Footstep / Impact / Scrape / Roll event
slots per material. Edited in place and written with *Save Surface Table*.

**Dialogue Lines** — the `.ldialogue` *Dialogue Table*, default language, bark cooldown and radius,
then per-line speech event, priority, interruptibility and per-language subtitle text. *Save
Dialogue Table* writes it.

**Audio Accessibility** — the built-in caption/cue overlay, the runtime menu, default player
preferences and the caption fallback language.

**File Streaming Threshold** — audio longer than this many seconds (0–600) streams from disk
instead of loading into memory.

> Both table assets are size-capped (surface 1 MiB, dialogue 8 MiB). Exceeding the cap logs an
> error and saves nothing, leaving the previous file intact — so a "save did nothing" symptom on a
> very large table is this limit, not a bug.

---

## Audio components

**Add Component → Audio** offers all five, in this order:

| Component | What it does |
|---|---|
| **Audio Source** | Plays a Studio event from the entity |
| **Audio Portal** | A rectangular acoustic shutter that opens between two zones |
| **Audio Zone** | A region contributing to listener zone weights |
| **Audio Surface** | Overrides the acoustic material tag from the mesh collider |
| **Audio Listener** | The listening position (index 0–7, weighted) |

### Audio Source

The event picker is the main control: a **Bank** filter (defaults to *All banks*), a searchable
event list, and *None* to clear. It lists only events the loaded banks actually describe — if the
list is empty, the banks are not built or not loaded, which the Audio Debugger's Banks section will
confirm.

**Parameter Overrides** below it set authored Studio parameters per source: *Add Parameter*, pick
the name, set the value. These are serialized with the component and reapplied when the instance is
created.

Play On Awake, volume and pitch are on the same component. Looping is **not** — it is authored in
the event's Studio timeline, not in the engine.

### Audio Surface, Zone, Portal

`AudioSurfaceComponent` overrides the acoustic tag that `MeshColliderComponent` otherwise supplies;
without a mesh collider it is metadata only. A mesh collider's tag can come from its render material:
set **Material Editor → Acoustics → Acoustic Material** on the material (e.g. Bricks → Brick) and
keep **Inherit From Material** on the collider (new colliders default to it; colliders from older
scenes keep their explicit tag until you tick it). The collider shows the **Effective Acoustics**
tag actually in use. `AudioZoneComponent` and `AudioPortalComponent` feed
the zone-weight system — a portal can link two zone entities, and its selected wireframe is drawn
in the viewport. The engine-side rules for all three are in `Architecture-LuxEngine.md` §2.10.

---

## Where to go next

- [Panels](Panels.md) — every other panel
- [Viewport & Camera](Viewport-and-Camera.md) — the overlay the acoustics visualisation draws into
- `Editor/LuxSampleProject/FMOD_SETUP.md` — hooking a Studio project up, start to finish
- `.claude/docs/Architecture-LuxEngine.md` §2.10 — the runtime audio architecture
