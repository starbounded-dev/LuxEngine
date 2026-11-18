// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Runtime.InteropServices;

namespace Lux
{
	// What the native side writes for one hit; the layout is checked against the C++ NativeRaycastHit2D (32 bytes).
	[StructLayout(LayoutKind.Sequential)]
	internal struct RaycastHit2DData
	{
		public ulong Entity;
		public Vector2 Point;
		public Vector2 Normal;
		public float Distance;
	}

	/// <summary>What a <see cref="Physics2D"/> ray hit.</summary>
	public struct RaycastHit2D
	{
		/// <summary>The entity whose collider was hit.</summary>
		public Entity Entity;
		/// <summary>Where the ray crossed the collider's surface, in world space.</summary>
		public Vector2 Point;
		/// <summary>The surface direction at <see cref="Point"/>, a unit vector pointing away from the collider.</summary>
		public Vector2 Normal;
		/// <summary>How far the ray travelled to reach the hit, in meters.</summary>
		public float Distance;

		internal RaycastHit2D(RaycastHit2DData data)
		{
			Entity = new Entity(data.Entity);
			Point = data.Point;
			Normal = data.Normal;
			Distance = data.Distance;
		}
	}

	/// <summary>
	/// Questions about the 2D physics world: what a ray hits and which colliders overlap a shape. They see
	/// Box Collider 2D, Circle Collider 2D, Polygon, Capsule and Edge colliders while the game is playing, and answer
	/// "nothing" otherwise. Call them from <c>OnUpdate</c> or <c>OnFixedUpdate</c>. By default they skip triggers, and every
	/// <c>ignore</c> parameter leaves out one entity (usually the caller), which is how a ray from inside a character
	/// avoids hitting it.
	/// </summary>
	public static unsafe class Physics2D
	{
		private enum Shape { Circle = 0, Box = 1 }

		// Results come back through a stack buffer first; a larger answer is asked for again with room for all of it.
		private const int FirstCapacity = 32;

		private static ulong IdOf(Entity? entity) => entity != null ? entity.ID : 0;

		/// <summary>Casts a ray and reports the nearest collider it enters.</summary>
		/// <param name="origin">Where the ray starts, in world space.</param>
		/// <param name="direction">The way it goes; it need not be a unit vector. A zero direction hits nothing.</param>
		/// <param name="hit">The nearest hit, when there is one.</param>
		/// <param name="maxDistance">How far the ray reaches, in meters.</param>
		/// <param name="ignore">An entity the ray passes through, or null.</param>
		/// <param name="includeTriggers">Whether trigger colliders count as hits.</param>
		/// <returns>True when something was hit.</returns>
		public static bool Raycast(Vector2 origin, Vector2 direction, out RaycastHit2D hit, float maxDistance = 1000.0f, Entity? ignore = null, bool includeTriggers = false)
		{
			RaycastHit2DData data;
			if (InternalCalls.Physics2D_Raycast(&origin, &direction, maxDistance, IdOf(ignore), includeTriggers, &data))
			{
				hit = new RaycastHit2D(data);
				return true;
			}
			hit = default;
			return false;
		}

		/// <summary>Casts a ray and reports only whether it hits anything.</summary>
		/// <param name="origin">Where the ray starts, in world space.</param>
		/// <param name="direction">The way it goes; it need not be a unit vector.</param>
		/// <param name="maxDistance">How far the ray reaches, in meters.</param>
		/// <param name="ignore">An entity the ray passes through, or null.</param>
		/// <param name="includeTriggers">Whether trigger colliders count as hits.</param>
		public static bool Raycast(Vector2 origin, Vector2 direction, float maxDistance = 1000.0f, Entity? ignore = null, bool includeTriggers = false)
		{
			return Raycast(origin, direction, out _, maxDistance, ignore, includeTriggers);
		}

