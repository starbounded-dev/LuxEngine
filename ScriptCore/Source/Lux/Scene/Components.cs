using System;
using Coral.Managed.Interop;

namespace Lux
{
	public abstract class Component
	{
		public Entity Entity { get; internal set; }
	}

	public unsafe class TransformComponent : Component
	{
		public Vector3 Translation
		{
			get { Vector3 r; InternalCalls.TransformComponent_GetTranslation(Entity.ID, &r); return r; }
			set { InternalCalls.TransformComponent_SetTranslation(Entity.ID, &value); }
		}

		public Vector3 Rotation
		{
			get { Vector3 r; InternalCalls.TransformComponent_GetRotation(Entity.ID, &r); return r; }
			set { InternalCalls.TransformComponent_SetRotation(Entity.ID, &value); }
		}

		public Vector3 Scale
		{
			get { Vector3 r; InternalCalls.TransformComponent_GetScale(Entity.ID, &r); return r; }
			set { InternalCalls.TransformComponent_SetScale(Entity.ID, &value); }
		}
	}

	public unsafe class TagComponent : Component
	{
		public string Tag
		{
			get => InternalCalls.TagComponent_GetTag(Entity.ID);
			set => InternalCalls.TagComponent_SetTag(Entity.ID, value);
		}
	}

	public unsafe class SpriteRendererComponent : Component
	{
		public Vector4 Color
		{
			get { Vector4 c; InternalCalls.SpriteRendererComponent_GetColor(Entity.ID, &c); return c; }
			set { InternalCalls.SpriteRendererComponent_SetColor(Entity.ID, &value); }
		}

		public float TilingFactor
		{
			get => InternalCalls.SpriteRendererComponent_GetTilingFactor(Entity.ID);
			set => InternalCalls.SpriteRendererComponent_SetTilingFactor(Entity.ID, value);
		}

		public Texture2D Texture
		{
			get
			{
				ulong handle = InternalCalls.SpriteRendererComponent_GetTexture(Entity.ID);
				return handle == 0 ? null : new Texture2D(handle);
			}
			set => InternalCalls.SpriteRendererComponent_SetTexture(Entity.ID, value != null ? value.Handle : 0);
		}
	}

	public unsafe class CircleRendererComponent : Component
	{
		public Vector4 Color
		{
			get { Vector4 c; InternalCalls.CircleRendererComponent_GetColor(Entity.ID, &c); return c; }
			set { InternalCalls.CircleRendererComponent_SetColor(Entity.ID, &value); }
		}

		public float Thickness
		{
			get => InternalCalls.CircleRendererComponent_GetThickness(Entity.ID);
			set => InternalCalls.CircleRendererComponent_SetThickness(Entity.ID, value);
		}

		public float Fade
		{
			get => InternalCalls.CircleRendererComponent_GetFade(Entity.ID);
			set => InternalCalls.CircleRendererComponent_SetFade(Entity.ID, value);
		}
	}

	public unsafe class CameraComponent : Component
	{
		public bool Primary
		{
			get => InternalCalls.CameraComponent_GetPrimary(Entity.ID);
			set => InternalCalls.CameraComponent_SetPrimary(Entity.ID, value);
		}
	}

	public unsafe class RigidBody2DComponent : Component
	{
		public enum BodyType { Static = 0, Dynamic, Kinematic }

		public BodyType Type
		{
			get => InternalCalls.RigidBody2DComponent_GetType(Entity.ID);
			set => InternalCalls.RigidBody2DComponent_SetType(Entity.ID, value);
		}

		public Vector2 LinearVelocity
		{
			get { Vector2 v; InternalCalls.RigidBody2DComponent_GetLinearVelocity(Entity.ID, &v); return v; }
			set { InternalCalls.RigidBody2DComponent_SetLinearVelocity(Entity.ID, &value); }
		}

		public float AngularVelocity
		{
			get => InternalCalls.RigidBody2DComponent_GetAngularVelocity(Entity.ID);
			set => InternalCalls.RigidBody2DComponent_SetAngularVelocity(Entity.ID, value);
		}

		public Vector2 Position
		{
			get { Vector2 p; InternalCalls.RigidBody2DComponent_GetPosition(Entity.ID, &p); return p; }
			set { InternalCalls.RigidBody2DComponent_SetPosition(Entity.ID, &value); }
		}

		public float Rotation
		{
			get => InternalCalls.RigidBody2DComponent_GetRotation(Entity.ID);
			set => InternalCalls.RigidBody2DComponent_SetRotation(Entity.ID, value);
		}

		public float Mass => InternalCalls.RigidBody2DComponent_GetMass(Entity.ID);

