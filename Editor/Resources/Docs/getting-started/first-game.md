# Your First Game

This walkthrough builds a small scene: a cube that falls onto a floor, a light, a camera, and a script that pushes the cube when you press Space. It then plays the scene and exports it as a standalone game. It takes about fifteen minutes.

**You need:** the editor running. Nothing else has to be installed: the .NET SDK used for scripts is already required by the editor.

## 1. Create a project

When no project is open the editor shows its start screen. Choose **Create Project**, pick an empty folder and a name. You can also use **File → Create Project** at any time. The editor creates the project file and its `Assets` folder, then opens the project with an empty scene.

## 2. Add a floor and a cube

1. In the **Scene Hierarchy**, right-click empty space and choose **Create 3D → Meshes → Plane**. This is the floor.
2. With the plane selected, set its **Scale** in the [Transform](../components/transform) section of the **Properties** panel to `10, 1, 10`.
3. Right-click again and choose **Create 3D → Meshes → Cube**.
4. Press **W** and drag the green arrow of the gizmo to lift the cube about three units above the floor.

## 3. Light the scene

Right-click the Hierarchy and add **Create 3D → Lights → Directional Light** (the sun) and **Create 3D → Lights → Sky Light** (light from the sky). Rotate the directional light with **E** until the cube casts a shadow you like. See [Lights](../components/lights) for every setting.

## 4. Add a camera

Choose **Create Utility → Camera** in the Hierarchy menu and move it so it looks at the cube. The **Game View** window shows the scene through the primary camera; the Scene View always uses the editor camera.

## 5. Make the cube fall

1. Select the cube. In the Properties panel click **Add Component → Rigidbody** and set **Body Type** to **Dynamic**.
2. Add a **Box Collider** to the cube.
3. Select the floor and add a **Box Collider** to it as well. An entity with a collider but no Rigidbody is a static, immovable body.

Click **Play** in the title bar. The cube drops onto the floor. Click **Stop** to return to editing: everything that changed during Play is discarded. [Physics](../components/physics) explains bodies and colliders.

## 6. Write a script

Scripts are C# classes in your project's `Assets/Scripts/Source` folder. Create a file named `Jumper.cs` there, in your own code editor or in Beam (the editor's text editor), with this content:

```
using Lux;

namespace MyGame
{
	public class Jumper : Entity
	{
		public float Strength = 5.0f;

		private RigidBodyComponent m_Body;

		void OnCreate()
		{
			m_Body = GetComponent<RigidBodyComponent>();
		}

		void OnUpdate(float ts)
		{
			if (m_Body != null && Input.IsKeyPressed(KeyCode.Space))
				m_Body.AddForce(new Vector3(0.0f, Strength, 0.0f), ForceMode.Impulse);
		}
	}
}
```

Build it: open **Project Settings**, expand **Runtime Export** and click **Build Scripts**. Then load the new code with **Edit → Reload C# Assembly** (**Ctrl+R**). A message at the bottom of the screen confirms how many scripts were loaded.

Select the cube, add a **Script** component and choose `MyGame.Jumper`. The public field **Strength** appears in the Properties panel. Press **Play** and then **Space**: the cube jumps. [C# Scripts](../scripting/csharp-scripts) covers the script lifecycle and editor fields.

## 7. Save and export

Save the scene with **Ctrl+S**. Then follow [Exporting a Game](../editor/export) to build a folder you can run without the editor.

## Where to go next

- [The Editor at a Glance](../editor/interface)
- [Content Browser and Assets](../editor/content-browser) to bring in your own meshes and textures.
- [Input](../scripting/input) for mouse and gamepad controls.
