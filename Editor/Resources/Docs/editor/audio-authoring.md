# Authoring Audio in FMOD Studio

Sound in LuxEngine is designed in **FMOD Studio**, FMOD's free sound-design application. You build **events** there (a footstep, a door, a music track, an ambience), with their sounds, randomisation, effects and 3D settings. FMOD Studio packs them into **banks**, and the engine loads the banks and plays the events. The editor places and triggers events; it does not edit sounds.

## Connect the FMOD Studio project

Open **Project Settings → Audio**:

- **Studio Project (.fspro)** — the FMOD Studio project, relative to the project's `Assets` folder. Keep it inside the game project so it travels with it.
- **Bank Output** — where FMOD Studio writes built banks, relative to the `.fspro`'s folder. FMOD's default is `Build/Desktop`.
- **Studio Platform** — the platform name as it appears in FMOD Studio (usually `Desktop`).
- **Rebuild Banks On Play** — before Play, rebuild the banks if the Studio project changed since the last build. It costs nothing when nothing changed.
- **Live Update** — lets FMOD Studio connect to the running editor and mix while the game plays. Takes effect the next time the project opens.

The status line underneath shows how many banks and events are loaded. **Build Banks Now** builds them, and **Open In FMOD Studio** opens the project. Building needs FMOD Studio installed: the editor runs its command-line builder, `fmodstudiocl`.

In FMOD Studio, build with **File → Build**. The engine loads every bank in the output folder; the **Master.strings.bank** lets the editor and scripts refer to events by path (`event:/Doors/Open`) as well as by GUID.

## Use events in the scene

- An [Audio Source](../components/audio) plays an event from an entity: pick the **Event**, and tick **Play On Awake** to start it with the game.
- An **Audio Listener** on the camera or player is where the game hears from.
- Scripts play events with `Audio.PlayOneShot("event:/UI/Click")`, or keep one with `Audio.CreateInstance` to start, move and stop it. See the [Audio API](../api/Audio).

## Parameters the engine drives

Name a parameter on your event like this and the engine sets it for you. Each is optional: an event without it simply ignores it.

| Parameter | Set by | Values |
|---|---|---|
| `Occlusion` | Walls and closed doors between the event and the listener | 0 clear to 1 fully blocked. Drive a low-pass filter or volume with it so walls muffle the sound. |
| `ReverbSend` | Ray-traced acoustics, by how reverberant the space is | 0 to 1. Drive a send to your reverb return. |
| `Speed`, `Weight`, `Surface` | Footsteps played by `Audio.PlayFootstep` | Walking speed, character weight in kg, and the surface's acoustic material number. |
| `Impulse`, `Surface` | Physics impact sounds | The collision's impulse and the surface. |
| `State` | `Music.SetState` | A labeled parameter: one label per musical state (`Explore`, `Combat`...). |
| `Intensity` | `Music.Intensity` | 0 to 1. |
| `Layer_<name>` | `Music.SetLayerEnabled("<name>", ...)` | 0 or 1, to fade a layer in and out. |

A snapshot started with `Audio.StartSnapshot` or an Audio Zone needs an exposed **Intensity** parameter from 0 to 100.

## Mixing for the player

**Project Settings → Audio → Audio Accessibility** maps the player's volume sliders (Master, Music, SFX, Dialogue, UI, Ambience) to buses of your FMOD mix, sets captions and visual cues per event, and gives speakers subtitle colours. Players change their own settings in the **F10** menu while the game runs.

## Budgets and checks

**Voice and Performance Budgets** sets FMOD's real-voice limit and the memory and CPU levels that log a warning. **View → Audio Debugger** shows live voices, meters and the problems the engine found, such as an event missing from the built banks or an occludable event without an `Occlusion` parameter.
