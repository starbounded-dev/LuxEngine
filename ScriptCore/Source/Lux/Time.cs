// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>
	/// The game clock. Scaled values follow <see cref="TimeScale"/> (0 freezes, 0.5 is slow motion) and stop while the
	/// game is paused; unscaled values always run at real speed. Scripts, physics and animation use the scaled clock;
	/// audio and the accessibility overlay use real time.
	/// </summary>
	public static unsafe class Time
	{
		private enum Field { Delta, UnscaledDelta, Elapsed, UnscaledElapsed, Scale, FixedDelta, FrameCount }

		/// <summary>
		/// Seconds since the last frame, scaled by <see cref="TimeScale"/>: the value <c>OnUpdate</c> receives. Inside
		/// <c>OnFixedUpdate</c> it is the fixed step.
		/// </summary>
		public static float DeltaTime => (float)InternalCalls.Time_Get((int)Field.Delta);

		/// <summary>Seconds since the last frame at real speed, ignoring <see cref="TimeScale"/>.</summary>
		public static float UnscaledDeltaTime => (float)InternalCalls.Time_Get((int)Field.UnscaledDelta);

		/// <summary>Seconds between physics steps, the step <c>OnFixedUpdate</c> receives. Set in the project's physics settings.</summary>
		public static float FixedDeltaTime => (float)InternalCalls.Time_Get((int)Field.FixedDelta);

		/// <summary>Seconds of scaled game time since Play started.</summary>
		public static double ElapsedTime => InternalCalls.Time_Get((int)Field.Elapsed);

		/// <summary>Seconds of real time since Play started, ignoring <see cref="TimeScale"/> and pause.</summary>
		public static double UnscaledTime => InternalCalls.Time_Get((int)Field.UnscaledElapsed);

		/// <summary>The number of frames updated since Play started.</summary>
		public static long FrameCount => (long)InternalCalls.Time_Get((int)Field.FrameCount);

		/// <summary>
		/// The speed of game time: 1 is normal, 0 freezes scripts, physics and animation, 2 doubles them. A negative value
		/// is treated as 0. It survives a scene load.
		/// </summary>
		public static float TimeScale
		{
			get => (float)InternalCalls.Time_Get((int)Field.Scale);
			set => InternalCalls.Time_SetScale(Math.Max(0.0f, value));
		}
	}
}
