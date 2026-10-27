// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

using System;
using Coral.Managed.Interop;

namespace Lux
{
	/// <summary>
	/// The base of the component classes scripts read and change. Get one with <see cref="Lux.Entity.GetComponent{T}"/>;
	/// it stays bound to its entity.
	/// </summary>
	public abstract class Component
	{
		/// <summary>The entity this component belongs to.</summary>
		public Entity Entity { get; internal set; }
	}

	/// <summary>Position, rotation and scale relative to the parent.</summary>
	public unsafe class TransformComponent : Component
	{
		/// <summary>Position relative to the parent, in meters.</summary>
		public Vector3 Translation
		{
			get { Vector3 r; InternalCalls.TransformComponent_GetTranslation(Entity.ID, &r); return r; }
			set { InternalCalls.TransformComponent_SetTranslation(Entity.ID, &value); }
		}

		/// <summary>Rotation relative to the parent, as Euler angles in radians.</summary>
		public Vector3 Rotation
		{
			get { Vector3 r; InternalCalls.TransformComponent_GetRotation(Entity.ID, &r); return r; }
			set { InternalCalls.TransformComponent_SetRotation(Entity.ID, &value); }
		}

		/// <summary>Scale relative to the parent.</summary>
		public Vector3 Scale
		{
			get { Vector3 r; InternalCalls.TransformComponent_GetScale(Entity.ID, &r); return r; }
			set { InternalCalls.TransformComponent_SetScale(Entity.ID, &value); }
		}
	}

	/// <summary>The entity's name.</summary>
	public unsafe class TagComponent : Component
	{
		/// <summary>The name, as shown in the Scene Hierarchy.</summary>
		public string Tag
		{
			get => InternalCalls.TagComponent_GetTag(Entity.ID);
			set => InternalCalls.TagComponent_SetTag(Entity.ID, value);
		}
	}

	/// <summary>Draws a textured or colored quad (2D).</summary>
	public unsafe class SpriteRendererComponent : Component
	{
		/// <summary>Tint (RGBA, 0 to 1), multiplied with the texture.</summary>
		public Vector4 Color
		{
			get { Vector4 c; InternalCalls.SpriteRendererComponent_GetColor(Entity.ID, &c); return c; }
			set { InternalCalls.SpriteRendererComponent_SetColor(Entity.ID, &value); }
		}

		/// <summary>How many times the texture repeats across the quad.</summary>
		public float TilingFactor
		{
			get => InternalCalls.SpriteRendererComponent_GetTilingFactor(Entity.ID);
			set => InternalCalls.SpriteRendererComponent_SetTilingFactor(Entity.ID, value);
		}

		/// <summary>The texture, or null for a plain colored quad.</summary>
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

	/// <summary>Draws a circle or ring (2D).</summary>
	public unsafe class CircleRendererComponent : Component
	{
		/// <summary>Color (RGBA, 0 to 1).</summary>
		public Vector4 Color
		{
			get { Vector4 c; InternalCalls.CircleRendererComponent_GetColor(Entity.ID, &c); return c; }
			set { InternalCalls.CircleRendererComponent_SetColor(Entity.ID, &value); }
		}

		/// <summary>Ring thickness as a fraction of the radius: 1 is a filled disc.</summary>
		public float Thickness
		{
			get => InternalCalls.CircleRendererComponent_GetThickness(Entity.ID);
			set => InternalCalls.CircleRendererComponent_SetThickness(Entity.ID, value);
		}

		/// <summary>Edge softness; 0 is a hard edge.</summary>
		public float Fade
		{
			get => InternalCalls.CircleRendererComponent_GetFade(Entity.ID);
			set => InternalCalls.CircleRendererComponent_SetFade(Entity.ID, value);
		}
	}

