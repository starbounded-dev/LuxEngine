# Audio

Sound is authored as **Sound** assets in the editor: a Sound carries its clips, variation, bus and 3D settings, so a component only places and triggers it. [Authoring Audio](../editor/audio-authoring) walks through it.

## Audio Source

Plays one Sound from an entity.

- **Sound** — the Sound to play.
- **Play On Awake** — start when the game starts.
- **Volume** and **Pitch** multipliers, and **Parameter Overrides** that set the Sound's own parameters for this source, so two entities can share one Sound and still sound different.
- **Priority** and **Distance Culling** keep busy scenes within the voice budget.
- Switching the component off stops the sound.

## Audio Listener

The ears of the scene. Put it on the camera or on the player.

## Acoustics

The engine can trace how sound travels through the level: walls muffle, rooms reverberate and doorways let sound through. **Audio Zone**, **Audio Portal** and **Audio Surface** components describe rooms, openings and what surfaces are made of. The **Audio Debugger** panel shows what the listener hears and why.

## From a script

```
AudioSourceComponent source = GetComponent<AudioSourceComponent>();
source.SetParameter("Speed", 3.0f);
if (!source.IsPlaying)
	source.Play();
```
