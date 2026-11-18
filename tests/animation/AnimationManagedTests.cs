using System;
using Coral.Managed.Interop;
using Lux;

// The C# animation API against stand-ins for the native calls: what each member sends, how it reads the answer,
// and the OnAnimationEvent bridge. The native side is covered by the golden and export checks.
internal static unsafe class AnimationManagedTests
{
	private static void Check(bool value, string what)
	{
		if (!value)
			throw new Exception("Managed animation contract failed: " + what);
	}

	private static float playing = 1, time = 0.5f, speed = 1, loop = 1;
	private static float GetScalar(ulong id, int field)
	{
		Check(id == 7, "scalar entity");
		return field switch { 0 => playing, 1 => time, 2 => speed, _ => loop };
	}
	private static Bool32 SetScalar(ulong id, int field, float value)
	{
		Check(id == 7, "scalar entity");
		if (field == 1 && value < 0) return false;
		switch (field) { case 0: playing = value; break; case 1: time = value; break; case 2: speed = value; break; default: loop = value; break; }
		return true;
	}

	private static ulong fadedClip; private static float fadeSeconds;
	private static Bool32 CrossFade(ulong id, ulong clip, float seconds) { fadedClip = clip; fadeSeconds = seconds; return clip != 0; }

	private static string? lastState; private static float lastFade;
	private static Bool32 PlayState(ulong id, NativeString state, float seconds) { string name = state; lastState = name; lastFade = seconds; return name == "Walk"; }
	private static NativeString GetState(ulong id) => "Walk";

	private static string? lastParameter; private static int lastKind; private static float lastValue;
	private static Bool32 SetParameter(ulong id, NativeString name, int kind, float value)
	{
		string parameter = name;
		lastParameter = parameter; lastKind = kind; lastValue = value;
		return parameter == "Speed" && kind == 0 || parameter == "Grounded" && kind == 1 || parameter == "Jump" && kind == 2;
	}
	private static float GetParameter(ulong id, NativeString name) { string parameter = name; return parameter == "Speed" ? 2.5f : 0; }

	private static Bool32 GetBone(ulong id, NativeString bone, Vector3* position, Vector3* rotation)
	{
		string name = bone;
		if (name != "Hand.R") return false;
		*position = new Vector3(1, 2, 3);
		*rotation = new Vector3(0.1f, 0.2f, 0.3f);
		return true;
	}

	private static ulong attachedChild; private static string? attachedBone; private static ulong detached;
	private static Bool32 Attach(ulong id, ulong child, NativeString bone) { string name = bone; attachedChild = child; attachedBone = name; return name == "Hand.R"; }
	private static Bool32 Detach(ulong id, ulong child) { detached = child; return child == attachedChild; }

	private class Probe : Entity
	{
		public string? ReceivedName, ReceivedArgument;
		public Probe() : base(1) { }
		protected override void OnAnimationEvent(string name, string argument) { ReceivedName = name; ReceivedArgument = argument; }
		public void Fire(string name, string argument) => OnAnimationEventInternal(name, argument);
	}

	private static int Main()
	{
		InternalCalls.Animation_GetScalar = &GetScalar;
		InternalCalls.Animation_SetScalar = &SetScalar;
		InternalCalls.Animation_CrossFade = &CrossFade;
		InternalCalls.Animation_PlayState = &PlayState;
		InternalCalls.Animation_GetState = &GetState;
		InternalCalls.Animation_SetParameter = &SetParameter;
		InternalCalls.Animation_GetParameter = &GetParameter;
		InternalCalls.Animation_GetBone = &GetBone;
		InternalCalls.Animation_Attach = &Attach;
		InternalCalls.Animation_Detach = &Detach;

		var skeleton = new SkeletonComponent { Entity = new Entity(7) };

		// Playback properties read and write the right fields.
		Check(skeleton.Playing && skeleton.Time == 0.5f && skeleton.Speed == 1 && skeleton.Loop, "initial values");
		skeleton.Playing = false; skeleton.Speed = -2; skeleton.Loop = false; skeleton.Time = 1.25f;
		Check(playing == 0 && speed == -2 && loop == 0 && time == 1.25f, "setters");
		Check(!skeleton.Playing && skeleton.Speed == -2 && !skeleton.Loop && skeleton.Time == 1.25f, "getters");
		bool rejected = false;
		try { skeleton.Time = -1; } catch (ArgumentOutOfRangeException) { rejected = true; }
		Check(rejected && time == 1.25f, "a negative time is refused");

		// Cross-fade and graph states.
		Check(skeleton.CrossFade(99, 0.3f) && fadedClip == 99 && fadeSeconds == 0.3f, "cross-fade");
		Check(!skeleton.CrossFade(0, 0.3f), "cross-fade to nothing is refused");
		Check(skeleton.PlayState("Walk", 0.4f) && lastState == "Walk" && lastFade == 0.4f, "play state");
		Check(!skeleton.PlayState("Nowhere"), "an unknown state is refused");
		Check(skeleton.CurrentState == "Walk", "current state");

		// Parameters map to the three kinds.
		Check(skeleton.SetFloat("Speed", 1.5f) && lastKind == 0 && lastValue == 1.5f, "float parameter");
		Check(skeleton.SetBool("Grounded", true) && lastKind == 1 && lastValue == 1, "bool parameter");
		Check(skeleton.SetBool("Grounded", false) && lastValue == 0, "bool false");
		Check(skeleton.SetTrigger("Jump") && lastKind == 2, "trigger parameter");
		Check(!skeleton.SetFloat("Grounded", 1) && !skeleton.SetTrigger("Speed"), "wrong type or name is refused");
		Check(skeleton.GetFloat("Speed") == 2.5f && skeleton.GetFloat("Nope") == 0, "parameter read");

		// Bone reads and attachments.
		Check(skeleton.GetBoneTransform("Hand.R", out Vector3 position, out Vector3 rotation) && position.X == 1 && position.Y == 2 && position.Z == 3 && rotation.Z == 0.3f, "bone transform");
		Check(!skeleton.GetBoneTransform("Nope", out position, out rotation) && position.X == 0 && rotation.Y == 0, "an unknown bone reads as false and zeroed");
		var weapon = new Entity(55);
		Check(skeleton.AttachToBone(weapon, "Hand.R") && attachedChild == 55 && attachedBone == "Hand.R", "attach");
		Check(!skeleton.AttachToBone(weapon, "Nope"), "attach to an unknown bone is refused");
		Check(skeleton.Detach(weapon) && detached == 55, "detach");
		Check(!skeleton.Detach(new Entity(56)), "detaching an unattached entity reports false");

		// The engine calls the bridge; the script override receives the event.
		var probe = new Probe();
		probe.Fire("Footstep", "left");
		Check(probe.ReceivedName == "Footstep" && probe.ReceivedArgument == "left", "animation event bridge");

		Console.WriteLine("animation managed tests: passed");
		return 0;
	}
}
