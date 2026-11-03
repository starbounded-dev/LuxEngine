# Authoring Audio

Sound in LuxEngine is authored in the editor, with nothing else to install. An **audio file** (`.wav`, `.ogg`, `.mp3`, `.flac`) becomes a **Sound** (`.lsound`): which clips play, with what variation, how loud, where in the mix, and how it behaves in 3D. **Sound Banks** (`.lbank`) group Sounds that load together, and the **Audio Mixer** shapes the mix.

## Make a Sound

- Drop an audio file into the Content Browser. Right-click it and choose **Create Sound** (several selected files make one Random Sound that plays a different one each time), or use **New › Sound**.
- Double-click the Sound to open the **Sound Editor**: a tree of clips and containers (Random, Sequence, Layer, Switch), volume and pitch with random variation, looping and loop regions, streaming for long music, 3D attenuation, parameters with curves, an instance limit, and the bus it plays on. **Play** auditions it in edit mode.

## Use Sounds in the scene

- An [Audio Source](../components/audio) plays a Sound from an entity: pick the **Sound**, and tick **Play On Awake** to start it with the game.
- An **Audio Listener** on the camera or player is where the game hears from.
- Scripts play Sounds with `Audio.PlayOneShot(sound)` or by path, `Audio.PlayOneShot("Audio/Sounds/Click")`, or keep one with `Audio.CreateInstance` to start, move and stop it. See the [Audio API](../api/Audio).

## Load Sounds with Sound Banks

A Sound that no loaded bank holds decodes when it first plays, which can stall that frame. Put each scene's Sounds in a Sound Bank:

- **New › Sound Bank** (with Sounds selected, they go in), or the Sound Editor's **Sound Banks** section.
- Double-click a bank to open the **Sound Bank Editor**: its Sounds, the memory it holds, and **Resident** for Sounds every scene uses.
- List the bank under **View › Audio Mixer › Scene Sound Banks**. The scene loads it before it plays and frees its clips when it stops. Scripts can also call `Audio.LoadBank` and `Audio.UnloadBank`.

## Parameters the engine drives

Add a parameter with this name to a Sound and the engine sets it. Each is optional.

| Parameter | Set by | Values |
|---|---|---|
| `Speed`, `Weight`, `Surface` | Footsteps played by `Audio.PlayFootstep` | Walking speed, character weight in kg, and the surface's acoustic material (a Switch picks the clip). |
| `Impulse`, `Surface` | Physics impact sounds | The collision's impulse and the surface. |
| `State` | `Music.SetState` | A labeled parameter: one label per musical state (`Explore`, `Combat`...). |
| `Intensity` | `Music.Intensity` | 0 to 1. |
| `Layer_<name>` | `Music.SetLayerEnabled("<name>", ...)` | 0 or 1, to fade a layer in and out. |

Walls and closed doors muffle every 3D Sound on their own (a low-pass and a gain), and the space's reverb sends it to the mixer's reverb return; no parameter is needed.

## Mixing for the player

**View › Audio Mixer** holds the project's buses, effects, reverb, snapshots and VCAs. **Project Settings › Audio › Audio Accessibility** maps the player's volume sliders (Master, Music, SFX, Dialogue, UI, Ambience) to those buses, sets captions and visual cues per Sound, and gives speakers subtitle colours. Players change their own settings in the **F10** menu while the game runs.

## Budgets and checks

**Voice and Performance Budgets** sets the real-voice limit and the clip memory and CPU levels that log a warning. **View › Audio Debugger** shows live voices, meters, the loaded Sound Banks and clips, and the problems the engine found, such as a Sound in no bank or a source with no Sound.