		public float GravityScale
		{
			get => InternalCalls.RigidBody2DComponent_GetGravityScale(Entity.ID);
			set => InternalCalls.RigidBody2DComponent_SetGravityScale(Entity.ID, value);
		}

		public void ApplyLinearImpulse(Vector2 impulse, Vector2 worldPosition, bool wake)
			=> InternalCalls.RigidBody2DComponent_ApplyLinearImpulse(Entity.ID, &impulse, &worldPosition, wake);

		public void ApplyLinearImpulse(Vector2 impulse, bool wake)
			=> InternalCalls.RigidBody2DComponent_ApplyLinearImpulseToCenter(Entity.ID, &impulse, wake);

		public void ApplyForce(Vector2 force, Vector2 worldPosition, bool wake)
			=> InternalCalls.RigidBody2DComponent_ApplyForce(Entity.ID, &force, &worldPosition, wake);

		public void ApplyForce(Vector2 force, bool wake)
			=> InternalCalls.RigidBody2DComponent_ApplyForceToCenter(Entity.ID, &force, wake);

		public void ApplyTorque(float torque, bool wake)
			=> InternalCalls.RigidBody2DComponent_ApplyTorque(Entity.ID, torque, wake);
	}

	public unsafe class BoxCollider2DComponent : Component
	{
		public Vector2 Offset
		{
			get { Vector2 v; InternalCalls.BoxCollider2DComponent_GetOffset(Entity.ID, &v); return v; }
			set { InternalCalls.BoxCollider2DComponent_SetOffset(Entity.ID, &value); }
		}

		public Vector2 Size
		{
			get { Vector2 v; InternalCalls.BoxCollider2DComponent_GetSize(Entity.ID, &v); return v; }
			set { InternalCalls.BoxCollider2DComponent_SetSize(Entity.ID, &value); }
		}

		public float Density
		{
			get => InternalCalls.BoxCollider2DComponent_GetDensity(Entity.ID);
			set => InternalCalls.BoxCollider2DComponent_SetDensity(Entity.ID, value);
		}

		public float Friction
		{
			get => InternalCalls.BoxCollider2DComponent_GetFriction(Entity.ID);
			set => InternalCalls.BoxCollider2DComponent_SetFriction(Entity.ID, value);
		}

		public float Restitution
		{
			get => InternalCalls.BoxCollider2DComponent_GetRestitution(Entity.ID);
			set => InternalCalls.BoxCollider2DComponent_SetRestitution(Entity.ID, value);
		}
	}

	public unsafe class CircleCollider2DComponent : Component
	{
		public Vector2 Offset
		{
			get { Vector2 v; InternalCalls.CircleCollider2DComponent_GetOffset(Entity.ID, &v); return v; }
			set { InternalCalls.CircleCollider2DComponent_SetOffset(Entity.ID, &value); }
		}

		public float Radius
		{
			get => InternalCalls.CircleCollider2DComponent_GetRadius(Entity.ID);
			set => InternalCalls.CircleCollider2DComponent_SetRadius(Entity.ID, value);
		}

		public float Density
		{
			get => InternalCalls.CircleCollider2DComponent_GetDensity(Entity.ID);
			set => InternalCalls.CircleCollider2DComponent_SetDensity(Entity.ID, value);
		}

		public float Friction
		{
			get => InternalCalls.CircleCollider2DComponent_GetFriction(Entity.ID);
			set => InternalCalls.CircleCollider2DComponent_SetFriction(Entity.ID, value);
		}

		public float Restitution
		{
			get => InternalCalls.CircleCollider2DComponent_GetRestitution(Entity.ID);
			set => InternalCalls.CircleCollider2DComponent_SetRestitution(Entity.ID, value);
		}
	}

	public unsafe class TextComponent : Component
	{
		public string Text
		{
			get => InternalCalls.TextComponent_GetText(Entity.ID);
			set => InternalCalls.TextComponent_SetText(Entity.ID, value);
		}

		public Vector4 Color
		{
			get { Vector4 c; InternalCalls.TextComponent_GetColor(Entity.ID, &c); return c; }
			set { InternalCalls.TextComponent_SetColor(Entity.ID, &value); }
		}

		public float Kerning
		{
			get => InternalCalls.TextComponent_GetKerning(Entity.ID);
			set => InternalCalls.TextComponent_SetKerning(Entity.ID, value);
		}

		public float LineSpacing
		{
			get => InternalCalls.TextComponent_GetLineSpacing(Entity.ID);
			set => InternalCalls.TextComponent_SetLineSpacing(Entity.ID, value);
		}

