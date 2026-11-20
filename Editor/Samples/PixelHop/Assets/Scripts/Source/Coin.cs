// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace PixelHop
{
	// A coin that bobs. Touching it takes it.
	public class Coin : Entity
	{
		Vector3 m_Home;
		float m_Time;
		bool m_Taken;

		void OnCreate()
		{
			m_Home = Translation;
			m_Time = m_Home.X;
		}

		void OnUpdate(float ts)
		{
			m_Time += ts * 4.0f;
			Translation = m_Home + new Vector3(0.0f, 0.08f * Mathf.Sin(m_Time), 0.0f);
		}

		protected override void OnTriggerBegin(Entity other)
		{
			if (m_Taken || other.Name != "Hero")
				return;
			m_Taken = true;
			Game.Coins++;
			Entity.Destroy(this);
		}
	}
}
