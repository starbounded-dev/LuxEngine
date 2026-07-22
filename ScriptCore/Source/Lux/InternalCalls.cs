using System;

using Coral.Managed.Interop;

namespace Lux
{
	// Static function-pointer fields filled in by native ScriptGlue::RegisterGlue. Field names
	// MUST match the second argument of AddInternalCall on the native side.
	internal static unsafe class InternalCalls
	{
#pragma warning disable CS0649 // assigned from native code

		#region Log
		internal static delegate*<NativeString, int, void> NativeLog;
		#endregion

		#region Input
		internal static delegate*<KeyCode, Bool32> Input_IsKeyDown;
		internal static delegate*<KeyCode, Bool32> Input_IsKeyPressed;
		internal static delegate*<KeyCode, Bool32> Input_IsKeyHeld;
		internal static delegate*<KeyCode, Bool32> Input_IsKeyReleased;
		internal static delegate*<MouseButton, Bool32> Input_IsMouseButtonDown;
		internal static delegate*<MouseButton, Bool32> Input_IsMouseButtonPressed;
		internal static delegate*<MouseButton, Bool32> Input_IsMouseButtonHeld;
		internal static delegate*<MouseButton, Bool32> Input_IsMouseButtonReleased;
		internal static delegate*<float> Input_GetMouseX;
		internal static delegate*<float> Input_GetMouseY;
		internal static delegate*<Vector2*, void> Input_GetMousePosition;
		internal static delegate*<float, float, void> Input_SetMousePosition;
		internal static delegate*<CursorMode, void> Input_SetCursorMode;
		internal static delegate*<int, Bool32> Input_IsControllerPresent;
		internal static delegate*<int, int, Bool32> Input_IsControllerButtonDown;
		internal static delegate*<int, int, float> Input_GetControllerAxis;
		#endregion

		#region Scene
		internal static delegate*<NativeString, ulong> Scene_CreateEntity;
		internal static delegate*<ulong, void> Scene_DestroyEntity;
		#endregion

		#region Entity
		internal static delegate*<ulong, IntPtr> GetScriptInstance;
		internal static delegate*<ulong, ReflectionType, Bool32> Entity_HasComponent;
		internal static delegate*<ulong, ReflectionType, void> Entity_AddComponent;
		internal static delegate*<ulong, ReflectionType, void> Entity_RemoveComponent;
		internal static delegate*<NativeString, ulong> Entity_FindEntityByName;
		internal static delegate*<ulong, NativeString> Entity_GetName;
		internal static delegate*<ulong, NativeString, void> Entity_SetName;
		internal static delegate*<ulong, ulong> Entity_GetParent;
		internal static delegate*<ulong, ulong, void> Entity_SetParent;
		internal static delegate*<ulong, NativeArray<ulong>> Entity_GetChildren;
		#endregion

		#region TransformComponent
		internal static delegate*<ulong, Vector3*, void> TransformComponent_GetTranslation;
		internal static delegate*<ulong, Vector3*, void> TransformComponent_SetTranslation;
		internal static delegate*<ulong, Vector3*, void> TransformComponent_GetRotation;
		internal static delegate*<ulong, Vector3*, void> TransformComponent_SetRotation;
		internal static delegate*<ulong, Vector3*, void> TransformComponent_GetScale;
		internal static delegate*<ulong, Vector3*, void> TransformComponent_SetScale;
		#endregion

		#region TagComponent
		internal static delegate*<ulong, NativeString> TagComponent_GetTag;
		internal static delegate*<ulong, NativeString, void> TagComponent_SetTag;
		#endregion

		#region SpriteRendererComponent
		internal static delegate*<ulong, Vector4*, void> SpriteRendererComponent_GetColor;
		internal static delegate*<ulong, Vector4*, void> SpriteRendererComponent_SetColor;
		internal static delegate*<ulong, float> SpriteRendererComponent_GetTilingFactor;
		internal static delegate*<ulong, float, void> SpriteRendererComponent_SetTilingFactor;
		internal static delegate*<ulong, ulong> SpriteRendererComponent_GetTexture;
		internal static delegate*<ulong, ulong, void> SpriteRendererComponent_SetTexture;
		#endregion

		#region CircleRendererComponent
		internal static delegate*<ulong, Vector4*, void> CircleRendererComponent_GetColor;
		internal static delegate*<ulong, Vector4*, void> CircleRendererComponent_SetColor;
		internal static delegate*<ulong, float> CircleRendererComponent_GetThickness;
		internal static delegate*<ulong, float, void> CircleRendererComponent_SetThickness;
		internal static delegate*<ulong, float> CircleRendererComponent_GetFade;
		internal static delegate*<ulong, float, void> CircleRendererComponent_SetFade;
		#endregion