	/// <summary>A camera the game can render from.</summary>
	public unsafe class CameraComponent : Component
	{
		/// <summary>Whether this is the camera the game renders from.</summary>
		public bool Primary
		{
			get => InternalCalls.CameraComponent_GetPrimary(Entity.ID);
			set => InternalCalls.CameraComponent_SetPrimary(Entity.ID, value);
		}
	}

	/// <summary>
	/// A 2D physics body (Box2D). Its shape comes from the entity's 2D colliders. The body exists only while the scene
	/// runs; outside Play the runtime values read as zero.
	/// </summary>
	public unsafe class RigidBody2DComponent : Component
	{
		/// <summary>How a 2D body moves.</summary>
		public enum BodyType { Static = 0, Dynamic, Kinematic }

		/// <summary>Static, dynamic (moved by forces) or kinematic (moved by velocity only).</summary>
		public BodyType Type
		{
			get => InternalCalls.RigidBody2DComponent_GetType(Entity.ID);
			set => InternalCalls.RigidBody2DComponent_SetType(Entity.ID, value);
		}

		/// <summary>Velocity in meters per second.</summary>
		public Vector2 LinearVelocity
		{
			get { Vector2 v; InternalCalls.RigidBody2DComponent_GetLinearVelocity(Entity.ID, &v); return v; }
			set { InternalCalls.RigidBody2DComponent_SetLinearVelocity(Entity.ID, &value); }
		}

		/// <summary>Spin in radians per second.</summary>
		public float AngularVelocity
		{
			get => InternalCalls.RigidBody2DComponent_GetAngularVelocity(Entity.ID);
			set => InternalCalls.RigidBody2DComponent_SetAngularVelocity(Entity.ID, value);
		}

		/// <summary>The body's world position; setting it teleports the body.</summary>
		public Vector2 Position
		{
			get { Vector2 p; InternalCalls.RigidBody2DComponent_GetPosition(Entity.ID, &p); return p; }
			set { InternalCalls.RigidBody2DComponent_SetPosition(Entity.ID, &value); }
		}

		/// <summary>The body's angle in radians; setting it teleports the body.</summary>
		public float Rotation
		{
			get => InternalCalls.RigidBody2DComponent_GetRotation(Entity.ID);
			set => InternalCalls.RigidBody2DComponent_SetRotation(Entity.ID, value);
		}

		/// <summary>The mass in kilograms, from the colliders' density and area.</summary>
		public float Mass => InternalCalls.RigidBody2DComponent_GetMass(Entity.ID);

		/// <summary>Multiplier on world gravity for this body.</summary>
		public float GravityScale
		{
			get => InternalCalls.RigidBody2DComponent_GetGravityScale(Entity.ID);
			set => InternalCalls.RigidBody2DComponent_SetGravityScale(Entity.ID, value);
		}

		/// <summary>Applies an instant change of momentum at a world point (which can also spin the body).</summary>
		/// <param name="wake">Whether to wake the body if it is asleep.</param>
		public void ApplyLinearImpulse(Vector2 impulse, Vector2 worldPosition, bool wake)
			=> InternalCalls.RigidBody2DComponent_ApplyLinearImpulse(Entity.ID, &impulse, &worldPosition, wake);

		/// <summary>Applies an instant change of momentum at the center of mass.</summary>
		/// <param name="wake">Whether to wake the body if it is asleep.</param>
		public void ApplyLinearImpulse(Vector2 impulse, bool wake)
			=> InternalCalls.RigidBody2DComponent_ApplyLinearImpulseToCenter(Entity.ID, &impulse, wake);

		/// <summary>Applies a force in newtons at a world point for this step.</summary>
		/// <param name="wake">Whether to wake the body if it is asleep.</param>
		public void ApplyForce(Vector2 force, Vector2 worldPosition, bool wake)
			=> InternalCalls.RigidBody2DComponent_ApplyForce(Entity.ID, &force, &worldPosition, wake);

