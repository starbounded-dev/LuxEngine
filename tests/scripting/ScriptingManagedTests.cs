using System;
using Coral.Managed.Interop;
using Lux;

// The production ScriptCore API against stand-ins for the native calls: what each member sends, how it reads the
// answer, and the pure C# maths. Each Run* method covers one checkpoint of the M4 plan.
internal static unsafe partial class ScriptingManagedTests
{
	private static int s_Checks;

	private static void Check(bool value, string what)
	{
		s_Checks++;
		if (!value)
			throw new Exception("Managed scripting contract failed: " + what);
	}

	private static bool Near(float a, float b, float tolerance = 1e-4f) => Math.Abs(a - b) <= tolerance;
	private static bool Near(Vector3 a, Vector3 b, float tolerance = 1e-4f) => Near(a.X, b.X, tolerance) && Near(a.Y, b.Y, tolerance) && Near(a.Z, b.Z, tolerance);

	private static int Main()
	{
		RunMaths();
		RunTime();
		RunActive();
		Console.WriteLine($"Managed scripting contract: {s_Checks} checks passed");
		return 0;
	}

	// ---- SC2: Mathf, vectors, Quaternion, Color, Random --------------------------------------------------------

	private static void RunMaths()
	{
		Check(Mathf.Clamp(5.0f, 0.0f, 1.0f) == 1.0f && Mathf.Clamp(-5, 0, 3) == 0 && Mathf.Clamp01(0.25f) == 0.25f, "Clamp");
		Check(Mathf.Lerp(0.0f, 10.0f, 0.5f) == 5.0f && Mathf.Lerp(0.0f, 10.0f, 2.0f) == 10.0f && Mathf.LerpUnclamped(0.0f, 10.0f, 2.0f) == 20.0f, "Lerp");
		Check(Mathf.InverseLerp(10.0f, 20.0f, 15.0f) == 0.5f && Mathf.InverseLerp(3.0f, 3.0f, 9.0f) == 0.0f, "InverseLerp");
		Check(Mathf.MoveTowards(0.0f, 10.0f, 3.0f) == 3.0f && Mathf.MoveTowards(9.0f, 10.0f, 3.0f) == 10.0f && Mathf.MoveTowards(5.0f, 0.0f, 2.0f) == 3.0f, "MoveTowards");
		Check(Near(Mathf.SmoothStep(0.0f, 1.0f, 0.5f), 0.5f) && Mathf.SmoothStep(0.0f, 1.0f, -1.0f) == 0.0f, "SmoothStep");
		Check(Mathf.Approximately(0.1f + 0.2f, 0.3f) && !Mathf.Approximately(1.0f, 1.001f), "Approximately");
		Check(Near(Mathf.Repeat(5.5f, 2.0f), 1.5f) && Near(Mathf.Repeat(-0.5f, 2.0f), 1.5f), "Repeat");
		Check(Near(Mathf.PingPong(3.0f, 2.0f), 1.0f) && Near(Mathf.PingPong(0.5f, 2.0f), 0.5f), "PingPong");
		Check(Near(Mathf.DeltaAngle(350.0f, 10.0f), 20.0f) && Near(Mathf.DeltaAngle(10.0f, 350.0f), -20.0f), "DeltaAngle");
		Check(Near(Mathf.LerpAngle(350.0f, 10.0f, 0.5f), 360.0f) && Near(Mathf.MoveTowardsAngle(350.0f, 10.0f, 5.0f), 355.0f), "LerpAngle and MoveTowardsAngle");
		Check(Near(180.0f * Mathf.Deg2Rad, Mathf.PI) && Near(Mathf.PI * Mathf.Rad2Deg, 180.0f), "Deg2Rad and Rad2Deg");

		float velocity = 0.0f, value = 0.0f;
		for (int i = 0; i < 600; i++)
			value = Mathf.SmoothDamp(value, 10.0f, ref velocity, 0.25f, 1.0f / 60.0f);
		Check(Near(value, 10.0f, 0.01f) && value <= 10.0f, "SmoothDamp settles on the target without overshoot");

		Vector3 a = new Vector3(1.0f, 2.0f, 3.0f), b = new Vector3(4.0f, 6.0f, 3.0f);
		Check(a - b == new Vector3(-3.0f, -4.0f, 0.0f) && -a == new Vector3(-1.0f, -2.0f, -3.0f) && 2.0f * a == a * 2.0f && a / 2.0f == new Vector3(0.5f, 1.0f, 1.5f), "Vector3 operators");
		Check(Near((b - a).Length(), 5.0f) && Near(Vector3.Distance(a, b), 5.0f) && Near(new Vector3(0.0f, 3.0f, 4.0f).Normalized().Length(), 1.0f) && Vector3.Zero.Normalized() == Vector3.Zero, "Vector3 length");
		Check(Vector3.Dot(Vector3.Right, Vector3.Up) == 0.0f && Vector3.Cross(Vector3.Right, Vector3.Up) == new Vector3(0.0f, 0.0f, 1.0f), "Vector3 dot and cross");
		Check(Near(Vector3.Angle(Vector3.Right, Vector3.Up), Mathf.PI * 0.5f) && Near(Vector3.Reflect(new Vector3(1.0f, -1.0f, 0.0f), Vector3.Up), new Vector3(1.0f, 1.0f, 0.0f)), "Vector3 angle and reflect");
		Check(Near(Vector3.Lerp(a, b, 0.5f), new Vector3(2.5f, 4.0f, 3.0f)) && Near(Vector3.MoveTowards(Vector3.Zero, new Vector3(0.0f, 10.0f, 0.0f), 4.0f), new Vector3(0.0f, 4.0f, 0.0f)), "Vector3 lerp and move");
		Check(Near(Vector3.ClampMagnitude(new Vector3(0.0f, 10.0f, 0.0f), 3.0f), new Vector3(0.0f, 3.0f, 0.0f)) && Near(Vector3.ProjectOnPlane(new Vector3(1.0f, 1.0f, 0.0f), Vector3.Up), new Vector3(1.0f, 0.0f, 0.0f)), "Vector3 clamp and project");
		Check(Vector3.Forward == new Vector3(0.0f, 0.0f, -1.0f) && Vector3.One == new Vector3(1.0f) && a.Equals((object)new Vector3(1.0f, 2.0f, 3.0f)) && a.GetHashCode() == new Vector3(1.0f, 2.0f, 3.0f).GetHashCode(), "Vector3 constants and equality");
		Check(new Vector2(1.0f, 2.0f) - new Vector2(3.0f, 5.0f) == new Vector2(-2.0f, -3.0f) && Near(new Vector2(3.0f, 4.0f).Normalized().Length(), 1.0f) && Near(Vector2.Distance(Vector2.Zero, new Vector2(3.0f, 4.0f)), 5.0f), "Vector2 operators");
		Check(Vector4.Lerp(Vector4.Zero, new Vector4(2.0f), 0.5f) == new Vector4(1.0f) && new Vector4(1.0f) - new Vector4(1.0f) == Vector4.Zero, "Vector4 operators");

		Quaternion identity = Quaternion.Identity;
		Check(identity * new Vector3(1.0f, 2.0f, 3.0f) == new Vector3(1.0f, 2.0f, 3.0f), "identity rotates nothing");
		Check(Near(Quaternion.Euler(new Vector3(0.0f, Mathf.PI * 0.5f, 0.0f)) * Vector3.Right, new Vector3(0.0f, 0.0f, -1.0f)), "yaw a quarter turn takes +X to -Z");
		Check(Near(Quaternion.Euler(new Vector3(Mathf.PI * 0.5f, 0.0f, 0.0f)) * Vector3.Up, new Vector3(0.0f, 0.0f, 1.0f)), "pitch a quarter turn takes +Y to +Z");
		Check(Near(Quaternion.Euler(new Vector3(0.0f, 0.0f, Mathf.PI * 0.5f)) * Vector3.Right, Vector3.Up), "roll a quarter turn takes +X to +Y");
		Vector3 euler = new Vector3(0.3f, -0.7f, 1.1f);
		Check(Near(Quaternion.Euler(euler).ToEuler(), euler), "Euler round trip");
		Check(Near(Quaternion.AngleAxis(Mathf.PI * 0.5f, Vector3.Up) * Vector3.Right, new Vector3(0.0f, 0.0f, -1.0f)), "AngleAxis");
		Quaternion q = Quaternion.Euler(new Vector3(0.4f, 0.9f, -0.2f));
		Check(Near(q.Length(), 1.0f) && Near(q * q.Inverse() * Vector3.One, Vector3.One) && Near(Quaternion.Dot(q, q), 1.0f), "inverse and length");
		Quaternion turn = Quaternion.Euler(new Vector3(0.0f, 1.0f, 0.0f)), other = Quaternion.Euler(new Vector3(0.5f, 0.0f, 0.0f));
		Check(Near((turn * other) * Vector3.Right, turn * (other * Vector3.Right)), "composition applies the right rotation first");
		Check(Near(Quaternion.Angle(identity, turn), 1.0f), "Angle");
		Quaternion half = Quaternion.Slerp(identity, turn, 0.5f);
		Check(Near(Quaternion.Angle(identity, half), 0.5f) && Near(Quaternion.Angle(half, turn), 0.5f) && Quaternion.Slerp(identity, turn, 0.0f) == identity, "Slerp");
		Check(Near(Quaternion.Angle(identity, Quaternion.RotateTowards(identity, turn, 0.25f)), 0.25f) && Quaternion.RotateTowards(identity, turn, 5.0f) == turn, "RotateTowards");
		Check(Near(Quaternion.FromToRotation(Vector3.Right, Vector3.Up) * Vector3.Right, Vector3.Up) && Near(Quaternion.FromToRotation(Vector3.Right, Vector3.Left) * Vector3.Right, Vector3.Left), "FromToRotation");
		foreach (Vector3 direction in new[] { new Vector3(1.0f, 0.0f, 0.0f), new Vector3(0.0f, 0.0f, 1.0f), new Vector3(0.3f, 0.5f, -0.8f), new Vector3(-1.0f, -1.0f, 0.2f) })
			Check(Near(Quaternion.LookRotation(direction, Vector3.Up) * Vector3.Forward, direction.Normalized(), 1e-3f), $"LookRotation faces {direction}");
		Check(Near(Quaternion.LookRotation(Vector3.Up, Vector3.Up) * Vector3.Forward, Vector3.Up, 1e-3f), "LookRotation straight up");

		Check(Color.Red == new Color(1.0f, 0.0f, 0.0f, 1.0f) && Color.Clear.A == 0.0f, "Color constants");
		Check(Color.Lerp(Color.Black, Color.White, 0.5f) == new Color(0.5f, 0.5f, 0.5f, 1.0f), "Color.Lerp");
		Vector4 asVector = Color.Green;
		Color fromVector = new Vector4(0.1f, 0.2f, 0.3f, 0.4f);
		Check(asVector == new Vector4(0.0f, 1.0f, 0.0f, 1.0f) && fromVector == new Color(0.1f, 0.2f, 0.3f, 0.4f), "Color converts to and from Vector4");
		Color hex = Color.FromHex("#ff8000", out bool parsed);
		Check(parsed && Near(hex.R, 1.0f) && Near(hex.G, 128.0f / 255.0f) && hex.B == 0.0f && hex.A == 1.0f && hex.ToHex() == "FF8000FF", "Color hex");
		Color.FromHex("zzz", out parsed);
		Check(!parsed, "Color hex rejects garbage");
		foreach (Color source in new[] { Color.Red, Color.Green, new Color(0.2f, 0.6f, 0.9f), new Color(0.9f, 0.8f, 0.1f) })
		{
			source.ToHSV(out float h, out float s, out float v);
			Color back = Color.FromHSV(h, s, v);
			Check(Near(back.R, source.R) && Near(back.G, source.G) && Near(back.B, source.B), $"HSV round trip of {source}");
		}
		Check(Color.White.WithAlpha(0.25f).A == 0.25f && Color.Red * Color.Green == Color.Black && Color.White * 0.5f == new Color(0.5f, 0.5f, 0.5f, 0.5f), "Color operators");

		Lux.Random.InitState(1234);
		float first = Lux.Random.Value, second = Lux.Random.Range(5.0f, 6.0f);
		int whole = Lux.Random.Range(10, 20);
		Lux.Random.InitState(1234);
		Check(first == Lux.Random.Value && second == Lux.Random.Range(5.0f, 6.0f) && whole == Lux.Random.Range(10, 20), "Random repeats from a seed");
		for (int i = 0; i < 1000; i++)
		{
			float v = Lux.Random.Value;
			int n = Lux.Random.Range(-3, 3);
			Check(v >= 0.0f && v < 1.0f && n >= -3 && n < 3, "Random ranges");
			Check(Lux.Random.InsideUnitSphere.Length() <= 1.0001f && Near(Lux.Random.OnUnitSphere.Length(), 1.0f, 1e-3f) && Lux.Random.InsideUnitCircle.Length() <= 1.0001f, "Random unit shapes");
			Check(Near(Lux.Random.Rotation.Length(), 1.0f, 1e-3f), "Random rotation is a unit quaternion");
		}
	}

