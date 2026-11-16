# Dynamic acoustic geometry and portals (Phase 13)

> **Point-in-time record.** This guide describes the FMOD-era audio system. FMOD was removed in
> November 2026: audio is now built in (Sound assets, Sound Banks, the Audio Mixer). Current
> behaviour is in [docs/Editor/Audio.md](Editor/Audio.md) and `.claude/docs/Architecture-LuxEngine.md` §2.10.

Phase 13 keeps acoustic geometry synchronized with moving mesh colliders and adds acoustic shutters
that can connect two audio zones. FMOD continues to own event playback and authored effects.

## Moving objects

1. Add a Mesh Collider to the object and select its collider mesh/submesh.
2. In the inspector, set **Acoustic Motion** to **Dynamic** for moving doors, lifts or platforms.
3. Set the collider's acoustic material, or add Audio Surface to override it.
4. Enter Play and move the entity through scripts or physics. Parent transforms are included.
5. Open **View → Audio Debugger**. The Soundwave section shows static/moving triangle and mesh
   instance counts, plus pending geometry layers.

| Acoustic Motion | Behavior |
|---|---|
| Static (default for old scenes) | Captures the initial world transform. Material and mesh/submesh selection changes still apply during Play. |
| Dynamic | Tracks changes to the entity's world transform; the moving snapshot is rebuilt on a worker. |
| Disabled | Omits this mesh from acoustics. Physics collision remains independent. |

Changing from Static to Dynamic follows the current transform; switching back captures that
position. Transform updates reuse shared local mesh data. Box/sphere/capsule physics colliders do
not become acoustic geometry; render meshes do (see the Architecture doc's Soundwave section). Mesh deformation and in-place mesh
asset hot reload are outside this phase; restart Play after those edits.

## A door between rooms

1. Author two room entities with **Audio Zone** components and their ambience/snapshot events.
2. Leave an actual doorway in the wall's acoustic mesh.
3. Place another entity in the doorway and add **Audio Portal**.
4. Set **Half Extents** to half the opening's width (local X), height (Y), and shutter thickness (Z).
5. Choose the shutter **Material** and assign the two room entities to **Room A** and **Room B**.
6. Set **Open** between 0 and 1. At 0 the rectangular shutter fills the opening; as Open rises,
   it retracts toward local -X. At 1 it no longer blocks sound.
7. Tune **Blend Distance** for how far from the opening the other room's ambience/snapshot leaks.

The selected portal shows the full opening and remaining shutter as wire boxes. Portal geometry
is acoustic only: animate the visible door and its physics separately. A swinging door can instead
use a Dynamic mesh collider when its actual shape/motion matters more than a retracting shutter.

A portal does **not** cut a hole through other geometry. If its physical door also has a mesh
collider, set that collider's Acoustic Motion to Disabled when the portal supplies the barrier.
Otherwise the overlapping mesh continues to block sound after the portal opens.

Room links are optional for the shutter. With both unset it still blocks sound. Invalid or missing
room links disable room transfer and report an error; the linked rooms must be different Audio
Zone entities. Disabling the portal removes its shutter and disables its room transfer.

Room transfer is a one-hop ambience approximation. A fully open portal transfers up to half of a
room's listener weight to its neighbor near the opening, falling to zero at Blend Distance. The
open factor scales this transfer. Multiple portals share the available source weight, so they
cannot amplify it or feed a cycle indefinitely. Existing zone fades smooth the resulting weights.
Soundwave clips each source's path against the shutter's box analytically, so a closed shutter
muffles sources behind it, shrinking with Open, and stops once Open reaches 1. Author the usual
`Occlusion` / `ReverbSend` event parameters in FMOD. Standalone scripted one-shots receive no scene
acoustics. Tune the overall amount with **Project Settings > Audio > Transmission Strength**.

## C# control

On the entity that owns the portal:

```csharp
var portal = GetComponent<AudioPortalComponent>();
portal.Open = 1.0f; // Open the acoustic shutter.
portal.Material = AcousticMaterial.Wood;
portal.BlendDistance = 4.0f;

// On a moving mesh collider:
var collider = GetComponent<MeshColliderComponent>();
collider.AcousticMotion = AcousticGeometryMode.Dynamic;
```

`Enabled`, `HalfExtents`, `ZoneA`, and `ZoneB` are also exposed. Room properties accept another
`Entity` with Audio Zone, or null to clear a link. Calls require the main thread. Invalid values
are rejected with managed exceptions. Changes persist in editor scene/prefab serialization;
runtime changes follow the editor's existing Play-mode persistence behavior.

## Scheduling and failure behavior

Captured geometry changes queue a rebuild of the static or moving snapshot on a JobSystem worker;
the previous snapshot keeps serving solves until the new one publishes, so main never waits.
Deleted or disabled geometry is masked at once, before its rebuild finishes. Edits coalesce to the
latest state; a failed rebuild reports the failure and retains the last valid snapshot until the
input changes. Play pause freezes geometry updates. Zone transfer uses the portal's current Open.

## Verification

Regenerate projects after pulling this phase: `scripts\Win-GenProjects.bat` on Windows, or the Linux
build script. Build Core, ScriptCore, Editor and Lux-Runtime.

`python3 tests/audio/run.py` exercises Soundwave's snapshot jobs (publication, coalescing, layer
migration, retries, detached mesh ownership), shutter clipping and validation, and portal
room-weight conservation. It also runs the existing FMOD regression suites and production
portal YAML validation. `python3 tests/audio/run_managed.py` checks managed portal dispatch and
main-thread/value guards. The scene serializer's round-trip self-tests include portal fields and
room-reference remapping through duplicate/prefab operations.

Interactive door animation, audible tuning and Windows execution still need a local editor check;
headless tests do not establish visual quality or a measured acoustic response for a particular room.