		/// <summary>Applies a force in newtons at the center of mass for this step.</summary>
		/// <param name="wake">Whether to wake the body if it is asleep.</param>
		public void ApplyForce(Vector2 force, bool wake)
			=> InternalCalls.RigidBody2DComponent_ApplyForceToCenter(Entity.ID, &force, wake);

		/// <summary>Applies a torque in newton-meters for this step.</summary>
		/// <param name="wake">Whether to wake the body if it is asleep.</param>
		public void ApplyTorque(float torque, bool wake)
			=> InternalCalls.RigidBody2DComponent_ApplyTorque(Entity.ID, torque, wake);
	}

	/// <summary>A box shape for the entity's 2D rigid body.</summary>
	public unsafe class BoxCollider2DComponent : Component
	{
		/// <summary>The box's center relative to the entity.</summary>
		public Vector2 Offset
		{
			get { Vector2 v; InternalCalls.BoxCollider2DComponent_GetOffset(Entity.ID, &v); return v; }
			set { InternalCalls.BoxCollider2DComponent_SetOffset(Entity.ID, &value); }
		}

		/// <summary>Half the box's size, scaled by the entity's scale.</summary>
		public Vector2 Size
		{
			get { Vector2 v; InternalCalls.BoxCollider2DComponent_GetSize(Entity.ID, &v); return v; }
			set { InternalCalls.BoxCollider2DComponent_SetSize(Entity.ID, &value); }
		}

		/// <summary>Mass per area, in kilograms per square meter.</summary>
		public float Density
		{
			get => InternalCalls.BoxCollider2DComponent_GetDensity(Entity.ID);
			set => InternalCalls.BoxCollider2DComponent_SetDensity(Entity.ID, value);
		}

		/// <summary>Friction coefficient, usually 0 to 1.</summary>
		public float Friction
		{
			get => InternalCalls.BoxCollider2DComponent_GetFriction(Entity.ID);
			set => InternalCalls.BoxCollider2DComponent_SetFriction(Entity.ID, value);
		}

		/// <summary>Bounciness: 0 doesn't bounce, 1 bounces back fully.</summary>
		public float Restitution
		{
			get => InternalCalls.BoxCollider2DComponent_GetRestitution(Entity.ID);
			set => InternalCalls.BoxCollider2DComponent_SetRestitution(Entity.ID, value);
		}
	}

	/// <summary>A circle shape for the entity's 2D rigid body.</summary>
	public unsafe class CircleCollider2DComponent : Component
	{
		/// <summary>The circle's center relative to the entity.</summary>
		public Vector2 Offset
		{
			get { Vector2 v; InternalCalls.CircleCollider2DComponent_GetOffset(Entity.ID, &v); return v; }
			set { InternalCalls.CircleCollider2DComponent_SetOffset(Entity.ID, &value); }
		}

		/// <summary>The radius, scaled by the entity's scale.</summary>
		public float Radius
		{
			get => InternalCalls.CircleCollider2DComponent_GetRadius(Entity.ID);
			set => InternalCalls.CircleCollider2DComponent_SetRadius(Entity.ID, value);
		}

		/// <summary>Mass per area, in kilograms per square meter.</summary>
		public float Density
		{
			get => InternalCalls.CircleCollider2DComponent_GetDensity(Entity.ID);
			set => InternalCalls.CircleCollider2DComponent_SetDensity(Entity.ID, value);
		}

		/// <summary>Friction coefficient, usually 0 to 1.</summary>
		public float Friction
		{
			get => InternalCalls.CircleCollider2DComponent_GetFriction(Entity.ID);
			set => InternalCalls.CircleCollider2DComponent_SetFriction(Entity.ID, value);
		}

		/// <summary>Bounciness: 0 doesn't bounce, 1 bounces back fully.</summary>
		public float Restitution
		{
			get => InternalCalls.CircleCollider2DComponent_GetRestitution(Entity.ID);
			set => InternalCalls.CircleCollider2DComponent_SetRestitution(Entity.ID, value);
		}
	}

