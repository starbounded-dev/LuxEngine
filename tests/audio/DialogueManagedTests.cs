using System;
using Coral.Managed.Interop;
using Lux;

internal static unsafe class DialogueManagedTests
{
	private static Bool32 MainThread() => true;
	private static void Check(bool value)
	{
		if (!value)
			throw new Exception("Managed dialogue contract failed");
	}
	private static float portalOpen;
	private static float PortalGet(ulong id, int field) { Check(id == 42 && field == 1); return portalOpen; }
	private static Bool32 PortalSet(ulong id, int field, float value)
	{
		Check(id == 42 && field == 1);
		if (value < 0 || value > 1) return false;
		portalOpen = value;
		return true;
	}
	private static Bool32 NotMainThread() => false;
	private static void CheckPortal()
	{
		InternalCalls.Audio_PortalGetScalar = &PortalGet;
		InternalCalls.Audio_PortalSetScalar = &PortalSet;
		var portal = new AudioPortalComponent { Entity = new Entity(42) };
		portal.Open = 0.75f;
		Check(portal.Open == 0.75f);
		foreach (float invalid in new[] { -1.0f, 2.0f, float.NaN, float.PositiveInfinity })
		{
			bool rejected = false;
			try { portal.Open = invalid; } catch (ArgumentException) { rejected = true; }
			Check(rejected && portal.Open == 0.75f);
		}
		InternalCalls.Audio_IsMainThread = &NotMainThread;
		bool threadRejected = false;
		try { portal.Open = 0; } catch (InvalidOperationException) { threadRejected = true; }
		InternalCalls.Audio_IsMainThread = &MainThread;
		Check(threadRejected && portal.Open == 0.75f);
		Check((int)AcousticGeometryMode.Static == 0 && (int)AcousticGeometryMode.Dynamic == 1 && (int)AcousticGeometryMode.Disabled == 2);
		Console.WriteLine("PASS: managed portal dispatch, invalid values, acoustic mode ABI and main-thread guard");
	}
	private static int sourcePriority = 128;
	private static bool sourceCulling;
	private static int PriorityGet(ulong id) { Check(id == 42); return sourcePriority; }
	private static Bool32 PrioritySet(ulong id, int value)
	{
		Check(id == 42);
		if (value < 0 || value > 256) return false;
		sourcePriority = value;
		return true;
	}
	private static Bool32 CullingGet(ulong id) { Check(id == 42); return sourceCulling; }
	private static void CullingSet(ulong id, Bool32 value) { Check(id == 42); sourceCulling = value; }
	private static void CheckSourceBudgets()
	{
		InternalCalls.Audio_SourceGetPriority = &PriorityGet;
		InternalCalls.Audio_SourceSetPriority = &PrioritySet;
		InternalCalls.Audio_SourceGetCulling = &CullingGet;
		InternalCalls.Audio_SourceSetCulling = &CullingSet;
		InternalCalls.Audio_SourceIsCulled = &CullingGet;
		var source = new AudioSourceComponent { Entity = new Entity(42) };
		Check(source.Priority == 128 && !source.DistanceCulling && !source.IsCulled);
		source.Priority = 0;
		source.DistanceCulling = true;
		Check(source.Priority == 0 && source.DistanceCulling && source.IsCulled);
		source.Priority = 256;
		foreach (int invalid in new[] { -1, 257 })
		{
			bool rejected = false;
			try { source.Priority = invalid; } catch (ArgumentOutOfRangeException) { rejected = true; }
			Check(rejected && source.Priority == 256);
		}
		InternalCalls.Audio_IsMainThread = &NotMainThread;
		foreach (Action action in new Action[] { () => source.Priority = 12, () => source.DistanceCulling = false, () => { _ = source.IsCulled; } })
		{
			bool rejected = false;
			try { action(); } catch (InvalidOperationException) { rejected = true; }
			Check(rejected);
		}
		InternalCalls.Audio_IsMainThread = &MainThread;
		Check(source.Priority == 256 && source.DistanceCulling);
		Console.WriteLine("PASS: managed source priority/culling ABI, bounds and thread guards");
	}
	public static void Main()
	{
		InternalCalls.Audio_IsMainThread = &MainThread;
		CheckPortal();
		CheckSourceBudgets();
		int shown = 0, hidden = 0;
		Subtitle? retained = null;
		Dialogue.SubtitleShown += subtitle =>
		{
			++shown;
			retained = subtitle;
			Check(subtitle.Handle.ID == 42 && subtitle.Text == "Bonjour" && subtitle.Language == "fr");
			Check(subtitle.SpeakerEntityID == 7 && subtitle.SpeakerName == "Garde" && subtitle.Key == "guard.hello");
			Check(subtitle.IsOffScreen && subtitle.SpeakerPosition.X == 1 && subtitle.Duration == 2.5f);
		};
		Dialogue.SubtitleHidden += subtitle => { ++hidden; Check(subtitle.Handle.ID == 42); };
		Dialogue.Dispatch(42, 1, "Bonjour", "Garde", 7, 1, 2, 3, 2.5f, 1, "guard.hello", "fr");
		Check(shown == 1 && hidden == 0 && retained != null);
		Dialogue.Reset();
		Check(hidden == 1 && retained!.Text == "Bonjour");
		Dialogue.Dispatch(42, 0, "Bonjour", "Garde", 7, 1, 2, 3, 2.5f, 1, "guard.hello", "fr");
		Check(hidden == 1); // A late native hide after assembly reset must not duplicate delivery.
		Dialogue.Dispatch(42, 1, "Bonjour", "Garde", 7, 1, 2, 3, 2.5f, 1, "guard.hello", "fr");
		Check(shown == 1); // Scene subscriptions were cleared.
		Dialogue.Reset();
		int captionCount = 0;
		Dialogue.SubtitleShown += subtitle => { Check(subtitle.IsCaption && !subtitle.IsDescription); ++captionCount; };
		Dialogue.Dispatch(99, 1, "[door]", "", 0, 0, 0, 0, 1, 0, "door", "en", 1, 0);
		Check(captionCount == 1);
		Dialogue.Reset();
		Check(System.Runtime.InteropServices.Marshal.SizeOf<SoundCueData>() == 48);
		int cues = 0, endedCues = 0;
		Accessibility.SoundEvent += cue =>
		{
			Check(cue.Handle == 7 && cue.Category == AudioCategory.SFX);
			Check(cue.Direction.X == -1 && cue.Position.Z == 3 && cue.Intensity == 0.5f);
			if (cue.Active)
				++cues;
			else
				++endedCues;
		};
		Accessibility.DispatchSound(7, 1, 2, 1, 2, 3, -1, 0, 0, 0.5f);
		Check(cues == 1);
		Accessibility.Reset();
		Check(endedCues == 1);
		Accessibility.DispatchSound(7, 1, 2, 1, 2, 3, -1, 0, 0, 0.5f);
		Check(cues == 1);
		Console.WriteLine("PASS: managed subtitle/caption payload, cue ABI/direction, subscriptions and reset");
	}
}
