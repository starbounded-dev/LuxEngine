using System;

using Lux;

namespace LuxSample
{
	// Free-fly camera. Attach to a camera entity (via a ScriptComponent) and press Play.
	//
	// Keyboard + mouse:
	//   - Hold RIGHT MOUSE BUTTON and move the mouse to look around
	//   - While looking: W/S forward/back, A/D strafe, E/Space up, Q/LeftControl down
	//   - LeftShift : sprint (move faster)
	//
	// Gamepad (any mapped controller; Xbox names, PlayStation in brackets):
	//   - Left stick  : move / strafe (analog, speed follows how far you push)
	//   - Right stick : look around
	//   - RT / RB [R2 / R1] : up,  LT / LB [L2 / L1] : down
	//   - Click the left stick [L3] : sprint until the stick is released
	//
	// The script owns yaw/pitch, so movement always follows where you're looking. If forward/back
	// feels reversed, flip the sign on 'forward'; if the look is inverted, flip the m_Yaw / m_Pitch
	// update signs. Both are one-character changes.
	public class FlyCamera : Entity
	{
		public float Speed = 5.0f;
		public float SprintMultiplier = 3.0f;
		public float MouseSensitivity = 0.0025f;
		public float GamepadLookSpeed = 2.5f; // Radians per second at full stick deflection.
		public bool InvertGamepadY = false;
		public int Gamepad = -1;              // Controller slot; -1 = first connected gamepad.

		private float m_Yaw;
		private float m_Pitch;
		private Vector2 m_LastMousePosition;
		private bool m_Initialized = false;
		private bool m_GamepadSprint = false;

		void OnCreate()
		{
			// Seed orientation from the entity's current rotation so we start where the camera points.
			Vector3 rotation = Rotation;
			m_Pitch = rotation.X;
			m_Yaw = rotation.Y;
		}

		void OnUpdate(float ts)
		{
			bool looked = UpdateMouseLook();
			looked |= UpdateGamepadLook(ts);
			if (looked)
			{
				m_Pitch = Math.Clamp(m_Pitch, -1.55f, 1.55f); // ~+/-89 degrees
				Rotation = new Vector3(m_Pitch, m_Yaw, 0.0f);
			}

			UpdateMovement(ts);
		}

		private bool UpdateMouseLook()
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
				return false;

			m_Yaw -= deltaX * MouseSensitivity;
			m_Pitch -= deltaY * MouseSensitivity;
			return true;
		}

		private bool UpdateGamepadLook(float ts)
		{
			// Deadzone is applied natively, so a resting stick reads exactly zero.
			Vector2 look = Input.GetGamepadRightStick(Gamepad);
			if (look.X == 0.0f && look.Y == 0.0f)
				return false;

			// Stick Y is +1 when pulled back, same direction as the mouse's screen-space Y.
			float invert = InvertGamepadY ? -1.0f : 1.0f;
			m_Yaw -= look.X * GamepadLookSpeed * ts;
			m_Pitch -= look.Y * invert * GamepadLookSpeed * ts;
			return true;
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

			bool gamepadSprint = UpdateGamepadMovement(ref velocity, forward, right, up);

			float speed = Speed;
			if (Input.IsKeyDown(KeyCode.LeftShift) || gamepadSprint)
				speed *= SprintMultiplier;

			Translation = Translation + (velocity * (speed * ts));
		}

		// Adds analog gamepad movement to 'velocity' and returns whether gamepad sprint is active.
		private bool UpdateGamepadMovement(ref Vector3 velocity, Vector3 forward, Vector3 right, Vector3 up)
		{
			if (!Input.IsGamepadConnected(Gamepad))
			{
				m_GamepadSprint = false;
				return false;
			}

			Vector2 move = Input.GetGamepadLeftStick(Gamepad);
			velocity = velocity + (forward * -move.Y) + (right * move.X);

			// Triggers are analog; bumpers are full speed.
			float rise = Input.IsGamepadButtonDown(GamepadButton.RightBumper, Gamepad) ? 1.0f : Input.GetGamepadAxis(GamepadAxis.RightTrigger, Gamepad);
			float fall = Input.IsGamepadButtonDown(GamepadButton.LeftBumper, Gamepad) ? 1.0f : Input.GetGamepadAxis(GamepadAxis.LeftTrigger, Gamepad);
			velocity = velocity + (up * (rise - fall));

			// L3 is hard to hold while steering, so a click latches sprint until the stick recenters.
			if (Input.IsGamepadButtonPressed(GamepadButton.LeftStick, Gamepad))
				m_GamepadSprint = true;
			else if (move.X == 0.0f && move.Y == 0.0f)
				m_GamepadSprint = false;

			return m_GamepadSprint;
		}
	}
}