	/// <summary>Draws text in the world with an MSDF font.</summary>
	public unsafe class TextComponent : Component
	{
		/// <summary>The text; <c>\n</c> starts a new line.</summary>
		public string Text
		{
			get => InternalCalls.TextComponent_GetText(Entity.ID);
			set => InternalCalls.TextComponent_SetText(Entity.ID, value);
		}

		/// <summary>Color (RGBA, 0 to 1).</summary>
		public Vector4 Color
		{
			get { Vector4 c; InternalCalls.TextComponent_GetColor(Entity.ID, &c); return c; }
			set { InternalCalls.TextComponent_SetColor(Entity.ID, &value); }
		}

		/// <summary>Extra space between characters.</summary>
		public float Kerning
		{
			get => InternalCalls.TextComponent_GetKerning(Entity.ID);
			set => InternalCalls.TextComponent_SetKerning(Entity.ID, value);
		}

		/// <summary>Extra space between lines.</summary>
		public float LineSpacing
		{
			get => InternalCalls.TextComponent_GetLineSpacing(Entity.ID);
			set => InternalCalls.TextComponent_SetLineSpacing(Entity.ID, value);
		}

		/// <summary>Width at which lines wrap.</summary>
		public float MaxWidth
		{
			get => InternalCalls.TextComponent_GetMaxWidth(Entity.ID);
			set => InternalCalls.TextComponent_SetMaxWidth(Entity.ID, value);
		}
	}

	/// <summary>How <see cref="RigidBodyComponent.AddForce(Vector3, ForceMode, bool)"/> treats its vector.</summary>
	public enum ForceMode { Force = 0, Impulse, VelocityChange, Acceleration }

	/// <summary>
	/// A 3D physics body (Jolt). Its shape comes from the entity's colliders. The body exists only while the scene runs;
	/// outside Play the values read as zero. Moving a body from a script works best in <c>OnFixedUpdate</c>.
	/// </summary>
	public unsafe class RigidBodyComponent : Component
	{
		/// <summary>Velocity in meters per second.</summary>
		public Vector3 LinearVelocity
		{
			get { Vector3 v; InternalCalls.RigidBodyComponent_GetLinearVelocity(Entity.ID, &v); return v; }
			set { InternalCalls.RigidBodyComponent_SetLinearVelocity(Entity.ID, &value); }
		}

		/// <summary>Spin in radians per second around each axis.</summary>
		public Vector3 AngularVelocity
		{
			get { Vector3 v; InternalCalls.RigidBodyComponent_GetAngularVelocity(Entity.ID, &v); return v; }
			set { InternalCalls.RigidBodyComponent_SetAngularVelocity(Entity.ID, &value); }
		}

		/// <summary>The body's world position; setting it teleports the body.</summary>
		public Vector3 Translation
		{
			get { Vector3 v; InternalCalls.RigidBodyComponent_GetTranslation(Entity.ID, &v); return v; }
			set { InternalCalls.RigidBodyComponent_SetTranslation(Entity.ID, &value); }
		}

		/// <summary>The body's world rotation as Euler angles in radians; setting it teleports the body.</summary>
		public Vector3 Rotation
		{
			get { Vector3 v; InternalCalls.RigidBodyComponent_GetRotation(Entity.ID, &v); return v; }
			set { InternalCalls.RigidBodyComponent_SetRotation(Entity.ID, &value); }
		}

		/// <summary>The mass in kilograms.</summary>
		public float Mass
		{
			get => InternalCalls.RigidBodyComponent_GetMass(Entity.ID);
			set => InternalCalls.RigidBodyComponent_SetMass(Entity.ID, value);
		}

		/// <summary>Whether the body is asleep (at rest and not simulated until something wakes it).</summary>
		public bool IsSleeping => InternalCalls.RigidBodyComponent_IsSleeping(Entity.ID);

