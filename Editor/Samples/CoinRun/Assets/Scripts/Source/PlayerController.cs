// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace CoinRun
{
	// Walks and jumps a character controller. WASD or the arrow keys move relative to the camera, Space jumps. Falling off the course
	// starts the level again.
	public class PlayerController : Entity
	{
		public float Speed = 5.0f;
		public float JumpPower = 6.0f;
		public float TurnSpeed = 14.0f;

		CharacterControllerComponent m_Controller;
		float m_Heading;

		void OnCreate()
		{
			m_Controller = GetComponent<CharacterControllerComponent>();
		}

		void OnFixedUpdate(float dt)
		{
			if (m_Controller == null || Game.Won)
				return;

			if (Translation.Y < -12.0f)
			{
				Scene.Current.Load(); // fell off: the same as pressing R
				return;
			}

			Vector3 move;
			bool jump;
			if (Game.Autoplay)
				Autopilot(out move, out jump);
			else
				ReadKeys(out move, out jump);

			m_Controller.Move(move * (Speed * dt));
			if (jump && m_Controller.IsGrounded)
				m_Controller.Jump(JumpPower);

			if (move.LengthSquared() > 0.01f)
			{
				float wanted = Mathf.Atan2(-move.X, -move.Z);
				float difference = wanted - m_Heading;
				while (difference > 3.14159265f) difference -= 6.2831853f;
				while (difference < -3.14159265f) difference += 6.2831853f;
				m_Heading += difference * Mathf.Clamp01(TurnSpeed * dt);
				Rotation = new Vector3(0.0f, m_Heading, 0.0f);
			}
		}

		static void ReadKeys(out Vector3 move, out bool jump)
		{
			float x = (Input.IsKeyDown(KeyCode.D) || Input.IsKeyDown(KeyCode.Right) ? 1.0f : 0.0f) - (Input.IsKeyDown(KeyCode.A) || Input.IsKeyDown(KeyCode.Left) ? 1.0f : 0.0f);
			float z = (Input.IsKeyDown(KeyCode.W) || Input.IsKeyDown(KeyCode.Up) ? 1.0f : 0.0f) - (Input.IsKeyDown(KeyCode.S) || Input.IsKeyDown(KeyCode.Down) ? 1.0f : 0.0f);
			// The camera looks along (-sin, 0, -cos) of its yaw.
			float sin = Mathf.Sin(Game.CameraYaw), cos = Mathf.Cos(Game.CameraYaw);
			move = new Vector3(cos * x - sin * z, 0.0f, -sin * x - cos * z);
			if (move.LengthSquared() > 1.0f)
				move = move.Normalized();
			jump = Input.IsKeyDown(KeyCode.Space);
		}

		// The next coin in order, then the goal: walk at it, and hop when it is up, far, or across a gap.
		void Autopilot(out Vector3 move, out bool jump)
		{
			// The first coin still there (one touched on the way is already gone), then the goal.
			Entity target = null;
			for (int i = 1; i <= Game.TotalCoins && target == null; i++)
				target = Entity.Find($"Coin{i:D2}");
			if (target == null)
				target = Entity.Find("Goal");
			move = default;
			jump = false;
			if (target == null)
				return;
			Vector3 to = target.Translation - Translation;
			Vector3 flat = new Vector3(to.X, 0.0f, to.Z);
			float distance = flat.Length();
			if (distance > 0.3f)
				move = flat / distance;

			// Jump when the target is above, or when there is no floor a stride ahead (a gap): not to hop everywhere, which carries too far.
			bool gapAhead = distance > 1.0f && !Physics.Raycast(Translation + move * 1.0f + new Vector3(0.0f, 0.2f, 0.0f), new Vector3(0.0f, -1.0f, 0.0f), 3.0f);
			jump = to.Y > 0.6f || gapAhead;
		}
	}
}