	// ---- SC2: Time -------------------------------------------------------------------------------------------------

	private static double[] s_Clock = new double[7];
	private static float s_ScaleSet = -1.0f;
	private static double TimeGet(int field) => s_Clock[field];
	private static void TimeSetScale(float scale) { s_ScaleSet = scale; s_Clock[4] = scale; }

	private static void RunTime()
	{
		InternalCalls.Time_Get = &TimeGet;
		InternalCalls.Time_SetScale = &TimeSetScale;
		s_Clock = new double[] { 0.008, 0.016, 12.5, 14.0, 0.5, 1.0 / 60.0, 777.0 };
		Check(Near(Time.DeltaTime, 0.008f) && Near(Time.UnscaledDeltaTime, 0.016f) && Near(Time.FixedDeltaTime, 1.0f / 60.0f), "Time deltas");
		Check(Time.ElapsedTime == 12.5 && Time.UnscaledTime == 14.0 && Time.FrameCount == 777 && Near(Time.TimeScale, 0.5f), "Time totals");
		Time.TimeScale = 2.0f;
		Check(s_ScaleSet == 2.0f && Time.TimeScale == 2.0f, "TimeScale sets the native scale");
		Time.TimeScale = -3.0f;
		Check(s_ScaleSet == 0.0f, "A negative TimeScale is sent as 0");
	}

