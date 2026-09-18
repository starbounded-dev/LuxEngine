using System;

namespace Lux
{
	public enum AcousticGeometryMode { Static, Dynamic, Disabled }

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
		public bool Enabled { get => Get(0) != 0; set => Set(0, value ? 1 : 0); }
		public float Open { get => Get(1); set => Set(1, value); }
		public float BlendDistance { get => Get(2); set => Set(2, value); }
		public AcousticMaterial Material { get => (AcousticMaterial)Get(3); set => Set(3, (int)value); }
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
		public Entity? ZoneA { get => GetRoom(false); set => SetRoom(false, value); }
		public Entity? ZoneB { get => GetRoom(true); set => SetRoom(true, value); }
	}
}
