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
		Console.WriteLine("PASS: managed subtitle payload, lifetime, reset and late hide deduplication");
	}
}
