// Play-mode checks for the M4 C# API (docs/M4_CSHARP_API_PLAN.md), reported through the same Report as PlayTests.cs.
// Like the older checks they count frames or physics steps, never wall-clock time.
using System;
using Lux;

namespace PlayTest
{
	// SC2: Time. The scaled clock follows TimeScale, OnUpdate receives it, OnFixedUpdate gets the fixed step.
	public class ClockCheck : Entity
	{
		int m_Frame;
		long m_FirstFrameCount;
		int m_FixedCalls, m_FixedAtFreeze;
		bool m_FixedStepMatches = true;
		string m_Problem;

		void Fail(string reason) { if (m_Problem == null) m_Problem = reason; }

		void OnUpdate(float ts)
		{
			m_Frame++;
			if (Math.Abs(ts - Time.DeltaTime) > 1e-6f)
				Fail($"frame {m_Frame}: OnUpdate got {ts} but Time.DeltaTime is {Time.DeltaTime}");
			if (m_Frame == 1)
				m_FirstFrameCount = Time.FrameCount;
			if (Time.FrameCount != m_FirstFrameCount + m_Frame - 1)
				Fail($"frame {m_Frame}: FrameCount is {Time.FrameCount}, expected {m_FirstFrameCount + m_Frame - 1}");

			if (m_Frame == 10)
				Time.TimeScale = 0.5f;
			if (m_Frame > 10 && m_Frame <= 30 && Math.Abs(Time.DeltaTime - 0.5f * Time.UnscaledDeltaTime) > 1e-5f)
				Fail($"frame {m_Frame}: at scale 0.5 Time.DeltaTime is {Time.DeltaTime}, unscaled {Time.UnscaledDeltaTime}");
			if (m_Frame == 30)
			{
				Time.TimeScale = 0.0f;
				m_FixedAtFreeze = m_FixedCalls;
			}
			if (m_Frame > 30 && m_Frame <= 50 && (ts != 0.0f || Time.DeltaTime != 0.0f))
				Fail($"frame {m_Frame}: at scale 0 OnUpdate got {ts}, Time.DeltaTime {Time.DeltaTime}");
			if (m_Frame == 50)
			{
				// The fixed clock may carry one step of time over from before the freeze.
				if (m_FixedCalls - m_FixedAtFreeze > 1)
					Fail($"{m_FixedCalls - m_FixedAtFreeze} physics steps ran while the scale was 0");
				Time.TimeScale = 1.0f;
			}
			if (m_Frame == 60)
			{
				if (Time.TimeScale != 1.0f || Time.DeltaTime <= 0.0f)
					Fail($"after restoring the scale: TimeScale {Time.TimeScale}, DeltaTime {Time.DeltaTime}");
				if (!(Time.ElapsedTime < Time.UnscaledTime))
					Fail($"scaled time {Time.ElapsedTime} should be behind real time {Time.UnscaledTime} after a slow-motion stretch");
				if (!m_FixedStepMatches)
					Fail("inside OnFixedUpdate Time.DeltaTime or Time.FixedDeltaTime differed from the step it received");
				Report.Result("Clock", m_Problem == null, m_Problem ?? "");
			}
		}

		void OnFixedUpdate(float dt)
		{
			m_FixedCalls++;
			if (Math.Abs(Time.DeltaTime - dt) > 1e-6f || Math.Abs(Time.FixedDeltaTime - dt) > 1e-6f)
				m_FixedStepMatches = false;
		}
	}

	// SC2: quaternion rotation and world-space transform members. The parent is yawed a quarter turn.
	public class TransformCheck : Entity
	{
		bool m_Done;

		void OnUpdate(float ts)
		{
			if (m_Done)
				return;
			m_Done = true;
			string problem = null;

			Vector3 world = WorldTranslation;
			if (Vector3.Distance(world, new Vector3(8.0f, 1.0f, -40.0f)) > 1e-3f)
				problem = $"WorldTranslation is {world}, expected (8, 1, -40) through the yawed parent";
			Vector3 forward = Forward;
			if (problem == null && Vector3.Distance(forward, new Vector3(-1.0f, 0.0f, 0.0f)) > 1e-3f)
				problem = $"Forward is {forward}, expected (-1, 0, 0)";

			if (problem == null)
			{
				Rotation = new Vector3(0.2f, 0.6f, -0.4f);
				Vector3 viaQuaternion = RotationQuaternion.ToEuler();
				if (Vector3.Distance(viaQuaternion, new Vector3(0.2f, 0.6f, -0.4f)) > 1e-4f)
					problem = $"RotationQuaternion.ToEuler() is {viaQuaternion} after setting Rotation to (0.2, 0.6, -0.4)";
			}
			if (problem == null)
			{
				RotationQuaternion = Quaternion.Euler(new Vector3(0.0f, 0.5f, 0.0f));
				if (Vector3.Distance(Rotation, new Vector3(0.0f, 0.5f, 0.0f)) > 1e-4f)
					problem = $"Rotation is {Rotation} after setting RotationQuaternion to a 0.5 rad yaw";
			}
			Report.Result("Transform", problem == null, problem ?? "");
		}
	}
}
