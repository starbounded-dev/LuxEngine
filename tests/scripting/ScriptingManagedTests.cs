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
		RunFind();
		RunPhysics();
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

	// ---- S4: tags and finders ---------------------------------------------------------------------------------

	private static string s_UserTag = "";
	private static string s_LastFindKind = "";
	private static string s_LastFindKey = "";
	private static bool s_LastIncludeInactive;
	private static NativeString UserTagGet(ulong id) => s_UserTag;
	private static void UserTagSet(ulong id, NativeString tag) { string text = tag; s_UserTag = text; }
	private static NativeArray<ulong> FindByName(NativeString name, Bool32 includeInactive)
	{
		string text = name;
		s_LastFindKind = "name"; s_LastFindKey = text; s_LastIncludeInactive = includeInactive;
		return new NativeArray<ulong>(text == "Twin" ? new ulong[] { 5, 9 } : new ulong[0]);
	}
	private static NativeArray<ulong> FindByTag(NativeString tag, Bool32 includeInactive)
	{
		string text = tag;
		s_LastFindKind = "tag"; s_LastFindKey = text; s_LastIncludeInactive = includeInactive;
		return new NativeArray<ulong>(text == "coin" ? new ulong[] { 11, 12, 13 } : new ulong[0]);
	}
	private static NativeArray<ulong> FindByComponent(ReflectionType type, Bool32 includeInactive)
	{
		s_LastFindKind = "component"; s_LastIncludeInactive = includeInactive;
		return new NativeArray<ulong>(new ulong[] { 21 });
	}

	private static void RunFind()
	{
		InternalCalls.Entity_GetUserTag = &UserTagGet;
		InternalCalls.Entity_SetUserTag = &UserTagSet;
		InternalCalls.Scene_FindEntitiesByName = &FindByName;
		InternalCalls.Scene_FindEntitiesByTag = &FindByTag;
		InternalCalls.Scene_FindEntitiesWithComponent = &FindByComponent;

		Entity entity = new Entity(1);
		entity.Tag = "coin";
		Check(s_UserTag == "coin" && entity.Tag == "coin" && entity.CompareTag("coin") && !entity.CompareTag("gem") && !entity.CompareTag(""), "Tag and CompareTag");
		entity.Tag = null;
		Check(s_UserTag == "" && !entity.CompareTag(""), "A null tag clears it");

		Entity[] twins = Entity.FindAll("Twin");
		Check(twins.Length == 2 && twins[0].ID == 5 && twins[1].ID == 9 && s_LastFindKind == "name" && !s_LastIncludeInactive, "FindAll keeps the creation order and skips inactive by default");
		Check(Entity.Find("Twin").ID == 5 && Entity.Find("Missing") == null, "Find returns the first or null");
		Entity.Find("Twin", includeInactive: true);
		Check(s_LastIncludeInactive, "includeInactive reaches the native call");

		Check(Entity.FindAllWithTag("coin").Length == 3 && Entity.FindWithTag("coin").ID == 11 && Entity.FindWithTag("none") == null, "tag finders");
		Check(Entity.FindWithTag(null) == null && s_LastFindKey == "", "A null tag finds nothing");
		Check(Entity.FindAllWithComponent<SpriteRendererComponent>().Length == 1 && Entity.FindWithComponent<SpriteRendererComponent>(true).ID == 21 && s_LastIncludeInactive && s_LastFindKind == "component", "component finders");
	}

	// ---- P3: physics queries ------------------------------------------------------------------------------------

	private struct SeenQuery
	{
		public string Call;
		public Vector3 Origin, Direction, Dimensions;
		public Vector4 Rotation;
		public float MaxDistance;
		public uint Mask;
		public ulong Ignore;
		public bool Triggers;
		public int Shape, Capacity;
	}
	private static SeenQuery s_Seen;
	private static int s_AllCount;      // how many hits the RaycastAll / overlap stand-ins claim to have
	private static int s_AllCalls;

	private static RaycastHitData MakeHit(ulong id) => new RaycastHitData { Entity = id, Point = new Vector3(1.0f, 2.0f, 3.0f), Normal = new Vector3(0.0f, 1.0f, 0.0f), Distance = 4.5f };

	private static Bool32 PhysicsRaycast(Vector3* origin, Vector3* direction, float maxDistance, uint mask, ulong ignore, Bool32 triggers, RaycastHitData* hit)
	{
		s_Seen = new SeenQuery { Call = "ray", Origin = *origin, Direction = *direction, MaxDistance = maxDistance, Mask = mask, Ignore = ignore, Triggers = triggers };
		if (maxDistance < 0.0f)
			return false;
		*hit = MakeHit(77);
		return true;
	}

	private static int PhysicsRaycastAll(Vector3* origin, Vector3* direction, float maxDistance, uint mask, ulong ignore, Bool32 triggers, RaycastHitData* buffer, int capacity)
	{
		s_Seen = new SeenQuery { Call = "rayAll", Origin = *origin, Direction = *direction, MaxDistance = maxDistance, Mask = mask, Ignore = ignore, Triggers = triggers, Capacity = capacity };
		s_AllCalls++;
		for (int i = 0; i < s_AllCount && i < capacity; i++)
			buffer[i] = MakeHit((ulong)(100 + i));
		return s_AllCount;
	}

	private static Bool32 PhysicsShapeCast(int shape, Vector3* origin, Vector4* rotation, Vector3* dimensions, Vector3* direction, float maxDistance, uint mask, ulong ignore, Bool32 triggers, RaycastHitData* hit)
	{
		s_Seen = new SeenQuery { Call = "shapeCast", Shape = shape, Origin = *origin, Rotation = *rotation, Dimensions = *dimensions, Direction = *direction, MaxDistance = maxDistance, Mask = mask, Ignore = ignore, Triggers = triggers };
		*hit = MakeHit(88);
		return true;
	}

	private static int PhysicsOverlap(int shape, Vector3* origin, Vector4* rotation, Vector3* dimensions, uint mask, ulong ignore, Bool32 triggers, ulong* buffer, int capacity)
	{
		s_Seen = new SeenQuery { Call = "overlap", Shape = shape, Origin = *origin, Rotation = *rotation, Dimensions = *dimensions, Mask = mask, Ignore = ignore, Triggers = triggers, Capacity = capacity };
		s_AllCalls++;
		for (int i = 0; i < s_AllCount && i < capacity; i++)
			buffer[i] = (ulong)(200 + i);
		return s_AllCount;
	}

	private static int PhysicsLayerIndex(NativeString name)
	{
		string text = name;
		return text == "Default" ? 0 : text == "Enemy" ? 3 : -1;
	}

	private static void RunPhysics()
	{
		InternalCalls.Physics_Raycast = &PhysicsRaycast;
		InternalCalls.Physics_RaycastAll = &PhysicsRaycastAll;
		InternalCalls.Physics_ShapeCast = &PhysicsShapeCast;
		InternalCalls.Physics_OverlapShape = &PhysicsOverlap;
		InternalCalls.Physics_GetLayerIndex = &PhysicsLayerIndex;

		Check(System.Runtime.InteropServices.Marshal.SizeOf<RaycastHitData>() == 40, "RaycastHitData matches the 40-byte native hit");

		Check(Physics.GetLayerMask("Default", "Enemy") == 9u && Physics.GetLayerMask() == 0u, "GetLayerMask sets one bit per layer index");
		bool rejected = false;
		try { Physics.GetLayerMask("Ghost"); } catch (ArgumentException) { rejected = true; }
		Check(rejected, "GetLayerMask rejects an unknown layer");

		Entity self = new Entity(5);
		bool hitSomething = Physics.Raycast(new Vector3(1.0f, 2.0f, 3.0f), new Vector3(0.0f, -1.0f, 0.0f), out RaycastHit hit, 25.0f, 9u, self, true);
		Check(hitSomething && hit.Entity.ID == 77 && hit.Point == new Vector3(1.0f, 2.0f, 3.0f) && hit.Normal == Vector3.Up && hit.Distance == 4.5f, "Raycast maps the native hit");
		Check(s_Seen.Call == "ray" && s_Seen.Origin == new Vector3(1.0f, 2.0f, 3.0f) && s_Seen.Direction == Vector3.Down && s_Seen.MaxDistance == 25.0f && s_Seen.Mask == 9u && s_Seen.Ignore == 5 && s_Seen.Triggers, "Raycast sends every argument");
		Physics.Raycast(Vector3.Zero, Vector3.Forward);
		Check(s_Seen.MaxDistance == 1000.0f && s_Seen.Mask == Physics.AllLayers && s_Seen.Ignore == 0 && !s_Seen.Triggers, "Raycast defaults: 1000 m, every layer, nothing ignored, no triggers");
		Check(!Physics.Raycast(Vector3.Zero, Vector3.Forward, out RaycastHit miss, -1.0f) && miss.Entity == null && miss.Distance == 0.0f, "A miss leaves a default hit");

		s_AllCount = 3; s_AllCalls = 0;
		RaycastHit[] few = Physics.RaycastAll(Vector3.Zero, Vector3.Forward, 50.0f, 1u);
		Check(few.Length == 3 && few[0].Entity.ID == 100 && few[2].Entity.ID == 102 && s_AllCalls == 1 && s_Seen.Mask == 1u, "RaycastAll returns the hits in order with one call");
		s_AllCount = 40; s_AllCalls = 0;
		RaycastHit[] many = Physics.RaycastAll(Vector3.Zero, Vector3.Forward);
		Check(many.Length == 40 && many[39].Entity.ID == 139 && s_AllCalls == 2 && s_Seen.Capacity == 40, "RaycastAll asks again with room for a larger answer");
		s_AllCount = 0;
		Check(Physics.RaycastAll(Vector3.Zero, Vector3.Forward).Length == 0, "RaycastAll with no hits is empty");

		Quaternion turn = Quaternion.Euler(new Vector3(0.0f, 1.0f, 0.0f));
		Check(Physics.SphereCast(new Vector3(1.0f), 0.5f, Vector3.Forward, out RaycastHit sphere, 9.0f) && sphere.Entity.ID == 88, "SphereCast maps the hit");
		Check(s_Seen.Shape == 0 && s_Seen.Dimensions.X == 0.5f && s_Seen.MaxDistance == 9.0f && s_Seen.Origin == new Vector3(1.0f), "SphereCast sends the radius as the first dimension");
		Physics.BoxCast(Vector3.Zero, new Vector3(1.0f, 2.0f, 3.0f), turn, Vector3.Right, out _);
		Check(s_Seen.Shape == 1 && s_Seen.Dimensions == new Vector3(1.0f, 2.0f, 3.0f) && s_Seen.Rotation == new Vector4(turn.X, turn.Y, turn.Z, turn.W), "BoxCast sends half extents and the rotation as x, y, z, w");
		Physics.CapsuleCast(Vector3.Zero, 0.3f, 0.9f, Quaternion.Identity, Vector3.Down, out _, 5.0f, 2u, self, true);
		Check(s_Seen.Shape == 2 && s_Seen.Dimensions == new Vector3(0.3f, 0.9f, 0.0f) && s_Seen.Mask == 2u && s_Seen.Ignore == 5 && s_Seen.Triggers && s_Seen.Rotation == new Vector4(0.0f, 0.0f, 0.0f, 1.0f), "CapsuleCast sends radius and half height");

		s_AllCount = 2; s_AllCalls = 0;
		Entity[] near = Physics.OverlapSphere(Vector3.One, 2.0f, 4u, self);
		Check(near.Length == 2 && near[0].ID == 200 && near[1].ID == 201 && s_Seen.Shape == 0 && s_Seen.Dimensions.X == 2.0f && s_Seen.Mask == 4u && s_Seen.Ignore == 5, "OverlapSphere");
		Physics.OverlapBox(Vector3.Zero, Vector3.One, turn);
		Check(s_Seen.Shape == 1 && s_Seen.Rotation.Y == turn.Y, "OverlapBox");
		Physics.OverlapCapsule(Vector3.Zero, 0.4f, 0.8f, Quaternion.Identity);
		Check(s_Seen.Shape == 2 && s_Seen.Dimensions == new Vector3(0.4f, 0.8f, 0.0f), "OverlapCapsule");
		s_AllCount = 70; s_AllCalls = 0;
		Check(Physics.OverlapSphere(Vector3.Zero, 1.0f).Length == 70 && s_AllCalls == 2, "Overlap asks again for a larger answer");
		s_AllCount = 1;
		Check(Physics.CheckSphere(Vector3.Zero, 1.0f), "CheckSphere is true with an overlap");
		s_AllCount = 0;
		Check(!Physics.CheckSphere(Vector3.Zero, 1.0f), "CheckSphere is false without one");
	}
}
