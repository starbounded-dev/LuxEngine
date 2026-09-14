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
	public static void Main()
	{
		InternalCalls.Audio_IsMainThread = &MainThread;
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
