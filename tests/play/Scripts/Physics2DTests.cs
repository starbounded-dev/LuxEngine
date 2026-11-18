// 2D checks for tests/play/run_play.py (M11): the Box2D world steps on the project's fixed clock, tells scripts about
// contacts and triggers, answers Physics2D queries, holds joints, and the sprite animator plays a sheet's clips.
// Same rules as PlayTests.cs: one PASS or FAIL line per check, counted in physics steps or frames.
using System;
using Lux;

namespace PlayTest
{
	public class Ground2D : Entity
	{
		public static int Begins, Ends;
		protected override void OnCollisionBegin(Entity other) { if (other.Name == "Ball2D") Begins++; }
		protected override void OnCollisionEnd(Entity other) { if (other.Name == "Ball2D") Ends++; }
	}

	// Drops onto Ground2D from 6.5 m up. Reports the fixed step, where it lands, the contacts it heard and the queries.
	public class Ball2D : Entity
	{
		int m_Steps;
		float m_FrameTime;
		float m_PreviousY = float.MaxValue;
		bool m_FallsEveryStep = true;
		int m_Begins, m_Ends;

		protected override void OnCollisionBegin(Entity other) { if (other.Name == "Ground2D") m_Begins++; }
		protected override void OnCollisionEnd(Entity other) { if (other.Name == "Ground2D") m_Ends++; }

		void OnUpdate(float ts)
		{
			if (m_Steps > 0)
				m_FrameTime += ts;
		}

		void OnFixedUpdate(float dt)
		{
			m_Steps++;
			float y = Translation.Y;
			// Each fixed step moves a falling body; frames with no step leave it where it was.
			if (m_Steps > 1 && m_Steps <= 20 && !(y < m_PreviousY))
				m_FallsEveryStep = false;
			m_PreviousY = y;

			if (m_Steps == 2)
				CheckQueries();

			if (m_Steps * dt >= 2.0f && m_Steps * dt < 2.0f + dt)
			{
				bool rateOk = Math.Abs(dt - 1.0f / 60.0f) < 1e-4f;
				bool countOk = Math.Abs(m_Steps * dt - m_FrameTime) <= 3.0f * dt;
				Report.Result("Physics2DFixed", rateOk && countOk && m_FallsEveryStep,
					$"dt={dt}, {m_Steps} steps = {m_Steps * dt:F3} s against {m_FrameTime:F3} s of frames, falls every step: {m_FallsEveryStep}");
			}

			if (m_Steps == (int)(3.0f / dt))
			{
				Report.Result("Physics2DLanding", Math.Abs(y - -4.5f) < 0.06f,
					$"a ball dropped from 2 m onto a floor at y = -5 rests at y = {y:F3}; expected -4.5");
			}

			if (m_Steps == (int)(4.0f / dt))
			{
				Report.Result("Contacts2D", m_Begins == 1 && m_Ends == 0 && Ground2D.Begins == 1 && Ground2D.Ends == 0,
					$"ball saw {m_Begins} begin / {m_Ends} end, ground saw {Ground2D.Begins} / {Ground2D.Ends}; expected 1 / 0 each");
			}
		}

		void CheckQueries()
		{
			string problem = null;

			if (!Physics2D.Raycast(new Vector2(3.0f, -3.0f), new Vector2(0.0f, -1.0f), out RaycastHit2D hit, 10.0f) || hit.Entity.Name != "Ground2D"
				|| Math.Abs(hit.Distance - 2.0f) > 0.01f || Math.Abs(hit.Point.Y - -5.0f) > 0.01f || hit.Normal.Y < 0.99f)
				problem = "a ray down from (3,-3) did not hit the floor 2 m away with an upward normal";

			if (problem == null)
			{
				RaycastHit2D[] hits = Physics2D.RaycastAll(new Vector2(6.0f, 0.0f), new Vector2(0.0f, -1.0f), 10.0f);
				RaycastHit2D[] withTriggers = Physics2D.RaycastAll(new Vector2(6.0f, 0.0f), new Vector2(0.0f, -1.0f), 10.0f, null, true);
				if (hits.Length != 1 || hits[0].Entity.Name != "Ground2D")
					problem = $"RaycastAll through a trigger found {hits.Length} hits instead of just the floor";
				else if (withTriggers.Length != 2 || withTriggers[0].Entity.Name != "Zone2D" || withTriggers[1].Entity.Name != "Ground2D" || withTriggers[0].Distance > withTriggers[1].Distance)
					problem = "RaycastAll with includeTriggers did not report the trigger, then the floor";
			}

			if (problem == null && Physics2D.Raycast(new Vector2(3.0f, -3.0f), new Vector2(0.0f, -1.0f), 10.0f, Entity.Find("Ground2D")))
				problem = "a ray ignoring the floor still hit it";

			if (problem == null)
			{
				Entity[] none = Physics2D.OverlapCircle(new Vector2(6.0f, -2.0f), 0.2f);
				Entity[] zone = Physics2D.OverlapCircle(new Vector2(6.0f, -2.0f), 0.2f, null, true);
				Entity[] ground = Physics2D.OverlapBox(new Vector2(10.0f, -5.0f), new Vector2(1.0f, 0.2f));
				Entity[] self = Physics2D.OverlapCircle(Translation.XY, 0.4f);
				if (none.Length != 0)
					problem = "an overlap saw a trigger without includeTriggers";
				else if (zone.Length != 1 || zone[0].Name != "Zone2D")
					problem = "an overlap with includeTriggers did not find the trigger zone";
				else if (ground.Length != 1 || ground[0].Name != "Ground2D")
					problem = "OverlapBox did not find the floor";
				else if (self.Length != 1 || self[0].ID != ID || Physics2D.OverlapCircle(Translation.XY, 0.4f, this).Length != 0)
					problem = "an overlap did not find the entity itself, or ignore did not leave it out";
			}

			Report.Result("Queries2D", problem == null, problem ?? "");
		}
	}