		public float MaxWidth
		{
			get => InternalCalls.TextComponent_GetMaxWidth(Entity.ID);
			set => InternalCalls.TextComponent_SetMaxWidth(Entity.ID, value);
		}
	}

	public enum ForceMode { Force = 0, Impulse, VelocityChange, Acceleration }

	public unsafe class RigidBodyComponent : Component
	{
		public Vector3 LinearVelocity
		{
			get { Vector3 v; InternalCalls.RigidBodyComponent_GetLinearVelocity(Entity.ID, &v); return v; }
			set { InternalCalls.RigidBodyComponent_SetLinearVelocity(Entity.ID, &value); }
		}

		public Vector3 AngularVelocity
		{
			get { Vector3 v; InternalCalls.RigidBodyComponent_GetAngularVelocity(Entity.ID, &v); return v; }
			set { InternalCalls.RigidBodyComponent_SetAngularVelocity(Entity.ID, &value); }
		}

		public Vector3 Translation
		{
			get { Vector3 v; InternalCalls.RigidBodyComponent_GetTranslation(Entity.ID, &v); return v; }
			set { InternalCalls.RigidBodyComponent_SetTranslation(Entity.ID, &value); }
		}

		public Vector3 Rotation
		{
			get { Vector3 v; InternalCalls.RigidBodyComponent_GetRotation(Entity.ID, &v); return v; }
			set { InternalCalls.RigidBodyComponent_SetRotation(Entity.ID, &value); }
		}

		public float Mass
		{
			get => InternalCalls.RigidBodyComponent_GetMass(Entity.ID);
			set => InternalCalls.RigidBodyComponent_SetMass(Entity.ID, value);
		}

		public bool IsSleeping => InternalCalls.RigidBodyComponent_IsSleeping(Entity.ID);

		public void SetGravityEnabled(bool enabled) => InternalCalls.RigidBodyComponent_SetGravityEnabled(Entity.ID, enabled);
		public void SetSleepState(bool sleep) => InternalCalls.RigidBodyComponent_SetSleepState(Entity.ID, sleep);

		public void AddForce(Vector3 force, ForceMode mode = ForceMode.Force, bool wake = true)
			=> InternalCalls.RigidBodyComponent_AddForce(Entity.ID, &force, mode, wake);

		public void AddForce(Vector3 force, Vector3 location, ForceMode mode = ForceMode.Force, bool wake = true)
			=> InternalCalls.RigidBodyComponent_AddForceAtLocation(Entity.ID, &force, &location, mode, wake);

		public void AddTorque(Vector3 torque, bool wake = true)
			=> InternalCalls.RigidBodyComponent_AddTorque(Entity.ID, &torque, wake);
	}

	public unsafe class CharacterControllerComponent : Component
	{
		public bool IsGrounded => InternalCalls.CharacterControllerComponent_IsGrounded(Entity.ID);

		public Vector3 LinearVelocity
		{
			get { Vector3 v; InternalCalls.CharacterControllerComponent_GetLinearVelocity(Entity.ID, &v); return v; }
			set { InternalCalls.CharacterControllerComponent_SetLinearVelocity(Entity.ID, &value); }
		}

		public void Move(Vector3 displacement) => InternalCalls.CharacterControllerComponent_Move(Entity.ID, &displacement);
		public void Jump(float power) => InternalCalls.CharacterControllerComponent_Jump(Entity.ID, power);
		public void SetGravityEnabled(bool enabled) => InternalCalls.CharacterControllerComponent_SetGravityEnabled(Entity.ID, enabled);
	}

	public unsafe class BoxColliderComponent : Component
	{
		public Vector3 HalfSize
		{
			get { Vector3 v; InternalCalls.BoxColliderComponent_GetHalfSize(Entity.ID, &v); return v; }
			set { InternalCalls.BoxColliderComponent_SetHalfSize(Entity.ID, &value); }
		}

		public Vector3 Offset
		{
			get { Vector3 v; InternalCalls.BoxColliderComponent_GetOffset(Entity.ID, &v); return v; }
			set { InternalCalls.BoxColliderComponent_SetOffset(Entity.ID, &value); }
		}
	}

	public unsafe class SphereColliderComponent : Component
	{
		public float Radius
		{
			get => InternalCalls.SphereColliderComponent_GetRadius(Entity.ID);
			set => InternalCalls.SphereColliderComponent_SetRadius(Entity.ID, value);
		}

		public Vector3 Offset
		{
			get { Vector3 v; InternalCalls.SphereColliderComponent_GetOffset(Entity.ID, &v); return v; }
			set { InternalCalls.SphereColliderComponent_SetOffset(Entity.ID, &value); }
		}
	}

