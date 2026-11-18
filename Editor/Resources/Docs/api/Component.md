# Component

`public abstract class Component`

The base of the component classes scripts read and change. Get one with [`Entity.GetComponent``1`](Entity); it stays bound to its entity.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Enabled` | `bool` | get, set | Whether the component is switched on, the checkbox in its inspector header. A component that is off does its job no more (a light gives no light, an audio source stays silent) but keeps its settings. A component with no such switch, such as a collider, is always on, and switching it off logs a warning and changes nothing. |
| `Entity` | `Entity` | get | The entity this component belongs to. |

[All types](index)