		/// <summary>Turns gravity on or off for this body.</summary>
		public void SetGravityEnabled(bool enabled) => InternalCalls.RigidBodyComponent_SetGravityEnabled(Entity.ID, enabled);
		/// <summary>Puts the body to sleep (true) or wakes it (false).</summary>
		public void SetSleepState(bool sleep) => InternalCalls.RigidBodyComponent_SetSleepState(Entity.ID, sleep);

		/// <summary>Pushes the body through its center of mass.</summary>
		/// <param name="force">The vector, in world space; its unit depends on <paramref name="mode"/>.</param>
		/// <param name="mode">How the vector is applied.</param>
		/// <param name="wake">Whether to wake the body if it is asleep.</param>
		public void AddForce(Vector3 force, ForceMode mode = ForceMode.Force, bool wake = true)
			=> InternalCalls.RigidBodyComponent_AddForce(Entity.ID, &force, mode, wake);

		/// <summary>Pushes the body at a world point, which can also spin it.</summary>
		/// <param name="force">The vector, in world space; its unit depends on <paramref name="mode"/>.</param>
		/// <param name="location">The world point to push at.</param>
		/// <param name="mode">How the vector is applied.</param>
		/// <param name="wake">Whether to wake the body if it is asleep.</param>
		public void AddForce(Vector3 force, Vector3 location, ForceMode mode = ForceMode.Force, bool wake = true)
			=> InternalCalls.RigidBodyComponent_AddForceAtLocation(Entity.ID, &force, &location, mode, wake);

		/// <summary>Applies a torque in newton-meters (world axes).</summary>
		/// <param name="wake">Whether to wake the body if it is asleep.</param>
		public void AddTorque(Vector3 torque, bool wake = true)
			=> InternalCalls.RigidBodyComponent_AddTorque(Entity.ID, &torque, wake);
	}

	/// <summary>
	/// A character that walks on the world instead of being pushed around by it (a Jolt virtual character): it climbs steps
	/// and slopes, rides moving platforms and stops at walls. It runs before each 3D physics step.
	/// </summary>
	public unsafe class CharacterControllerComponent : Component
	{
		/// <summary>Whether the character stands on something walkable.</summary>
		public bool IsGrounded => InternalCalls.CharacterControllerComponent_IsGrounded(Entity.ID);

		/// <summary>
		/// The character's velocity in meters per second. Setting it replaces the velocity for the next physics step.
		/// </summary>
		public Vector3 LinearVelocity
		{
			get { Vector3 v; InternalCalls.CharacterControllerComponent_GetLinearVelocity(Entity.ID, &v); return v; }
			set { InternalCalls.CharacterControllerComponent_SetLinearVelocity(Entity.ID, &value); }
		}

		/// <summary>
		/// Moves the character by this much (meters, world space) by the next physics step; call it every frame with
		/// <c>speed * ts</c>. While gravity is on only the horizontal part counts. In the air it is ignored unless the
		/// controller allows air control.
		/// </summary>
		public void Move(Vector3 displacement) => InternalCalls.CharacterControllerComponent_Move(Entity.ID, &displacement);
		/// <summary>Jumps on the next physics step, if the character is grounded.</summary>
		/// <param name="power">The upward speed in meters per second.</param>
		public void Jump(float power) => InternalCalls.CharacterControllerComponent_Jump(Entity.ID, power);
		/// <summary>Turns gravity on or off for the character (off: it flies).</summary>
		public void SetGravityEnabled(bool enabled) => InternalCalls.CharacterControllerComponent_SetGravityEnabled(Entity.ID, enabled);
	}

	/// <summary>A box shape for the entity's 3D rigid body or character.</summary>
	public unsafe class BoxColliderComponent : Component
	{
		/// <summary>Half the box's size on each axis.</summary>
		public Vector3 HalfSize
		{
			get { Vector3 v; InternalCalls.BoxColliderComponent_GetHalfSize(Entity.ID, &v); return v; }
			set { InternalCalls.BoxColliderComponent_SetHalfSize(Entity.ID, &value); }
		}

