# Your First 2D Game

This tutorial makes a small 2D scene: a ball you steer with the arrow keys, bouncing on platforms, with a score on screen. It uses sprites, circles, text, 2D physics and a script. Do [Your First Game](first-game) first if you have never used the editor.

2D in LuxEngine happens in the X/Y plane: X to the right, Y up. Sprites face +Z, so a camera looking down -Z sees them.

## 1. A 2D camera

1. Create a project (or a new scene with **File → New Scene**).
2. Choose **Create Utility → Camera**. Set its **Translation** to `0, 0, 10` so it sits in front of the X/Y plane.
3. In its Camera section set **Projection** to **Orthographic** and **Size** to `12`: the Game View then shows 12 units from bottom to top, with no perspective.

## 2. Ground and platforms

1. Choose **Create 2D → Sprite**, rename it `Ground`, set **Translation** to `0, -5, 0` and **Scale** to `16, 1, 1`, and give it a colour.
2. Add **Rigidbody 2D** (leave **Body Type** on **Static**) and **Box Collider 2D**. The collider's default **Size** of `0.5, 0.5` is half a unit each way, so it matches the sprite at any scale.
3. Duplicate `Ground` (**Ctrl+D**) twice to make two shorter platforms higher up, at different heights.

## 3. The ball

1. Choose **Create 2D → Circle**, rename it `Ball` and move it to `0, 2, 0`.
2. Add **Rigidbody 2D** with **Body Type** set to **Dynamic**.
3. Add **Circle Collider 2D** (its default **Radius** of 0.5 matches the circle) and set **Restitution** to `0.5` so it bounces.

Press **Play**: the ball falls and bounces on the ground. Press **Stop**.

## 4. A score

Choose **Create 2D → Text**, rename it `Score`, move it to `-7, 5, 0` and set its text to `Score: 0`.

## 5. Steering the ball

Create `Ball.cs` in `Assets/Scripts/Source`:

```
using Lux;

namespace MyGame
{
	public class Ball : Entity
	{
		public float Speed = 6.0f;
		public float JumpSpeed = 8.0f;

		private RigidBody2DComponent m_Body;
		private TextComponent m_Score;
		private int m_Bounces;

		void OnCreate()
		{
			m_Body = GetComponent<RigidBody2DComponent>();
			m_Score = FindEntityByName("Score")?.GetComponent<TextComponent>();
		}

		void OnUpdate(float ts)
		{
			Vector2 velocity = m_Body.LinearVelocity;
			velocity.X = 0.0f;
			if (Input.IsKeyDown(KeyCode.Left))
				velocity.X = -Speed;
			if (Input.IsKeyDown(KeyCode.Right))
				velocity.X = Speed;
			if (Input.IsKeyPressed(KeyCode.Up))
				velocity.Y = JumpSpeed;
			m_Body.LinearVelocity = velocity;
		}

		protected override void OnCollisionBegin(Entity other)
		{
			m_Bounces++;
			if (m_Score != null)
				m_Score.Text = $"Score: {m_Bounces}";
		}
	}
}
```

Build it with **Build Scripts** (Project Settings → Runtime Export), reload with **Ctrl+R**, select `Ball`, add a **Script** component and choose `MyGame.Ball`.

Press **Play**. The arrow keys roll the ball, **Up** throws it upward, and every landing adds a point.

## Where to go next

- [2D: Sprites, Circles and Text](../components/2d) for every 2D setting; give the sprites a **Texture** for real artwork.
- [Input](../scripting/input) for gamepad controls.
- [Exporting a Game](../editor/export) to share it.
