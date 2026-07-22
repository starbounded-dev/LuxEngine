using System;

using Coral.Managed.Interop;

namespace Lux
{
	// Static function-pointer fields filled in by native ScriptGlue::RegisterGlue via
	// AddInternalCall + UploadInternalCalls. Field names MUST match the second argument to
	// AddInternalCall on the native side ("Lux.InternalCalls" + field name).
	internal static unsafe class InternalCalls
	{
#pragma warning disable CS0649 // assigned from native code

		#region Log
		internal static delegate*<NativeString, int, void> NativeLog;
		#endregion

		#region Entity
		internal static delegate*<ulong, IntPtr> GetScriptInstance;
		internal static delegate*<ulong, ReflectionType, Bool32> Entity_HasComponent;
		internal static delegate*<ulong, ReflectionType, void> Entity_AddComponent;
		internal static delegate*<ulong, ReflectionType, void> Entity_RemoveComponent;
		internal static delegate*<NativeString, ulong> Entity_FindEntityByName;
		internal static delegate*<ulong, NativeString> Entity_GetName;
		#endregion

		#region TransformComponent
		internal static delegate*<ulong, Vector3*, void> TransformComponent_GetTranslation;
		internal static delegate*<ulong, Vector3*, void> TransformComponent_SetTranslation;
		internal static delegate*<ulong, Vector3*, void> TransformComponent_GetScale;
		internal static delegate*<ulong, Vector3*, void> TransformComponent_SetScale;
		#endregion

		#region RigidBody2DComponent
		internal static delegate*<ulong, Vector2*, Vector2*, Bool32, void> RigidBody2DComponent_ApplyLinearImpulse;
		internal static delegate*<ulong, Vector2*, Bool32, void> RigidBody2DComponent_ApplyLinearImpulseToCenter;
		internal static delegate*<ulong, Vector2*, void> RigidBody2DComponent_GetLinearVelocity;
		internal static delegate*<ulong, RigidBody2DComponent.BodyType> RigidBody2DComponent_GetType;
		internal static delegate*<ulong, RigidBody2DComponent.BodyType, void> RigidBody2DComponent_SetType;
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
		#endregion

		#region Input
		internal static delegate*<KeyCode, Bool32> Input_IsKeyDown;
		internal static delegate*<MouseButton, Bool32> Input_IsMouseButtonDown;
		#endregion

#pragma warning restore CS0649
	}
}
