// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using System.Runtime.InteropServices;

namespace Lux
{
	// What the native side writes for one hit; the layout is checked against the C++ NativeRaycastHit (40 bytes).
	[StructLayout(LayoutKind.Sequential)]
	internal struct RaycastHitData
	{
		public ulong Entity;
		public Vector3 Point;
		public Vector3 Normal;
		public float Distance;
	}

	/// <summary>What a <see cref="Physics"/> ray or shape cast hit.</summary>
	public struct RaycastHit
	{
		/// <summary>The entity whose collider was hit.</summary>
		public Entity Entity;
		/// <summary>Where the ray crossed the surface, or where the cast shape touched it, in world space.</summary>
		public Vector3 Point;
		/// <summary>The surface direction at <see cref="Point"/>, a unit vector pointing away from the collider.</summary>
		public Vector3 Normal;
		/// <summary>How far the ray or the center of the shape travelled to reach the hit, in meters.</summary>
		public float Distance;

		internal RaycastHit(RaycastHitData data)
		{
			Entity = new Entity(data.Entity);
			Point = data.Point;
			Normal = data.Normal;
			Distance = data.Distance;
		}
	}

	/// <summary>
	/// Questions about the physics world: what a ray or a moving shape would hit, and what overlaps a shape. They see
	/// colliders and character controllers while the game is playing, and answer "nothing" otherwise. Call them from
	/// <c>OnUpdate</c> or <c>OnFixedUpdate</c>. By default they skip triggers, and every <c>ignore</c> parameter
	/// leaves out one entity (usually the caller), which is how a ray from inside a character avoids hitting it.
	/// </summary>
	/// <remarks>
	/// A <c>layerMask</c> has one bit for each physics layer of the project, numbered by its position in the layer list
	/// in Project Settings: layer 0 is bit 0. Build one with <see cref="GetLayerMask"/> or use <see cref="AllLayers"/>.
	/// </remarks>
	public static unsafe class Physics
	{
		private enum Shape { Sphere = 0, Box = 1, Capsule = 2 }

		// Results come back through a stack buffer first; a larger answer is asked for again with room for all of it.
		private const int FirstCapacity = 32;

		/// <summary>A layer mask that includes every physics layer.</summary>
		public const uint AllLayers = 0xFFFFFFFFu;

		/// <summary>A layer mask for the physics layers with the given names.</summary>
		/// <param name="layerNames">Names as written in the project's physics layers. A name the project does not have is an error.</param>
		/// <exception cref="ArgumentException">A name is not one of the project's physics layers.</exception>
		public static uint GetLayerMask(params string[] layerNames)
		{
			uint mask = 0;
			foreach (string name in layerNames)
			{
				int index = InternalCalls.Physics_GetLayerIndex(name);
				if (index < 0 || index >= 32)
					throw new ArgumentException($"The project has no physics layer named \"{name}\" (layers beyond the 32nd cannot be masked).", nameof(layerNames));
				mask |= 1u << index;
			}
			return mask;
		}

		private static ulong IdOf(Entity? entity) => entity != null ? entity.ID : 0;

		/// <summary>Casts a ray and reports the nearest collider it enters.</summary>
		/// <param name="origin">Where the ray starts, in world space.</param>
		/// <param name="direction">The way it goes; it need not be a unit vector. A zero direction hits nothing.</param>
		/// <param name="hit">The nearest hit, when there is one.</param>
		/// <param name="maxDistance">How far the ray reaches, in meters.</param>
		/// <param name="layerMask">Which physics layers the ray can hit.</param>
		/// <param name="ignore">An entity the ray passes through, or null.</param>
		/// <param name="includeTriggers">Whether trigger volumes count as hits.</param>
		/// <returns>True when something was hit.</returns>
		public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit, float maxDistance = 1000.0f, uint layerMask = AllLayers,
			Entity? ignore = null, bool includeTriggers = false)
		{
			RaycastHitData data;
			if (InternalCalls.Physics_Raycast(&origin, &direction, maxDistance, layerMask, IdOf(ignore), includeTriggers, &data))
			{
				hit = new RaycastHit(data);
				return true;
			}
			hit = default;
			return false;
		}

