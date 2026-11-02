// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Collections.Generic;
using Coral.Managed.Interop;

namespace Lux
{
	/// <summary>What <see cref="Dialogue.Speak"/> does while another line is playing.</summary>
	public enum DialogueQueueMode
	{
		/// <summary>The new line cuts the playing one off.</summary>
		Interrupt,
		/// <summary>The new line waits its turn.</summary>
		Queue,
		/// <summary>The new line is dropped.</summary>
		DropIfBusy
	}

	/// <summary>A line of dialogue that was asked to play.</summary>
	public readonly struct DialogueHandle
	{
		/// <summary>The line's ID; 0 when the request was rejected or deduplicated.</summary>
		public ulong ID { get; }
		internal DialogueHandle(ulong id) { ID = id; }
		/// <summary>Whether the line is queued or playing.</summary>
		public bool IsActive => Dialogue.IsActive(this);
		/// <summary>Stops the line.</summary>
		/// <param name="allowFadeOut">True lets the event's authored fade-out play; false cuts it.</param>
		public void Stop(bool allowFadeOut = true) => Dialogue.Stop(this, allowFadeOut);
	}

	/// <summary>A subtitle, caption or description line, for drawing your own subtitle UI.</summary>
	public sealed class Subtitle
	{
		/// <summary>The line it belongs to.</summary>
		public DialogueHandle Handle { get; }
		/// <summary>The line's key in the dialogue table.</summary>
		public string Key { get; }
		/// <summary>The language of <see cref="Text"/>.</summary>
		public string Language { get; }
		/// <summary>The text to show.</summary>
		public string Text { get; }
		/// <summary>The speaker's name in this language, or empty.</summary>
		public string SpeakerName { get; }
		/// <summary>The speaking entity's ID, or 0.</summary>
		public ulong SpeakerEntityID { get; }
		/// <summary>Where the speaker was when the line started.</summary>
		public Vector3 SpeakerPosition { get; }
		/// <summary>Source sound length in seconds; zero if FMOD has not supplied a length.
		/// Hide on SubtitleHidden, since authored fades and interruptions can change playback time.</summary>
		public float Duration { get; }
		/// <summary>Whether the speaker is off screen (to show a direction arrow, say).</summary>
		public bool IsOffScreen { get; }
		/// <summary>Whether this is a closed caption for a sound rather than speech.</summary>
		public bool IsCaption { get; }
		/// <summary>Whether this is an audio-description line.</summary>
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
		/// <summary>Raised when a subtitle should appear.</summary>
		public static event Action<Subtitle> SubtitleShown
		{
			add { Audio.RequireMainThread(); s_Shown += value; }
			remove { Audio.RequireMainThread(); s_Shown -= value; }
		}
		/// <summary>Raised when a subtitle should disappear.</summary>
		public static event Action<Subtitle> SubtitleHidden
		{
			add { Audio.RequireMainThread(); s_Hidden += value; }
			remove { Audio.RequireMainThread(); s_Hidden -= value; }
		}
		/// <summary>How many lines wait in the queue.</summary>
		public static int QueueLength { get { Audio.RequireMainThread(); return InternalCalls.Dialogue_GetQueueLength(); } }
		/// <summary>Plays a line from the dialogue table, following the queue mode.</summary>
		/// <param name="key">The line's key.</param>
		/// <param name="speaker">The entity speaking, for position and subtitles, or null.</param>
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
		/// <summary>
		/// Plays a short ambient line from an entity, outside the queue. Repeats of the same line nearby are skipped
		/// for a cooldown set in the dialogue table.
		/// </summary>
		/// <param name="key">The line's key.</param>
		/// <param name="speaker">The entity speaking.</param>
		public static DialogueHandle Bark(string key, Entity speaker)
		{
			ArgumentNullException.ThrowIfNull(speaker);
			using NativeString native = Audio.String(key);
			return new DialogueHandle(InternalCalls.Dialogue_Speak(native, speaker.ID, true));
		}
		/// <summary>Stops a line.</summary>
		public static void Stop(DialogueHandle handle, bool allowFadeOut = true)
		{
			Audio.RequireMainThread();
			InternalCalls.Dialogue_Stop(handle.ID, allowFadeOut);
		}
		/// <summary>Stops every line and clears the queue.</summary>
		public static void StopAll()
		{
			Audio.RequireMainThread();
			InternalCalls.Dialogue_StopAll();
		}
		/// <summary>Whether an entity is speaking a line.</summary>
		public static bool IsSpeaking(Entity speaker)
		{
			Audio.RequireMainThread();
			ArgumentNullException.ThrowIfNull(speaker);
			return InternalCalls.Dialogue_Query(speaker.ID, true);
		}
		/// <summary>Whether a line is queued or playing.</summary>
		public static bool IsActive(DialogueHandle handle)
		{
			Audio.RequireMainThread();
			return InternalCalls.Dialogue_Query(handle.ID, false);
		}
		/// <summary>Switches the dialogue language (a language code in the dialogue table, such as <c>en</c>).</summary>
		public static void SetLanguage(string language)
		{
			using NativeString native = Audio.String(language);
			if (!InternalCalls.Dialogue_SetLanguage(native))
				throw new InvalidOperationException("Dialogue language could not be set; check the Audio log.");
		}
		/// <summary>Sets what <see cref="Speak"/> does while another line is playing.</summary>
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
