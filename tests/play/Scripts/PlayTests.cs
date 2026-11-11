// Play-mode checks for tests/play/run_play.py. Each check logs exactly one line,
// "PLAYTEST PASS <name>" or "PLAYTEST FAIL <name>: <reason>"; the runner requires every
// expected PASS and no FAIL. Checks count physics steps (OnFixedUpdate) or frames, never
// wall-clock time, so they hold at any frame rate and under both threading policies.
using System;
using System.Collections.Generic;
using Lux;

namespace PlayTest
{
	static class Report
	{
		static readonly HashSet<string> s_Reported = new HashSet<string>();
		public static int Count => s_Reported.Count;

		public static void Result(string name, bool passed, string reason = "")
		{
			if (!s_Reported.Add(name))
				return;
			if (passed)
				Log.Info($"PLAYTEST PASS {name}");
			else
				Log.Error($"PLAYTEST FAIL {name}: {reason}");
		}

		public static string V(Vector3 v) => $"({v.X:F3}, {v.Y:F3}, {v.Z:F3})";

		// ScriptCore's Vector3 has no Length or subtraction yet (catalog SC2).
		public static float Length(Vector3 v) => (float)Math.Sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
		public static float Distance(Vector3 a, Vector3 b) => Length(new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z));
	}

	// OnFixedUpdate runs once per physics step with the fixed timestep, before the step.
	public class FixedStep : Entity
	{
		int m_Steps;
		float m_FrameTime;
		float m_StartX;
		bool m_Done;

		void OnCreate() => m_StartX = Translation.X;

		void OnUpdate(float ts)
		{
			if (m_Steps > 0)
				m_FrameTime += ts;
		}

		void OnFixedUpdate(float dt)
		{
			if (m_Done)
				return;
			m_Steps++;
			var body = GetComponent<RigidBodyComponent>();

			// Before the step: a velocity set here moves the body in this very step.
			if (m_Steps == 10)
				body.LinearVelocity = new Vector3(1.0f, 0.0f, 0.0f);
			if (m_Steps == 11)
			{
				float moved = body.Translation.X - m_StartX;
				Report.Result("FixedBeforeStep", moved > 0.5f * dt && moved < 1.5f * dt,
					$"after one step at 1 m/s the body moved {moved:F5} m, expected about {dt:F5}");
			}

			if (m_Steps * dt >= 2.0f)
			{
				m_Done = true;
				bool rateOk = Math.Abs(dt - 1.0f / 60.0f) < 1e-4f;
				// The fixed clock may carry up to one step of time over, and the first frame starts it.
				bool countOk = Math.Abs(m_Steps * dt - m_FrameTime) <= 3.0f * dt;
				Report.Result("FixedStep", rateOk && countOk,
					$"dt={dt}, {m_Steps} steps = {m_Steps * dt:F3} s against {m_FrameTime:F3} s of frames");
			}
		}
	}

	public class FallAndLand : Entity
	{
		int m_Steps;

		void OnFixedUpdate(float dt)
		{
			if (++m_Steps != (int)(3.0f / dt))
				return;
			var body = GetComponent<RigidBodyComponent>();
			Vector3 p = body.Translation;
			float speed = Report.Length(body.LinearVelocity);
			Report.Result("FallAndLand", Math.Abs(p.Y - 0.5f) < 0.05f && speed < 0.05f,
				$"box dropped from 5 m rests at {Report.V(p)} moving {speed:F3} m/s; expected y=0.5 at rest");
		}
	}

	// Lands, reports grounded, walks, jumps and lands again. Walks through HeroTrigger.
	public class Hero : Entity
	{
		int m_Steps;
		float m_WalkStartX;
		float m_JumpStartY;
		float m_JumpPeak;
		string m_Failure;

		void OnFixedUpdate(float dt)
		{
			m_Steps++;
			var controller = GetComponent<CharacterControllerComponent>();
			float t = m_Steps * dt;
			Vector3 p = Translation;

			if (m_Failure != null)
				return;
			if (t < 1.5f)
				return;
			if (m_WalkStartX == 0.0f)
			{
				if (!controller.IsGrounded)
				{
					Fail($"not grounded 1.5 s after spawning 1 m above the floor, at {Report.V(p)}");
					return;
				}
				m_WalkStartX = p.X;
			}
			if (t < 4.0f)
			{
				controller.Move(new Vector3(2.0f * dt, 0.0f, 0.0f));
				return;
			}
			if (m_JumpStartY == 0.0f)
			{
				if (p.X - m_WalkStartX < 4.0f)
				{
					Fail($"walked {p.X - m_WalkStartX:F2} m in 2.5 s at 2 m/s");
					return;
				}
				m_JumpStartY = p.Y;
				m_JumpPeak = p.Y;
				controller.Jump(5.0f);
				return;
			}
			m_JumpPeak = Math.Max(m_JumpPeak, p.Y);
			if (t >= 6.5f)
			{
				bool rose = m_JumpPeak - m_JumpStartY > 0.5f;
				bool landed = controller.IsGrounded && Math.Abs(p.Y - m_JumpStartY) < 0.1f;
				Report.Result("Character", rose && landed,
					$"jump peak {m_JumpPeak - m_JumpStartY:F2} m above start, grounded={controller.IsGrounded}, y now {p.Y:F2} against {m_JumpStartY:F2}");
				m_Failure = "";
			}
		}

		void Fail(string reason)
		{
			m_Failure = reason;
			Report.Result("Character", false, reason);
		}
	}

	// Walks along +X into a ramp; the scene sets the ramp's angle and names the check.
	public class Climber : Entity
	{
		int m_Steps;
		float m_StartY;

		void OnFixedUpdate(float dt)
		{
			m_Steps++;
			float t = m_Steps * dt;
			if (t < 1.5f)
			{
				m_StartY = Translation.Y;
				return;
			}
			if (t < 5.5f)
			{
				GetComponent<CharacterControllerComponent>().Move(new Vector3(2.0f * dt, 0.0f, 0.0f));
				return;
			}
			if (m_Steps != (int)(5.5f / dt) + 1)
				return;

			float rise = Translation.Y - m_StartY;
			if (Name == "Climber30")
				Report.Result("SlopeClimb", rise > 1.0f, $"rose {rise:F2} m walking 8 m up a 30 degree ramp (slope limit 45)");
			else
				Report.Result("SlopeBlock", rise < 0.6f, $"rose {rise:F2} m walking into a 60 degree ramp (slope limit 45, step 0.5)");
		}
	}

	public class ContactTarget : Entity
	{
		public static int Begins, Ends;
		protected override void OnCollisionBegin(Entity other) { if (other.Name == "Mover") Begins++; }
		protected override void OnCollisionEnd(Entity other) { if (other.Name == "Mover") Ends++; }
	}

	// Pushes into ContactTarget, then backs away; both sides must see one Begin and one End.
	public class ContactMover : Entity
	{
		int m_Steps;
		int m_Begins, m_Ends;
		int m_BackAt;

		protected override void OnCollisionBegin(Entity other) { if (other.Name == "Target") m_Begins++; }
		protected override void OnCollisionEnd(Entity other) { if (other.Name == "Target") m_Ends++; }

		void OnFixedUpdate(float dt)
		{
			m_Steps++;
			var body = GetComponent<RigidBodyComponent>();
			if (m_Begins > 0 && m_BackAt == 0)
				m_BackAt = m_Steps + (int)(0.25f / dt);
			body.LinearVelocity = new Vector3(m_BackAt != 0 && m_Steps >= m_BackAt ? -2.0f : 2.0f, 0.0f, 0.0f);

			if (m_Steps == (int)(4.0f / dt))
			{
				bool ok = m_Begins == 1 && m_Ends == 1 && ContactTarget.Begins == 1 && ContactTarget.Ends == 1;
				Report.Result("Contacts", ok,
					$"mover saw {m_Begins} begin / {m_Ends} end, target saw {ContactTarget.Begins} / {ContactTarget.Ends}; expected 1 / 1 each");
			}
		}
	}

	// A static trigger; counts Begin/End per entity that passes through it.
	public class TriggerZone : Entity
	{
		public static readonly Dictionary<string, int> Begins = new Dictionary<string, int>();
		public static readonly Dictionary<string, int> Ends = new Dictionary<string, int>();

		protected override void OnTriggerBegin(Entity other) => Add(Begins, other.Name);
		protected override void OnTriggerEnd(Entity other) => Add(Ends, other.Name);

		static void Add(Dictionary<string, int> counts, string name) => counts[name] = (counts.TryGetValue(name, out int n) ? n : 0) + 1;
		public static int Count(Dictionary<string, int> counts, string name) => counts.TryGetValue(name, out int n) ? n : 0;
	}

	// Flies through TriggerZone at 3 m/s, then judges both the body and the character (Hero).
	public class TriggerRunner : Entity
	{
		int m_Steps;

		void OnFixedUpdate(float dt)
		{
			m_Steps++;
			GetComponent<RigidBodyComponent>().LinearVelocity = new Vector3(3.0f, 0.0f, 0.0f);
			if (m_Steps != (int)(7.0f / dt))
				return;

			var b = TriggerZone.Begins;
			var e = TriggerZone.Ends;
			bool ok = TriggerZone.Count(b, "TriggerRunner") == 1 && TriggerZone.Count(e, "TriggerRunner") == 1
				&& TriggerZone.Count(b, "Hero") == 1 && TriggerZone.Count(e, "Hero") == 1;
			Report.Result("Triggers", ok,
				$"body {TriggerZone.Count(b, "TriggerRunner")} begin / {TriggerZone.Count(e, "TriggerRunner")} end, " +
				$"character {TriggerZone.Count(b, "Hero")} / {TriggerZone.Count(e, "Hero")}; expected 1 / 1 each");
		}
	}

	// On the prefab's root: records which instances have started.
	public class CrateScript : Entity
	{
		public static readonly HashSet<ulong> Created = new HashSet<ulong>();
		void OnCreate() => Created.Add(ID);
	}

	public class Spawner : Entity
	{
		public Prefab Crate;
		int m_Frames;
		int m_Steps;
		Entity m_Spawned;
		string m_Failure;

		void OnUpdate(float ts)
		{
			if (++m_Frames != 10)
				return;
			if (Crate == null || Crate.Handle == 0)
			{
				Fail("the Crate prefab field is not assigned");
				return;
			}
			Vector3 at = new Vector3(20.0f, 3.0f, 0.0f);
			m_Spawned = Instantiate(Crate, at);
			if (m_Spawned == null)
			{
				Fail("Instantiate returned null");
				return;
			}
			Vector3 p = m_Spawned.Translation;
			if (Report.Distance(p, at) > 0.01f)
				Fail($"root spawned at {Report.V(p)}, asked {Report.V(at)}");
			else if (m_Spawned.Children.Length != 3)
				Fail($"root has {m_Spawned.Children.Length} children, the prefab has 3");
			else if (!CrateScript.Created.Contains(m_Spawned.ID))
				Fail("the instance's script had not run OnCreate when Instantiate returned");
		}

		void OnFixedUpdate(float dt)
		{
			if (m_Spawned == null || m_Failure != null)
				return;
			if (++m_Steps != (int)(3.0f / dt))
				return;
			float y = m_Spawned.GetComponent<RigidBodyComponent>().Translation.Y;
			Report.Result("Spawn", Math.Abs(y - 0.5f) < 0.05f, $"spawned crate came to rest at y={y:F3}, expected 0.5 on the floor");
		}

		void Fail(string reason)
		{
			m_Failure = reason;
			Report.Result("Spawn", false, reason);
		}
	}

	public class SelfDestruct : Entity
	{
		public static int Updates, Destroys;
		void OnUpdate(float ts)
		{
			if (++Updates == 5)
				Destroy();
		}
		void OnDestroy() => Destroys++;
	}

	public class DestroyWatcher : Entity
	{
		int m_Frames;
		void OnUpdate(float ts)
		{
			if (++m_Frames != 30)
				return;
			bool gone = FindEntityByName("SelfDestruct") == null;
			Report.Result("Destroy", gone && SelfDestruct.Destroys == 1 && SelfDestruct.Updates == 5,
				$"entity gone={gone}, OnDestroy ran {SelfDestruct.Destroys} time(s), OnUpdate {SelfDestruct.Updates} time(s); expected gone, 1, 5");
		}
	}

	public class GizmoDrawer : Entity
	{
		int m_Frames;
		void OnUpdate(float ts)
		{
			Vector3 p = Translation;
			Gizmos.DrawLine(p, p + new Vector3(0.0f, 2.0f, 0.0f), new Vector4(1.0f, 0.0f, 0.0f, 1.0f));
			Gizmos.DrawWireBox(p, new Vector3(0.5f), new Vector4(0.0f, 1.0f, 0.0f, 1.0f));
			Gizmos.DrawWireSphere(p, 0.75f, new Vector4(0.0f, 0.0f, 1.0f, 1.0f), 0.5f);
			if (++m_Frames == 120)
				Report.Result("Gizmos", true);
		}
		void OnFixedUpdate(float dt) => Gizmos.DrawRay(Translation, new Vector3(1.0f, 0.0f, 0.0f), new Vector4(1.0f));
	}

	// Looks at the scene's instance of Crate, saved before the prefab gained PrefabAdded: tracked,
	// with Removed recorded as deleted and InstanceAdded added by the instance. Before frame 10,
	// when Spawner adds a second "Crate".
	public class PrefabSyncCheck : Entity
	{
		int m_Frames;
		void OnUpdate(float ts)
		{
			if (++m_Frames != 5)
				return;
			Entity crate = FindEntityByName("Crate");
			if (crate == null)
			{
				Report.Result("PrefabSync", false, "the scene's Crate instance is missing");
				return;
			}
			var names = new List<string>();
			foreach (Entity child in crate.Children)
				names.Add(child.Name);
			names.Sort(StringComparer.Ordinal);
			string got = string.Join(",", names);
			Report.Result("PrefabSync", got == "InstanceAdded,OldChild,PrefabAdded",
				$"instance children are [{got}], expected [InstanceAdded,OldChild,PrefabAdded]");
		}
	}

	// Ends the exported game once every check has reported (or after 10 s of physics, so a check
	// that never reports still ends the run). The editor ignores Application.Quit; its hook stops Play.
	public class Finisher : Entity
	{
		public int ExpectedChecks = 12;
		int m_Steps;
		bool m_Quit;

		void OnFixedUpdate(float dt)
		{
			if (m_Quit || (Report.Count < ExpectedChecks && ++m_Steps < (int)(10.0f / dt)))
				return;
			m_Quit = true;
			Log.Info($"PLAYTEST END {Report.Count} reported");
			Application.Quit();
		}
	}
}
