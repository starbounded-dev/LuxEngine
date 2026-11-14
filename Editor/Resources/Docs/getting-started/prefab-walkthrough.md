# Prefabs, Step by Step

This walkthrough turns a lamp post into a prefab, places several copies, changes them all at once, gives one copy its own look, and spawns more from a script. It assumes you have done [Your First Game](first-game). [Prefabs](../editor/prefabs) is the reference for everything shown here.

## 1. Build the original

1. In the Scene Hierarchy choose **Create 3D → Meshes → Cylinder** and scale it to `0.2, 3, 0.2`. Rename it `LampPost` (double-click its name).
2. Add **Create 3D → Lights → Point Light**, drag it onto `LampPost` in the Hierarchy to make it a child, and move it to the top of the post.
3. Add a **Box Collider** to `LampPost` so things bump into it.

## 2. Make it a prefab

Select `LampPost` and choose **Edit → Create Prefab from Selection**. A `LampPost` prefab appears in the Content Browser, and the entity in the scene is now its first **instance**. The Properties panel shows a **Prefab** section for it.

## 3. Place copies

Drag the prefab from the Content Browser into the Scene Hierarchy three times and move each copy along a street. Every copy has the post, the light and the collider.

## 4. Change them all

Double-click the prefab in the Content Browser. The scene is replaced by the prefab on its own, with an **Editing Prefab: LampPost** banner at the top.

1. Select the light and change its colour to a warm orange.
2. Click **Save & Return** in the banner.

Every lamp post in the scene now glows orange. Hierarchy changes follow too: add a child in the prefab and every instance gets it, even in scenes that were closed.

## 5. Make one copy different

Select one lamp post's light in the scene and make it blue. That copy now **overrides** the colour: the **Prefab** section marks the changed component.

- Edit the prefab again and change the light's intensity: all four posts get the new intensity, and the blue one stays blue.
- **Revert** in the Prefab section turns it back to the prefab's colour; **Apply** would make blue the colour of every lamp post instead.

## 6. A variant

Right-click the prefab and choose **Create Variant**. A variant starts as a copy of `LampPost` that keeps following its changes. Open it, make the post taller, and save: placing the variant gives a tall lamp post that still picks up every later change to the original.

## 7. Spawn from a script

Add a script to any entity:

```
using Lux;

namespace MyGame
{
	public class LampSpawner : Entity
	{
		public Prefab Lamp;

		void OnUpdate(float ts)
		{
			if (Input.IsKeyPressed(KeyCode.L))
				Entity.Instantiate(Lamp, Translation + new Vector3(2.0f, 0.0f, 0.0f));
		}
	}
}
```

Build and reload the scripts, then drag the `LampPost` prefab onto the **Lamp** field in the Properties panel. Press **Play** and then **L**: a lamp post appears two metres along. Prefabs that scripts refer to are packed into exported games, so this works there too.