		#region CameraComponent
		internal static delegate*<ulong, Bool32> CameraComponent_GetPrimary;
		internal static delegate*<ulong, Bool32, void> CameraComponent_SetPrimary;
		#endregion

		#region RigidBody2DComponent
		internal static delegate*<ulong, Vector2*, Vector2*, Bool32, void> RigidBody2DComponent_ApplyLinearImpulse;
		internal static delegate*<ulong, Vector2*, Bool32, void> RigidBody2DComponent_ApplyLinearImpulseToCenter;
		internal static delegate*<ulong, Vector2*, Vector2*, Bool32, void> RigidBody2DComponent_ApplyForce;
		internal static delegate*<ulong, Vector2*, Bool32, void> RigidBody2DComponent_ApplyForceToCenter;
		internal static delegate*<ulong, float, Bool32, void> RigidBody2DComponent_ApplyTorque;
		internal static delegate*<ulong, Vector2*, void> RigidBody2DComponent_GetLinearVelocity;
		internal static delegate*<ulong, Vector2*, void> RigidBody2DComponent_SetLinearVelocity;
		internal static delegate*<ulong, float> RigidBody2DComponent_GetAngularVelocity;
		internal static delegate*<ulong, float, void> RigidBody2DComponent_SetAngularVelocity;
		internal static delegate*<ulong, Vector2*, void> RigidBody2DComponent_GetPosition;
		internal static delegate*<ulong, Vector2*, void> RigidBody2DComponent_SetPosition;
		internal static delegate*<ulong, float> RigidBody2DComponent_GetRotation;
		internal static delegate*<ulong, float, void> RigidBody2DComponent_SetRotation;
		internal static delegate*<ulong, float> RigidBody2DComponent_GetMass;
		internal static delegate*<ulong, float> RigidBody2DComponent_GetGravityScale;
		internal static delegate*<ulong, float, void> RigidBody2DComponent_SetGravityScale;
		internal static delegate*<ulong, RigidBody2DComponent.BodyType> RigidBody2DComponent_GetType;
		internal static delegate*<ulong, RigidBody2DComponent.BodyType, void> RigidBody2DComponent_SetType;
		#endregion

		#region BoxCollider2DComponent
		internal static delegate*<ulong, Vector2*, void> BoxCollider2DComponent_GetOffset;
		internal static delegate*<ulong, Vector2*, void> BoxCollider2DComponent_SetOffset;
		internal static delegate*<ulong, Vector2*, void> BoxCollider2DComponent_GetSize;
		internal static delegate*<ulong, Vector2*, void> BoxCollider2DComponent_SetSize;
		internal static delegate*<ulong, float> BoxCollider2DComponent_GetDensity;
		internal static delegate*<ulong, float, void> BoxCollider2DComponent_SetDensity;
		internal static delegate*<ulong, float> BoxCollider2DComponent_GetFriction;
		internal static delegate*<ulong, float, void> BoxCollider2DComponent_SetFriction;
		internal static delegate*<ulong, float> BoxCollider2DComponent_GetRestitution;
		internal static delegate*<ulong, float, void> BoxCollider2DComponent_SetRestitution;
		#endregion

		#region CircleCollider2DComponent
		internal static delegate*<ulong, Vector2*, void> CircleCollider2DComponent_GetOffset;
		internal static delegate*<ulong, Vector2*, void> CircleCollider2DComponent_SetOffset;
		internal static delegate*<ulong, float> CircleCollider2DComponent_GetRadius;
		internal static delegate*<ulong, float, void> CircleCollider2DComponent_SetRadius;
		internal static delegate*<ulong, float> CircleCollider2DComponent_GetDensity;
		internal static delegate*<ulong, float, void> CircleCollider2DComponent_SetDensity;
		internal static delegate*<ulong, float> CircleCollider2DComponent_GetFriction;
		internal static delegate*<ulong, float, void> CircleCollider2DComponent_SetFriction;
		internal static delegate*<ulong, float> CircleCollider2DComponent_GetRestitution;
		internal static delegate*<ulong, float, void> CircleCollider2DComponent_SetRestitution;
		#endregion

