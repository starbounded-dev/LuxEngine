# Surface and physics audio (Phase 9)

Phase 9 connects 3D Jolt contacts and ground queries to FMOD Studio events. A shared surface table
maps each `AcousticMaterial` to footstep, impact, scrape and roll sounds. VA continues to use the
same material tags for acoustic geometry; FMOD authors the sound and its spatialization.

## Set up a project

1. In FMOD Studio, author the events below. Use local, writable continuous parameters with these
   exact names. Set ranges appropriate to your game. Add spatialization to events that should
   sound positional, and assign them to a bank included in the project's runtime bank manifest.

   | Event slot | Playback | Parameters supplied by Lux |
   |---|---|---|
   | Footstep | One-shot | `Speed` (m/s), `Weight` (kg), `Surface` (material enum value) |
   | Impact | One-shot | `Impulse` (estimated kg m/s), `Surface` |
   | Scrape | Continuous | `Speed` (contact-point slip m/s), `Surface` |
   | Roll | Continuous | `Speed` (tangential centre motion minus slip, m/s), `Surface` |

   Use an FMOD parameter sheet or conditions to vary the sound by surface and strength. Snapshot
   events are not accepted in these slots. Continuous events must remain alive until stopped;
   configure release behavior in FMOD if they should fade out.

2. Build/load the banks. In Content Browser, choose **New → Audio Surface Table**.
3. Assign the `.lsurfaces` asset in **Project Settings → Audio → Surface Table**.
4. Expand **Surface Sounds**, choose the events for each material, tune the minimum impulse,
   motion speed and impact cooldown, then click **Save Surface Table**. An empty slot uses the
   corresponding **Default** slot. Empty Default slots produce no sound.
5. Put **Audio Surface** on floor/wall entities to choose their material and optional footstep or
   impact override. Without this component, a mesh collider's acoustic material is used; other
   colliders use Default. Compound colliders currently share their entity's material.
6. Dynamic 3D bodies produce impacts and motion sounds against these colliders. A dynamic body's
   **Physics Sounds** toggle disables its automatic contact audio. Its Impact Override takes
   precedence over the opposing collider's override and table entry. When both bodies are dynamic,
   the first body in Jolt's stable pair ordering owns playback and the other supplies the material.
7. For automatic footsteps, put **Audio Surface** on the moving entity and enable **Auto Footsteps**.
   Set **Stride Length** to the horizontal distance per footstep and **Ground Probe** to the distance
   from the entity origin to its feet, with a small tolerance. The downward query excludes the
   moving entity and triggers, and requires a walkable upward normal. This does not implement
   character movement or ground collision response. Teleports reset cadence; there is no backlog
   of footsteps after a large movement.

For script-controlled timing, leave Auto Footsteps disabled and call:

```csharp
// Inside a Lux.Entity script, at a footfall in your movement/animation logic:
bool played = Audio.PlayFootstep(this, speed: 3.5f, weight: 75.0f, probeDistance: 1.2f);
```

The collider underfoot supplies the footstep override and material. A missing ground hit, paused
scene or unassigned/unavailable event returns `false`. Animation-notify integration can call this
same API when the animation system supplies notifications.

## Runtime behavior

- Jolt workers capture immutable contact data. The scene drains it after simulation and resolves
  entity IDs/materials on the main thread. Audio never reads ECS or calls FMOD from Jolt jobs.
- Jolt reports contacts before solving. `Impulse` uses Jolt's `EstimateCollisionResponse`; it is
  not the final solver impulse and is less accurate in simultaneous multi-body collisions.
- Speculative contacts do not play audio. First real contact can arrive through Persist.
- Impacts have a per-dynamic-body cooldown. Compound manifolds share one scrape and roll voice per
  body pair; ending one manifold does not stop a voice while another is still moving in contact.
- Contact end and body sleep stop motion voices. Fade-outs retain their instances until FMOD stops
  them. Entity destruction and Play stop release voices immediately. Pause freezes cooldowns and
  pauses voices; bank unload/reload invalidates and recreates active motion instances safely.
- This phase targets 3D Jolt. Box2D contact audio is not connected.
- Runtime project format 20 stores the surface-table handle after the version-19 reverb setting.
  Older projects default to no table. The assigned table is packed with each runtime scene, so it
  remains available across scene transitions. Missing/invalid table handles fail export, and asset
  serializer or output-stream failures now propagate to the export caller.

## Verification

Regenerate projects after pulling the new source files (`scripts/Win-GenProjects.bat --last` on
Windows, or the Linux project generator), then rebuild Core, Editor, Lux-Runtime and ScriptCore.

`python tests/audio/run.py` runs Debug-mode production audio code with real FMOD and Jolt, using
FMOD's no-sound output and a disposable authoring project. See [test prerequisites](../tests/audio/README.md).
The scene serializer's existing round-trip self-test also checks the new surface fields through
copy, duplicate, prefab and serialization paths.

Interactive audible mixing, Windows execution and a visual inspector check still need a normal
editor session; headless SDK tests do not substitute for those checks.