		/// <summary>The box's center relative to the entity.</summary>
		public Vector3 Offset
		{
			get { Vector3 v; InternalCalls.BoxColliderComponent_GetOffset(Entity.ID, &v); return v; }
			set { InternalCalls.BoxColliderComponent_SetOffset(Entity.ID, &value); }
		}
	}

	/// <summary>A sphere shape for the entity's 3D rigid body or character.</summary>
	public unsafe class SphereColliderComponent : Component
	{
		/// <summary>The radius.</summary>
		public float Radius
		{
			get => InternalCalls.SphereColliderComponent_GetRadius(Entity.ID);
			set => InternalCalls.SphereColliderComponent_SetRadius(Entity.ID, value);
		}

		/// <summary>The sphere's center relative to the entity.</summary>
		public Vector3 Offset
		{
			get { Vector3 v; InternalCalls.SphereColliderComponent_GetOffset(Entity.ID, &v); return v; }
			set { InternalCalls.SphereColliderComponent_SetOffset(Entity.ID, &value); }
		}
	}

	/// <summary>A capsule shape (along Y) for the entity's 3D rigid body or character.</summary>
	public unsafe class CapsuleColliderComponent : Component
	{
		/// <summary>The radius of the capsule's cylinder and caps.</summary>
		public float Radius
		{
			get => InternalCalls.CapsuleColliderComponent_GetRadius(Entity.ID);
			set => InternalCalls.CapsuleColliderComponent_SetRadius(Entity.ID, value);
		}

		/// <summary>Half the height of the cylinder part, without the caps.</summary>
		public float HalfHeight
		{
			get => InternalCalls.CapsuleColliderComponent_GetHalfHeight(Entity.ID);
			set => InternalCalls.CapsuleColliderComponent_SetHalfHeight(Entity.ID, value);
		}

		/// <summary>The capsule's center relative to the entity.</summary>
		public Vector3 Offset
		{
			get { Vector3 v; InternalCalls.CapsuleColliderComponent_GetOffset(Entity.ID, &v); return v; }
			set { InternalCalls.CapsuleColliderComponent_SetOffset(Entity.ID, &value); }
		}
	}

	/// <summary>What a surface is made of, for ray-traced acoustics and surface sounds.</summary>
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

	/// <summary>A collider shaped like a mesh; also acoustic geometry.</summary>
	public unsafe class MeshColliderComponent : Component
	{
		/// <summary>How the mesh takes part in ray-traced acoustics. Main thread only.</summary>
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

