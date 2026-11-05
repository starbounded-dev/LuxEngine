# C# Scripts

Game code is written in C# (.NET 9). A script is a class that derives from `Lux.Entity`; adding a **Script** component to an entity and choosing the class makes that entity run it while the game plays.

## Where scripts live

Each project has a C# project in `Assets/Scripts` (`<ProjectName>.csproj`), and your `.cs` files go in `Assets/Scripts/Source`. Edit them in any code editor, or in **Beam**, the editor's own text editor.

## Editing in Beam

Open **View → Panels → Scripting → Beam**, or use the Scripting workspace. **New** creates an
untitled file; **Save** writes the active file and asks for a destination if it is untitled.
Open existing text files from the Content Browser or **Ctrl+O** (**More → Open File**). The toolbar adapts to its panel width: primary
actions can become icons, and **More** keeps the remaining actions available. Hover an icon to see
its name and shortcut. More also contains **Save As**, **Go to Line** and **Close File**.

Use **Ctrl+N**, **Ctrl+S**, **Ctrl+Shift+S**, **Ctrl+F**, **Ctrl+G** and **Ctrl+W** while Beam is
focused. The Editor and Diff actions switch views; Diff is read-only and disables file-edit actions.
Closing a modified file asks whether to save, discard or cancel. A failed save retains your text
and shows an error above the editor; retry Save or choose another destination with More → Save As.
Resizing, hiding the panel and changing workspaces keep your open documents and unsaved edits.

Tabs show a dot for unsaved changes. Files with the same name show enough of their parent folders
to distinguish them; hover for the full path. **Ctrl+PageDown/PageUp** follows the visible tab order,
including tabs you have dragged. Use the tab-list arrow or **Ctrl+P** (**Open Documents**) when
many files are open. The document list filters by filename or path; Enter selects the first match.

**Ctrl+G** opens Go to Line with the input selected; Enter jumps and returns focus to editing.
**Ctrl++**, **Ctrl+-** and **Ctrl+0** increase, decrease and reset text zoom (50–250%). More also
contains zoom, line-number and minimap controls. These view choices stay with Beam for this session
and do not modify file contents. Find/Replace wraps its controls in narrow panels. An empty
replacement removes matched text. The status bar shows cursor position, saved/modified state,
language, line count and zoom when space permits; hover its compact form for all details.

The close dialog identifies the full file path. A cancelled or failed save keeps the document open;
use **Save As** inside the dialog to recover to a different destination. Failed Save As retains
the original document path and language. A file that cannot be read reports an error without
creating an empty replacement document.

## Create a script

In the **Content Browser**, choose **New → C# Script…** (also in the right-click **New** menu).
Enter a class name such as `PlayerController`. The preview updates to that name and the project's
namespace. **Create Script** saves `Assets/Scripts/Source/PlayerController.cs`, selects it and opens
it in Beam. Existing files are preserved; choose a different name if one exists.
Use letters, digits and underscores, starting with a letter or underscore. C# keywords, reserved filenames, and the starter member names `OnCreate`, `OnUpdate` and `MoveSpeed` are rejected.

The starter contains `OnCreate`, `OnUpdate`, an editable `MoveSpeed` field, and a commented movement
example. Uncomment that line to try it, or replace it with your own code. Lifecycle methods do not
need `override`. Creating the file does not automatically attach it to an entity.

The **New Project** form also offers a **Starter C# class**, defaulting to `GameController`.
It uses the same template and the new project's name as its namespace. The class and filename are
filled in automatically. To customize future starters, edit the editor installation's
`Resources/Templates/NewClassTemplate.cs`, keeping `$CLASS_NAME$` and `$NAMESPACE_NAME$` in place.

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

## Debug drawing

`Gizmos` draws lines in the world while the game runs, to see what a script is doing: `DrawLine`, `DrawRay`, `DrawWireBox` and `DrawWireSphere`, each with a colour (`Vector4`, red green blue alpha), an optional duration in seconds and an optional depth test (off draws on top of everything).

A line stays until the method that drew it runs again, so draw it every `OnUpdate`, or every `OnFixedUpdate` for physics things, and it holds still. With a duration it stays that long instead. Gizmos are not drawn in Dist builds.

```
void OnFixedUpdate(float fixedTs)
{
	Gizmos.DrawRay(Translation, new Vector3(0.0f, -2.0f, 0.0f), new Vector4(1.0f, 0.2f, 0.2f, 1.0f));
}
```

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

`Log.Info`, `Log.Warn` and `Log.Error` print to the **Log** panel. Read the keyboard, mouse and gamepads with [Input](input). The [C# API Reference](../api/index) lists everything else.
