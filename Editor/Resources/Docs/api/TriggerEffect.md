# TriggerEffect

`public struct TriggerEffect`

A DualSense adaptive-trigger effect, built with [`TriggerEffect.Resistance`](TriggerEffect), [`TriggerEffect.Weapon`](TriggerEffect), [`TriggerEffect.Vibration`](TriggerEffect) or [`TriggerEffect.Off`](TriggerEffect). Positions are trigger-travel zones from 0 (at rest) to 9 (fully pulled).

## Fields

| Name | Type | Description |
|---|---|---|
| `End` | `int` | The zone where a weapon effect gives way. |
| `Frequency` | `int` | Vibration frequency in Hz, 1 to 255. |
| `Start` | `int` | The zone where the effect starts. |
| `Strength` | `int` | Strength (or vibration amplitude), 1 to 8. |
| `Type` | `TriggerEffectType` | The kind of effect. |

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Off` | `TriggerEffect` | get | No effect: the trigger moves freely. |

## Methods

```
public static TriggerEffect Resistance(int start, int strength)
```

Constant resistance from `start` to the end of travel.

- `start`: Zone 0 to 9.
- `strength`: 1 to 8.

```
public static TriggerEffect Vibration(int start, int amplitude, int frequency)
```

Vibrates from `start` onward.

- `start`: Zone 0 to 9.
- `amplitude`: 1 to 8.
- `frequency`: 1 to 255 Hz.

```
public static TriggerEffect Weapon(int start, int end, int strength)
```

A gun-trigger feel: resistance that gives way with a click.

- `start`: Where the resistance starts, zone 2 to 7.
- `end`: Where it gives way, `start + 1` to 8.
- `strength`: 1 to 8.

[All types](index)
