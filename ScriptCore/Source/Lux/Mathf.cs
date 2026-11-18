// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>Single-precision maths helpers for gameplay: clamping, interpolation, angles and smoothing.</summary>
	public static class Mathf
	{
		/// <summary>The ratio of a circle's circumference to its diameter.</summary>
		public const float PI = 3.14159265358979f;
		/// <summary>Multiply degrees by this to get radians.</summary>
		public const float Deg2Rad = PI / 180.0f;
		/// <summary>Multiply radians by this to get degrees.</summary>
		public const float Rad2Deg = 180.0f / PI;
		/// <summary>The smallest value that is not zero, for tolerant comparisons.</summary>
		public const float Epsilon = 1.401298E-45f;
		/// <summary>Positive infinity.</summary>
		public const float Infinity = float.PositiveInfinity;

		/// <summary>The sine of an angle in radians.</summary>
		public static float Sin(float radians) => (float)Math.Sin(radians);
		/// <summary>The cosine of an angle in radians.</summary>
		public static float Cos(float radians) => (float)Math.Cos(radians);
		/// <summary>The tangent of an angle in radians.</summary>
		public static float Tan(float radians) => (float)Math.Tan(radians);
		/// <summary>The angle in radians whose sine is <paramref name="value"/>.</summary>
		public static float Asin(float value) => (float)Math.Asin(value);
		/// <summary>The angle in radians whose cosine is <paramref name="value"/>.</summary>
		public static float Acos(float value) => (float)Math.Acos(value);
		/// <summary>The angle in radians whose tangent is <paramref name="value"/>.</summary>
		public static float Atan(float value) => (float)Math.Atan(value);
		/// <summary>The angle in radians of the point (<paramref name="x"/>, <paramref name="y"/>).</summary>
		public static float Atan2(float y, float x) => (float)Math.Atan2(y, x);
		/// <summary>The square root.</summary>
		public static float Sqrt(float value) => (float)Math.Sqrt(value);
		/// <summary>The absolute value.</summary>
		public static float Abs(float value) => Math.Abs(value);
		/// <summary><paramref name="value"/> raised to <paramref name="power"/>.</summary>
		public static float Pow(float value, float power) => (float)Math.Pow(value, power);
		/// <summary>The largest whole number not above <paramref name="value"/>.</summary>
		public static float Floor(float value) => (float)Math.Floor(value);
		/// <summary>The smallest whole number not below <paramref name="value"/>.</summary>
		public static float Ceil(float value) => (float)Math.Ceiling(value);
		/// <summary>The nearest whole number, halves going to the even one.</summary>
		public static float Round(float value) => (float)Math.Round(value);

		/// <summary>The smaller of two values.</summary>
		public static float Min(float a, float b) => a < b ? a : b;
		/// <summary>The larger of two values.</summary>
		public static float Max(float a, float b) => a > b ? a : b;
		/// <summary>The smaller of two whole numbers.</summary>
		public static int Min(int a, int b) => a < b ? a : b;
		/// <summary>The larger of two whole numbers.</summary>
		public static int Max(int a, int b) => a > b ? a : b;

		/// <summary>-1 for a negative value, 1 for zero or a positive one.</summary>
		public static float Sign(float value) => value < 0.0f ? -1.0f : 1.0f;

		/// <summary><paramref name="value"/> limited to the range from <paramref name="min"/> to <paramref name="max"/>.</summary>
		public static float Clamp(float value, float min, float max) => value < min ? min : value > max ? max : value;
		/// <summary><paramref name="value"/> limited to the range from <paramref name="min"/> to <paramref name="max"/>.</summary>
		public static int Clamp(int value, int min, int max) => value < min ? min : value > max ? max : value;
		/// <summary><paramref name="value"/> limited to the range from 0 to 1.</summary>
		public static float Clamp01(float value) => value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;

		/// <summary>The point <paramref name="t"/> of the way from <paramref name="a"/> to <paramref name="b"/>, with <paramref name="t"/> limited to 0..1.</summary>
		public static float Lerp(float a, float b, float t) => a + (b - a) * Clamp01(t);
		/// <summary>Like <see cref="Lerp"/> but <paramref name="t"/> is not limited, so the result can pass either end.</summary>
		public static float LerpUnclamped(float a, float b, float t) => a + (b - a) * t;
		/// <summary>Where <paramref name="value"/> lies between <paramref name="a"/> and <paramref name="b"/>, as 0..1. Returns 0 when they are equal.</summary>
		public static float InverseLerp(float a, float b, float value) => a != b ? Clamp01((value - a) / (b - a)) : 0.0f;

		/// <summary>Moves <paramref name="current"/> toward <paramref name="target"/> by at most <paramref name="maxDelta"/>.</summary>
		public static float MoveTowards(float current, float target, float maxDelta)
		{
			if (Math.Abs(target - current) <= maxDelta)
				return target;
			return current + Sign(target - current) * maxDelta;
		}

		/// <summary>Eases from 0 to 1 as <paramref name="x"/> goes from <paramref name="edge0"/> to <paramref name="edge1"/>, flat at both ends.</summary>
		public static float SmoothStep(float edge0, float edge1, float x)
		{
			float t = Clamp01((x - edge0) / (edge1 - edge0));
			return t * t * (3.0f - 2.0f * t);
		}

		/// <summary>Whether two floats are equal within rounding error.</summary>
		public static bool Approximately(float a, float b)
		{
			return Math.Abs(b - a) < Math.Max(1e-6f * Math.Max(Math.Abs(a), Math.Abs(b)), 8.0f * 1.175494E-38f);
		}

		/// <summary>Wraps <paramref name="t"/> into 0..<paramref name="length"/>, so it loops.</summary>
		public static float Repeat(float t, float length) => Clamp(t - Floor(t / length) * length, 0.0f, length);

		/// <summary>Bounces <paramref name="t"/> back and forth between 0 and <paramref name="length"/>.</summary>
		public static float PingPong(float t, float length)
		{
			t = Repeat(t, length * 2.0f);
			return length - Math.Abs(t - length);
		}

		/// <summary>The shortest signed difference in degrees from angle <paramref name="current"/> to <paramref name="target"/>, in -180..180.</summary>
		public static float DeltaAngle(float current, float target)
		{
			float delta = Repeat(target - current, 360.0f);
			return delta > 180.0f ? delta - 360.0f : delta;
		}

		/// <summary>Like <see cref="Lerp"/> for angles in degrees: takes the short way round.</summary>
		public static float LerpAngle(float a, float b, float t) => a + DeltaAngle(a, b) * Clamp01(t);

		/// <summary>Like <see cref="MoveTowards"/> for angles in degrees: takes the short way round.</summary>
		public static float MoveTowardsAngle(float current, float target, float maxDelta)
		{
			float delta = DeltaAngle(current, target);
			if (-maxDelta < delta && delta < maxDelta)
				return target;
			return MoveTowards(current, current + delta, maxDelta);
		}

		/// <summary>
		/// Eases <paramref name="current"/> toward <paramref name="target"/> like a spring that never overshoots.
		/// Call it every frame, keeping <paramref name="velocity"/> between calls.
		/// </summary>
		/// <param name="current">The value now.</param>
		/// <param name="target">The value to reach.</param>
		/// <param name="velocity">The rate of change, updated by each call. Start it at 0.</param>
		/// <param name="smoothTime">Roughly how many seconds to take; smaller is faster.</param>
		/// <param name="deltaTime">The time since the last call, usually <see cref="Time.DeltaTime"/>.</param>
		/// <returns>The new value.</returns>
		public static float SmoothDamp(float current, float target, ref float velocity, float smoothTime, float deltaTime)
		{
			smoothTime = Math.Max(0.0001f, smoothTime);
			float omega = 2.0f / smoothTime;
			float x = omega * deltaTime;
			float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
			float change = current - target;
			float originalTarget = target;
			target = current - change;
			float temp = (velocity + omega * change) * deltaTime;
			velocity = (velocity - omega * temp) * exp;
			float result = target + (change + temp) * exp;
			if ((originalTarget - current > 0.0f) == (result > originalTarget))
			{
				result = originalTarget;
				velocity = (result - originalTarget) / deltaTime;
			}
			return result;
		}
	}
}