	public unsafe class CapsuleColliderComponent : Component
	{
		public float Radius
		{
			get => InternalCalls.CapsuleColliderComponent_GetRadius(Entity.ID);
			set => InternalCalls.CapsuleColliderComponent_SetRadius(Entity.ID, value);
		}

		public float HalfHeight
		{
			get => InternalCalls.CapsuleColliderComponent_GetHalfHeight(Entity.ID);
			set => InternalCalls.CapsuleColliderComponent_SetHalfHeight(Entity.ID, value);
		}

		public Vector3 Offset
		{
			get { Vector3 v; InternalCalls.CapsuleColliderComponent_GetOffset(Entity.ID, &v); return v; }
			set { InternalCalls.CapsuleColliderComponent_SetOffset(Entity.ID, &value); }
		}
	}

	public enum AcousticMaterial
	{
		Default = 0,
		Brick = 1,
		Carpet = 2,
		Cloth = 3,
		Concrete = 4,
		ConcretePolished = 5,
		Dirt = 6,
		Glass = 7,
		Grass = 8,
		Gravel = 9,
		Marble = 10,
		Metal = 11,
		Plaster = 12,
		Plastic = 13,
		Rock = 14,
		Snow = 15,
		Soil = 16,
		Water = 17,
		Wood = 18,
		WoodThin = 19,
		Ceramic = 20,
		Rubber = 21,
		Foliage = 22,
	}

	public unsafe class MeshColliderComponent : Component
	{
		public AcousticGeometryMode AcousticMotion
		{
			get { Audio.RequireMainThread(); return (AcousticGeometryMode)InternalCalls.Audio_MeshGetMotion(Entity.ID); }
			set
			{
				Audio.RequireMainThread();
				if (!InternalCalls.Audio_MeshSetMotion(Entity.ID, (int)value))
					throw new ArgumentOutOfRangeException(nameof(value));
			}
		}
		/// <summary>Surface override, or this collider's authored acoustic material.</summary>
		public AcousticMaterial Material => (AcousticMaterial)InternalCalls.Audio_GetSurfaceMaterial(Entity.ID);
	}

	public unsafe class AudioSurfaceComponent : Component
	{
		/// <summary>Authored surface tag used by acoustic geometry.</summary>
		public AcousticMaterial Material => (AcousticMaterial)InternalCalls.Audio_GetSurfaceMaterial(Entity.ID);
	}
	public unsafe class AudioSourceComponent : Component
	{
		public bool IsPlaying => InternalCalls.Audio_SourceIsPlaying(Entity.ID);
		public bool IsPaused
		{
			get => InternalCalls.Audio_SourceIsPaused(Entity.ID);
			set => InternalCalls.Audio_SourceSetPaused(Entity.ID, value);
		}
		public float Volume
		{
			get => InternalCalls.Audio_SourceGetVolume(Entity.ID);
			set { Audio.Finite(value); InternalCalls.Audio_SourceSetVolume(Entity.ID, value); }
		}
		public float Pitch
		{
			get => InternalCalls.Audio_SourceGetPitch(Entity.ID);
			set { Audio.Finite(value); InternalCalls.Audio_SourceSetPitch(Entity.ID, value); }
		}
		public void Play() => InternalCalls.Audio_SourcePlay(Entity.ID);
		public void Stop(bool allowFadeOut = true) => InternalCalls.Audio_SourceStop(Entity.ID, allowFadeOut);
		public void Restart() { Stop(false); Play(); }
		private void RequireEvent()
		{
			if (!InternalCalls.Audio_SourceHasEvent(Entity.ID))
				throw new System.InvalidOperationException("This operation requires an assigned Studio event.");
		}
		public void SetParameter(string name, float value)
		{
			RequireEvent(); Audio.Finite(value);
			using NativeString parameter = Audio.String(name);
			InternalCalls.Audio_SourceSetParameter(Entity.ID, parameter, value);
		}
		public float GetParameter(string name)
		{
			RequireEvent();
			using NativeString parameter = Audio.String(name);
			return InternalCalls.Audio_SourceGetParameter(Entity.ID, parameter);
		}
		public void SetParameterLabel(string name, string label)
		{
			RequireEvent();
			using NativeString parameter = Audio.String(name);
			using NativeString nativeLabel = Audio.String(label);
			InternalCalls.Audio_SourceSetParameterLabel(Entity.ID, parameter, nativeLabel);
		}
		public void SetEvent(string guidOrPath)
		{
			using NativeString reference = Audio.String(guidOrPath);
			InternalCalls.Audio_SourceSetEvent(Entity.ID, reference);
		}
		public int TimelinePosition
		{
			get { RequireEvent(); return InternalCalls.Audio_SourceGetTimeline(Entity.ID); }
			set
			{
				RequireEvent();
				if (value < 0) throw new System.ArgumentOutOfRangeException(nameof(value));
				InternalCalls.Audio_SourceSetTimeline(Entity.ID, value);
			}
		}
	}

