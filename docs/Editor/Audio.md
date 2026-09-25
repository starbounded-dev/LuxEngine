# Audio in the Editor

Everything the editor exposes for the audio stack: the **Audio Debugger** panel, the **Audio**
section of Project Settings, the five audio components in the Inspector, and the acoustics
visualisation drawn into the viewport.

> Scope: the editor-facing surface. The runtime design — `AudioEngine`, `AudioEventInstance`,
> `RaytracedAudioScene`, bank lifetime, zones, music, dialogue, budgets — is documented in
> `.claude/docs/Architecture-LuxEngine.md` §2.10. Authoring workflow for the sample project lives in
> `Editor/LuxSampleProject/FMOD_SETUP.md`.

The stack is **FMOD Studio** for playback and **Vercidium Audio (VA)** for ray-traced acoustics.
Both are mandatory SDKs; the editor cannot be built without them.

---

## The map

| Where | What it covers |
|---|---|
| **View → Audio Debugger** | Read-only runtime state: backends, banks, events, voices, acoustics, budgets, validation |
| **Project Settings → Audio** | Studio project wiring, budgets, desktop profiles, surface/dialogue tables, accessibility |
| **Inspector → Add Component → Audio** | Audio Source, Portal, Zone, Surface, Listener |
| **Viewport** | Ray paths, bounce points, emitters, zone/portal wireframes |

---

## Audio Debugger

### What it is

A read-only window onto the live audio stack. It is **pure visualisation** — it reads data the two
backends already publish (`AudioEngine::GetStats` / `GetReverbSnapshot`, and
`RaytracedAudioScene::GetStats` / `GetResult` / `GetAmbience` / `GetVisualisation`) and adds no
instrumentation of its own. Nothing you do here changes what you hear, with one exception: the
visualisation controls, which drive debug raytracing work.

Registered in `EditorLayer.cpp:393` under `PanelCategory::View` as `"AudioDebugPanel"`. It is
**closed by default**; the default layout docks it to the bottom region (`EditorLayer.cpp:1025`).

### How to use it

Open it with **View → Audio Debugger**. Most of it only has content in Play mode — outside Play the
acoustics sections report `Idle (enter Play)`.

Sections, in draw order:

| Section | What it tells you |
|---|---|
| **Backends** | Whether FMOD and VA initialised, the acoustics backend name, state and SDK version |
| **Playback** | Channels playing, DSP CPU, event instance count |
| **FMOD Studio** | Studio system state and live-update status |
| **Banks** | Every loaded bank — name, event count, kind |
| **Events** | Searchable table of events the loaded banks describe — path, kind, GUID |
| **Acoustics** | World bounds, acoustic/dynamic triangle and primitive counts, worker timings |
| **Ambience (listener)** | Ambient gain LF/HF and energy returned/outside at the dominant listener |
| **Analysis / Reverb** | The EAX-style reverb VA computed — decay time, density, diffusion, echo, HF/LF ratios |
| **Sources** | Per-source table: entity, event, distance, playback state, and the acoustics VA resolved for it — occlusion, gain LF/HF, muffle |
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
| Show Occlusion Paths | off | Each source's line from the listener, green (clear) to red (blocked), with every wall it crosses marked and labelled `Brick 0.30 m -9.0 dB`, plus the source's total. Drawn through walls so the blocked part stays visible. *Wall Labels* and *Selected Source Only* refine it. Engine occlusion only; costs nothing extra |
| All Zones and Portals | off | Every zone and portal, brightened by its live weight (zones) or how open it is (portals), labelled with the number |
| Room Acoustics Readout | off | Top-left of the viewport: returned vs escaping energy, LF/HF decay, absorption and scattering, as VA measures the listener's room |
| Colour Bounces by Material | off | Tints each VA bounce point with the acoustic material it landed on (one short raycast per bounce while shown) |

**Acoustic Materials** (viewport options popup, edit mode and Play) tints every surface the acoustics
see — non-Disabled mesh colliders and closed portal shutters — by its effective acoustic material,
with a legend of the materials in the scene. It replaces the physics collider view while on.

The VA ray controls below are the SDK's own debug rays:

| Control | Default | Notes |
|---|---|---|
| Show VA Rays | **off** | The ray settings below are inert until this is on |
| Ray Count | 64 | Passed straight to the SDK |
| Bounce Count | 4 | Passed straight to the SDK |
| Update Interval | 50 ms | How often the debug trace refreshes |
| Draw Ray Paths | on | Faded along their length, listener end brightest |
| Draw Bounce Points | on | |
| Draw Emitters | on | Radius 0.35 |
| Draw Surface Normals | off | Length 0.25 |
| Draw World Bounds | off | VA's world box |

> **These cost real raytracing work and feed nothing back into the audio.** They are a diagnostic
> view, off by default and deliberately modest when on. Turn the whole section off when you are
> done looking at it.

### How it was built

Two decisions in `AudioDebugPanel.h` are worth knowing before you change anything:

**It does not branch on `LUX_ENABLE_FMOD` / `LUX_ENABLE_RAYTRACED_AUDIO`.** Those defines are set
only for the Core project (`Core/premake5.lua`), so the Editor cannot see them. Each backend
identifies itself through its stats struct instead. Adding an `#ifdef LUX_ENABLE_FMOD` here
compiles to nothing and silently removes the section.

**The visualisation settings live in the panel, but the panel does not draw them.**
`AudioVisualisationSettings` is owned by `AudioDebugPanel` because that is where it is edited, and
read by `EditorLayer::DrawAudioVisualisation`, called from `OnOverlayRender`, which owns the only
`Renderer2D` with a camera already set up for the frame. It also applies the VA ray settings every
frame, so they work with the panel closed. The panel is the editing half; the overlay is the
drawing half.

**Nothing in the overlay reads VA's live results.** VA's workers write them between frames, so the
scene copies what the overlay needs (the applied occlusion, the room ambience) inside the window
where that is safe, and the overlay reads the copies.

### How to modify

- **Add a readout:** extend the relevant backend's stats struct first, then add a row. Do not reach
  into FMOD or VA from the panel — that would put instrumentation in the editor, where it cannot be
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

**Voice and Performance Budgets** — Real Voices, CPU warning %, VA warning (ms of worker raytracing
time), FMOD memory (MiB), per-bus voice limits, and *Mute When Unfocused*. These drive the warnings
the Audio Debugger's budget section reports.

**Desktop Audio Profiles** — optional per-OS overrides (*Windows*, *Linux*). Each has *Override
defaults*, a *Studio Platform* name that must match a platform configured in your Studio project,
a *Bank Output* directory, and its own budgets. *Copy Default Budgets* seeds a profile from the
project defaults. The host OS selects its enabled profile for bank builds, Play, validation and
export.

**Zone Reverb** — how zone reverb and VA combine: *Layer zones and VA*, *Prefer zones*, or
*Prefer VA*.

**Occlusion** — what muffles a source behind a wall, through its event's `Occlusion` parameter:
*Engine raycasts* (default) or *VA (raytraced)*. Engine occlusion casts from the listener to each
source through acoustic mesh colliders and closed portals and costs each wall 30 dB per its
material's transmission distance (a 0.3 m brick wall is about 18 dB). *Occlusion Strength* scales
every wall (1 follows the material data); *Rate* and *Casts / Frame* bound the cost. VA's own
occlusion does not respond to geometry in the current SDK. Applies on the next Play. The Audio
Debugger's validation warns about source events with no `Occlusion` parameter.

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
| **Audio Portal** | A rectangular VA shutter that opens between two zones |
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
