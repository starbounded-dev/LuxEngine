// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>
	/// Debug lines drawn in the world while the game runs, in the editor and in the player (not in Dist builds).
	/// </summary>
	/// <remarks>
	/// A drawing belongs to the update method that made it and stays until that method runs again, so draw it every
	/// <c>OnUpdate</c> (or every <c>OnFixedUpdate</c>) to keep it. With a duration it stays that many seconds instead.
	/// </remarks>
	public static class Gizmos
	{
		/// <summary>Opaque white, the default color.</summary>
		public static readonly Vector4 White = new Vector4(1.0f);

		/// <summary>Draws a line between two world positions.</summary>
		/// <param name="color">RGBA, components from 0 to 1.</param>
		/// <param name="duration">Seconds the drawing stays; 0 keeps it until the drawing method runs again.</param>
		/// <param name="depthTest">False draws it on top of everything.</param>
		public static unsafe void DrawLine(Vector3 start, Vector3 end, Vector4 color, float duration = 0.0f, bool depthTest = true)
		{
			InternalCalls.Gizmos_DrawLine(&start, &end, &color, duration, depthTest);
		}

		/// <summary>Draws a white line between two world positions until the drawing method runs again.</summary>
		public static void DrawLine(Vector3 start, Vector3 end) => DrawLine(start, end, White);

		/// <summary>Draws a line from <paramref name="origin"/> to <c>origin + direction</c>.</summary>
		/// <param name="color">RGBA, components from 0 to 1.</param>
		/// <param name="duration">Seconds the drawing stays; 0 keeps it until the drawing method runs again.</param>
		/// <param name="depthTest">False draws it on top of everything.</param>
		public static void DrawRay(Vector3 origin, Vector3 direction, Vector4 color, float duration = 0.0f, bool depthTest = true)
		{
			DrawLine(origin, origin + direction, color, duration, depthTest);
		}

		/// <summary>Draws the twelve edges of an axis-aligned box.</summary>
		/// <param name="center">The box's center.</param>
		/// <param name="halfExtents">Half the box's size on each axis.</param>
		/// <param name="color">RGBA, components from 0 to 1.</param>
		/// <param name="duration">Seconds the drawing stays; 0 keeps it until the drawing method runs again.</param>
		/// <param name="depthTest">False draws it on top of everything.</param>
		public static void DrawWireBox(Vector3 center, Vector3 halfExtents, Vector4 color, float duration = 0.0f, bool depthTest = true)
		{
			Vector3 Corner(int i) => new Vector3(
				center.X + ((i & 1) != 0 ? halfExtents.X : -halfExtents.X),
				center.Y + ((i & 2) != 0 ? halfExtents.Y : -halfExtents.Y),
				center.Z + ((i & 4) != 0 ? halfExtents.Z : -halfExtents.Z));

			for (int i = 0; i < 8; i++)
			{
				for (int axis = 1; axis < 8; axis <<= 1)
				{
					if ((i & axis) == 0)
						DrawLine(Corner(i), Corner(i | axis), color, duration, depthTest);
				}
			}
		}

		/// <summary>Draws a sphere as three circles, one around each axis.</summary>
		/// <param name="center">The sphere's center.</param>
		/// <param name="radius">The sphere's radius.</param>
		/// <param name="color">RGBA, components from 0 to 1.</param>
		/// <param name="duration">Seconds the drawing stays; 0 keeps it until the drawing method runs again.</param>
		/// <param name="depthTest">False draws it on top of everything.</param>
		public static void DrawWireSphere(Vector3 center, float radius, Vector4 color, float duration = 0.0f, bool depthTest = true)
		{
			const int segments = 24;
			for (int plane = 0; plane < 3; plane++)
			{
				Vector3 Point(int s)
				{
					float angle = s * (2.0f * MathF.PI / segments);
					float a = MathF.Cos(angle) * radius, b = MathF.Sin(angle) * radius;
					return plane switch
					{
						0 => new Vector3(center.X, center.Y + a, center.Z + b),
						1 => new Vector3(center.X + a, center.Y, center.Z + b),
						_ => new Vector3(center.X + a, center.Y + b, center.Z),
					};
				}
				for (int s = 0; s < segments; s++)
					DrawLine(Point(s), Point(s + 1), color, duration, depthTest);
			}
		}
	}
}
