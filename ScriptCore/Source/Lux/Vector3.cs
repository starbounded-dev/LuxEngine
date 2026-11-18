// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>A 3D vector of floats: a position, direction, scale or Euler rotation.</summary>
	public struct Vector3 : IEquatable<Vector3>
	{
		/// <summary>The X component.</summary>
		public float X;
		/// <summary>The Y component (up, in the world).</summary>
		public float Y;
		/// <summary>The Z component.</summary>
		public float Z;

		/// <summary>(0, 0, 0).</summary>
		public static Vector3 Zero => new Vector3(0.0f);
		/// <summary>(1, 1, 1).</summary>
		public static Vector3 One => new Vector3(1.0f);
		/// <summary>(0, 1, 0), up in the world.</summary>
		public static Vector3 Up => new Vector3(0.0f, 1.0f, 0.0f);
		/// <summary>(0, -1, 0), down in the world.</summary>
		public static Vector3 Down => new Vector3(0.0f, -1.0f, 0.0f);
		/// <summary>(1, 0, 0), the world's right.</summary>
		public static Vector3 Right => new Vector3(1.0f, 0.0f, 0.0f);
		/// <summary>(-1, 0, 0), the world's left.</summary>
		public static Vector3 Left => new Vector3(-1.0f, 0.0f, 0.0f);
		/// <summary>(0, 0, -1), the world's forward: the way an entity with no rotation faces.</summary>
		public static Vector3 Forward => new Vector3(0.0f, 0.0f, -1.0f);
		/// <summary>(0, 0, 1), the world's back.</summary>
		public static Vector3 Back => new Vector3(0.0f, 0.0f, 1.0f);

		/// <summary>A vector with every component set to <paramref name="scalar"/>.</summary>
		public Vector3(float scalar)
		{
			X = scalar;
			Y = scalar;
			Z = scalar;
		}

		/// <summary>A vector from its components.</summary>
		public Vector3(float x, float y, float z)
		{
			X = x;
			Y = y;
			Z = z;
		}

		/// <summary>A vector from a 2D vector and a Z component.</summary>
		public Vector3(Vector2 xy, float z)
		{
			X = xy.X;
			Y = xy.Y;
			Z = z;
		}

		/// <summary>The X and Y components.</summary>
		public Vector2 XY
		{
			get => new Vector2(X, Y);
			set
			{
				X = value.X;
				Y = value.Y;
			}
		}

		/// <summary>The squared length, cheaper than <see cref="Length"/> for comparisons.</summary>
		public float LengthSquared() => X * X + Y * Y + Z * Z;

		/// <summary>The vector's length.</summary>
		public float Length() => (float)Math.Sqrt(LengthSquared());

		/// <summary>The same direction with length 1, or <see cref="Zero"/> for a zero vector.</summary>
		public Vector3 Normalized()
		{
			float length = Length();
			return length > 1e-9f ? new Vector3(X / length, Y / length, Z / length) : Zero;
		}

		/// <summary>The dot product: the cosine of the angle between two unit vectors.</summary>
		public static float Dot(Vector3 a, Vector3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

		/// <summary>The cross product: a vector at right angles to both, following the right-hand rule.</summary>
		public static Vector3 Cross(Vector3 a, Vector3 b)
		{
			return new Vector3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
		}

		/// <summary>The distance between two points.</summary>
		public static float Distance(Vector3 a, Vector3 b) => (a - b).Length();

		/// <summary>The point <paramref name="t"/> of the way from <paramref name="a"/> to <paramref name="b"/>, with <paramref name="t"/> limited to 0..1.</summary>
		public static Vector3 Lerp(Vector3 a, Vector3 b, float t)
		{
			t = Mathf.Clamp01(t);
			return new Vector3(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t);
		}

		/// <summary>Moves <paramref name="current"/> toward <paramref name="target"/> by at most <paramref name="maxDistance"/>.</summary>
		public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistance)
		{
			Vector3 delta = target - current;
			float length = delta.Length();
			if (length <= maxDistance || length < 1e-9f)
				return target;
			return current + delta * (maxDistance / length);
		}

		/// <summary>The angle between two directions, in radians.</summary>
		public static float Angle(Vector3 a, Vector3 b)
		{
			float denominator = (float)Math.Sqrt(a.LengthSquared() * b.LengthSquared());
			return denominator < 1e-15f ? 0.0f : (float)Math.Acos(Mathf.Clamp(Dot(a, b) / denominator, -1.0f, 1.0f));
		}

		/// <summary>The part of <paramref name="vector"/> that lies along <paramref name="onNormal"/>.</summary>
		public static Vector3 Project(Vector3 vector, Vector3 onNormal)
		{
			float lengthSquared = onNormal.LengthSquared();
			return lengthSquared < 1e-15f ? Zero : onNormal * (Dot(vector, onNormal) / lengthSquared);
		}

		/// <summary>The part of <paramref name="vector"/> that lies in the plane with the unit normal <paramref name="planeNormal"/>.</summary>
		public static Vector3 ProjectOnPlane(Vector3 vector, Vector3 planeNormal) => vector - Project(vector, planeNormal);

		/// <summary>The direction after bouncing off a surface with the unit normal <paramref name="normal"/>.</summary>
		public static Vector3 Reflect(Vector3 direction, Vector3 normal) => direction - normal * (2.0f * Dot(direction, normal));

		/// <summary>The smaller of each component.</summary>
		public static Vector3 Min(Vector3 a, Vector3 b) => new Vector3(Math.Min(a.X, b.X), Math.Min(a.Y, b.Y), Math.Min(a.Z, b.Z));

		/// <summary>The larger of each component.</summary>
		public static Vector3 Max(Vector3 a, Vector3 b) => new Vector3(Math.Max(a.X, b.X), Math.Max(a.Y, b.Y), Math.Max(a.Z, b.Z));

		/// <summary>The vector with its length limited to <paramref name="maxLength"/>.</summary>
		public static Vector3 ClampMagnitude(Vector3 vector, float maxLength)
		{
			float length = vector.Length();
			return length > maxLength && length > 1e-9f ? vector * (maxLength / length) : vector;
		}

		/// <summary>Component-wise sum.</summary>
		public static Vector3 operator +(Vector3 a, Vector3 b)
		{
			return new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
		}

		/// <summary>Component-wise difference.</summary>
		public static Vector3 operator -(Vector3 a, Vector3 b)
		{
			return new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
		}

		/// <summary>The vector pointing the opposite way.</summary>
		public static Vector3 operator -(Vector3 vector) => new Vector3(-vector.X, -vector.Y, -vector.Z);

		/// <summary>The vector scaled by <paramref name="scalar"/>.</summary>
		public static Vector3 operator *(Vector3 vector, float scalar)
		{
			return new Vector3(vector.X * scalar, vector.Y * scalar, vector.Z * scalar);
		}

		/// <summary>The vector scaled by <paramref name="scalar"/>.</summary>
		public static Vector3 operator *(float scalar, Vector3 vector) => vector * scalar;

		/// <summary>Component-wise product.</summary>
		public static Vector3 operator *(Vector3 a, Vector3 b) => new Vector3(a.X * b.X, a.Y * b.Y, a.Z * b.Z);

		/// <summary>The vector divided by <paramref name="scalar"/>.</summary>
		public static Vector3 operator /(Vector3 vector, float scalar) => new Vector3(vector.X / scalar, vector.Y / scalar, vector.Z / scalar);

		/// <summary>Whether the components are exactly equal.</summary>
		public static bool operator ==(Vector3 a, Vector3 b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z;

		/// <summary>Whether any component differs.</summary>
		public static bool operator !=(Vector3 a, Vector3 b) => !(a == b);

		/// <summary>Whether the components are exactly equal.</summary>
		public bool Equals(Vector3 other) => this == other;
		public override bool Equals(object? obj) => obj is Vector3 other && this == other;
		public override int GetHashCode() => HashCode.Combine(X, Y, Z);
		public override string ToString() => $"({X:F3}, {Y:F3}, {Z:F3})";
	}
}
