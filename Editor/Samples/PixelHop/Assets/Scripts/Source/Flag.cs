// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace PixelHop
{
	// The flag at the end of a level. Reaching it loads the next level, or ends the game when it has none. Assign Next in the inspector.
	public class Flag : Entity
	{
		public Scene Next;

		Entity m_Hero;
		float m_Timer;
		bool m_Reached;

		void OnCreate()
		{
			m_Hero = Entity.Find("Hero");
		}

		void OnUpdate(float ts)
		{
			if (!m_Reached)
			{
				if (m_Hero == null)
					return;
				Vector3 to = m_Hero.Translation - Translation;
				if (Mathf.Abs(to.X) < 0.8f && Mathf.Abs(to.Y) < 4.0f)
				{
					m_Reached = true;
					Game.LevelDone = true;
					GameManager.LevelReached((Next != null && Next.Handle != 0));
				}
				return;
			}

			m_Timer += ts;
			if ((Next != null && Next.Handle != 0) && m_Timer > 1.0f)
			{
				Next.Load();
				m_Reached = false;
				m_Timer = -100.0f;
			}
		}
	}
}
