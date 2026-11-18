// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>A 2D vector of floats: a 2D position, size or direction.</summary>
	public struct Vector2 : IEquatable<Vector2>
	{
		/// <summary>The X component.</summary>
		public float X;
		/// <summary>The Y component.</summary>
		public float Y;

		/// <summary>(0, 0).</summary>
		public static Vector2 Zero => new Vector2(0.0f);
		/// <summary>(1, 1).</summary>
		public static Vector2 One => new Vector2(1.0f);
		/// <summary>(0, 1).</summary>
		public static Vector2 Up => new Vector2(0.0f, 1.0f);
		/// <summary>(1, 0).</summary>
		public static Vector2 Right => new Vector2(1.0f, 0.0f);

		/// <summary>A vector with every component set to <paramref name="scalar"/>.</summary>
		public Vector2(float scalar)
		{
			X = scalar;
			Y = scalar;
		}

		/// <summary>A vector from its components.</summary>
		public Vector2(float x, float y)
		{
			X = x;
			Y = y;
		}

		/// <summary>Component-wise sum.</summary>
		public static Vector2 operator +(Vector2 a, Vector2 b)
		{
			return new Vector2(a.X + b.X, a.Y + b.Y);
		}

		/// <summary>The vector scaled by <paramref name="scalar"/>.</summary>
		public static Vector2 operator *(Vector2 vector, float scalar)
		{
			return new Vector2(vector.X * scalar, vector.Y * scalar);
		}

		/// <summary>Component-wise difference.</summary>
		public static Vector2 operator -(Vector2 a, Vector2 b)
		{
			return new Vector2(a.X - b.X, a.Y - b.Y);
		}

		/// <summary>The vector pointing the opposite way.</summary>
		public static Vector2 operator -(Vector2 vector) => new Vector2(-vector.X, -vector.Y);

		/// <summary>The vector scaled by <paramref name="scalar"/>.</summary>
		public static Vector2 operator *(float scalar, Vector2 vector) => vector * scalar;

		/// <summary>The vector divided by <paramref name="scalar"/>.</summary>
		public static Vector2 operator /(Vector2 vector, float scalar) => new Vector2(vector.X / scalar, vector.Y / scalar);

		/// <summary>Whether the components are exactly equal.</summary>
		public static bool operator ==(Vector2 a, Vector2 b) => a.X == b.X && a.Y == b.Y;

		/// <summary>Whether any component differs.</summary>
		public static bool operator !=(Vector2 a, Vector2 b) => !(a == b);

		/// <summary>The dot product: the cosine of the angle between two unit vectors.</summary>
		public static float Dot(Vector2 a, Vector2 b) => a.X * b.X + a.Y * b.Y;

		/// <summary>The distance between two points.</summary>
		public static float Distance(Vector2 a, Vector2 b) => (a - b).Length();

		/// <summary>The point <paramref name="t"/> of the way from <paramref name="a"/> to <paramref name="b"/>, with <paramref name="t"/> limited to 0..1.</summary>
		public static Vector2 Lerp(Vector2 a, Vector2 b, float t)
		{
			t = Mathf.Clamp01(t);
			return new Vector2(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t);
		}

		/// <summary>The same direction with length 1, or <see cref="Zero"/> for a zero vector.</summary>
		public Vector2 Normalized()
		{
			float length = Length();
			return length > 1e-9f ? new Vector2(X / length, Y / length) : Zero;
		}

		/// <summary>The squared length, cheaper than <see cref="Length"/> for comparisons.</summary>
		public float LengthSquared()
		{
			return X * X + Y * Y;
		}

		/// <summary>The vector's length.</summary>
		public float Length()
		{
			return (float)Math.Sqrt(LengthSquared());
		}

		/// <summary>Whether the components are exactly equal.</summary>
		public bool Equals(Vector2 other) => this == other;
		public override bool Equals(object? obj) => obj is Vector2 other && this == other;
		public override int GetHashCode() => HashCode.Combine(X, Y);
		public override string ToString() => $"({X:F3}, {Y:F3})";
	}
}
