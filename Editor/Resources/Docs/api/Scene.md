# Scene

`public class Scene : AssetRef`

A scene asset. Assign one to a field in the inspector, then load it from a script. A scene change is carried out when the frame's update ends: the call returns at once and the running scripts finish their update first.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `Current` | `Scene` | get | The scene that is running now. `Scene.Current.Load()` restarts it. |

## Methods

```
public void Load()
```

Replaces the running scene with this one. Everything in the old scene ends (its scripts get `OnDestroy`) except entities with [`Entity.Persistent`](Entity) set, which move to the new scene with their children and scripts, and [`Time.TimeScale`](Time), which carries over. If the scene cannot be loaded an error is logged and the current one keeps running. Several calls in one frame: the last one wins.

```
public void LoadAdditive()
```

Adds this scene's entities to the running scene without ending it, as spawning a prefab does: their scripts run `OnCreate` and their bodies start. Loading the same scene again adds another copy. The entities' IDs are kept unless the running scene already uses them; script fields that hold an [`Entity`](Entity) of the added scene are not updated for the new IDs.

```
public void Unload()
```

Removes every entity added by [`Scene.LoadAdditive`](Scene) from this scene, with their scripts' `OnDestroy`.

[All types](index)
