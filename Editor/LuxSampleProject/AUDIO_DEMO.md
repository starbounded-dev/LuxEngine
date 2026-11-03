# Audio demo

`Assets/Scenes/AudioDemo.luxscene` plays two Sound assets through the built-in audio engine:
`Assets/Audio/Sounds/Fart.lsound` (a Random container with pitch variation) on the emitter you
control, and `Assets/Audio/Sounds/Music.lsound` (a looping, streaming bed) inside the building.
Nothing else needs to be installed. Acoustics come from the built-in Soundwave kernel.

## Run it

1. Open `Editor/LuxSampleProject/LuxSample.luxproj`, then `Assets/Scenes/AudioDemo.luxscene`.
2. Press **Play** and click the game viewport to give it keyboard focus. The emitter plays once on
   startup, and the console reports `Audio Demo ready`.
3. Hold the right mouse button and use **W/A/S/D** to move the listener. **Q/E** move down/up, and
   **Left Shift** speeds up. Walk around the walls and compare near and far positions.

| Key | Action |
| --- | --- |
| K | Restart the Audio Source component's Sound |
| L | Fire an extra one-shot at the emitter position |
| I | Start/restart an independently owned instance |
| P | Pause/resume the component's Sound |
| O | Stop the component and the independent instance |
| F | Open/close the door (the panel and the door's acoustic portal slide together) |
| C | Start/stop the showcase camera path |

One-shots finish on their own; O cannot cancel them. The emitter's `LuxSample.AudioDemo` script
drives playback (its `PlayOnStart` field), so the component's Play On Awake is off.

### See the Sound

During Play, open the viewport options (gear) and tick **See the Sound**. Each source gets a line from
the listener, green when clear and red where walls block it, with each wall labelled by material,
thickness and dB. From outside the building the music is muffled; press **F** and watch the
muffling go as the door opens. **Acoustic Materials** (same menu, works outside Play) tints each wall
by the material it sounds like. **C** flies the camera around the building, the easiest way to
record the demo.

## Author your own sounds

1. Drop a `.wav`, `.ogg`, `.mp3` or `.flac` into the Content Browser; it becomes an Audio asset.
2. Right-click **New > Sound**, double-click it, and add the clip in the **Sound Editor**: containers
   (random, sequence, layer, switch), volume and pitch variation, looping, streaming, 3D attenuation,
   parameters, and a bus.
3. Assign the Sound to an **Audio Source** (or a zone's ambience, the Music Director, a dialogue
   line, or a script's `Sound` field). Scripts play it with `Audio.PlayOneShot(sound)` or
   `Audio.CreateInstance(sound)`.
4. Shape the mix in **View > Audio Mixer**: buses, effects, reverb, snapshots and VCAs.

**File > Export Runtime** packs every Sound and its clips into the game; nothing ships beside it.
The editor manual's audio chapter (`docs/Editor/Audio.md`) covers each panel in detail.
