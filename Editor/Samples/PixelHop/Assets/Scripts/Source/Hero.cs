// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace PixelHop
{
	// Runs and jumps a Box2D body. A / D or the arrows move, Space (W, Up) jumps. Falling into a pit, or touching a blob from the side, starts
	// the level again; landing on one from above stomps it.
	public class Hero : Entity
	{
		public float Speed = 5.0f;
		public float JumpSpeed = 11.0f;

		RigidBody2DComponent m_Body;
		SpriteAnimatorComponent m_Animator;
		SpriteRendererComponent m_Sprite;
		string m_Clip = "idle";
		float m_Grace;

		void OnCreate()
		{
			m_Body = GetComponent<RigidBody2DComponent>();
			m_Animator = GetComponent<SpriteAnimatorComponent>();
			m_Sprite = GetComponent<SpriteRendererComponent>();
		}

		void OnFixedUpdate(float dt)
		{
			if (m_Body == null || Game.LevelDone)
				return;

			Vector3 p = Translation;
			if (p.Y < -4.0f)
			{
				Scene.Current.Load();
				return;
			}

			float direction;
			bool jump;
			if (Game.Autoplay)
				Autopilot(p, out direction, out jump);
			else
			{
				direction = (Input.IsKeyDown(KeyCode.D) || Input.IsKeyDown(KeyCode.Right) ? 1.0f : 0.0f) - (Input.IsKeyDown(KeyCode.A) || Input.IsKeyDown(KeyCode.Left) ? 1.0f : 0.0f);
				jump = Input.IsKeyDown(KeyCode.Space) || Input.IsKeyDown(KeyCode.W) || Input.IsKeyDown(KeyCode.Up);
			}

			Vector2 velocity = m_Body.LinearVelocity;
			velocity.X = direction * Speed;
			if (jump && Grounded(p))
				velocity.Y = JumpSpeed;
			m_Body.LinearVelocity = velocity;

			string clip = direction != 0.0f ? "run" : "idle";
			if (clip != m_Clip)
			{
				m_Clip = clip;
				m_Animator.Play(clip);
			}
			if (direction != 0.0f)
				Scale = new Vector3(direction > 0.0f ? 1.0f : -1.0f, 1.0f, 1.0f);
		}

		bool Grounded(Vector3 p)
		{
			return Physics2D.Raycast(new Vector2(p.X, p.Y - 0.35f), new Vector2(0.0f, -1.0f), 0.2f, this)
				|| Physics2D.Raycast(new Vector2(p.X - 0.25f, p.Y - 0.35f), new Vector2(0.0f, -1.0f), 0.2f, this)
				|| Physics2D.Raycast(new Vector2(p.X + 0.25f, p.Y - 0.35f), new Vector2(0.0f, -1.0f), 0.2f, this);
		}

		// Run right; jump at a wall, a pit or a blob ahead.
		void Autopilot(Vector3 p, out float direction, out bool jump)
		{
			direction = 1.0f;
			Vector2 feet = new Vector2(p.X, p.Y - 0.2f);
			bool wall = Physics2D.Raycast(feet, new Vector2(1.0f, 0.0f), 0.8f, this);
			bool pit = !Physics2D.Raycast(new Vector2(p.X + 0.9f, p.Y), new Vector2(0.0f, -1.0f), 1.6f, this);
			bool blob = Physics2D.Raycast(feet, new Vector2(1.0f, 0.0f), out RaycastHit2D hit, 3.0f, this) && hit.Entity != null && hit.Entity.Name.StartsWith("Enemy");
			jump = wall || pit || blob;
		}

		protected override void OnCollisionBegin(Entity other)
		{
			if (!other.Name.StartsWith("Enemy") || Game.LevelDone)
				return;
			// From above (falling, and higher than the blob): a stomp. Otherwise the blob wins.
			if (m_Body.LinearVelocity.Y < 0.5f && Translation.Y > other.Translation.Y + 0.3f)
			{
				Entity.Destroy(other);
				Vector2 velocity = m_Body.LinearVelocity;
				velocity.Y = 8.0f;
				m_Body.LinearVelocity = velocity;
			}
			else
				Scene.Current.Load();
		}
	}
}
