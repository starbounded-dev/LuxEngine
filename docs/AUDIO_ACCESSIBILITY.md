# Audio accessibility (Phase 12)

Phase 12 adds player preferences, subtitle/caption presentation, visual cues and narration to the
FMOD-backed engine. VA acoustics continue to run normally. There is no alternate playback backend.

## Set up the project

1. In FMOD Studio, route events to separate category buses: Music, SFX, Dialogue, UI and Ambience.
   Route dialogue and description events to Dialogue. Category buses must not contain one another.
   Leave the master mapping at `bus:/`. A sound routed directly to Master cannot receive category
   gain or narration ducking; route all sounds that should duck through a mapped category bus.
2. Build the banks and configure them in Lux **Project Settings > Audio** using the existing FMOD project and bank-output settings. Load the banks before starting the scene.
3. Expand **Audio Accessibility**. Enter the exact FMOD category bus paths. Empty mappings disable
   those sliders; a configured but missing bus reports an Audio error. Set the default player
   preferences and description duck level (0.25 means a quarter of the normal category gain).
4. Select events under **Add event caption/cue**, add them, and add localized captions such as
   `[door creaks]`. Captions and visual cues are separately optional. Category is the cue label;
   it does not change the event's FMOD routing. Set importance and world-space range.
5. Add speaker colors using the exact localized speaker names from the dialogue table. Configure
   dialogue and description lines through the existing [dialogue setup](AUDIO_DIALOGUE.md).
6. Save the project and start Play. Authoring changes apply on the next Play session. Press **F10**
   with the viewport focused to open the player menu. In the standalone runtime the menu pauses
   gameplay and releases the cursor, then restores the previous pause/cursor state when closed.
   **Dist exports have no F10 menu and no built-in caption/cue overlay** — the Dist runtime does
   not load ImGui. A game shipped as Dist must draw subtitles, captions and cues itself from the
   C# events below; the accessibility service, preferences and mixing still work.
7. Use **Save preferences** to persist player choices. **Restore project defaults** applies the
   authored defaults; save afterward to persist that reset.

The runtime export includes configuration in project format 22. Earlier exports use default
preferences and no caption metadata. Export again after changing banks or accessibility metadata.

## Presentation and mixing

The built-in overlay draws within the game image, including letterboxing in the editor. It supports
speaker names/colors, offscreen direction, wrapped text, font size (12–72), background opacity,
maximum displayed lines (1–10), and display duration (0.5–3 times playback duration). Newest entries
win when the line limit is reached. Pause freezes subtitle age. Shorter durations require a known
source duration; longer durations hold finished text based on its observed playback time. Enabling
subtitles or captions during an existing line takes effect for subsequent show notifications.

Player volume is an additional gain stage, preserving authored bus automation and gameplay bus
volume. Master and category gains multiply. Dialogue boost (1–2) affects Dialogue only. Mono uses
an FMOD channel-mix DSP on final output; Reduced and Night use compressor presets (respectively
-12 dB/3:1 and -24 dB/6:1). Zero volume is an exact mute. DSPs are created at scene setup or bank
changes, retained across frames, and detached before bank/system teardown.

Visual cues use listener-relative direction: X right, Y up, Z forward. Nonspatial sounds have zero
direction. Intensity combines authored importance, instance volume and linear falloff over the cue
range. It is an accessibility hint, not measured loudness or a substitute for VA occlusion. Player
bus volume does not remove cues, allowing them to work while sound is muted. The built-in indicator
uses horizontal direction; custom UI can also use the vertical component. Captions are range-gated
when playback begins and follow their source until it stops.

## C# integration

Call these APIs on the main thread in a running scene:

```csharp
Accessibility.Captions = true;
Accessibility.VisualCues = true;
Accessibility.Mono = true;
Accessibility.DynamicRange = AudioDynamicRange.Night;
if (Accessibility.HasBus(AudioCategory.Music))
    Accessibility.SetVolume(AudioCategory.Music, 0.6f);
Accessibility.DialogueBoost = 1.25f;
Accessibility.Save();

// Description is opt-in and shares dialogue priority/interrupt/queue rules.
Accessibility.AudioDescriptions = true;
DialogueHandle narration = Dialogue.Describe("door.description");
```

`Dialogue.SubtitleShown` and `Dialogue.SubtitleHidden` carry both speech and non-speech notifications. `Subtitle.IsCaption`
and `IsDescription` distinguish them. `Accessibility.SoundEvent` reports cue start/end; use
`Accessibility.GetSoundCues()` for the current moving positions, directions and intensities.
Subscribers receive final hide/end notifications at teardown and are cleared on script reset.
Raw notifications are not filtered by player display settings, so custom UI must respect them.
The cue snapshot is filtered by VisualCues and positive in-range intensity. `GetSpeakerColor(name)`
returns configured RGBA or white. Disable **Built-in caption/cue overlay and runtime menu** when
providing your own UI; the service and scripting APIs remain available. Dist exports always need
that custom UI, because the built-in overlay is ImGui and Dist runtimes do not include it.

Descriptions duck mapped Music/SFX/UI/Ambience while playing or fading, preserving Dialogue.
Disabling descriptions cancels active/queued narration. The game triggers authored description
keys; this feature does not generate narration or describe the scene automatically.

## Persistence and verification

Preferences live at `FileSystem::GetPersistentStoragePath()/AudioAccessibility/<project-name>.yaml`.
The project name is sanitized to letters, digits, hyphens and underscores. Linux uses
`$XDG_DATA_HOME/Editor` or `~/.local/share/Editor`; Windows uses `%APPDATA%/Editor`. Projects with the
same sanitized name share preferences. Writes replace a completed temporary file on the same
volume, including when a destination already exists. Invalid files report errors and retain defaults.

New C++ and C# files require project regeneration. On Linux run `./premake5 gmake2` and
`./premake5 --os=linux vs2022` (the latter regenerates ScriptCore.csproj), then build Core,
ScriptCore, Editor and Lux-Runtime. On Windows use `scripts/Win-GenProjects.bat --last`.

`python3 tests/audio/run.py` exercises real FMOD playback with a disposable fixture and no-sound
output. Accessibility checks cover bounded serialization, preference replacement/reload, mixer DSP
settings, independent bus gains, localized captions, moving cues, narration opt-in, bank/system recreation and teardown. It also
checks production ImGui overlay/menu drawing, viewport clipping and explicit newline limits.
`python3 tests/audio/run_managed.py` checks managed payloads, ABI layout, subscriptions and reset.
An interactive editor/runtime check and Windows build remain separate from these headless tests.
