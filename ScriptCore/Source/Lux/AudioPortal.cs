// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;

namespace Lux
{
	/// <summary>How a mesh takes part in ray-traced acoustics.</summary>
	public enum AcousticGeometryMode
	{
		/// <summary>Captured once at its starting transform.</summary>
		Static,
		/// <summary>Follows the entity as it moves.</summary>
		Dynamic,
		/// <summary>Not acoustic geometry.</summary>
		Disabled
	}

	/// <summary>Acoustic shutter and optional link between two AudioZone components. Main thread only.
	/// Open is 0 (closed) to 1 (open); geometry changes use the scene's update budget.</summary>
	public unsafe class AudioPortalComponent : Component
	{
		private float Get(int field)
		{
			Audio.RequireMainThread();
			return InternalCalls.Audio_PortalGetScalar(Entity.ID, field);
		}
		private void Set(int field, float value)
		{
			Audio.RequireMainThread();
			Audio.Finite(value);
			if (!InternalCalls.Audio_PortalSetScalar(Entity.ID, field, value))
				throw new ArgumentOutOfRangeException(nameof(value), "Invalid portal value or unavailable component.");
		}
		/// <summary>Whether the portal is active.</summary>
		public bool Enabled { get => Get(0) != 0; set => Set(0, value ? 1 : 0); }
		/// <summary>0 closes the shutter (a shut door); 1 removes it.</summary>
		public float Open { get => Get(1); set => Set(1, value); }
		/// <summary>Distance in meters over which the room's sound leaking through the opening fades.</summary>
		public float BlendDistance { get => Get(2); set => Set(2, value); }
		/// <summary>What the shutter is made of.</summary>
		public AcousticMaterial Material { get => (AcousticMaterial)Get(3); set => Set(3, (int)value); }
		/// <summary>Half the opening's size: X width, Y height, Z thickness.</summary>
		public Vector3 HalfExtents
		{
			get
			{
				Audio.RequireMainThread();
				Vector3 result;
				InternalCalls.Audio_PortalGetExtents(Entity.ID, &result);
				return result;
			}
			set
			{
				Audio.RequireMainThread();
				if (!InternalCalls.Audio_PortalSetExtents(Entity.ID, &value))
					throw new ArgumentOutOfRangeException(nameof(value));
			}
		}
		private Entity? GetRoom(bool second)
		{
			Audio.RequireMainThread();
			ulong id = InternalCalls.Audio_PortalGetRoom(Entity.ID, second);
			return id == 0 ? null : new Entity(id);
		}
		private void SetRoom(bool second, Entity? room)
		{
			Audio.RequireMainThread();
			if (!InternalCalls.Audio_PortalSetRoom(Entity.ID, second, room?.ID ?? 0))
				throw new ArgumentException("Portal rooms must reference two different AudioZone entities in the current scene.", nameof(room));
		}
		/// <summary>The audio zone on one side, or null.</summary>
		public Entity? ZoneA { get => GetRoom(false); set => SetRoom(false, value); }
		/// <summary>The audio zone on the other side, or null.</summary>
		public Entity? ZoneB { get => GetRoom(true); set => SetRoom(true, value); }
	}
}
