# DialogueQueueMode

`public enum DialogueQueueMode`

What [`Dialogue.Speak`](Dialogue) does while another line is playing.

## Values

| Name | Value | Description |
|---|---|---|
| `Interrupt` | 0 | The new line cuts the playing one off. |
| `Queue` | 1 | The new line waits its turn. |
| `DropIfBusy` | 2 | The new line is dropped. |

[All types](index)
