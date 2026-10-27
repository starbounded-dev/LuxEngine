# Subtitle

`public class Subtitle`

A subtitle, caption or description line, for drawing your own subtitle UI.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Duration` | `float` | get | Source sound length in seconds; zero if FMOD has not supplied a length. Hide on SubtitleHidden, since authored fades and interruptions can change playback time. |
| `Handle` | `DialogueHandle` | get | The line it belongs to. |
| `IsCaption` | `bool` | get | Whether this is a closed caption for a sound rather than speech. |
| `IsDescription` | `bool` | get | Whether this is an audio-description line. |
| `IsOffScreen` | `bool` | get | Whether the speaker is off screen (to show a direction arrow, say). |
| `Key` | `string` | get | The line's key in the dialogue table. |
| `Language` | `string` | get | The language of [`Subtitle.Text`](Subtitle). |
| `SpeakerEntityID` | `ulong` | get | The speaking entity's ID, or 0. |
| `SpeakerName` | `string` | get | The speaker's name in this language, or empty. |
| `SpeakerPosition` | `Vector3` | get | Where the speaker was when the line started. |
| `Text` | `string` | get | The text to show. |

[All types](index)