	/// <summary>Overrides the acoustic material and surface sounds of the entity's colliders.</summary>
	public unsafe class AudioSurfaceComponent : Component
	{
		/// <summary>Authored surface tag used by acoustic geometry.</summary>
		public AcousticMaterial Material => (AcousticMaterial)InternalCalls.Audio_GetSurfaceMaterial(Entity.ID);
	}
	/// <summary>Plays an FMOD Studio event from the entity.</summary>
	public unsafe class AudioSourceComponent : Component
	{
		/// <summary>FMOD voice priority: 0 is highest, 256 is lowest.</summary>
		public int Priority
		{
			get { Audio.RequireMainThread(); return InternalCalls.Audio_SourceGetPriority(Entity.ID); }
			set
			{
				Audio.RequireMainThread();
				if (!InternalCalls.Audio_SourceSetPriority(Entity.ID, value))
					throw new ArgumentOutOfRangeException(nameof(value));
			}
		}
		/// <summary>Release instances beyond every listener's authored event range.
		/// Loops suspend and resume; inaudible one-shots are discarded. Default false.</summary>
		public bool DistanceCulling
		{
			get { Audio.RequireMainThread(); return InternalCalls.Audio_SourceGetCulling(Entity.ID); }
			set { Audio.RequireMainThread(); InternalCalls.Audio_SourceSetCulling(Entity.ID, value); }
		}
		/// <summary>Whether <see cref="DistanceCulling"/> has released the event right now. Main thread only.</summary>
		public bool IsCulled
		{
			get { Audio.RequireMainThread(); return InternalCalls.Audio_SourceIsCulled(Entity.ID); }
		}
		/// <summary>Low-frequency occlusion applied to this source's Occlusion parameter this frame:
		/// 1 is a clear path, 0 fully blocked. 1 outside Play and while culled. Read-only; useful for
		/// gameplay such as AI hearing.</summary>
		public float OcclusionGain
		{
			get { Audio.RequireMainThread(); return InternalCalls.Audio_SourceGetOcclusion(Entity.ID); }
		}
		/// <summary>Whether the event is playing.</summary>
		public bool IsPlaying => InternalCalls.Audio_SourceIsPlaying(Entity.ID);
		/// <summary>Pauses or resumes the event from the script, on top of the scene's pause.</summary>
		public bool IsPaused
		{
			get => InternalCalls.Audio_SourceIsPaused(Entity.ID);
			set => InternalCalls.Audio_SourceSetPaused(Entity.ID, value);
		}
		/// <summary>Volume multiplier on top of the event's authored volume (1 is unchanged).</summary>
		public float Volume
		{
			get => InternalCalls.Audio_SourceGetVolume(Entity.ID);
			set { Audio.Finite(value); InternalCalls.Audio_SourceSetVolume(Entity.ID, value); }
		}
		/// <summary>Pitch multiplier (1 is unchanged).</summary>
		public float Pitch
		{
			get => InternalCalls.Audio_SourceGetPitch(Entity.ID);
			set { Audio.Finite(value); InternalCalls.Audio_SourceSetPitch(Entity.ID, value); }
		}
		/// <summary>Starts the event.</summary>
		public void Play() => InternalCalls.Audio_SourcePlay(Entity.ID);
		/// <summary>Stops the event.</summary>
		/// <param name="allowFadeOut">True lets the event's authored fade-out play; false cuts it.</param>
		public void Stop(bool allowFadeOut = true) => InternalCalls.Audio_SourceStop(Entity.ID, allowFadeOut);
		/// <summary>Stops the event at once and starts it again.</summary>
		public void Restart() { Stop(false); Play(); }
		private void RequireEvent()
		{
			if (!InternalCalls.Audio_SourceHasEvent(Entity.ID))
				throw new System.InvalidOperationException("This operation requires an assigned Studio event.");
		}
		/// <summary>Sets one of the event's parameters.</summary>
		/// <exception cref="System.InvalidOperationException">No event is assigned.</exception>
		public void SetParameter(string name, float value)
		{
			RequireEvent(); Audio.Finite(value);
			using NativeString parameter = Audio.String(name);
			InternalCalls.Audio_SourceSetParameter(Entity.ID, parameter, value);
		}
		/// <summary>Reads one of the event's parameters.</summary>
		/// <exception cref="System.InvalidOperationException">No event is assigned.</exception>
		public float GetParameter(string name)
		{
			RequireEvent();
			using NativeString parameter = Audio.String(name);
			return InternalCalls.Audio_SourceGetParameter(Entity.ID, parameter);
		}
		/// <summary>Sets a labeled parameter by its label.</summary>
		/// <exception cref="System.InvalidOperationException">No event is assigned.</exception>
		public void SetParameterLabel(string name, string label)
		{
			RequireEvent();
			using NativeString parameter = Audio.String(name);
			using NativeString nativeLabel = Audio.String(label);
			InternalCalls.Audio_SourceSetParameterLabel(Entity.ID, parameter, nativeLabel);
		}
		/// <summary>Assigns a different event; it plays on the next <see cref="Play"/>.</summary>
		/// <param name="guidOrPath">The event's GUID or path (<c>event:/...</c>). An unknown event leaves the current one.</param>
		public void SetEvent(string guidOrPath)
		{
			using NativeString reference = Audio.String(guidOrPath);
			InternalCalls.Audio_SourceSetEvent(Entity.ID, reference);
		}
		/// <summary>The event's timeline position in milliseconds; setting it seeks.</summary>
		/// <exception cref="System.InvalidOperationException">No event is assigned.</exception>
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

