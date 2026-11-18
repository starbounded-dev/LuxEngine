# Timer

`public class Timer`

A callback set to run later, or every so often, by [`Timer.After`](Timer), [`Timer.Every`](Timer), [`Entity.After`](Entity) or [`Entity.Every`](Entity).

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `IsActive` | `bool` | get | Whether the timer has not yet been cancelled or run out. A repeating timer stays active until cancelled. |

## Methods

```
public static Timer After(float seconds, Action callback, bool unscaled = false)
```

Runs `callback` once, `seconds` from now. It keeps running across [`Scene.Load`](Scene) and stops when the game does. Use [`Entity.After`](Entity) to tie it to an entity.

- `seconds`: The delay.
- `callback`: What to run.
- `unscaled`: Count real time instead of game time, so [`Time.TimeScale`](Time) does not slow it.

```
public void Cancel()
```

Stops the timer; a callback that has not run yet never will. Cancelling twice does nothing.

```
public static Timer Every(float seconds, Action callback, bool unscaled = false)
```

Runs `callback` every `seconds`, until cancelled. See [`Timer.After`](Timer).

- `seconds`: The time between calls, above 0.
- `callback`: What to run.
- `unscaled`: Count real time instead of game time.

[All types](index)
