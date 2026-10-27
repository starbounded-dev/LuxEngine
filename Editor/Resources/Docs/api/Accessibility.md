# Accessibility

`public static class Accessibility`

Player audio preferences. Call on the main thread in a running scene. Save explicitly to persist. Project mappings connect category sliders to authored FMOD buses.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `AudioDescriptions` | `bool` | get, set | Play audio-description lines. |
| `BackgroundOpacity` | `float` | get, set | Opacity of the subtitle background, 0 to 1 (default 0.75). |
| `Captions` | `bool` | get, set | Show closed captions for sounds. |
| `DialogueBoost` | `float` | get, set | Extra dialogue volume over the rest of the mix (default 1). |
| `DirectionIndicators` | `bool` | get, set | Show where off-screen speakers and sounds are. |
| `DurationMultiplier` | `float` | get, set | How much longer subtitles stay up (default 1). |
| `DynamicRange` | `AudioDynamicRange` | get, set | How much the mix is compressed. |
| `MaxLines` | `int` | get, set | The most subtitle lines shown at once (default 3). |
| `Mono` | `bool` | get, set | Mix everything down to mono. |
| `SpeakerNames` | `bool` | get, set | Show the speaker's name with subtitles. |
| `Subtitles` | `bool` | get, set | Show subtitles for speech. |
| `TextSize` | `float` | get, set | Subtitle text size in pixels (default 24). |
| `VisualCues` | `bool` | get, set | Show visual cues for important sounds. |

## Events

| Name | Type | Description |
|---|---|---|
| `SoundEvent` | `Action<SoundCue>` | Raised when a sound marked for visual cues starts or ends. |

## Methods

```
public static SoundCue[] GetSoundCues()
```

The sounds currently playing that are marked for visual cues.

```
public static Vector4 GetSpeakerColor(string name)
```

The subtitle color set for a speaker in Project Settings.

```
public static float GetVolume(AudioCategory category)
```

A category's volume, 0 to 1.

```
public static bool HasBus(AudioCategory category)
```

Whether the project maps the category to an FMOD bus (so its slider does something).

```
public static void Save()
```

Saves the preferences so they persist across runs.

```
public static void SetVolume(AudioCategory category, float volume)
```

Sets a category's volume, 0 to 1.

[All types](index)
