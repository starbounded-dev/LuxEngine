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

	// S3: counts its own OnUpdate calls, so ActiveCheck can see when Active or ScriptEnabled stopped it.
	public class ToggleCounter : Entity
	{
		public int Count;

		void OnUpdate(float ts) => Count++;
	}

	// S3: Entity.Active, ActiveInHierarchy, ScriptEnabled and Component.Enabled, driven from a script.
	public class ActiveCheck : Entity
	{
		int m_Frame, m_TargetMark, m_ChildMark;
		Entity m_Target, m_Child;
		ToggleCounter m_TargetCounter, m_ChildCounter;
		string m_Problem;

		void Fail(string reason) { if (m_Problem == null) m_Problem = reason; }

		void OnUpdate(float ts)
		{
			m_Frame++;
			if (m_Frame == 1)
			{
				m_Target = FindEntityByName("ToggleTarget");
				m_Child = FindEntityByName("ToggleChild");
				m_TargetCounter = m_Target?.As<ToggleCounter>();
				m_ChildCounter = m_Child?.As<ToggleCounter>();
				if (m_TargetCounter == null || m_ChildCounter == null)
					Fail("the ToggleTarget / ToggleChild scripts were not found");
			}
			if (m_Problem != null && m_Frame < 60)
			{
				if (m_Frame == 59)
					Report.Result("ActiveEnabled", false, m_Problem);
				return;
			}

			switch (m_Frame)
			{
				case 5:
					SpriteChecks();
					break;
				case 10:
					if (m_TargetCounter.Count == 0 || m_ChildCounter.Count == 0)
						Fail("the counters were not running before anything was switched off");
					m_Target.Active = false;
					if (m_Target.Active || m_Target.ActiveInHierarchy)
						Fail("Active did not read back false");
					if (!m_Child.Active || m_Child.ActiveInHierarchy)
						Fail("a child of an inactive entity should keep its own Active but not be active in the hierarchy");
					break;
				case 11:
					m_TargetMark = m_TargetCounter.Count;
					m_ChildMark = m_ChildCounter.Count;
					break;
				case 20:
					if (m_TargetCounter.Count != m_TargetMark || m_ChildCounter.Count != m_ChildMark)
						Fail($"scripts on a switched-off entity and its child kept running ({m_TargetCounter.Count - m_TargetMark}, {m_ChildCounter.Count - m_ChildMark} more updates)");
					m_Target.Active = true;
					break;
				case 30:
					if (m_TargetCounter.Count <= m_TargetMark || m_ChildCounter.Count <= m_ChildMark)
						Fail("scripts did not resume after Active was set back to true");
					m_Child.ScriptEnabled = false;
					if (m_Child.ScriptEnabled || !m_Target.ScriptEnabled)
						Fail("ScriptEnabled did not read back");
					m_ChildMark = m_ChildCounter.Count;
					m_TargetMark = m_TargetCounter.Count;
					break;
				case 40:
					if (m_ChildCounter.Count != m_ChildMark)
						Fail("a script with ScriptEnabled = false kept updating");
					if (m_TargetCounter.Count <= m_TargetMark)
						Fail("switching one script off stopped its neighbour");
					m_Child.ScriptEnabled = true;
					m_ChildMark = m_ChildCounter.Count;
					break;
				case 50:
					if (m_ChildCounter.Count <= m_ChildMark)
						Fail("the script did not resume after ScriptEnabled = true");
					Report.Result("ActiveEnabled", m_Problem == null, m_Problem ?? "");
					break;
			}
		}

		void SpriteChecks()
		{
			Entity sprite = FindEntityByName("SwitchSprite");
			SpriteRendererComponent component = sprite?.GetComponent<SpriteRendererComponent>();
			if (component == null)
			{
				Fail("SwitchSprite has no SpriteRendererComponent");
				return;
			}
			if (!component.Enabled)
				Fail("a new sprite renderer should be enabled");
			component.Enabled = false;
			if (sprite.GetComponent<SpriteRendererComponent>().Enabled)
				Fail("Component.Enabled = false was not stored");
			component.Enabled = true;
			if (!sprite.GetComponent<SpriteRendererComponent>().Enabled)
				Fail("Component.Enabled = true was not stored");

			// A collider has no switch: it reads as on and stays on.
			BoxColliderComponent collider = FindEntityByName("Floor")?.GetComponent<BoxColliderComponent>();
			if (collider == null || !collider.Enabled)
				Fail("a component with no switch should read as enabled");
		}
	}

	// S4: finders by name, tag and component, and the user tag.
	public class FindCheck : Entity
	{
		int m_Frame;

		void OnUpdate(float ts)
		{
			if (++m_Frame != 3)
				return;
			string problem = null;
			void Expect(bool ok, string what) { if (!ok && problem == null) problem = what; }

			Entity[] twins = Entity.FindAll("TwinFind");
			Expect(twins.Length == 2 && Math.Abs(twins[0].Translation.X - 1.0f) < 1e-4f && Math.Abs(twins[1].Translation.X - 2.0f) < 1e-4f,
				$"FindAll(name) returned {twins.Length} entities, expected the two twins in creation order");
			Expect(Entity.Find("TwinFind") != null && Math.Abs(Entity.Find("TwinFind").Translation.X - 1.0f) < 1e-4f, "Find(name) should return the first twin");
			Expect(Entity.Find("NoSuchEntity") == null, "Find of a missing name should be null");

			Expect(Entity.FindAllWithTag("coin").Length == 2, $"FindAllWithTag(coin) found {Entity.FindAllWithTag("coin").Length}, expected 2 (the switched-off one and its child are skipped)");
			Expect(Entity.FindAllWithTag("coin", true).Length == 4, $"FindAllWithTag(coin, includeInactive) found {Entity.FindAllWithTag("coin", true).Length}, expected 4");
			Entity gem = Entity.FindWithTag("gem");
			Expect(gem != null && gem.Name == "TagGem" && gem.Tag == "gem" && gem.CompareTag("gem") && !gem.CompareTag("coin"), "FindWithTag(gem) / Tag / CompareTag");
			Expect(Entity.FindWithTag("nothing") == null && Entity.FindWithTag("") == null, "an unknown or empty tag should find nothing");
			Expect(Entity.FindWithTag("coin", true)?.Name == "TagCoinA", "the first coin in creation order is TagCoinA");

			if (gem != null)
			{
				gem.Tag = "coin";
				Expect(Entity.FindAllWithTag("coin").Length == 3, "a tag set from a script should be found at once");
				gem.Tag = "gem";
				Expect(Entity.FindAllWithTag("coin").Length == 2 && Entity.FindWithTag("gem") != null, "putting the tag back should restore the lookup");
			}

			Entity[] sprites = Entity.FindAllWithComponent<SpriteRendererComponent>();
			Expect(sprites.Length == 2 && sprites[0].Name == "SwitchSprite" && sprites[1].Name == "SpriteTwo", $"FindAllWithComponent found {sprites.Length} sprites in the wrong order or number");
			Expect(Entity.FindWithComponent<CircleRendererComponent>() == null, "no entity has a circle renderer");
			Expect(Entity.FindWithComponent<SpriteRendererComponent>().Name == "SwitchSprite", "FindWithComponent returns the first");
			Report.Result("Find", problem == null, problem ?? "");
		}
	}
}
