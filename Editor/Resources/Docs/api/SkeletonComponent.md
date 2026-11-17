# SkeletonComponent

`public class SkeletonComponent : Component`

A skinned mesh's animation: playback of a clip or an animation graph, graph parameters, bone transforms and bone attachments. Main thread only. A mesh plays either one clip (set in the inspector, or with [`SkeletonComponent.CrossFade`](SkeletonComponent)) or, when the component has a Graph, the graph's states.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `CurrentState` | `string` | get | The name of the animation graph's current state, or an empty string without a graph. |
| `Loop` | `bool` | get, set | Whether the clip wraps at its end instead of holding its last pose. A graph state's own Loop setting wins. |
| `Playing` | `bool` | get, set | Whether the clip advances each frame. False holds the current pose. |
| `Speed` | `float` | get, set | Playback rate: 1 is normal, 2 twice as fast, negative plays backwards. |
| `Time` | `float` | get, set | Seconds into the current clip. Setting it jumps; it wraps or clamps on the next update. |

## Methods

```
public bool AttachToBone(Entity child, string bone)
```

Makes an entity follow a bone: from the next update its position and rotation track the bone (it keeps its own scale), whatever its parent. Attaching again moves it to another bone.

- `child`: The entity to move, for example a weapon.
- `bone`: The bone's name in the skeleton.

Returns: False when the bone or entity does not exist.

```
public bool CrossFade(ulong clip, float seconds)
```

Switches to another clip, fading from the current one over `seconds` (0 switches at once). Not available while the component has an animation graph: use [`SkeletonComponent.PlayState`](SkeletonComponent).

- `clip`: The handle of an Animation asset (.lanim).
- `seconds`: Fade length in seconds.

Returns: False when the handle is not an animation or the component runs a graph.

```
public bool Detach(Entity child)
```

Stops an entity following a bone; it stays where it is.

Returns: False when the entity was not attached.

```
public bool GetBoneTransform(string bone, ref Vector3 position, ref Vector3 rotation)
```

The world-space position and rotation (Euler radians, like [`TransformComponent.Rotation`](TransformComponent)) of a bone in the current pose.

- `bone`: The bone's name in the skeleton.
- `position`: The bone's world position.
- `rotation`: The bone's world rotation.

Returns: False when the bone does not exist or no pose has been evaluated yet.

```
public float GetFloat(string parameter)
```

Reads a parameter of the animation graph (a Bool is 0 or 1, a Trigger 1 while pending).

```
public bool PlayState(string state, float seconds = 0.2f)
```

Moves the animation graph to a state at once, fading over `seconds`.

- `state`: The state's name in the graph.
- `seconds`: Fade length in seconds.

Returns: False when there is no graph or no such state.

```
public bool SetBool(string parameter, bool value)
```

Sets a Bool parameter of the animation graph.

Returns: False for an unknown parameter or one of another type.

```
public bool SetFloat(string parameter, float value)
```

Sets a Float parameter of the animation graph.

Returns: False for an unknown parameter or one of another type.

```
public bool SetTrigger(string parameter)
```

Sets a Trigger parameter: the next transition that tests it fires and clears it.

Returns: False for an unknown parameter or one of another type.

[All types](index)
