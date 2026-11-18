# Gameplay API

The calls most scripts need beyond moving an entity: time, maths, switching things off, finding entities, asking the physics world questions, changing scenes, waiting, and reacting to the window. Each section is short; the [C# API Reference](../api/index) has every member.

## Time

`Time.DeltaTime` is the seconds since the last frame, the value `OnUpdate` also receives. It follows `Time.TimeScale`: 1 is normal, 0.5 is slow motion, 0 freezes scripts, physics and animation. `Time.UnscaledDeltaTime` ignores the scale, which is what a pause menu or a UI animation wants. `Time.ElapsedTime` and `Time.UnscaledTime` count seconds since Play started, `Time.FrameCount` counts frames, and `Time.FixedDeltaTime` is the physics step. Inside `OnFixedUpdate`, `Time.DeltaTime` is that step.

```csharp
void OnUpdate(float ts)
{
	if (Input.IsKeyPressed(KeyCode.T))
		Time.TimeScale = Time.TimeScale < 1.0f ? 1.0f : 0.25f;
}
```

## Maths

`Mathf` has the everyday functions (`Clamp`, `Lerp`, `MoveTowards`, `SmoothDamp`, `DeltaAngle`, `Approximately`, ...). `Vector2`, `Vector3` and `Vector4` have the usual operators plus `Length()`, `Normalized()`, `Dot`, `Cross`, `Distance` and `Lerp`. `Quaternion` composes with `*` (the right rotation applies first) and rotates a vector with `rotation * vector`; `Quaternion.Euler`, `AngleAxis`, `LookRotation` and `Slerp` build and blend them, and `Entity.RotationQuaternion` is the same rotation as `Entity.Rotation`. `Color` converts to and from `Vector4`, so it fits every color property (`sprite.Color = Color.Red`). `Random.Value`, `Random.Range` and `Random.InsideUnitSphere` give random numbers; `Random.InitState(seed)` repeats a sequence.

`Entity.WorldTranslation`, `Entity.Forward` (-Z), `Entity.Right` and `Entity.Up` are in world space, through every parent.

> A script with both `using System;` and `using Lux;` must write `Lux.Random` or `System.Random` in full.

## Switching things off

`Entity.Active = false` switches an entity and all its children off: renderers, lights, audio sources and scripts skip it, at once. `ActiveInHierarchy` says whether it is on in effect. `Component.Enabled` switches one component off and keeps its settings (`GetComponent<PointLightComponent>().Enabled = false`); a component with no switch, such as a collider, always reads as enabled. `Entity.ScriptEnabled = false` stops this entity's script from getting `OnUpdate`.

## Finding entities

Give an entity a **Tag** in the Properties panel (the field under its name), or set `Entity.Tag` in code, then find groups of entities by it. Results come in the order the entities were created, and skip switched-off entities unless you pass `includeInactive: true`.

```csharp
Entity player = Entity.Find("Player");
Entity[] coins = Entity.FindAllWithTag("coin");
Entity boss = Entity.FindWithComponent<CharacterControllerComponent>();
foreach (Entity enemy in Entity.FindAllWithTag("enemy"))
	enemy.Active = false;
```

## Physics queries

`Physics` answers questions about the physics world while the game plays. Rays and sweeps report a `RaycastHit` with the entity, the point, the surface normal and the distance. Pass `ignore` to leave out one entity (usually yourself), a layer mask to choose which physics layers count (`Physics.GetLayerMask("Ground", "Props")`; the bit of a layer is its position in the project's layer list), and `includeTriggers` to count trigger volumes, which are skipped by default.

```csharp
if (Physics.Raycast(Translation, Vector3.Down, out RaycastHit hit, 2.0f, Physics.AllLayers, this))
	Log.Info($"standing on {hit.Entity.Name}, {hit.Distance:F2} m below");

foreach (Entity near in Physics.OverlapSphere(Translation, 5.0f, Physics.AllLayers, this))
	near.Tag = "seen";
```

`Raycast`, `RaycastAll` (every body along the ray, nearest first), `SphereCast`, `BoxCast`, `CapsuleCast` (a shape swept along a direction), `OverlapSphere`, `OverlapBox`, `OverlapCapsule` and `CheckSphere` are there.

## Lights, cameras and meshes

`GetComponent<PointLightComponent>()` (and the directional, spot, area and sky lights) exposes `Color`, `Intensity`, `Range` and the other settings. `CameraComponent` has `FieldOfView`, `NearClip`, `FarClip` and the orthographic settings. `StaticMeshComponent` swaps the mesh, assigns a material to a slot (`SetMaterial`) and changes a slot's color or values for that entity alone (`SetMaterialColor`, `SetMaterialValue`): the first change gives the entity its own copy of the material, so other entities that share it keep theirs.

```csharp
var mesh = GetComponent<StaticMeshComponent>();
mesh.SetMaterialColor(0, Color.Red);                      // a hit flash, for this entity only
mesh.SetMaterialValue(0, MaterialValue.Roughness, 0.2f);
```

## Changing scenes

A **Scene** field in the Properties panel takes a scene asset. `scene.Load()` replaces the running scene when the frame ends: everything in the old scene ends with its scripts' `OnDestroy`, except entities with `Persistent = true`, which move to the new scene with their children and scripts (without a second `OnCreate`). `Time.TimeScale` carries over. `scene.LoadAdditive()` adds a scene's entities to the running one, and `scene.Unload()` removes what it added. `Scene.Current` is the running scene, so `Scene.Current.Load()` restarts it.

```csharp
public Scene NextLevel;

void OnCreate() => Persistent = true;          // this script and its entity survive the load
void OnTriggerBegin(Entity other) => NextLevel.Load();
```

## Timers and coroutines

```csharp
Timer.After(2.0f, () => Log.Info("two seconds later"));
Timer flash = Every(0.5f, ToggleLight);        // an entity's timer stops when the entity is destroyed
flash.Cancel();

IEnumerator Blink()
{
	var mesh = GetComponent<StaticMeshComponent>();
	mesh.Enabled = false;
	yield return new WaitForSeconds(3.0f);      // game time: a Time.TimeScale of 0 holds it
	mesh.Enabled = true;
}
void OnCollisionBegin(Entity other) => StartCoroutine(Blink());
```

`yield return null` waits a frame; `WaitForSeconds` counts game time and `WaitForSecondsRealtime` real time; `WaitUntil`, `WaitWhile` and `WaitForFixedUpdate` wait for a condition or the next physics step; yielding another `Coroutine` or `IEnumerator` waits for it. Timers and coroutines owned by an entity end when it is destroyed and wait while it is switched off.

## The application

`Application.IsFocused`, `Width` and `Height` describe the window (in the editor, the Game View). `Application.Paused = true` pauses scripts, physics and animation; timers started with `unscaled: true` and `WaitForSecondsRealtime` keep counting, which is how a paused game un-pauses itself. The events `FocusChanged`, `PauseChanged`, `Resized` and `Quitting` report changes; `Quitting` fires when the game ends (and when Play stops in the editor), before scripts get `OnDestroy`. Remove handlers you added in `OnDestroy`.