		/// <summary>Casts a ray and reports only whether it hits anything.</summary>
		/// <param name="origin">Where the ray starts, in world space.</param>
		/// <param name="direction">The way it goes; it need not be a unit vector.</param>
		/// <param name="maxDistance">How far the ray reaches, in meters.</param>
		/// <param name="layerMask">Which physics layers the ray can hit.</param>
		/// <param name="ignore">An entity the ray passes through, or null.</param>
		/// <param name="includeTriggers">Whether trigger volumes count as hits.</param>
		public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance = 1000.0f, uint layerMask = AllLayers,
			Entity? ignore = null, bool includeTriggers = false)
		{
			return Raycast(origin, direction, out _, maxDistance, layerMask, ignore, includeTriggers);
		}

		/// <summary>Casts a ray and reports every collider it enters, nearest first. A collider the ray starts inside is not reported.</summary>
		/// <param name="origin">Where the ray starts, in world space.</param>
		/// <param name="direction">The way it goes; it need not be a unit vector.</param>
		/// <param name="maxDistance">How far the ray reaches, in meters.</param>
		/// <param name="layerMask">Which physics layers the ray can hit.</param>
		/// <param name="ignore">An entity the ray passes through, or null.</param>
		/// <param name="includeTriggers">Whether trigger volumes count as hits.</param>
		/// <returns>The hits, possibly none.</returns>
		public static RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction, float maxDistance = 1000.0f, uint layerMask = AllLayers,
			Entity? ignore = null, bool includeTriggers = false)
		{
			RaycastHitData[] data = new RaycastHitData[FirstCapacity];
			int count;
			fixed (RaycastHitData* buffer = data)
				count = InternalCalls.Physics_RaycastAll(&origin, &direction, maxDistance, layerMask, IdOf(ignore), includeTriggers, buffer, data.Length);
			if (count > data.Length)
			{
				data = new RaycastHitData[count];
				fixed (RaycastHitData* buffer = data)
					count = Math.Min(count, InternalCalls.Physics_RaycastAll(&origin, &direction, maxDistance, layerMask, IdOf(ignore), includeTriggers, buffer, data.Length));
			}
			RaycastHit[] hits = new RaycastHit[count];
			for (int i = 0; i < count; i++)
				hits[i] = new RaycastHit(data[i]);
			return hits;
		}

		private static bool ShapeCast(Shape shape, Vector3 origin, Quaternion orientation, Vector3 dimensions, Vector3 direction, out RaycastHit hit,
			float maxDistance, uint layerMask, Entity ignore, bool includeTriggers)
		{
			Vector4 rotation = new Vector4(orientation.X, orientation.Y, orientation.Z, orientation.W);
			RaycastHitData data;
			if (InternalCalls.Physics_ShapeCast((int)shape, &origin, &rotation, &dimensions, &direction, maxDistance, layerMask, IdOf(ignore), includeTriggers, &data))
			{
				hit = new RaycastHit(data);
				return true;
			}
			hit = default;
			return false;
		}

		/// <summary>Sweeps a sphere along a direction and reports the first collider it touches.</summary>
		/// <param name="origin">The center of the sphere at the start, in world space.</param>
		/// <param name="radius">The sphere's radius, in meters.</param>
		/// <param name="direction">The way it sweeps; it need not be a unit vector.</param>
		/// <param name="hit">The first hit: <see cref="RaycastHit.Point"/> is where the sphere touches, <see cref="RaycastHit.Distance"/> how far its center moved.</param>
		/// <param name="maxDistance">How far the sphere sweeps, in meters.</param>
		/// <param name="layerMask">Which physics layers the sphere can hit.</param>
		/// <param name="ignore">An entity the sphere passes through, or null.</param>
		/// <param name="includeTriggers">Whether trigger volumes count as hits.</param>
		/// <returns>True when something was hit.</returns>
		public static bool SphereCast(Vector3 origin, float radius, Vector3 direction, out RaycastHit hit, float maxDistance = 1000.0f,
			uint layerMask = AllLayers, Entity? ignore = null, bool includeTriggers = false)
		{
			return ShapeCast(Shape.Sphere, origin, Quaternion.Identity, new Vector3(radius, 0.0f, 0.0f), direction, out hit, maxDistance, layerMask, ignore, includeTriggers);
		}

		/// <summary>Sweeps a box along a direction and reports the first collider it touches.</summary>
		/// <param name="center">The center of the box at the start, in world space.</param>
		/// <param name="halfExtents">Half the box's size along each of its own axes, in meters.</param>
		/// <param name="orientation">The box's rotation.</param>
		/// <param name="direction">The way it sweeps; it need not be a unit vector.</param>
		/// <param name="hit">The first hit: <see cref="RaycastHit.Point"/> is where the box touches, <see cref="RaycastHit.Distance"/> how far its center moved.</param>
		/// <param name="maxDistance">How far the box sweeps, in meters.</param>
		/// <param name="layerMask">Which physics layers the box can hit.</param>
		/// <param name="ignore">An entity the box passes through, or null.</param>
		/// <param name="includeTriggers">Whether trigger volumes count as hits.</param>
		/// <returns>True when something was hit.</returns>
		public static bool BoxCast(Vector3 center, Vector3 halfExtents, Quaternion orientation, Vector3 direction, out RaycastHit hit,
			float maxDistance = 1000.0f, uint layerMask = AllLayers, Entity? ignore = null, bool includeTriggers = false)
		{
			return ShapeCast(Shape.Box, center, orientation, halfExtents, direction, out hit, maxDistance, layerMask, ignore, includeTriggers);
		}

		/// <summary>Sweeps a capsule along a direction and reports the first collider it touches. The capsule stands along Y before <paramref name="orientation"/> turns it.</summary>
		/// <param name="center">The center of the capsule at the start, in world space.</param>
		/// <param name="radius">The radius of the capsule and of its rounded ends, in meters.</param>
		/// <param name="halfHeight">Half the length of the straight part between the rounded ends, in meters.</param>
		/// <param name="orientation">The capsule's rotation.</param>
		/// <param name="direction">The way it sweeps; it need not be a unit vector.</param>
		/// <param name="hit">The first hit: <see cref="RaycastHit.Point"/> is where the capsule touches, <see cref="RaycastHit.Distance"/> how far its center moved.</param>
		/// <param name="maxDistance">How far the capsule sweeps, in meters.</param>
		/// <param name="layerMask">Which physics layers the capsule can hit.</param>
		/// <param name="ignore">An entity the capsule passes through, or null.</param>
		/// <param name="includeTriggers">Whether trigger volumes count as hits.</param>
		/// <returns>True when something was hit.</returns>
		public static bool CapsuleCast(Vector3 center, float radius, float halfHeight, Quaternion orientation, Vector3 direction, out RaycastHit hit,
			float maxDistance = 1000.0f, uint layerMask = AllLayers, Entity? ignore = null, bool includeTriggers = false)
		{
			return ShapeCast(Shape.Capsule, center, orientation, new Vector3(radius, halfHeight, 0.0f), direction, out hit, maxDistance, layerMask, ignore, includeTriggers);
		}

		private static Entity[] Overlap(Shape shape, Vector3 center, Quaternion orientation, Vector3 dimensions, uint layerMask, Entity ignore, bool includeTriggers)
		{
			Vector4 rotation = new Vector4(orientation.X, orientation.Y, orientation.Z, orientation.W);
			ulong[] ids = new ulong[FirstCapacity];
			int count;
			fixed (ulong* buffer = ids)
				count = InternalCalls.Physics_OverlapShape((int)shape, &center, &rotation, &dimensions, layerMask, IdOf(ignore), includeTriggers, buffer, ids.Length);
			if (count > ids.Length)
			{
				ids = new ulong[count];
				fixed (ulong* buffer = ids)
					count = Math.Min(count, InternalCalls.Physics_OverlapShape((int)shape, &center, &rotation, &dimensions, layerMask, IdOf(ignore), includeTriggers, buffer, ids.Length));
			}
			Entity[] entities = new Entity[count];
			for (int i = 0; i < count; i++)
				entities[i] = new Entity(ids[i]);
			return entities;
		}

		/// <summary>The entities whose colliders touch a sphere, each once.</summary>
		/// <param name="center">The sphere's center, in world space.</param>
		/// <param name="radius">The sphere's radius, in meters.</param>
		/// <param name="layerMask">Which physics layers count.</param>
		/// <param name="ignore">An entity to leave out, or null.</param>
		/// <param name="includeTriggers">Whether trigger volumes count.</param>
		public static Entity[] OverlapSphere(Vector3 center, float radius, uint layerMask = AllLayers, Entity? ignore = null, bool includeTriggers = false)
		{
			return Overlap(Shape.Sphere, center, Quaternion.Identity, new Vector3(radius, 0.0f, 0.0f), layerMask, ignore, includeTriggers);
		}

		/// <summary>The entities whose colliders touch a box, each once.</summary>
		/// <param name="center">The box's center, in world space.</param>
		/// <param name="halfExtents">Half the box's size along each of its own axes, in meters.</param>
		/// <param name="orientation">The box's rotation.</param>
		/// <param name="layerMask">Which physics layers count.</param>
		/// <param name="ignore">An entity to leave out, or null.</param>
		/// <param name="includeTriggers">Whether trigger volumes count.</param>
		public static Entity[] OverlapBox(Vector3 center, Vector3 halfExtents, Quaternion orientation, uint layerMask = AllLayers, Entity? ignore = null,
			bool includeTriggers = false)
		{
			return Overlap(Shape.Box, center, orientation, halfExtents, layerMask, ignore, includeTriggers);
		}

		/// <summary>The entities whose colliders touch a capsule, each once. The capsule stands along Y before <paramref name="orientation"/> turns it.</summary>
		/// <param name="center">The capsule's center, in world space.</param>
		/// <param name="radius">The radius of the capsule and of its rounded ends, in meters.</param>
		/// <param name="halfHeight">Half the length of the straight part between the rounded ends, in meters.</param>
		/// <param name="orientation">The capsule's rotation.</param>
		/// <param name="layerMask">Which physics layers count.</param>
		/// <param name="ignore">An entity to leave out, or null.</param>
		/// <param name="includeTriggers">Whether trigger volumes count.</param>
		public static Entity[] OverlapCapsule(Vector3 center, float radius, float halfHeight, Quaternion orientation, uint layerMask = AllLayers,
			Entity? ignore = null, bool includeTriggers = false)
		{
			return Overlap(Shape.Capsule, center, orientation, new Vector3(radius, halfHeight, 0.0f), layerMask, ignore, includeTriggers);
		}

		/// <summary>Whether any collider touches a sphere.</summary>
		/// <param name="center">The sphere's center, in world space.</param>
		/// <param name="radius">The sphere's radius, in meters.</param>
		/// <param name="layerMask">Which physics layers count.</param>
		/// <param name="ignore">An entity to leave out, or null.</param>
		/// <param name="includeTriggers">Whether trigger volumes count.</param>
		public static bool CheckSphere(Vector3 center, float radius, uint layerMask = AllLayers, Entity? ignore = null, bool includeTriggers = false)
		{
			return OverlapSphere(center, radius, layerMask, ignore, includeTriggers).Length > 0;
		}
	}
}
