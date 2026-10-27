# Dialogue

`public static class Dialogue`

Scene-owned dialogue. Configure a .ldialogue table in Project Settings > Audio. Call only from the main thread. Rejected/deduplicated requests return a handle with ID zero.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `QueueLength` | `int` | get | How many lines wait in the queue. |

## Events

| Name | Type | Description |
|---|---|---|
| `SubtitleHidden` | `Action<Subtitle>` | Raised when a subtitle should disappear. |
| `SubtitleShown` | `Action<Subtitle>` | Raised when a subtitle should appear. |

## Methods

```
public static DialogueHandle Bark(string key, Entity speaker)
```

Plays a short ambient line from an entity, outside the queue. Repeats of the same line nearby are skipped for a cooldown set in the dialogue table.

- `key`: The line's key.
- `speaker`: The entity speaking.

```
public static DialogueHandle Describe(string key)
```

Queue an authored description line when audio descriptions are enabled. Route its FMOD event to the mapped Dialogue bus so other categories can be ducked.

```
public static bool IsActive(DialogueHandle handle)
```

Whether a line is queued or playing.

```
public static bool IsSpeaking(Entity speaker)
```

Whether an entity is speaking a line.

```
public static void SetLanguage(string language)
```

Switches the dialogue language (a language code in the dialogue table, such as `en`).

```
public static void SetQueueMode(DialogueQueueMode mode)
```

Sets what [`Dialogue.Speak`](Dialogue) does while another line is playing.

```
public static DialogueHandle Speak(string key, Entity? speaker = null)
```

Plays a line from the dialogue table, following the queue mode.

- `key`: The line's key.
- `speaker`: The entity speaking, for position and subtitles, or null.

```
public static void Stop(DialogueHandle handle, bool allowFadeOut = true)
```

Stops a line.

```
public static void StopAll()
```

Stops every line and clears the queue.

[All types](index)
