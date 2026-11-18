# Coroutine

`public class Coroutine : YieldInstruction`

A routine started with [`Entity.StartCoroutine`](Entity): a method that returns `System.Collections.IEnumerator` and pauses at each `yield return`. `yield return null` continues next frame; `yield return` a [`YieldInstruction`](YieldInstruction) waits as it says; `yield return` another [`Coroutine`](Coroutine) (or an `System.Collections.IEnumerator`) waits for that one to end. It stops when it finishes, when [`Entity.StopCoroutine`](Entity) is called, when its entity is destroyed, and when the scene ends. It is paused while its entity is switched off.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `IsFinished` | `bool` | get | Whether the routine has ended (finished or stopped). |

[All types](index)
