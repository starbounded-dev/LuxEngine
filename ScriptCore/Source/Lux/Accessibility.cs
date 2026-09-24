// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Coral.Managed.Interop;

namespace Lux
{
	public enum AudioCategory { Master, Music, SFX, Dialogue, UI, Ambience }
	public enum AudioDynamicRange { Full, Reduced, Night }
	[StructLayout(LayoutKind.Sequential)]
	internal struct SoundCueData
	{
		internal ulong Handle;
		internal int Active, Category;
		internal Vector3 Position, Direction;
		internal float Intensity;
	}
	public readonly struct SoundCue
	{
		public ulong Handle { get; }
		public bool Active { get; }
		public AudioCategory Category { get; }
		public Vector3 Position { get; }
		/// <summary>Listener-relative direction: X right, Y up, Z forward. Zero for nonspatial sounds.</summary>
		public Vector3 Direction { get; }
		/// <summary>Authored importance scaled by instance volume and linear cue range, not a loudness meter.</summary>
		public float Intensity { get; }
		internal SoundCue(SoundCueData data)
		{
			Handle = data.Handle; Active = data.Active != 0; Category = (AudioCategory)data.Category;
			Position = data.Position; Direction = data.Direction; Intensity = data.Intensity;
		}
	}

	/// <summary>Player audio preferences. Call on the main thread in a running scene.
	/// Save explicitly to persist. Project mappings connect category sliders to authored FMOD buses.</summary>
	public static unsafe class Accessibility
	{
		private static Action<SoundCue>? s_Sound;
		private static readonly Dictionary<ulong, SoundCue> s_Active = new();
		public static event Action<SoundCue> SoundEvent
		{
			add { Audio.RequireMainThread(); s_Sound += value; }
			remove { Audio.RequireMainThread(); s_Sound -= value; }
		}
		private static float Get(int option) { Audio.RequireMainThread(); return InternalCalls.Accessibility_GetOption(option); }
		private static void Set(int option, float value)
		{
			Audio.RequireMainThread();
			Audio.Finite(value);
			if (!InternalCalls.Accessibility_SetOption(option, value))
				throw new InvalidOperationException("Accessibility preference was rejected. Check its range and the Audio log.");
		}
		public static bool Subtitles { get => Get(0) != 0; set => Set(0, value ? 1 : 0); }
		public static bool Captions { get => Get(1) != 0; set => Set(1, value ? 1 : 0); }
		public static bool VisualCues { get => Get(2) != 0; set => Set(2, value ? 1 : 0); }
		public static bool SpeakerNames { get => Get(3) != 0; set => Set(3, value ? 1 : 0); }
		public static bool DirectionIndicators { get => Get(4) != 0; set => Set(4, value ? 1 : 0); }
		public static bool Mono { get => Get(5) != 0; set => Set(5, value ? 1 : 0); }
		public static bool AudioDescriptions { get => Get(6) != 0; set => Set(6, value ? 1 : 0); }
		public static AudioDynamicRange DynamicRange { get => (AudioDynamicRange)Get(7); set => Set(7, (int)value); }
		public static float TextSize { get => Get(8); set => Set(8, value); }
		public static float BackgroundOpacity { get => Get(9); set => Set(9, value); }
		public static float DurationMultiplier { get => Get(10); set => Set(10, value); }
		public static int MaxLines { get => (int)Get(11); set => Set(11, value); }
		public static float DialogueBoost { get => Get(12); set => Set(12, value); }
		public static float GetVolume(AudioCategory category) { ValidateCategory(category); return Get(13 + (int)category); }
		public static void SetVolume(AudioCategory category, float volume) { ValidateCategory(category); Set(13 + (int)category, volume); }
		public static bool HasBus(AudioCategory category)
		{
			Audio.RequireMainThread(); ValidateCategory(category);
			return InternalCalls.Accessibility_HasBus((int)category);
		}
		private static void ValidateCategory(AudioCategory category)
		{
			if (!Enum.IsDefined(category))
				throw new ArgumentOutOfRangeException(nameof(category));
		}
		public static Vector4 GetSpeakerColor(string name)
		{
			using NativeString native = Audio.String(name);
			Vector4 color;
			InternalCalls.Accessibility_GetSpeakerColor(native, &color);
			return color;
		}
		public static SoundCue[] GetSoundCues()
		{
			Audio.RequireMainThread();
			var result = new SoundCue[InternalCalls.Accessibility_GetCueCount()];
			for (int i = 0; i < result.Length; ++i)
			{
				SoundCueData data;
				if (!InternalCalls.Accessibility_GetCue(i, &data))
					throw new InvalidOperationException("Sound cue snapshot changed during access.");
				result[i] = new SoundCue(data);
			}
			return result;
		}
		public static void Save()
		{
			Audio.RequireMainThread();
			if (!InternalCalls.Accessibility_Save())
				throw new InvalidOperationException("Could not save accessibility preferences; check the Audio log.");
		}
		internal static void DispatchSound(ulong handle, int active, int category, float x, float y, float z,
			float dx, float dy, float dz, float intensity)
		{
			var cue = new SoundCue(new SoundCueData { Handle = handle, Active = active, Category = category,
				Position = new Vector3(x, y, z), Direction = new Vector3(dx, dy, dz), Intensity = intensity });
			if (cue.Active)
				s_Active[handle] = cue;
			else if (!s_Active.Remove(handle))
				return;
			Notify(s_Sound, cue);
		}
		private static void Notify(Action<SoundCue>? handlers, SoundCue cue)
		{
			if (handlers == null)
				return;
			foreach (Action<SoundCue> callback in handlers.GetInvocationList())
			{
				try { callback(cue); }
				catch (Exception exception)
				{
					using NativeString message = $"Sound cue callback failed: {exception}";
					InternalCalls.NativeLog(message, 3);
				}
			}
		}
		internal static void Reset()
		{
			var active = new List<SoundCue>(s_Active.Values);
			s_Active.Clear();
			var handlers = s_Sound;
			s_Sound = null;
			foreach (var cue in active)
				Notify(handlers, new SoundCue(new SoundCueData { Handle = cue.Handle, Active = 0, Category = (int)cue.Category,
					Position = cue.Position, Direction = cue.Direction, Intensity = cue.Intensity }));
		}
	}
}
