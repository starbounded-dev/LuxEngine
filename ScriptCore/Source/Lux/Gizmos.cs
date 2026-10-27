// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	// Debug lines drawn in the world while the game runs (not in Dist builds). A line belongs to the
	// update method that draws it and stays until that method runs again: draw every OnUpdate (or every
	// OnFixedUpdate) to keep it. With a duration (seconds) it stays that long instead.
	// depthTest: false draws it on top of everything.
	public static class Gizmos
	{
		public static readonly Vector4 White = new Vector4(1.0f);

		public static unsafe void DrawLine(Vector3 start, Vector3 end, Vector4 color, float duration = 0.0f, bool depthTest = true)
		{
			InternalCalls.Gizmos_DrawLine(&start, &end, &color, duration, depthTest);
		}

		public static void DrawLine(Vector3 start, Vector3 end) => DrawLine(start, end, White);

		public static void DrawRay(Vector3 origin, Vector3 direction, Vector4 color, float duration = 0.0f, bool depthTest = true)
		{
			DrawLine(origin, origin + direction, color, duration, depthTest);
		}

		// An axis-aligned box: its twelve edges.
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

		// Three circles, one around each axis.
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
