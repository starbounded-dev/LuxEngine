// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace CoinRun
{
	// A camera behind the player. Q and E turn it around the character.
	public class FollowCamera : Entity
	{
		public float Distance = 9.0f;
		public float Height = 5.0f;
		public float Follow = 6.0f;
		public float TurnSpeed = 1.8f;

		Entity m_Target;
		float m_Yaw;
		bool m_Placed;

		void OnCreate()
		{
			m_Target = Entity.Find("Player");
		}

		void OnUpdate(float ts)
		{
			if (m_Target == null)
				return;

			if (Input.IsKeyDown(KeyCode.Q)) m_Yaw += TurnSpeed * ts;
			if (Input.IsKeyDown(KeyCode.E)) m_Yaw -= TurnSpeed * ts;
			Game.CameraYaw = m_Yaw;

			Vector3 player = m_Target.Translation;
			Vector3 behind = new Vector3(Mathf.Sin(m_Yaw), 0.0f, Mathf.Cos(m_Yaw));
			Vector3 wanted = player + behind * Distance + new Vector3(0.0f, Height, 0.0f);
			Translation = m_Placed ? Translation + (wanted - Translation) * Mathf.Clamp01(Follow * ts) : wanted;
			m_Placed = true;
			Rotation = new Vector3(-Mathf.Atan2(Height - 1.0f, Distance), m_Yaw, 0.0f);
		}
	}
}
