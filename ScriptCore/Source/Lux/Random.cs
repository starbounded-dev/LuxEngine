// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>
	/// Random numbers for gameplay. Seed it with <see cref="InitState"/> to repeat a sequence. A script that has both
	/// <c>using System;</c> and <c>using Lux;</c> must write <c>Lux.Random</c> or <c>System.Random</c> in full.
	/// </summary>
	public static class Random
	{
		private static System.Random s_Generator = new System.Random();

		/// <summary>Restarts the sequence from a seed, so the same seed gives the same numbers.</summary>
		public static void InitState(int seed) => s_Generator = new System.Random(seed);

		/// <summary>A random float from 0 up to, not including, 1.</summary>
		public static float Value => (float)s_Generator.NextDouble();

		/// <summary>A random float from <paramref name="min"/> up to, not including, <paramref name="max"/>.</summary>
		public static float Range(float min, float max) => min + (max - min) * Value;

		/// <summary>A random whole number from <paramref name="min"/> up to, not including, <paramref name="max"/>.</summary>
		public static int Range(int min, int max) => s_Generator.Next(min, max);

		/// <summary>A random point on or inside a circle of radius 1.</summary>
		public static Vector2 InsideUnitCircle
		{
			get
			{
				float angle = Range(0.0f, 2.0f * Mathf.PI), radius = Mathf.Sqrt(Value);
				return new Vector2(Mathf.Cos(angle) * radius, Mathf.Sin(angle) * radius);
			}
		}

		/// <summary>A random point on the surface of a sphere of radius 1.</summary>
		public static Vector3 OnUnitSphere
		{
			get
			{
				float z = Range(-1.0f, 1.0f), angle = Range(0.0f, 2.0f * Mathf.PI);
				float r = Mathf.Sqrt(1.0f - z * z);
				return new Vector3(r * Mathf.Cos(angle), r * Mathf.Sin(angle), z);
			}
		}

		/// <summary>A random point on or inside a sphere of radius 1.</summary>
		public static Vector3 InsideUnitSphere => OnUnitSphere * (float)Math.Cbrt(Value);

		/// <summary>A random rotation, evenly spread over all orientations.</summary>
		public static Quaternion Rotation
		{
			get
			{
				float u1 = Value, u2 = Range(0.0f, 2.0f * Mathf.PI), u3 = Range(0.0f, 2.0f * Mathf.PI);
				float a = Mathf.Sqrt(1.0f - u1), b = Mathf.Sqrt(u1);
				return new Quaternion(a * Mathf.Sin(u2), a * Mathf.Cos(u2), b * Mathf.Sin(u3), b * Mathf.Cos(u3));
			}
		}

		/// <summary>A random opaque color with full saturation and value, or the ranges given, for varied but even tints.</summary>
		public static Color ColorHSV(float saturationMin = 1.0f, float saturationMax = 1.0f, float valueMin = 1.0f, float valueMax = 1.0f)
		{
			return Color.FromHSV(Value, Range(saturationMin, saturationMax), Range(valueMin, valueMax));
		}
	}
}
