// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>
	/// A rotation in 3D. Compose rotations with <c>*</c> (the right one applies first), rotate a vector with
	/// <c>rotation * vector</c>, and convert from or to the Euler angles (radians) that <see cref="Entity.Rotation"/> uses.
	/// </summary>
	public struct Quaternion : IEquatable<Quaternion>
	{
		/// <summary>The X component of the imaginary part.</summary>
		public float X;
		/// <summary>The Y component of the imaginary part.</summary>
		public float Y;
		/// <summary>The Z component of the imaginary part.</summary>
		public float Z;
		/// <summary>The real part.</summary>
		public float W;

		/// <summary>A quaternion from its four components. Prefer <see cref="Euler"/> or <see cref="AngleAxis"/>.</summary>
		public Quaternion(float x, float y, float z, float w)
		{
			X = x;
			Y = y;
			Z = z;
			W = w;
		}

		/// <summary>No rotation.</summary>
		public static Quaternion Identity => new Quaternion(0.0f, 0.0f, 0.0f, 1.0f);

		/// <summary>The rotation for Euler angles in radians, applied about X, then Y, then Z (the order <see cref="Entity.Rotation"/> uses).</summary>
		public static Quaternion Euler(Vector3 radians)
		{
			float cx = (float)Math.Cos(radians.X * 0.5f), sx = (float)Math.Sin(radians.X * 0.5f);
			float cy = (float)Math.Cos(radians.Y * 0.5f), sy = (float)Math.Sin(radians.Y * 0.5f);
			float cz = (float)Math.Cos(radians.Z * 0.5f), sz = (float)Math.Sin(radians.Z * 0.5f);
			return new Quaternion(
				sx * cy * cz - cx * sy * sz,
				cx * sy * cz + sx * cy * sz,
				cx * cy * sz - sx * sy * cz,
				cx * cy * cz + sx * sy * sz);
		}

		/// <summary>The rotation of <paramref name="radians"/> about <paramref name="axis"/>.</summary>
		public static Quaternion AngleAxis(float radians, Vector3 axis)
		{
			Vector3 n = axis.Normalized();
			float s = (float)Math.Sin(radians * 0.5f);
			return new Quaternion(n.X * s, n.Y * s, n.Z * s, (float)Math.Cos(radians * 0.5f));
		}

		/// <summary>The rotation that turns the direction <paramref name="from"/> onto <paramref name="to"/> by the shortest way.</summary>
		public static Quaternion FromToRotation(Vector3 from, Vector3 to)
		{
			Vector3 a = from.Normalized(), b = to.Normalized();
			float dot = Vector3.Dot(a, b);
			if (dot >= 1.0f - 1e-6f)
				return Identity;
			if (dot <= -1.0f + 1e-6f)
			{
				Vector3 axis = Vector3.Cross(Vector3.Right, a);
				if (axis.LengthSquared() < 1e-6f)
					axis = Vector3.Cross(Vector3.Up, a);
				return AngleAxis(Mathf.PI, axis);
			}
			Vector3 c = Vector3.Cross(a, b);
			return new Quaternion(c.X, c.Y, c.Z, 1.0f + dot).Normalized();
		}

		/// <summary>
		/// The rotation that makes -Z (an entity's forward) point along <paramref name="forward"/> with +Y as near
		/// <paramref name="up"/> as possible.
		/// </summary>
		public static Quaternion LookRotation(Vector3 forward, Vector3 up)
		{
			Vector3 f = forward.Normalized();
			if (f.LengthSquared() < 1e-12f)
				return Identity;
			Vector3 r = Vector3.Cross(f, up).Normalized();
			if (r.LengthSquared() < 1e-12f)
				r = Vector3.Cross(f, Math.Abs(f.Y) < 0.99f ? Vector3.Up : Vector3.Right).Normalized();
			Vector3 u = Vector3.Cross(r, f);
			// Columns of the rotation matrix: right, up, back (-forward).
			float m00 = r.X, m01 = u.X, m02 = -f.X;
			float m10 = r.Y, m11 = u.Y, m12 = -f.Y;
			float m20 = r.Z, m21 = u.Z, m22 = -f.Z;
			float trace = m00 + m11 + m22;
			Quaternion q;
			if (trace > 0.0f)
			{
				float s = (float)Math.Sqrt(trace + 1.0f) * 2.0f;
				q = new Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
			}
			else if (m00 > m11 && m00 > m22)
			{
				float s = (float)Math.Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
				q = new Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
			}
			else if (m11 > m22)
			{
				float s = (float)Math.Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
				q = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
			}
			else
			{
				float s = (float)Math.Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
				q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
			}
			return q.Normalized();
		}

		/// <summary>The rotation as Euler angles in radians, in the form <see cref="Entity.Rotation"/> uses.</summary>
		public Vector3 ToEuler()
		{
			float pitch;
			float y = 2.0f * (Y * Z + W * X), x = W * W - X * X - Y * Y + Z * Z;
			if (Math.Abs(x) < 1e-12f && Math.Abs(y) < 1e-12f)
				pitch = 2.0f * (float)Math.Atan2(X, W);
			else
				pitch = (float)Math.Atan2(y, x);
			float yaw = (float)Math.Asin(Mathf.Clamp(-2.0f * (X * Z - W * Y), -1.0f, 1.0f));
			float roll = (float)Math.Atan2(2.0f * (X * Y + W * Z), W * W + X * X - Y * Y - Z * Z);
			return new Vector3(pitch, yaw, roll);
		}

		/// <summary>The length of the four components. A rotation has length 1.</summary>
		public float Length() => (float)Math.Sqrt(X * X + Y * Y + Z * Z + W * W);

		/// <summary>The same rotation with length 1, or <see cref="Identity"/> for a zero quaternion.</summary>
		public Quaternion Normalized()
		{
			float length = Length();
			return length > 1e-12f ? new Quaternion(X / length, Y / length, Z / length, W / length) : Identity;
		}

		/// <summary>The rotation that undoes this one.</summary>
		public Quaternion Inverse()
		{
			float lengthSquared = X * X + Y * Y + Z * Z + W * W;
			return lengthSquared > 1e-12f
				? new Quaternion(-X / lengthSquared, -Y / lengthSquared, -Z / lengthSquared, W / lengthSquared)
				: Identity;
		}

		/// <summary>The cosine of half the angle between two rotations: 1 when they are the same.</summary>
		public static float Dot(Quaternion a, Quaternion b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;

		/// <summary>The angle between two rotations, in radians.</summary>
		public static float Angle(Quaternion a, Quaternion b)
		{
			float dot = Math.Min(Math.Abs(Dot(a, b)), 1.0f);
			return 2.0f * (float)Math.Acos(dot);
		}

		/// <summary>Blends from <paramref name="a"/> to <paramref name="b"/> along the shortest arc at constant speed; <paramref name="t"/> is limited to 0..1.</summary>
		public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
		{
			t = Mathf.Clamp01(t);
			float dot = Dot(a, b);
			if (dot < 0.0f)
			{
				b = new Quaternion(-b.X, -b.Y, -b.Z, -b.W);
				dot = -dot;
			}
			if (dot > 0.9995f)
				return new Quaternion(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t, a.W + (b.W - a.W) * t).Normalized();
			float theta = (float)Math.Acos(dot);
			float sin = (float)Math.Sin(theta);
			float wa = (float)Math.Sin((1.0f - t) * theta) / sin, wb = (float)Math.Sin(t * theta) / sin;
			return new Quaternion(a.X * wa + b.X * wb, a.Y * wa + b.Y * wb, a.Z * wa + b.Z * wb, a.W * wa + b.W * wb);
		}

		/// <summary>Turns <paramref name="from"/> toward <paramref name="to"/> by at most <paramref name="maxRadians"/>.</summary>
		public static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxRadians)
		{
			float angle = Angle(from, to);
			return angle < 1e-6f ? to : Slerp(from, to, Math.Min(1.0f, maxRadians / angle));
		}

		/// <summary>Applies <paramref name="b"/> first, then <paramref name="a"/>.</summary>
		public static Quaternion operator *(Quaternion a, Quaternion b)
		{
			return new Quaternion(
				a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
				a.W * b.Y + a.Y * b.W + a.Z * b.X - a.X * b.Z,
				a.W * b.Z + a.Z * b.W + a.X * b.Y - a.Y * b.X,
				a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);
		}

		/// <summary>Rotates <paramref name="v"/> by <paramref name="q"/>.</summary>
		public static Vector3 operator *(Quaternion q, Vector3 v)
		{
			Vector3 u = new Vector3(q.X, q.Y, q.Z);
			Vector3 t = Vector3.Cross(u, v) * 2.0f;
			return v + t * q.W + Vector3.Cross(u, t);
		}

		/// <summary>Whether the components are exactly equal.</summary>
		public static bool operator ==(Quaternion a, Quaternion b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.W == b.W;
		/// <summary>Whether any component differs.</summary>
		public static bool operator !=(Quaternion a, Quaternion b) => !(a == b);
		/// <summary>Whether the components are exactly equal.</summary>
		public bool Equals(Quaternion other) => this == other;
		public override bool Equals(object? obj) => obj is Quaternion other && this == other;
		public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
		public override string ToString() => $"({X:F3}, {Y:F3}, {Z:F3}, {W:F3})";
	}
}