	/// <summary>
	/// Where the game hears from. Up to eight listeners (split screen) are blended by <see cref="Weight"/>; ray-traced
	/// acoustics use the heaviest one.
	/// </summary>
	public unsafe class AudioListenerComponent : Component
	{
		/// <summary>Whether this listener is in use.</summary>
		public bool Active
		{
			get => InternalCalls.Audio_ListenerGetActive(Entity.ID);
			set => InternalCalls.Audio_ListenerSetActive(Entity.ID, value);
		}
		/// <summary>The FMOD listener slot, 0 to 7.</summary>
		public int ListenerIndex
		{
			get => InternalCalls.Audio_ListenerGetIndex(Entity.ID);
			set
			{
				if (value < 0 || value > 7) throw new System.ArgumentOutOfRangeException(nameof(value));
				InternalCalls.Audio_ListenerSetIndex(Entity.ID, value);
			}
		}
		/// <summary>How much this listener counts when blending listeners, 0 to 1.</summary>
		public float Weight
		{
			get => InternalCalls.Audio_ListenerGetWeight(Entity.ID);
			set { Audio.Finite(value); InternalCalls.Audio_ListenerSetWeight(Entity.ID, value); }
		}
		/// <summary>Whether distance attenuation is measured from <see cref="AttenuationTarget"/>.</summary>
		public bool UseAttenuationTarget
		{
			get => InternalCalls.Audio_ListenerGetUseTarget(Entity.ID);
			set => InternalCalls.Audio_ListenerSetUseTarget(Entity.ID, value);
		}
		/// <summary>
		/// The entity distance attenuation is measured from (a third-person character, say) while the listener stays on the
		/// camera, or null.
		/// </summary>
		public Entity? AttenuationTarget
		{
			get { ulong id = InternalCalls.Audio_ListenerGetTarget(Entity.ID); return id == 0 ? null : new Entity(id); }
			set => InternalCalls.Audio_ListenerSetTarget(Entity.ID, value?.ID ?? 0);
		}
	}
}


namespace Lux
{
	/// <summary>The shape of an <see cref="AudioZoneComponent"/>.</summary>
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
		/// <summary>Whether the zone is active.</summary>
		public bool Enabled { get => Get(0) != 0; set => Set(0, value ? 1 : 0); }
		/// <summary>The zone's shape.</summary>
		public AudioZoneShape Shape { get => (AudioZoneShape)(int)Get(1); set => Set(1, (int)value); }
		/// <summary>Higher-priority zones claim the listener first where zones overlap.</summary>
		public float Priority { get => Get(2); set => Set(2, value); }
		/// <summary>Distance in meters over which the zone fades in at its edge.</summary>
		public float BlendDistance { get => Get(3); set => Set(3, value); }
		/// <summary>Seconds the zone takes to fade its ambience and snapshot in or out.</summary>
		public float FadeTime { get => Get(4); set => Set(4, value); }
		/// <summary>Ambience volume multiplier.</summary>
		public float Volume { get => Get(5); set => Set(5, value); }
		/// <summary>The radius of a sphere zone.</summary>
		public float Radius { get => Get(6); set => Set(6, value); }
		/// <summary>How much the zone is active right now, 0 to 1.</summary>
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
		/// <summary>The zone's center relative to the entity.</summary>
		public Vector3 Offset { get => GetVector(false); set => SetVector(false, value); }
		/// <summary>Half the size of a box zone.</summary>
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
