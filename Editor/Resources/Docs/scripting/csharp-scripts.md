# C# Scripts

Game code is written in C# (.NET 9). A script is a class that derives from `Lux.Entity`; adding a **Script** component to an entity and choosing the class makes that entity run it while the game plays.

## Where scripts live

Each project has a C# project in `Assets/Scripts` (`<ProjectName>.csproj`), and your `.cs` files go in `Assets/Scripts/Source`. Edit them in any code editor, or in **Beam**, the editor's own text editor.

To use new or changed code:

1. **Build:** open **Project Settings**, expand **Runtime Export** and click **Build Scripts**. Build errors are printed to the terminal the editor was started from.
2. **Reload:** **Edit → Reload C# Assembly** (**Ctrl+R**). A message confirms how many scripts loaded, or why loading failed.

Values you set in the Properties panel are kept across reloads, even for a script that is missing for a while.

## The lifecycle

The engine calls these methods by name if your class has them:

| Method | When |
|---|---|
| `void OnCreate()` | Once, before the script's first `OnUpdate`. |
| `void OnUpdate(float ts)` | Every frame. `ts` is the time since the last frame, in seconds. |
| `void OnFixedUpdate(float fixedTs)` | Before each physics step, after the frame's `OnUpdate`. Physics steps at the project's Fixed Timestep (Project Settings > Physics, 1/60 s by default), so this runs zero, one or several times a frame, always with the same `fixedTs`. Apply forces and velocities here so they do not depend on the frame rate. |
| `void OnDestroy()` | When play stops, or at the end of the frame in which the entity is destroyed. |
| `protected override void OnCollisionBegin(Entity other)` | This entity's physics body starts touching another. |
| `protected override void OnCollisionEnd(Entity other)` | They stop touching. |
| `protected override void OnTriggerBegin(Entity other)` | This entity and another start to overlap, where one of them is a trigger (**Is Trigger** on its 3D rigid body). Character controllers count. |
| `protected override void OnTriggerEnd(Entity other)` | They stop overlapping. |

A script whose component is switched off is still created, so other scripts can find it, but it gets no `OnCreate` or `OnUpdate` until it is switched on.

```
using Lux;

namespace MyGame
{
	public class Spinner : Entity
	{
		public float DegreesPerSecond = 90.0f;

		void OnUpdate(float ts)
		{
			Vector3 rotation = Rotation;
			rotation.Y += DegreesPerSecond * ts * 0.0174533f; // radians
			Rotation = rotation;
		}
	}
}
```

## Fields in the Properties panel

Public fields appear in the Properties panel, where you can set a value per entity: numbers, `bool`, `Vector2`, `Vector3`, `Vector4`, other entities (`Entity`), and assets (`Prefab`, `Mesh`, `StaticMesh`, `Material`, `Texture2D`). The value written in the code is the default. Attributes refine how a field is shown:

| Attribute | Effect |
|---|---|
| `[Range(0, 10)]` | A slider between the two values. |
| `[Header("Movement")]` | A bold label above the field, to group fields. |
| `[Tooltip("Metres per second")]` | Help text when the label is hovered. |
| `[ShowInEditor]` | Shows a non-public field too. |

## Working with the entity

Inside a script, the entity is `this`:

- `Translation`, `Rotation` (radians) and `Scale` read and write the [Transform](../components/transform).
- `Name`, `Parent` and `Children` navigate the hierarchy.
- `GetComponent<T>()`, `HasComponent<T>()`, `AddComponent<T>()` and `RemoveComponent<T>()` reach components such as `RigidBodyComponent` or `AudioSourceComponent`. `GetComponent` returns `null` when there is none.
- `FindEntityByName("Player")` finds another entity; `As<MyScript>()` returns its script.
- `Entity.Create("Name")` creates an entity, and `Destroy()` removes one, with its children, at the end of the current frame. Until then it still exists, so code later in the same frame can still read it.

`Log.Info`, `Log.Warn` and `Log.Error` print to the **Log** panel. Read the keyboard, mouse and gamepads with [Input](input).
