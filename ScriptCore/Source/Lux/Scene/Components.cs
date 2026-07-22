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

	// Registered for HasComponent/AddComponent/RemoveComponent; no scriptable surface yet.
	public class AudioSourceComponent : Component { }
	public class AudioListenerComponent : Component { }
}
