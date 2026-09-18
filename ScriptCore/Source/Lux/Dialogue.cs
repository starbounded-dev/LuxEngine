using System;
using System.Collections.Generic;
using Coral.Managed.Interop;

namespace Lux
{
	public enum DialogueQueueMode { Interrupt, Queue, DropIfBusy }

	public readonly struct DialogueHandle
	{
		public ulong ID { get; }
		internal DialogueHandle(ulong id) { ID = id; }
		public bool IsActive => Dialogue.IsActive(this);
		public void Stop(bool allowFadeOut = true) => Dialogue.Stop(this, allowFadeOut);
	}

	public sealed class Subtitle
	{
		public DialogueHandle Handle { get; }
		public string Key { get; }
		public string Language { get; }
		public string Text { get; }
		public string SpeakerName { get; }
		public ulong SpeakerEntityID { get; }
		public Vector3 SpeakerPosition { get; }
		/// <summary>Source sound length in seconds; zero if FMOD has not supplied a length.
		/// Hide on SubtitleHidden, since authored fades and interruptions can change playback time.</summary>
		public float Duration { get; }
		public bool IsOffScreen { get; }
		public bool IsCaption { get; }
		public bool IsDescription { get; }
		internal Subtitle(ulong handle, string text, string name, ulong speaker, Vector3 position,
			float duration, bool offscreen, string key, string language, bool caption = false, bool description = false)
		{
			Handle = new DialogueHandle(handle); Text = text; SpeakerName = name;
			SpeakerEntityID = speaker; SpeakerPosition = position; Duration = duration;
			IsOffScreen = offscreen; Key = key; Language = language; IsCaption = caption; IsDescription = description;
		}
	}

	/// <summary>Scene-owned dialogue. Configure a .ldialogue table in Project Settings > Audio.
	/// Call only from the main thread. Rejected/deduplicated requests return a handle with ID zero.</summary>
	public static unsafe class Dialogue
	{
		private static Action<Subtitle>? s_Shown, s_Hidden;
		private static readonly Dictionary<ulong, Subtitle> s_Visible = new();
		public static event Action<Subtitle> SubtitleShown
		{
			add { Audio.RequireMainThread(); s_Shown += value; }
			remove { Audio.RequireMainThread(); s_Shown -= value; }
		}
		public static event Action<Subtitle> SubtitleHidden
		{
			add { Audio.RequireMainThread(); s_Hidden += value; }
			remove { Audio.RequireMainThread(); s_Hidden -= value; }
		}
		public static int QueueLength { get { Audio.RequireMainThread(); return InternalCalls.Dialogue_GetQueueLength(); } }
		public static DialogueHandle Speak(string key, Entity? speaker = null)
		{
			using NativeString native = Audio.String(key);
			return new DialogueHandle(InternalCalls.Dialogue_Speak(native, speaker?.ID ?? 0, false));
		}
		/// <summary>Queue an authored description line when audio descriptions are enabled.
		/// Route its FMOD event to the mapped Dialogue bus so other categories can be ducked.</summary>
		public static DialogueHandle Describe(string key)
		{
			using NativeString native = Audio.String(key);
			return new DialogueHandle(InternalCalls.Dialogue_Describe(native));
		}
		public static DialogueHandle Bark(string key, Entity speaker)
		{
			ArgumentNullException.ThrowIfNull(speaker);
			using NativeString native = Audio.String(key);
			return new DialogueHandle(InternalCalls.Dialogue_Speak(native, speaker.ID, true));
		}
		public static void Stop(DialogueHandle handle, bool allowFadeOut = true)
		{
			Audio.RequireMainThread();
			InternalCalls.Dialogue_Stop(handle.ID, allowFadeOut);
		}
		public static void StopAll()
		{
			Audio.RequireMainThread();
			InternalCalls.Dialogue_StopAll();
		}
		public static bool IsSpeaking(Entity speaker)
		{
			Audio.RequireMainThread();
			ArgumentNullException.ThrowIfNull(speaker);
			return InternalCalls.Dialogue_Query(speaker.ID, true);
		}
		public static bool IsActive(DialogueHandle handle)
		{
			Audio.RequireMainThread();
			return InternalCalls.Dialogue_Query(handle.ID, false);
		}
		public static void SetLanguage(string language)
		{
			using NativeString native = Audio.String(language);
			if (!InternalCalls.Dialogue_SetLanguage(native))
				throw new InvalidOperationException("Dialogue language could not be set; check the Audio log.");
		}
		public static void SetQueueMode(DialogueQueueMode mode)
		{
			Audio.RequireMainThread();
			if (!Enum.IsDefined(mode))
				throw new ArgumentOutOfRangeException(nameof(mode));
			if (!InternalCalls.Dialogue_SetQueueMode((int)mode))
				throw new InvalidOperationException("Dialogue requires a running scene.");
		}
		internal static void Dispatch(ulong handle, int shown, string text, string name, ulong speaker,
			float x, float y, float z, float duration, int offscreen, string key, string language, int caption = 0, int description = 0)
		{
			var subtitle = new Subtitle(handle, text, name, speaker, new Vector3(x, y, z), duration, offscreen != 0, key, language, caption != 0, description != 0);
			if (shown != 0)
				s_Visible[handle] = subtitle;
			else if (!s_Visible.Remove(handle))
				return;
			Notify(shown != 0 ? s_Shown : s_Hidden, subtitle);
		}
		private static void Notify(Action<Subtitle>? handlers, Subtitle subtitle)
		{
			if (handlers == null)
				return;
			foreach (Action<Subtitle> callback in handlers.GetInvocationList())
			{
				try { callback(subtitle); }
				catch (Exception exception)
				{
					using NativeString message = $"Subtitle callback failed: {exception}";
					InternalCalls.NativeLog(message, 3);
				}
			}
		}
		internal static void Reset()
		{
			var visible = new List<Subtitle>(s_Visible.Values);
			s_Visible.Clear();
			var hidden = s_Hidden;
			s_Shown = null;
			s_Hidden = null;
			foreach (var subtitle in visible)
				Notify(hidden, subtitle);
		}
	}
}
