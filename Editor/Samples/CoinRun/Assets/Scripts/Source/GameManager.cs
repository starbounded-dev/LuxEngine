// SPDX-License-Identifier: Apache-2.0
using Lux;

namespace CoinRun
{
	// The interface, the clock, the coin sparkles and the end of a run. R starts the level again.
	public class GameManager : Entity
	{
		static GameManager s_Instance;

		UITextComponent m_CoinsText, m_TimeText, m_MessageText;
		ParticleSystemComponent m_Sparkle, m_GoalSparks;
		Entity m_SparkleEntity;
		float m_MessageTimer;
		float m_WinTimer;
		float m_LastProgress;
		bool m_GoalOpened;
		bool m_Reported;

		void OnCreate()
		{
			Game.Reset();
			s_Instance = this;
			m_CoinsText = Entity.Find("CoinsText").GetComponent<UITextComponent>();
			m_TimeText = Entity.Find("TimeText").GetComponent<UITextComponent>();
			m_MessageText = Entity.Find("MessageText").GetComponent<UITextComponent>();
			m_SparkleEntity = Entity.Find("Sparkle");
			m_Sparkle = m_SparkleEntity.GetComponent<ParticleSystemComponent>();
			m_GoalSparks = Entity.Find("GoalSparks").GetComponent<ParticleSystemComponent>();
			Say("Collect all ten coins", 3.0f);
		}

		void OnUpdate(float ts)
		{
			if (!Game.Won)
				Game.Clock += ts;
			m_CoinsText.Text = $"Coins {Game.Coins} / {Game.TotalCoins}";
			m_TimeText.Text = $"{Game.Clock:F1} s";

			if (Game.Coins >= Game.TotalCoins && !m_GoalOpened)
			{
				m_GoalOpened = true;
				m_GoalSparks.Play();
				Say("The goal is open", 3.0f);
			}

			if (m_MessageTimer > 0.0f)
			{
				m_MessageTimer -= ts;
				if (m_MessageTimer <= 0.0f && !Game.Won)
					m_MessageText.Text = "";
			}

			if (Input.IsKeyPressed(KeyCode.R))
				Scene.Current.Load();

			if (Game.Autoplay)
				ReportAutoplay(ts);
		}

		// The sample tests (tests/samples) read this line: the run is done when the goal is reached, or has failed after two minutes.
		void ReportAutoplay(float ts)
		{
			if (m_Reported)
				return;
			if (Game.Clock - m_LastProgress >= 5.0f)
			{
				m_LastProgress = Game.Clock;
				Entity player = Entity.Find("Player");
				Vector3 p = player != null ? player.Translation : default;
				Log.Info($"SAMPLE PROGRESS CoinRun t={Game.Clock:F0} coins={Game.Coins} at ({p.X:F1}, {p.Y:F1}, {p.Z:F1})");
			}
			if (Game.Won)
			{
				m_WinTimer += ts;
				if (m_WinTimer > 1.0f)
				{
					m_Reported = true;
					Log.Info($"SAMPLE COMPLETE CoinRun coins={Game.Coins}/{Game.TotalCoins} time={Game.Clock:F1}");
					Application.Quit();
				}
			}
			else if (Game.Clock > 120.0f)
			{
				m_Reported = true;
				Log.Error($"SAMPLE FAILED CoinRun coins={Game.Coins}/{Game.TotalCoins} after {Game.Clock:F0} s");
				Application.Quit();
			}
		}

		void Say(string text, float seconds)
		{
			m_MessageText.Text = text;
			m_MessageTimer = seconds;
		}

		public static void CoinTaken(Vector3 at)
		{
			if (s_Instance == null)
				return;
			s_Instance.m_SparkleEntity.Translation = at;
			s_Instance.m_Sparkle.Emit(24);
		}

		public static void Win()
		{
			if (s_Instance == null || Game.Won)
				return;
			Game.Won = true;
			s_Instance.m_MessageTimer = 0.0f;
			s_Instance.m_MessageText.Text = $"You made it!\n{Game.Clock:F1} seconds\nPress R to play again";
			s_Instance.m_GoalSparks.Emit(80);
		}
	}
}
