// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>A 2D vector of floats: a 2D position, size or direction.</summary>
	public struct Vector2
	{
		/// <summary>The X component.</summary>
		public float X;
		/// <summary>The Y component.</summary>
		public float Y;

		/// <summary>(0, 0).</summary>
		public static Vector2 Zero => new Vector2(0.0f);

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
	}
}
