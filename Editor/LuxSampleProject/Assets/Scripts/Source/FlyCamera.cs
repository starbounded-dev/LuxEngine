using System;

using Lux;

namespace LuxSample
{
	// Free-fly camera. Attach to a camera entity (via a ScriptComponent) and press Play:
	//   - Hold RIGHT MOUSE BUTTON and move the mouse to look around
	//   - While looking: W/S forward/back, A/D strafe, E/Space up, Q/LeftControl down
	//   - LeftShift : sprint (move faster)
	//
	// The script owns yaw/pitch, so movement always follows where you're looking. If forward/back
	// feels reversed, flip the sign on 'forward'; if the mouse look is inverted, flip the m_Yaw /
	// m_Pitch update signs. Both are one-character changes.
	public class FlyCamera : Entity
	{
		public float Speed = 5.0f;
		public float SprintMultiplier = 3.0f;
		public float MouseSensitivity = 0.0025f;

		private float m_Yaw;
		private float m_Pitch;
		private Vector2 m_LastMousePosition;
		private bool m_Initialized = false;

		void OnCreate()
		{
			// Seed orientation from the entity's current rotation so we start where the camera points.
			Vector3 rotation = Rotation;
			m_Pitch = rotation.X;
			m_Yaw = rotation.Y;
		}

		void OnUpdate(float ts)
		{
			UpdateLook();
			UpdateMovement(ts);
		}

		private void UpdateLook()
		{
			Vector2 mouse = Input.MousePosition;

			// Track the mouse every frame so there's no jump when you press the right button.
			if (!m_Initialized)
			{
				m_LastMousePosition = mouse;
				m_Initialized = true;
			}

			float deltaX = mouse.X - m_LastMousePosition.X;
			float deltaY = mouse.Y - m_LastMousePosition.Y;
			m_LastMousePosition = mouse;

			if (!Input.IsMouseButtonDown(MouseButton.Right))
				return;

			m_Yaw -= deltaX * MouseSensitivity;
			m_Pitch -= deltaY * MouseSensitivity;
			m_Pitch = Math.Clamp(m_Pitch, -1.55f, 1.55f); // ~+/-89 degrees

			Rotation = new Vector3(m_Pitch, m_Yaw, 0.0f);
		}

		private void UpdateMovement(float ts)
		{
			// Basis vectors from the script-owned orientation (Y-up, -Z forward at yaw = 0).
			Vector3 forward = new Vector3(
				-MathF.Sin(m_Yaw) * MathF.Cos(m_Pitch),
				 MathF.Sin(m_Pitch),
				-MathF.Cos(m_Yaw) * MathF.Cos(m_Pitch));
			Vector3 right = new Vector3(MathF.Cos(m_Yaw), 0.0f, -MathF.Sin(m_Yaw));
			Vector3 up = new Vector3(0.0f, 1.0f, 0.0f);

			Vector3 velocity = Vector3.Zero;
			if (Input.IsKeyDown(KeyCode.W)) velocity = velocity + forward;
			if (Input.IsKeyDown(KeyCode.S)) velocity = velocity + (forward * -1.0f);
			if (Input.IsKeyDown(KeyCode.D)) velocity = velocity + right;
			if (Input.IsKeyDown(KeyCode.A)) velocity = velocity + (right * -1.0f);
			if (Input.IsKeyDown(KeyCode.E) || Input.IsKeyDown(KeyCode.Space)) velocity = velocity + up;
			if (Input.IsKeyDown(KeyCode.Q) || Input.IsKeyDown(KeyCode.LeftControl)) velocity = velocity + (up * -1.0f);

			float speed = Speed;
			if (Input.IsKeyDown(KeyCode.LeftShift))
				speed *= SprintMultiplier;

			Translation = Translation + (velocity * (speed * ts));
		}
	}
}
