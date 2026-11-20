// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace CoinRun
{
	// A spinning coin. Touching it takes it.
	public class Coin : Entity
	{
		float m_Spin;
		float m_Phase;
		Vector3 m_Home;
		bool m_Taken;

		void OnCreate()
		{
			m_Home = Translation;
			m_Phase = m_Home.X * 0.7f + m_Home.Z * 0.3f;
		}

		void OnUpdate(float ts)
		{
			m_Spin += 2.5f * ts;
			// Stood on its edge (a quarter turn about X), spinning about the world's vertical axis, bobbing a little.
			Rotation = new Vector3(1.5708f, m_Spin, 0.0f);
			Translation = m_Home + new Vector3(0.0f, 0.12f * Mathf.Sin(m_Spin * 1.3f + m_Phase), 0.0f);
		}

		protected override void OnTriggerBegin(Entity other)
		{
			if (m_Taken || other.Name != "Player")
				return;
			m_Taken = true;
			Game.Coins++;
			GameManager.CoinTaken(m_Home);
			Entity.Destroy(this);
		}
	}
}
