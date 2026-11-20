// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace PixelHop
{
	// The interface of a level, R to restart, and the end of the game.
	public class GameManager : Entity
	{
		static GameManager s_Instance;

		UITextComponent m_CoinsText, m_MessageText;
		float m_LastProgress;
		float m_EndTimer;
		bool m_Ended;
		bool m_Reported;

		void OnCreate()
		{
			Game.StartLevel();
			s_Instance = this;
			m_CoinsText = Entity.Find("CoinsText").GetComponent<UITextComponent>();
			m_MessageText = Entity.Find("MessageText").GetComponent<UITextComponent>();
			m_MessageText.Text = "Reach the flag";
		}

		void OnUpdate(float ts)
		{
			m_CoinsText.Text = $"Coins {Game.Coins}";
			if (Input.IsKeyPressed(KeyCode.R))
			{
				Game.Coins = 0;
				Scene.Current.Load();
			}

			if (!Game.Autoplay || m_Reported)
				return;

			Game.AutoplayClock += ts;
			if (Game.AutoplayClock - m_LastProgress >= 5.0f)
			{
				m_LastProgress = Game.AutoplayClock;
				Entity hero = Entity.Find("Hero");
				Vector3 p = hero != null ? hero.Translation : default;
				Log.Info($"SAMPLE PROGRESS PixelHop t={Game.AutoplayClock:F0} coins={Game.Coins} hero at ({p.X:F1}, {p.Y:F1})");
			}

			if (m_Ended)
			{
				m_EndTimer += ts;
				if (m_EndTimer > 1.0f)
				{
					m_Reported = true;
					Log.Info($"SAMPLE COMPLETE PixelHop coins={Game.Coins} time={Game.AutoplayClock:F1}");
					Application.Quit();
				}
			}
			else if (Game.AutoplayClock > 150.0f)
			{
				m_Reported = true;
				Log.Error($"SAMPLE FAILED PixelHop coins={Game.Coins} after {Game.AutoplayClock:F0} s");
				Application.Quit();
			}
		}

		public static void LevelReached(bool hasNext)
		{
			if (s_Instance == null)
				return;
			s_Instance.m_MessageText.Text = hasNext ? "Level complete!" : $"You won with {Game.Coins} coins!\nPress R to play again";
			if (!hasNext)
				s_Instance.m_Ended = true;
		}
	}
}
