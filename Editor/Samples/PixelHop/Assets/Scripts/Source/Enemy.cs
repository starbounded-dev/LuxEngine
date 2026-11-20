// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace PixelHop
{
	// A blob that patrols: it walks until it meets a wall or the edge of the ground, then turns around.
	public class Enemy : Entity
	{
		public float Speed = 1.6f;

		RigidBody2DComponent m_Body;
		float m_Direction = -1.0f;

		void OnCreate()
		{
			m_Body = GetComponent<RigidBody2DComponent>();
		}

		void OnFixedUpdate(float dt)
		{
			if (m_Body == null)
				return;
			Vector3 p = Translation;
			bool wall = Physics2D.Raycast(new Vector2(p.X, p.Y), new Vector2(m_Direction, 0.0f), 0.5f, this);
			bool edge = !Physics2D.Raycast(new Vector2(p.X + m_Direction * 0.5f, p.Y), new Vector2(0.0f, -1.0f), 0.8f, this);
			if (wall || edge)
				m_Direction = -m_Direction;
			Vector2 velocity = m_Body.LinearVelocity;
			velocity.X = m_Direction * Speed;
			m_Body.LinearVelocity = velocity;
		}
	}
}