	public unsafe class AudioListenerComponent : Component
	{
		public bool Active
		{
			get => InternalCalls.Audio_ListenerGetActive(Entity.ID);
			set => InternalCalls.Audio_ListenerSetActive(Entity.ID, value);
		}
		public int ListenerIndex
		{
			get => InternalCalls.Audio_ListenerGetIndex(Entity.ID);
			set
			{
				if (value < 0 || value > 7) throw new System.ArgumentOutOfRangeException(nameof(value));
				InternalCalls.Audio_ListenerSetIndex(Entity.ID, value);
			}
		}
		public float Weight
		{
			get => InternalCalls.Audio_ListenerGetWeight(Entity.ID);
			set { Audio.Finite(value); InternalCalls.Audio_ListenerSetWeight(Entity.ID, value); }
		}
		public bool UseAttenuationTarget
		{
			get => InternalCalls.Audio_ListenerGetUseTarget(Entity.ID);
			set => InternalCalls.Audio_ListenerSetUseTarget(Entity.ID, value);
		}
		public Entity? AttenuationTarget
		{
			get { ulong id = InternalCalls.Audio_ListenerGetTarget(Entity.ID); return id == 0 ? null : new Entity(id); }
			set => InternalCalls.Audio_ListenerSetTarget(Entity.ID, value?.ID ?? 0);
		}
	}
}


namespace Lux
{
	public enum AudioZoneShape { Box, Sphere, Collider }

	/// <summary>Scene-owned ambience and snapshot zone. Properties are main-thread only.
	/// Collider mode uses one box, sphere or capsule on this entity; its dimensions override the zone's.</summary>
	public unsafe class AudioZoneComponent : Component
	{
		private float Get(int field)
		{
			Audio.RequireMainThread();
			return InternalCalls.Audio_ZoneGetScalar(Entity.ID, field);
		}
		private void Set(int field, float value)
		{
			Audio.RequireMainThread();
			Audio.Finite(value);
			if (!InternalCalls.Audio_ZoneSetScalar(Entity.ID, field, value))
				throw new ArgumentOutOfRangeException(nameof(value), "Invalid audio zone setting or unavailable component.");
		}
		public bool Enabled { get => Get(0) != 0; set => Set(0, value ? 1 : 0); }
		public AudioZoneShape Shape { get => (AudioZoneShape)(int)Get(1); set => Set(1, (int)value); }
		public float Priority { get => Get(2); set => Set(2, value); }
		public float BlendDistance { get => Get(3); set => Set(3, value); }
		public float FadeTime { get => Get(4); set => Set(4, value); }
		public float Volume { get => Get(5); set => Set(5, value); }
		public float Radius { get => Get(6); set => Set(6, value); }
		public float Weight => Get(7);
		private Vector3 GetVector(bool extents)
		{
			Audio.RequireMainThread();
			Vector3 result;
			InternalCalls.Audio_ZoneGetVector(Entity.ID, extents, &result);
			return result;
		}
		private void SetVector(bool extents, Vector3 value)
		{
			Audio.RequireMainThread();
			if (!InternalCalls.Audio_ZoneSetVector(Entity.ID, extents, &value))
				throw new ArgumentOutOfRangeException(nameof(value), "Zone vectors must be finite; half extents must be positive.");
		}
		public Vector3 Offset { get => GetVector(false); set => SetVector(false, value); }
		public Vector3 HalfExtents { get => GetVector(true); set => SetVector(true, value); }
		private void SetEvent(bool snapshot, string reference)
		{
			Audio.RequireMainThread();
			ArgumentNullException.ThrowIfNull(reference);
			using NativeString text = reference;
			if (!InternalCalls.Audio_ZoneSetEvent(Entity.ID, snapshot, text))
				throw new InvalidOperationException("Zone reference is unavailable or has the wrong type. Check loaded banks and the Audio log.");
		}
		/// <summary>Assign a looping event GUID/path; an empty string clears it.</summary>
		public void SetAmbience(string reference) => SetEvent(false, reference);
		/// <summary>Assign a snapshot GUID/path with exposed Intensity; an empty string clears it.</summary>
		public void SetSnapshot(string reference) => SetEvent(true, reference);
	}
}
