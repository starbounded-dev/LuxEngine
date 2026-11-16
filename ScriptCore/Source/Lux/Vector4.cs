// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

namespace Lux
{
	/// <summary>A 4D vector of floats, most often an RGBA color with components from 0 to 1.</summary>
	public struct Vector4
	{
		/// <summary>The X component (red, for a color).</summary>
		public float X;
		/// <summary>The Y component (green, for a color).</summary>
		public float Y;
		/// <summary>The Z component (blue, for a color).</summary>
		public float Z;
		/// <summary>The W component (alpha, for a color).</summary>
		public float W;

		/// <summary>(0, 0, 0, 0).</summary>
		public static Vector4 Zero => new Vector4(0.0f);

		/// <summary>A vector with every component set to <paramref name="scalar"/>.</summary>
		public Vector4(float scalar)
		{
			X = scalar;
			Y = scalar;
			Z = scalar;
			W = scalar;
		}

		/// <summary>A vector from its components.</summary>
		public Vector4(float x, float y, float z, float w)
		{
			X = x;
			Y = y;
			Z = z;
			W = w;
		}

		/// <summary>A vector from a 3D vector and a W component.</summary>
		public Vector4(Vector3 xyz, float w)
		{
			X = xyz.X;
			Y = xyz.Y;
			Z = xyz.Z;
			W = w;
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

		/// <summary>The X, Y and Z components.</summary>
		public Vector3 XYZ
		{
			get => new Vector3(X, Y, Z);
			set
			{
				X = value.X;
				Y = value.Y;
				Z = value.Z;
			}
		}

		/// <summary>Component-wise sum.</summary>
		public static Vector4 operator +(Vector4 a, Vector4 b)
		{
			return new Vector4(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
		}

		/// <summary>The vector scaled by <paramref name="scalar"/>.</summary>
		public static Vector4 operator *(Vector4 vector, float scalar)
		{
			return new Vector4(vector.X * scalar, vector.Y * scalar, vector.Z * scalar, vector.W * scalar);
		}

	}
}