	// ---- S3: Active and Enabled ---------------------------------------------------------------------------------

	private static bool s_EntityActive = true, s_InHierarchy = true, s_ScriptEnabled = true, s_ComponentEnabled = true;
	private static ulong s_ActiveId;
	private static Bool32 EntityGetActive(ulong id) { s_ActiveId = id; return s_EntityActive; }
	private static void EntitySetActive(ulong id, Bool32 active) { s_ActiveId = id; s_EntityActive = active; s_InHierarchy = active; }
	private static Bool32 EntityInHierarchy(ulong id) => s_InHierarchy;
	private static Bool32 EntityGetScriptEnabled(ulong id) => s_ScriptEnabled;
	private static void EntitySetScriptEnabled(ulong id, Bool32 enabled) { s_ScriptEnabled = enabled; }
	private static Bool32 ComponentGetEnabled(ulong id, ReflectionType type) { s_ActiveId = id; return s_ComponentEnabled; }
	private static void ComponentSetEnabled(ulong id, ReflectionType type, Bool32 enabled) { s_ActiveId = id; s_ComponentEnabled = enabled; }

	private static void RunActive()
	{
		InternalCalls.Entity_GetActive = &EntityGetActive;
		InternalCalls.Entity_SetActive = &EntitySetActive;
		InternalCalls.Entity_IsActiveInHierarchy = &EntityInHierarchy;
		InternalCalls.Entity_GetScriptEnabled = &EntityGetScriptEnabled;
		InternalCalls.Entity_SetScriptEnabled = &EntitySetScriptEnabled;
		InternalCalls.Component_GetEnabled = &ComponentGetEnabled;
		InternalCalls.Component_SetEnabled = &ComponentSetEnabled;

		Entity entity = new Entity(42);
		Check(entity.Active && entity.ActiveInHierarchy && s_ActiveId == 42, "Active reads the entity's own switch");
		entity.Active = false;
		Check(!entity.Active && !entity.ActiveInHierarchy && s_ActiveId == 42, "Active writes through");
		entity.ScriptEnabled = false;
		Check(!entity.ScriptEnabled, "ScriptEnabled round trip");

		SpriteRendererComponent sprite = new SpriteRendererComponent { Entity = new Entity(7) };
		Check(sprite.Enabled && s_ActiveId == 7, "Component.Enabled reads for the component's entity");
		sprite.Enabled = false;
		Check(!sprite.Enabled && s_ActiveId == 7, "Component.Enabled writes through");
	}
}
