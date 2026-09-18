# Audio zones and snapshots — Phase 8

Audio zones blend authored FMOD ambience and mixer snapshots as listeners move through the scene.
They work in the editor's Play mode and exported runtime. FMOD Studio remains responsible for the
sound, routing, spatialization, effects and snapshot mixer scope.

## Authoring a zone

1. In FMOD Studio, create a looping ambience event and assign it to a bank. Choose 2D for a global
   ambience bed, or 3D if you want Studio to spatialize it at the zone's center.
2. Create a snapshot and include the buses/effects it should change. **Right-click the snapshot's
   Intensity dial and choose Expose as Parameter.** Keep its name `Intensity` and its continuous
   range 0–100. Do not substitute an unrelated parameter named Intensity. A local continuous
   parameter named Intensity with a linear 0–100 automation curve on the dial is also supported.
3. Build the banks. In LuxEngine's Project Settings → Audio, set the Studio project and bank output
   directory, then build/reload banks using the existing FMOD controls. Enable rebuild on Play if
   desired. The event and snapshot appear in the zone's separate pickers.
4. Add **Audio Zone** to an entity. Choose Box or Sphere and edit its dimensions and offset.
   Alternatively choose Collider and add exactly one Box, Sphere or Capsule Collider to that
   entity. Collider mode uses the collider's dimensions and offset; no rigid body or trigger is
   required. Mesh and 2D colliders do not define audio zones.
5. Assign the ambience event, snapshot, or both. Set `Blend Distance` in metres and `Fade Time` in
   seconds. Blend distance runs **inside** the boundary: zero influence at the edge, full influence
   that distance inside. A zero distance makes a hard boundary. Small volumes may never reach full
   weight if their blend distance exceeds their interior depth.
6. Add an active Audio Listener, enter Play, and move it through the volume. The inspector shows
   the current weight. Selecting a zone shows its outer boundary and a faint inner full-weight
   boundary; the inner boundary is absent when the full-weight interior is empty.

Higher-priority zones take the available mix first. Equal-priority zones share their combined
coverage; lower-priority zones receive the remainder. Active listener weights are normalized.
A listener with an attenuation target uses that target for zone occupancy. With no active listener,
all zone weights fade to zero. Pause freezes the fade and pauses the owned events.

Multiple zones using the same snapshot share one instance with their summed, capped intensity.
FMOD averages separate instances of the same snapshot, so independently starting that snapshot
from scripts can change the resulting mix. Studio's own snapshot priority and scope still apply.

## Zone reverb and Vercidium Audio

Project Settings → Audio → **Zone Reverb** is saved with the project and included in exports:

| Setting | Result |
|---|---|
| Layer zones and VA | Both zone snapshots and VA-driven event reverb are active. Default. |
| Prefer zones | Active zone snapshot coverage reduces the VA `ReverbSend` parameter. |
| Prefer VA | Zone snapshots are suppressed while VA has valid ambience; otherwise zones apply. |

Ambience beds and VA occlusion are independent of this choice. The engine adjusts authored Studio
parameters; it does not create a second reverb DSP path. Missing banks or snapshots without the
required intensity parameter are reported in the Audio log and do not suppress VA reverb.

## C#

Call on the main thread during Play, after banks load:

```csharp
private EventInstance caveSnapshot;
private bool stopping;

void EnterCave()
{
    stopping = false;
    caveSnapshot = Audio.StartSnapshot("snapshot:/Cave", 0.25f);
    caveSnapshot.SetSnapshotIntensity(0.8f); // Normalized 0–1, not 0–100.
}

void LeaveCave()
{
    caveSnapshot?.Stop(allowFadeOut: true);
    stopping = true;
}

void OnUpdate(float dt)
{
    // Keep ownership until Studio's authored fade-out finishes.
    if (stopping && caveSnapshot != null && !caveSnapshot.IsPlaying)
    {
        caveSnapshot.Dispose();
        caveSnapshot = null;
    }
}

void OnDestroy()
{
    caveSnapshot?.Dispose(); // Immediate teardown, also handled when the scene stops.
}
```

`AudioZoneComponent` exposes `Enabled`, `Shape`, `Offset`, `HalfExtents`, `Radius`, `Priority`,
`BlendDistance`, `FadeTime`, `Volume`, and read-only `Weight`. Use `SetAmbience(reference)` and
`SetSnapshot(reference)` with a loaded GUID or path; pass an empty string to clear either assignment.
Scene-owned zones need no manual event lifetime management.

## Compatibility and verification

New scene keys are optional; older scenes keep their existing behavior. Runtime project format 19
stores the reverb policy after the material settings. Versions 16, 17 and 18 load with Layered as
the default, preserving following physics/render/script fields. Re-export to include the new mode.
Regenerate Premake projects because Phase 8 adds native source files.

Verification for this implementation covers production zone geometry and priority blending,
weighted/attenuation listeners, real FMOD SDK snapshot intensity and instance coalescing, ambience
volume/fade/pause, VA policy, bank/system reload and cleanup. The headless FMOD fixture uses a
continuous Intensity parameter automating the snapshot dial. Project serializer tests cover YAML,
format 19, older-format defaults and invalid/truncated mode bytes. Scene YAML tests cover every
zone field and malformed values; the built-in scene roundtrip suite also covers copy/prefab data.
Linux Release Core, Editor and Lux-Runtime and managed assemblies are built separately from the
Debug-mode headless checks. Audible mixing, viewport appearance and Windows execution still need
an interactive check.

Weather/time-of-day automation, random ambient one-shots, mesh-shaped zones and portals are not
part of Phase 8.