	// A trigger the faller passes through: one Begin and one End, and it blocks nothing.
	public class Zone2D : Entity
	{
		int m_Begins, m_Ends, m_Steps;

		protected override void OnTriggerBegin(Entity other) { if (other.Name == "Faller2D") m_Begins++; }
		protected override void OnTriggerEnd(Entity other) { if (other.Name == "Faller2D") m_Ends++; }

		void OnFixedUpdate(float dt)
		{
			if (++m_Steps != (int)(5.0f / dt))
				return;
			Entity faller = Entity.Find("Faller2D");
			float y = faller != null ? faller.Translation.Y : 99.0f;
			Report.Result("Triggers2D", m_Begins == 1 && m_Ends == 1 && Math.Abs(y - -4.7f) < 0.06f,
				$"zone saw {m_Begins} begin / {m_Ends} end; the faller rests at y = {y:F3} (expected -4.7, so the trigger did not block it)");
		}
	}

	// Swings from a distance joint pinned to the world at (8, 5): it stays 2 m from the pivot.
	public class Pendulum2D : Entity
	{
		int m_Steps;
		float m_Worst;

		void OnFixedUpdate(float dt)
		{
			m_Steps++;
			Vector3 p = Translation;
			float dx = p.X - 8.0f, dy = p.Y - 5.0f;
			m_Worst = Math.Max(m_Worst, Math.Abs((float)Math.Sqrt(dx * dx + dy * dy) - 2.0f));
			if (m_Steps == (int)(3.0f / dt))
				Report.Result("Joint2D", m_Worst < 0.06f && p.Y < 5.0f,
					$"a distance joint of 2 m: the pendulum strayed {m_Worst:F3} m from its length and is at y = {p.Y:F2}");
		}
	}

	// Plays the sheet's "blink" clip (frames 0 and 1), then a script switches to "still" (frame 1, plays once).
	public class SpriteAnimatorCheck : Entity
	{
		float m_Time;
		bool m_SawFirst, m_SawSecond, m_Switched;
		float m_SwitchedAt;

		void OnUpdate(float ts)
		{
			m_Time += ts;
			var sprite = GetComponent<SpriteRendererComponent>();
			var animator = GetComponent<SpriteAnimatorComponent>();
			if (!m_Switched)
			{
				if (sprite.SpriteIndex == 0)
					m_SawFirst = true;
				if (sprite.SpriteIndex == 1)
					m_SawSecond = true;
				if (m_Time > 1.0f)
				{
					Report.Result("SpriteAnimator", m_SawFirst && m_SawSecond && animator.Clip == "blink" && animator.IsPlaying,
						$"in a second of the 'blink' clip the sprite showed frame 0: {m_SawFirst}, frame 1: {m_SawSecond}; clip '{animator.Clip}', playing {animator.IsPlaying}");
					animator.Play("still");
					m_Switched = true;
					m_SwitchedAt = m_Time;
				}
			}
			else if (m_Time > m_SwitchedAt + 0.5f)
			{
				Report.Result("SpriteAnimatorScript", animator.Clip == "still" && sprite.SpriteIndex == 1 && animator.IsFinished && !animator.IsPlaying,
					$"after Play(\"still\"): clip '{animator.Clip}', frame {sprite.SpriteIndex}, finished {animator.IsFinished}, playing {animator.IsPlaying}; expected 'still', 1, true, false");
				m_Switched = false;
				m_Time = -1000.0f;
			}
		}
	}
}