		#region TextComponent
		internal static delegate*<ulong, NativeString> TextComponent_GetText;
		internal static delegate*<ulong, NativeString, void> TextComponent_SetText;
		internal static delegate*<ulong, Vector4*, void> TextComponent_GetColor;
		internal static delegate*<ulong, Vector4*, void> TextComponent_SetColor;
		internal static delegate*<ulong, float> TextComponent_GetKerning;
		internal static delegate*<ulong, float, void> TextComponent_SetKerning;
		internal static delegate*<ulong, float> TextComponent_GetLineSpacing;
		internal static delegate*<ulong, float, void> TextComponent_SetLineSpacing;
		internal static delegate*<ulong, float> TextComponent_GetMaxWidth;
		internal static delegate*<ulong, float, void> TextComponent_SetMaxWidth;
		#endregion

		#region RigidBodyComponent (3D)
		internal static delegate*<ulong, Vector3*, ForceMode, Bool32, void> RigidBodyComponent_AddForce;
		internal static delegate*<ulong, Vector3*, Vector3*, ForceMode, Bool32, void> RigidBodyComponent_AddForceAtLocation;
		internal static delegate*<ulong, Vector3*, Bool32, void> RigidBodyComponent_AddTorque;
		internal static delegate*<ulong, Vector3*, void> RigidBodyComponent_GetLinearVelocity;
		internal static delegate*<ulong, Vector3*, void> RigidBodyComponent_SetLinearVelocity;
		internal static delegate*<ulong, Vector3*, void> RigidBodyComponent_GetAngularVelocity;
		internal static delegate*<ulong, Vector3*, void> RigidBodyComponent_SetAngularVelocity;
		internal static delegate*<ulong, Vector3*, void> RigidBodyComponent_GetTranslation;
		internal static delegate*<ulong, Vector3*, void> RigidBodyComponent_SetTranslation;
		internal static delegate*<ulong, Vector3*, void> RigidBodyComponent_GetRotation;
		internal static delegate*<ulong, Vector3*, void> RigidBodyComponent_SetRotation;
		internal static delegate*<ulong, float> RigidBodyComponent_GetMass;
		internal static delegate*<ulong, float, void> RigidBodyComponent_SetMass;
		internal static delegate*<ulong, Bool32, void> RigidBodyComponent_SetGravityEnabled;
		internal static delegate*<ulong, Bool32> RigidBodyComponent_IsSleeping;
		internal static delegate*<ulong, Bool32, void> RigidBodyComponent_SetSleepState;
		#endregion

		#region CharacterControllerComponent
		internal static delegate*<ulong, Vector3*, void> CharacterControllerComponent_Move;
		internal static delegate*<ulong, float, void> CharacterControllerComponent_Jump;
		internal static delegate*<ulong, Bool32> CharacterControllerComponent_IsGrounded;
		internal static delegate*<ulong, Vector3*, void> CharacterControllerComponent_GetLinearVelocity;
		internal static delegate*<ulong, Vector3*, void> CharacterControllerComponent_SetLinearVelocity;
		internal static delegate*<ulong, Bool32, void> CharacterControllerComponent_SetGravityEnabled;
		#endregion

		#region 3D Colliders
		internal static delegate*<ulong, Vector3*, void> BoxColliderComponent_GetHalfSize;
		internal static delegate*<ulong, Vector3*, void> BoxColliderComponent_SetHalfSize;
		internal static delegate*<ulong, Vector3*, void> BoxColliderComponent_GetOffset;
		internal static delegate*<ulong, Vector3*, void> BoxColliderComponent_SetOffset;
		internal static delegate*<ulong, float> SphereColliderComponent_GetRadius;
		internal static delegate*<ulong, float, void> SphereColliderComponent_SetRadius;
		internal static delegate*<ulong, Vector3*, void> SphereColliderComponent_GetOffset;
		internal static delegate*<ulong, Vector3*, void> SphereColliderComponent_SetOffset;
		internal static delegate*<ulong, float> CapsuleColliderComponent_GetRadius;
		internal static delegate*<ulong, float, void> CapsuleColliderComponent_SetRadius;
		internal static delegate*<ulong, float> CapsuleColliderComponent_GetHalfHeight;
		internal static delegate*<ulong, float, void> CapsuleColliderComponent_SetHalfHeight;
		internal static delegate*<ulong, Vector3*, void> CapsuleColliderComponent_GetOffset;
		internal static delegate*<ulong, Vector3*, void> CapsuleColliderComponent_SetOffset;
		#endregion

#pragma warning restore CS0649
	}
}
