// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Collections.Generic;
using System.Globalization;
using Coral.Managed.Interop;

namespace Lux
{
	/// <summary>Gameplay audio. Call on the engine's main thread; handles expire when Play stops.</summary>
	public static unsafe class Audio
	{
		private static readonly Dictionary<ulong, WeakReference<EventInstance>> Instances = new();

		/// <summary>Queries the walkable 3D collider below a character and plays its surface footstep.
		/// Returns false when airborne, paused, or no valid event is assigned. Distance is measured from the entity origin.</summary>
		public static bool PlayFootstep(Entity character, float speed, float weight = 75.0f, float probeDistance = 1.2f)
		{
			RequireMainThread();
			ArgumentNullException.ThrowIfNull(character);
			Finite(speed);
			Finite(weight);
			Finite(probeDistance);
			if (speed < 0.0f || weight <= 0.0f || probeDistance <= 0.0f)
				throw new ArgumentOutOfRangeException(nameof(speed), "Speed must be non-negative; weight and probe distance must be positive.");
			return InternalCalls.Audio_PlayFootstep(character.ID, speed, weight, probeDistance);
		}

		internal static void RequireMainThread()
		{
			if (!InternalCalls.Audio_IsMainThread())
				throw new InvalidOperationException("Audio must be accessed on the engine main thread.");
		}

		internal static NativeString String(string value)
		{
			RequireMainThread();
			ArgumentException.ThrowIfNullOrWhiteSpace(value);
			return value;
		}

		internal static void Finite(float value)
		{
			if (!float.IsFinite(value))
				throw new ArgumentOutOfRangeException(nameof(value), "Audio values must be finite.");
		}

		/// <summary>Always true: the engine plays Sound assets. FMOD is gone.</summary>
		[Obsolete("FMOD is gone: the engine always plays Sound assets. Remove this check.", true)]
		public static bool PlaysSoundAssets => true;

		/// <summary>Loads a Sound Bank: its Sounds' clips decode in the background, and the call returns at once.
		/// Loads are counted; match each with <see cref="UnloadBank(SoundBank)"/>. Banks the scene lists load on their own.</summary>
		/// <returns>False when the bank is unknown.</returns>
		public static bool LoadBank(SoundBank bank) => LoadBank(BankReference(bank));

		/// <summary>Loads a Sound Bank by its path relative to Assets, with or without <c>.lbank</c>
		/// (<c>"Audio/Banks/Level1"</c>). FMOD <c>.bank</c> files are refused: make a Sound Bank instead.</summary>
		/// <returns>False when no Sound Bank has that path.</returns>
		public static bool LoadBank(string bankPath)
		{
			using NativeString reference = String(bankPath);
			return InternalCalls.Audio_LoadBank(reference);
		}

		/// <summary>Drops one load of a Sound Bank; its clips are freed once no loaded bank or playing voice holds them.</summary>
		public static void UnloadBank(SoundBank bank) => UnloadBank(BankReference(bank));

		/// <summary>Drops one load of a Sound Bank, named by its path relative to Assets.</summary>
		public static void UnloadBank(string bankPath)
		{
			using NativeString reference = String(bankPath);
			InternalCalls.Audio_UnloadBank(reference);
		}

		/// <summary>True once the bank is loaded and every clip it decodes is ready.</summary>
		public static bool IsBankLoaded(SoundBank bank)
		{
			using NativeString reference = String(BankReference(bank));
			return InternalCalls.Audio_IsBankLoaded(reference);
		}

		internal static string BankReference(SoundBank bank)
		{
			ArgumentNullException.ThrowIfNull(bank);
			if (bank.Handle == 0)
				throw new ArgumentException("No Sound Bank asset is assigned.", nameof(bank));
			return bank.Handle.ToString(CultureInfo.InvariantCulture);
		}

		/// <summary>Plays a Sound once, without position, and lets it release itself.</summary>
		/// <param name="eventRef">The Sound's path relative to Assets, with or without <c>.lsound</c> (<c>"Audio/Sounds/Fart"</c>).</param>
		public static void PlayOneShot(string eventRef)
		{
			using NativeString reference = String(eventRef);
			InternalCalls.Audio_CreateInstance(reference, true, null);
		}

		/// <summary>Plays a Sound once at a world position and lets it release itself.</summary>
		/// <param name="eventRef">The Sound's path relative to Assets, with or without <c>.lsound</c>.</param>
		/// <param name="position">Where the sound plays.</param>
		public static void PlayOneShot(string eventRef, Vector3 position)
		{
			Finite(position.X); Finite(position.Y); Finite(position.Z);
			using NativeString reference = String(eventRef);
			InternalCalls.Audio_CreateInstance(reference, true, &position);
		}

		/// <summary>Plays a Sound asset once. It must not loop.</summary>
		public static void PlayOneShot(Sound sound) => PlayOneShot(SoundReference(sound));

