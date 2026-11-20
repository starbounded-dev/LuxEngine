// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace CoinRun
{
	// The pillar at the end of the course. It does nothing until every coin is in, then it pulses and sends up sparks, and standing next to
	// it wins. The distance is measured here rather than with a trigger, so a player who was already beside it when the last coin came in
	// still wins.
	public class Goal : Entity
	{
		public float Reach = 1.6f;

		Entity m_Player;
		float m_Time;

		void OnCreate()
		{
			m_Player = Entity.Find("Player");
		}

		void OnUpdate(float ts)
		{
			if (Game.Coins < Game.TotalCoins || Game.Won)
				return;

			m_Time += ts;
			float pulse = 1.0f + 0.08f * Mathf.Sin(m_Time * 6.0f);
			Scale = new Vector3(1.2f * pulse, 3.0f, 1.2f * pulse);

			if (m_Player == null)
				return;
			Vector3 to = m_Player.Translation - Translation;
			if (to.X * to.X + to.Z * to.Z < Reach * Reach && Mathf.Abs(to.Y) < 3.0f)
				GameManager.Win();
		}
	}
}
