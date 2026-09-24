// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using Coral.Managed.Interop;

namespace Lux
{
	public enum MusicSync { Immediate, NextBeat, NextBar, NextMarker, NextSection }

	/// <summary>Scene-owned interactive music. All calls and callbacks run on the main thread.
	/// Author State labels, continuous Intensity (0–1), and Layer_name parameters (0–1) in FMOD.</summary>
	public static unsafe class Music
	{
		private static Action<int, int>? s_Beat;
		private static Action<string>? s_Marker;
		public static event Action<int, int> Beat
		{
			add { Audio.RequireMainThread(); s_Beat += value; }
			remove { Audio.RequireMainThread(); s_Beat -= value; }
		}
		public static event Action<string> Marker
		{
			add { Audio.RequireMainThread(); s_Marker += value; }
			remove { Audio.RequireMainThread(); s_Marker -= value; }
		}
		public static bool IsPlaying { get { Audio.RequireMainThread(); return InternalCalls.Music_IsPlaying(); } }
		public static int CurrentBeat { get { Audio.RequireMainThread(); return InternalCalls.Music_GetBeat(false); } }
		public static int CurrentBar { get { Audio.RequireMainThread(); return InternalCalls.Music_GetBeat(true); } }
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
		public static void Play(string eventRef)
		{
			using NativeString reference = Audio.String(eventRef);
			Check(InternalCalls.Music_Play(reference));
		}
		public static void Stop(bool allowFadeOut = true)
		{
			Audio.RequireMainThread();
			InternalCalls.Music_Stop(allowFadeOut);
		}
		public static void SetState(string state)
		{
			using NativeString label = Audio.String(state);
			Check(InternalCalls.Music_SetState(label));
		}
		public static void SetLayerEnabled(string layer, bool enabled)
		{
			using NativeString name = Audio.String(layer);
			Check(InternalCalls.Music_SetLayerEnabled(name, enabled));
		}
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
