using Coral.Managed.Interop;

namespace Lux
{
	public abstract class Component
	{
		public Entity Entity { get; internal set; }
	}

	public class TransformComponent : Component
	{
		public unsafe Vector3 Translation
		{
			get
			{
				Vector3 translation;
				InternalCalls.TransformComponent_GetTranslation(Entity.ID, &translation);
				return translation;
			}
			set { InternalCalls.TransformComponent_SetTranslation(Entity.ID, &value); }
		}

		public unsafe Vector3 Scale
		{
			get
			{
				Vector3 scale;
				InternalCalls.TransformComponent_GetScale(Entity.ID, &scale);
				return scale;
			}
			set { InternalCalls.TransformComponent_SetScale(Entity.ID, &value); }
		}
	}

	public class RigidBody2DComponent : Component
	{
		public enum BodyType { Static = 0, Dynamic, Kinematic }

		public unsafe Vector2 LinearVelocity
		{
			get
			{
				Vector2 velocity;
				InternalCalls.RigidBody2DComponent_GetLinearVelocity(Entity.ID, &velocity);
				return velocity;
			}
		}

		public unsafe BodyType Type
		{
			get => InternalCalls.RigidBody2DComponent_GetType(Entity.ID);
			set => InternalCalls.RigidBody2DComponent_SetType(Entity.ID, value);
		}

		public unsafe void ApplyLinearImpulse(Vector2 impulse, Vector2 worldPosition, bool wake)
		{
			InternalCalls.RigidBody2DComponent_ApplyLinearImpulse(Entity.ID, &impulse, &worldPosition, wake);
		}

		public unsafe void ApplyLinearImpulse(Vector2 impulse, bool wake)
		{
			InternalCalls.RigidBody2DComponent_ApplyLinearImpulseToCenter(Entity.ID, &impulse, wake);
		}
	}

	public unsafe class TextComponent : Component
	{
		public string Text
		{
			get => InternalCalls.TextComponent_GetText(Entity.ID);
			set => InternalCalls.TextComponent_SetText(Entity.ID, value);
		}

		public unsafe Vector4 Color
		{
			get
			{
				Vector4 color;
				InternalCalls.TextComponent_GetColor(Entity.ID, &color);
				return color;
			}
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
	}

	// Query/add/remove-able components with no scriptable surface yet. Each must have a matching
	// native component registered in ScriptGlue::RegisterComponentTypes.
	public class SpriteRendererComponent : Component { }
	public class CircleRendererComponent : Component { }
	public class CameraComponent : Component { }
	public class BoxCollider2DComponent : Component { }
	public class CircleCollider2DComponent : Component { }
	public class AudioSourceComponent : Component { }
	public class AudioListenerComponent : Component { }
}
