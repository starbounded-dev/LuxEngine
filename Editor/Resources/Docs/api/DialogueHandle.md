# DialogueHandle

`public struct DialogueHandle`

A line of dialogue that was asked to play.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `ID` | `ulong` | get | The line's ID; 0 when the request was rejected or deduplicated. |
| `IsActive` | `bool` | get | Whether the line is queued or playing. |

## Methods

```
public void Stop(bool allowFadeOut = true)
```

Stops the line.

- `allowFadeOut`: True lets the event's authored fade-out play; false cuts it.

[All types](index)
