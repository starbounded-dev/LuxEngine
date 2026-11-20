// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace PixelHop
{
	// An orthographic camera that follows the hero along the level.
	public class CameraFollow : Entity
	{
		public float Follow = 8.0f;
		public float Ahead = 2.0f;

		Entity m_Hero;

		void OnCreate()
		{
			m_Hero = Entity.Find("Hero");
		}

		void OnUpdate(float ts)
		{
			if (m_Hero == null)
				return;
			Vector3 p = Translation;
			float wantedX = m_Hero.Translation.X + Ahead;
			if (wantedX < 0.0f) wantedX = 0.0f;
			p.X += (wantedX - p.X) * Mathf.Clamp01(Follow * ts);
			p.Y = 3.5f;
			Translation = p;
		}
	}
}