		/// <summary>Casts a ray and reports every entity it enters, nearest first, each once.</summary>
		/// <param name="origin">Where the ray starts, in world space.</param>
		/// <param name="direction">The way it goes; it need not be a unit vector.</param>
		/// <param name="maxDistance">How far the ray reaches, in meters.</param>
		/// <param name="ignore">An entity the ray passes through, or null.</param>
		/// <param name="includeTriggers">Whether trigger colliders count as hits.</param>
		/// <returns>The hits, possibly none.</returns>
		public static RaycastHit2D[] RaycastAll(Vector2 origin, Vector2 direction, float maxDistance = 1000.0f, Entity? ignore = null, bool includeTriggers = false)
		{
			RaycastHit2DData[] data = new RaycastHit2DData[FirstCapacity];
			int count;
			fixed (RaycastHit2DData* buffer = data)
				count = InternalCalls.Physics2D_RaycastAll(&origin, &direction, maxDistance, IdOf(ignore), includeTriggers, buffer, data.Length);
			if (count > data.Length)
			{
				data = new RaycastHit2DData[count];
				fixed (RaycastHit2DData* buffer = data)
					count = Math.Min(count, InternalCalls.Physics2D_RaycastAll(&origin, &direction, maxDistance, IdOf(ignore), includeTriggers, buffer, data.Length));
			}
			RaycastHit2D[] hits = new RaycastHit2D[count];
			for (int i = 0; i < count; i++)
				hits[i] = new RaycastHit2D(data[i]);
			return hits;
		}

		private static Entity[] Overlap(Shape shape, Vector2 center, Vector2 dimensions, float angle, Entity? ignore, bool includeTriggers)
		{
			ulong[] ids = new ulong[FirstCapacity];
			int count;
			fixed (ulong* buffer = ids)
				count = InternalCalls.Physics2D_Overlap((int)shape, &center, &dimensions, angle, IdOf(ignore), includeTriggers, buffer, ids.Length);
			if (count > ids.Length)
			{
				ids = new ulong[count];
				fixed (ulong* buffer = ids)
					count = Math.Min(count, InternalCalls.Physics2D_Overlap((int)shape, &center, &dimensions, angle, IdOf(ignore), includeTriggers, buffer, ids.Length));
			}
			Entity[] entities = new Entity[count];
			for (int i = 0; i < count; i++)
				entities[i] = new Entity(ids[i]);
			return entities;
		}

		/// <summary>The entities whose colliders contain a point, each once.</summary>
		/// <param name="point">The point, in world space.</param>
		/// <param name="ignore">An entity to leave out, or null.</param>
		/// <param name="includeTriggers">Whether trigger colliders count.</param>
		public static Entity[] OverlapPoint(Vector2 point, Entity? ignore = null, bool includeTriggers = false)
		{
			return Overlap(Shape.Circle, point, new Vector2(0.0f, 0.0f), 0.0f, ignore, includeTriggers);
		}

		/// <summary>The entities whose colliders touch a circle, each once.</summary>
		/// <param name="center">The circle's center, in world space.</param>
		/// <param name="radius">The circle's radius, in meters.</param>
		/// <param name="ignore">An entity to leave out, or null.</param>
		/// <param name="includeTriggers">Whether trigger colliders count.</param>
		public static Entity[] OverlapCircle(Vector2 center, float radius, Entity? ignore = null, bool includeTriggers = false)
		{
			return Overlap(Shape.Circle, center, new Vector2(radius, 0.0f), 0.0f, ignore, includeTriggers);
		}

		/// <summary>The entities whose colliders touch a box, each once.</summary>
		/// <param name="center">The box's center, in world space.</param>
		/// <param name="halfExtents">Half the box's width and height, in meters.</param>
		/// <param name="angle">The box's rotation, in radians.</param>
		/// <param name="ignore">An entity to leave out, or null.</param>
		/// <param name="includeTriggers">Whether trigger colliders count.</param>
		public static Entity[] OverlapBox(Vector2 center, Vector2 halfExtents, float angle = 0.0f, Entity? ignore = null, bool includeTriggers = false)
		{
			return Overlap(Shape.Box, center, halfExtents, angle, ignore, includeTriggers);
		}
	}
}
