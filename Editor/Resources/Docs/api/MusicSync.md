# MusicSync

`public enum MusicSync`

When [`Music.QueueTransition`](Music) switches to the new music.

## Values

| Name | Value | Description |
|---|---|---|
| `Immediate` | 0 | Right away. |
| `NextBeat` | 1 | On the next beat. |
| `NextBar` | 2 | On the next bar. |
| `NextMarker` | 3 | On the next timeline marker. |
| `NextSection` | 4 | On the next marker whose name starts with `Section:`. |

[All types](index)
