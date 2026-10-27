// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

namespace Lux
{
	/// <summary>A 3D vector of floats: a position, direction, scale or Euler rotation.</summary>
	public struct Vector3
	{
		/// <summary>The X component.</summary>
		public float X;
		/// <summary>The Y component (up, in the world).</summary>
		public float Y;
		/// <summary>The Z component.</summary>
		public float Z;

		/// <summary>(0, 0, 0).</summary>
		public static Vector3 Zero => new Vector3(0.0f);

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

		/// <summary>Component-wise sum.</summary>
		public static Vector3 operator +(Vector3 a, Vector3 b)
		{
			return new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
		}

		/// <summary>The vector scaled by <paramref name="scalar"/>.</summary>
		public static Vector3 operator *(Vector3 vector, float scalar)
		{
			return new Vector3(vector.X * scalar, vector.Y * scalar, vector.Z * scalar);
		}

	}
}
