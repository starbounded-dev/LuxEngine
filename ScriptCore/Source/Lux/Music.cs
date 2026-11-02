// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using Coral.Managed.Interop;

namespace Lux
{
	/// <summary>When <see cref="Music.QueueTransition"/> switches to the new music.</summary>
	public enum MusicSync
	{
		/// <summary>Right away.</summary>
		Immediate,
		/// <summary>On the next beat.</summary>
		NextBeat,
		/// <summary>On the next bar.</summary>
		NextBar,
		/// <summary>On the next timeline marker.</summary>
		NextMarker,
		/// <summary>On the next marker whose name starts with <c>Section:</c>.</summary>
		NextSection
	}

	/// <summary>Scene-owned interactive music. All calls and callbacks run on the main thread.
	/// Author State labels, continuous Intensity (0–1), and Layer_name parameters (0–1) in FMOD.</summary>
	public static unsafe class Music
	{
		private static Action<int, int>? s_Beat;
		private static Action<string>? s_Marker;
		/// <summary>Raised on every beat of the playing music, with the bar and the beat in it.</summary>
		public static event Action<int, int> Beat
		{
			add { Audio.RequireMainThread(); s_Beat += value; }
			remove { Audio.RequireMainThread(); s_Beat -= value; }
		}
		/// <summary>Raised when the playing music passes a timeline marker, with the marker's name.</summary>
		public static event Action<string> Marker
		{
			add { Audio.RequireMainThread(); s_Marker += value; }
			remove { Audio.RequireMainThread(); s_Marker -= value; }
		}
		/// <summary>Whether music is playing.</summary>
		public static bool IsPlaying { get { Audio.RequireMainThread(); return InternalCalls.Music_IsPlaying(); } }
		/// <summary>The beat within the current bar.</summary>
		public static int CurrentBeat { get { Audio.RequireMainThread(); return InternalCalls.Music_GetBeat(false); } }
		/// <summary>The current bar.</summary>
		public static int CurrentBar { get { Audio.RequireMainThread(); return InternalCalls.Music_GetBeat(true); } }
		/// <summary>The music's <c>Intensity</c> parameter, 0 to 1; kept across <see cref="Play"/> and transitions.</summary>
		public static float Intensity
		{
			get { Audio.RequireMainThread(); return InternalCalls.Music_GetIntensity(); }
			set
			{
				Audio.RequireMainThread();
				Audio.Finite(value);
				if (value < 0 || value > 1)
					throw new ArgumentOutOfRangeException(nameof(value));
				Check(InternalCalls.Music_SetIntensity(value));
			}
		}
		/// <summary>Starts a music event as the bed, replacing what was playing.</summary>
		/// <param name="eventRef">The event's GUID or path (<c>event:/...</c>).</param>
		/// <exception cref="System.InvalidOperationException">The event could not be played (check the Audio log).</exception>
		public static void Play(string eventRef)
		{
			using NativeString reference = Audio.String(eventRef);
			Check(InternalCalls.Music_Play(reference));
		}
		/// <summary>Stops the music.</summary>
		/// <param name="allowFadeOut">True lets the event's authored fade-out play; false cuts it.</param>
		public static void Stop(bool allowFadeOut = true)
		{
			Audio.RequireMainThread();
			InternalCalls.Music_Stop(allowFadeOut);
		}
		/// <summary>Sets the music's <c>State</c> labeled parameter (combat, explore...); kept across transitions.</summary>
		public static void SetState(string state)
		{
			using NativeString label = Audio.String(state);
			Check(InternalCalls.Music_SetState(label));
		}
		/// <summary>Turns a layer on or off: sets the music's <c>Layer_name</c> parameter to 1 or 0.</summary>
		public static void SetLayerEnabled(string layer, bool enabled)
		{
			using NativeString name = Audio.String(layer);
			Check(InternalCalls.Music_SetLayerEnabled(name, enabled));
		}
		/// <summary>Plays a short event over the music.</summary>
		/// <param name="eventRef">The event's GUID or path (<c>event:/...</c>).</param>
		public static void PlayStinger(string eventRef)
		{
			using NativeString reference = Audio.String(eventRef);
			Check(InternalCalls.Music_PlayStinger(reference));
		}
		/// <summary>Replaces the bed on the main-thread notification of a boundary. NextSection uses
		/// markers beginning with "Section:". Use Studio transitions for sample-accurate musical timing.</summary>
		public static void QueueTransition(string eventRef, MusicSync sync)
		{
			if (!Enum.IsDefined(sync))
				throw new ArgumentOutOfRangeException(nameof(sync));
			using NativeString reference = Audio.String(eventRef);
			Check(InternalCalls.Music_QueueTransition(reference, (int)sync));
		}
		private static void Check(bool success)
		{
			if (!success)
				throw new InvalidOperationException("Music operation failed. Check the Audio log and authored FMOD event/parameters.");
		}
		internal static void DispatchBeat(int bar, int beat)
		{
			if (s_Beat == null)
				return;
			foreach (Action<int, int> callback in s_Beat.GetInvocationList())
			{
				try { callback(bar, beat); }
				catch (Exception exception) { Report(exception); }
			}
		}
		internal static void DispatchMarker(string marker)
		{
			if (s_Marker == null)
				return;
			foreach (Action<string> callback in s_Marker.GetInvocationList())
			{
				try { callback(marker); }
				catch (Exception exception) { Report(exception); }
			}
		}
		private static void Report(Exception exception)
		{
			using NativeString message = $"Music callback failed: {exception}";
			InternalCalls.NativeLog(message, 3);
		}
		internal static void Reset() { s_Beat = null; s_Marker = null; }
	}
}