		/// <summary>Plays a Sound asset once at a world position. It must not loop.</summary>
		public static void PlayOneShot(Sound sound, Vector3 position) => PlayOneShot(SoundReference(sound), position);

		/// <summary>Creates an explicitly owned instance of a Sound asset.</summary>
		public static EventInstance CreateInstance(Sound sound) => CreateInstance(SoundReference(sound));

		// The engine resolves a Sound by its handle in decimal, the same string scenes store.
		internal static string SoundReference(Sound sound)
		{
			ArgumentNullException.ThrowIfNull(sound);
			if (sound.Handle == 0)
				throw new ArgumentException("No Sound asset is assigned.", nameof(sound));
			return sound.Handle.ToString(CultureInfo.InvariantCulture);
		}

		/// <summary>Creates a Sound instance you control: start, stop, move and change it, then dispose it.</summary>
		/// <param name="eventRef">The Sound's path relative to Assets, with or without <c>.lsound</c>.</param>
		/// <returns>The new instance, not started yet.</returns>
		/// <exception cref="System.InvalidOperationException">No Sound has that path.</exception>
		public static EventInstance CreateInstance(string eventRef)
		{
			using NativeString reference = String(eventRef);
			ulong handle = InternalCalls.Audio_CreateInstance(reference, false, null);
			if (handle == 0)
				throw new InvalidOperationException($"Cannot create Sound '{eventRef}'. Check the path and the Audio log.");
			var instance = new EventInstance(handle);
			Instances[handle] = new WeakReference<EventInstance>(instance);
			return instance;
		}

		/// <summary>Starts an authored snapshot. Expose its Intensity dial as a continuous 0–100
		/// parameter in Studio. The caller owns the returned instance; Stop fades, Dispose stops immediately.</summary>
		public static EventInstance StartSnapshot(string snapshotRef, float intensity = 1.0f)
		{
			Finite(intensity);
			if (intensity < 0 || intensity > 1)
				throw new ArgumentOutOfRangeException(nameof(intensity));
			var instance = CreateInstance(snapshotRef);
			try
			{
				instance.SetSnapshotIntensity(intensity);
				instance.Start();
				return instance;
			}
			catch
			{
				instance.Dispose();
				throw;
			}
		}

		/// <summary>Sets a mixer bus's volume (1 is unchanged).</summary>
		/// <param name="busPath">The bus path, <c>bus:/</c> for the master bus.</param>
		/// <param name="volume">Linear volume multiplier.</param>
		public static void SetBusVolume(string busPath, float volume)
		{
			Finite(volume);
			using NativeString path = String(busPath);
			InternalCalls.Audio_SetBusVolume(path, volume);
		}
		/// <summary>Hands a bus back to the mix after <see cref="SetBusVolume"/>: its volume returns to the project mixer's and snapshots apply to it again .</summary>
		/// <param name="busPath">The bus path, <c>bus:/</c> for the master bus.</param>
		public static void ClearBusVolume(string busPath)
		{
			using NativeString path = String(busPath);
			InternalCalls.Audio_ClearBusVolume(path);
		}
		/// <summary>A mixer bus's volume.</summary>
		/// <param name="busPath">The bus path, <c>bus:/</c> for the master bus.</param>
		public static float GetBusVolume(string busPath)
		{
			using NativeString path = String(busPath);
			return InternalCalls.Audio_GetBusVolume(path);
		}
		/// <summary>Mutes or unmutes a mixer bus.</summary>
		/// <param name="busPath">The bus path, <c>bus:/</c> for the master bus.</param>
		/// <param name="muted">True mutes.</param>
		public static void SetBusMuted(string busPath, bool muted)
		{
			using NativeString path = String(busPath);
			InternalCalls.Audio_SetBusMuted(path, muted);
		}
		/// <summary>Sets a VCA's volume (1 is unchanged).</summary>
		/// <param name="vcaPath">The VCA path (<c>vca:/...</c>).</param>
		/// <param name="volume">Linear volume multiplier.</param>
		public static void SetVCAVolume(string vcaPath, float volume)
		{
			Finite(volume);
			using NativeString path = String(vcaPath);
			InternalCalls.Audio_SetVCAVolume(path, volume);
		}
		/// <summary>Sets a global parameter (one every Sound sees).</summary>
		public static void SetGlobalParameter(string name, float value)
		{
			Finite(value);
			using NativeString parameter = String(name);
			InternalCalls.Audio_SetGlobalParameter(parameter, value);
		}
		/// <summary>Reads a global parameter.</summary>
		public static float GetGlobalParameter(string name)
		{
			using NativeString parameter = String(name);
			return InternalCalls.Audio_GetGlobalParameter(parameter);
		}

		internal static void Forget(ulong handle) => Instances.Remove(handle);

		// Invoked by Coral before OnUpdate, after the native mixer queue has been drained.
		internal static void Dispatch(ulong handle, int kind, string marker)
		{
			if (Instances.TryGetValue(handle, out var weak) && weak.TryGetTarget(out var instance))
				instance.Dispatch(kind, marker);
			else
				Instances.Remove(handle);
		}
		internal static void Reset()
		{
			foreach (var weak in Instances.Values)
				if (weak.TryGetTarget(out var instance))
					instance.Invalidate();
			Instances.Clear();
		}
	}

	/// <summary>An owned Studio event. Dispose on the main thread; scene teardown also releases it.</summary>
	public sealed unsafe class EventInstance : IDisposable
	{
		private ulong m_Handle;
		internal EventInstance(ulong handle) { m_Handle = handle; }
		/// <summary>Whether the instance still exists (it was not disposed and Play has not stopped).</summary>
		public bool IsValid => m_Handle != 0 && InternalCalls.Audio_IsValid(m_Handle);
		private ulong Handle => IsValid ? m_Handle : throw new ObjectDisposedException(nameof(EventInstance));
		/// <summary>Whether the event is playing.</summary>
		public bool IsPlaying => IsValid && InternalCalls.Audio_IsPlaying(m_Handle);
		/// <summary>Raised on the main thread when the event stops.</summary>
		public event Action? Stopped;
		/// <summary>Raised on the main thread when playback passes a timeline marker, with the marker's name.</summary>
		public event Action<string>? Marker;
		/// <summary>Normalized snapshot intensity, 0–1. This does not change event volume.</summary>
		public void SetSnapshotIntensity(float intensity)
		{
			Audio.RequireMainThread();
			Audio.Finite(intensity);
			if (intensity < 0 || intensity > 1)
				throw new ArgumentOutOfRangeException(nameof(intensity));
			if (!InternalCalls.Audio_SetSnapshotIntensity(Handle, intensity))
				throw new InvalidOperationException("Snapshot requires an exposed continuous Intensity parameter with range 0–100. Check the Audio log.");
		}

		/// <summary>Starts (or restarts) the event.</summary>
		/// <exception cref="System.ObjectDisposedException">The instance is no longer valid.</exception>
		public void Start() => InternalCalls.Audio_Start(Handle);
		/// <summary>Stops the event; the instance stays usable.</summary>
		/// <param name="allowFadeOut">True lets the event's authored fade-out play; false cuts it.</param>
		public void Stop(bool allowFadeOut = true) => InternalCalls.Audio_Stop(Handle, allowFadeOut);
		/// <summary>Sets the volume multiplier (1 is unchanged).</summary>
		public void SetVolume(float volume) { Audio.Finite(volume); InternalCalls.Audio_SetVolume(Handle, volume); }
		/// <summary>Sets the pitch multiplier (1 is unchanged).</summary>
		public void SetPitch(float pitch) { Audio.Finite(pitch); InternalCalls.Audio_SetPitch(Handle, pitch); }
		/// <summary>Sets one of the event's parameters.</summary>
		public void SetParameter(string name, float value)
		{
			Audio.Finite(value);
			using NativeString parameter = Audio.String(name);
			InternalCalls.Audio_SetParameter(Handle, parameter, value);
		}
		/// <summary>Places the event in the world.</summary>
		/// <param name="position">World position.</param>
		/// <param name="velocity">Velocity in meters per second, for doppler.</param>
		/// <param name="forward">The sound's forward direction.</param>
		/// <param name="up">The sound's up direction.</param>
		public void Set3DAttributes(Vector3 position, Vector3 velocity, Vector3 forward, Vector3 up)
		{
			Audio.Finite(position.X); Audio.Finite(position.Y); Audio.Finite(position.Z);
			Audio.Finite(velocity.X); Audio.Finite(velocity.Y); Audio.Finite(velocity.Z);
			Audio.Finite(forward.X); Audio.Finite(forward.Y); Audio.Finite(forward.Z);
			Audio.Finite(up.X); Audio.Finite(up.Y); Audio.Finite(up.Z);
			InternalCalls.Audio_Set3DAttributes(Handle, &position, &velocity, &forward, &up);
		}
		/// <summary>Stops the event at once and releases it. Call on the main thread.</summary>
		public void Dispose()
		{
			Audio.RequireMainThread();
			if (m_Handle == 0)
				return;
			InternalCalls.Audio_Dispose(m_Handle);
			Audio.Forget(m_Handle);
			Invalidate();
		}
		internal void Invalidate() { m_Handle = 0; Stopped = null; Marker = null; }
		internal void Dispatch(int kind, string marker)
		{
			if (!IsValid)
				return;
			try
			{
				if (kind == 0) Stopped?.Invoke(); else Marker?.Invoke(marker);
			}
			catch (Exception exception)
			{
				using NativeString message = $"Audio event callback failed: {exception}";
				InternalCalls.NativeLog(message, 3);
			}
		}
	}
}
