#include "lpch.h"
#include "ScriptGlue.h"
#include "ScriptEngine.h"

#include "Lux/Core/UUID.h"
#include "Lux/Core/KeyCodes.h"
#include "Lux/Core/Input.h"

#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Entity.h"

#include "Lux/Physics2D/ContactListener2D.h"

#include "Lux/Physics/PhysicsScene.h"
#include "Lux/Physics/PhysicsBody.h"
#include "Lux/Physics/CharacterController.h"
#include "Lux/Physics/PhysicsTypes.h"

#include <glm/gtc/quaternion.hpp>

#include <Coral/Assembly.hpp>
#include <Coral/Type.hpp>
#include <Coral/String.hpp>
#include <Coral/Array.hpp>

#include "box2d/b2_body.h"

#include <format>

#if !defined(LUX_PLATFORM_WINDOWS)
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace Lux {

	// Component dispatch maps, keyed by Coral::TypeId (== managed typeof(T) cache id).
	static std::unordered_map<Coral::TypeId, std::function<bool(Entity)>> s_HasComponentFuncs;
	static std::unordered_map<Coral::TypeId, std::function<void(Entity)>> s_AddComponentFuncs;
	static std::unordered_map<Coral::TypeId, std::function<void(Entity)>> s_RemoveComponentFuncs;

#define LUX_ADD_INTERNAL_CALL(Name) coreAssembly.AddInternalCall("Lux.InternalCalls", #Name, reinterpret_cast<void*>(&Name))

	static Scene* GetScene()
	{
		Scene* scene = ScriptEngine::GetInstance().GetCurrentScene().Raw();
		LUX_CORE_ASSERT(scene, "No active scene bound to ScriptEngine");
		return scene;
	}

	static Entity GetEntity(uint64_t entityID)
	{
		Entity entity = GetScene()->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		return entity;
	}

	static b2Body* GetRuntimeBody(uint64_t entityID)
	{
		Entity entity = GetEntity(entityID);
		if (!entity.HasComponent<RigidBody2DComponent>())
			return nullptr;
		return (b2Body*)entity.GetComponent<RigidBody2DComponent>().RuntimeBody;
	}

	#pragma region Log

	static void NativeLog(Coral::String message, int32_t level)
	{
		std::string str = message;
		switch (level)
		{
			case 0: LUX_CORE_TRACE("[Script] {}", str); break;
			case 1: LUX_CORE_INFO("[Script] {}", str); break;
			case 2: LUX_CORE_WARN("[Script] {}", str); break;
			default: LUX_CORE_ERROR("[Script] {}", str); break;
		}
	}

	#pragma endregion

	#pragma region Input

	static Coral::Bool32 Input_IsKeyDown(KeyCode keycode)        { return Input::IsKeyDown(keycode); }
	static Coral::Bool32 Input_IsKeyPressed(KeyCode keycode)     { return Input::IsKeyPressed(keycode); }
	static Coral::Bool32 Input_IsKeyHeld(KeyCode keycode)        { return Input::IsKeyHeld(keycode); }
	static Coral::Bool32 Input_IsKeyReleased(KeyCode keycode)    { return Input::IsKeyReleased(keycode); }

	static Coral::Bool32 Input_IsMouseButtonDown(MouseButton button)     { return Input::IsMouseButtonDown(button); }
	static Coral::Bool32 Input_IsMouseButtonPressed(MouseButton button)  { return Input::IsMouseButtonPressed(button); }
	static Coral::Bool32 Input_IsMouseButtonHeld(MouseButton button)     { return Input::IsMouseButtonHeld(button); }
	static Coral::Bool32 Input_IsMouseButtonReleased(MouseButton button) { return Input::IsMouseButtonReleased(button); }

	static float Input_GetMouseX() { return Input::GetMouseX(); }
	static float Input_GetMouseY() { return Input::GetMouseY(); }

	static void Input_GetMousePosition(glm::vec2* outPosition)
	{
		auto [x, y] = Input::GetMousePosition();
		*outPosition = { x, y };
	}

	static void Input_SetMousePosition(float x, float y) { Input::SetMousePosition(x, y); }
	static void Input_SetCursorMode(CursorMode mode)     { Input::SetCursorMode(mode); }

	static Coral::Bool32 Input_IsControllerPresent(int32_t id) { return Input::IsControllerPresent(id); }
	static Coral::Bool32 Input_IsControllerButtonDown(int32_t id, int32_t button) { return Input::IsControllerButtonDown(id, button); }
	static float Input_GetControllerAxis(int32_t id, int32_t axis) { return Input::GetControllerAxis(id, axis); }

	#pragma endregion

	#pragma region Scene

	static uint64_t Scene_CreateEntity(Coral::String name)
	{
		std::string nameStr = name;
		return GetScene()->CreateEntity(nameStr).GetUUID();
	}

	static void Scene_DestroyEntity(uint64_t entityID)
	{
		GetScene()->DestroyEntity(GetEntity(entityID));
	}

	#pragma endregion

	#pragma region Entity

	static void* GetScriptInstance(uint64_t entityID)
	{
		Scene* scene = GetScene();
		auto& storage = scene->GetScriptStorage();
		auto it = storage.EntityStorage.find(entityID);
		if (it == storage.EntityStorage.end() || !it->second.Instance)
			return nullptr;
		return it->second.Instance->m_Handle;
	}

	static Coral::Bool32 Entity_HasComponent(uint64_t entityID, Coral::ReflectionType componentType)
	{
		auto it = s_HasComponentFuncs.find(componentType.m_TypeID);
		if (it == s_HasComponentFuncs.end())
			return false;
		return it->second(GetEntity(entityID));
	}

	static void Entity_AddComponent(uint64_t entityID, Coral::ReflectionType componentType)
	{
		auto it = s_AddComponentFuncs.find(componentType.m_TypeID);
		if (it != s_AddComponentFuncs.end())
			it->second(GetEntity(entityID));
	}

	static void Entity_RemoveComponent(uint64_t entityID, Coral::ReflectionType componentType)
	{
		auto it = s_RemoveComponentFuncs.find(componentType.m_TypeID);
		if (it != s_RemoveComponentFuncs.end())
			it->second(GetEntity(entityID));
	}

	static uint64_t Entity_FindEntityByName(Coral::String name)
	{
		std::string nameStr = name;
		Entity entity = GetScene()->FindEntityByName(nameStr);
		return entity ? (uint64_t)entity.GetUUID() : 0;
	}

	static Coral::String Entity_GetName(uint64_t entityID)
	{
		return Coral::String::New(GetEntity(entityID).GetName());
	}

	static void Entity_SetName(uint64_t entityID, Coral::String name)
	{
		Entity entity = GetEntity(entityID);
		if (entity.HasComponent<TagComponent>())
			entity.GetComponent<TagComponent>().Tag = name;
	}

	static uint64_t Entity_GetParent(uint64_t entityID)
	{
		Entity parent = GetEntity(entityID).GetParent();
		return parent ? (uint64_t)parent.GetUUID() : 0;
	}

	static void Entity_SetParent(uint64_t entityID, uint64_t parentID)
	{
		Entity entity = GetEntity(entityID);
		if (parentID == 0)
			entity.SetParent({});
		else
			entity.SetParent(GetEntity(parentID));
	}

	static Coral::Array<uint64_t> Entity_GetChildren(uint64_t entityID)
	{
		const std::vector<UUID>& children = GetEntity(entityID).Children();
		Coral::Array<uint64_t> result = Coral::Array<uint64_t>::New((int32_t)children.size());
		for (size_t i = 0; i < children.size(); i++)
			result[i] = (uint64_t)children[i];
		return result;
	}

	#pragma endregion

	#pragma region TransformComponent

	static void TransformComponent_GetTranslation(uint64_t entityID, glm::vec3* out) { *out = GetEntity(entityID).GetComponent<TransformComponent>().Translation; }
	static void TransformComponent_SetTranslation(uint64_t entityID, glm::vec3* in)  { GetEntity(entityID).GetComponent<TransformComponent>().Translation = *in; }
	static void TransformComponent_GetRotation(uint64_t entityID, glm::vec3* out)    { *out = GetEntity(entityID).GetComponent<TransformComponent>().GetRotationEuler(); }
	static void TransformComponent_SetRotation(uint64_t entityID, glm::vec3* in)     { GetEntity(entityID).GetComponent<TransformComponent>().SetRotationEuler(*in); }
	static void TransformComponent_GetScale(uint64_t entityID, glm::vec3* out)       { *out = GetEntity(entityID).GetComponent<TransformComponent>().Scale; }
	static void TransformComponent_SetScale(uint64_t entityID, glm::vec3* in)        { GetEntity(entityID).GetComponent<TransformComponent>().Scale = *in; }

	#pragma endregion

	#pragma region TagComponent

	static Coral::String TagComponent_GetTag(uint64_t entityID) { return Coral::String::New(GetEntity(entityID).GetComponent<TagComponent>().Tag); }
	static void TagComponent_SetTag(uint64_t entityID, Coral::String tag) { GetEntity(entityID).GetComponent<TagComponent>().Tag = tag; }

	#pragma endregion

	#pragma region SpriteRendererComponent

	static void SpriteRendererComponent_GetColor(uint64_t entityID, glm::vec4* out) { *out = GetEntity(entityID).GetComponent<SpriteRendererComponent>().Color; }
	static void SpriteRendererComponent_SetColor(uint64_t entityID, glm::vec4* in)  { GetEntity(entityID).GetComponent<SpriteRendererComponent>().Color = *in; }
	static float SpriteRendererComponent_GetTilingFactor(uint64_t entityID)         { return GetEntity(entityID).GetComponent<SpriteRendererComponent>().TilingFactor; }
	static void SpriteRendererComponent_SetTilingFactor(uint64_t entityID, float v) { GetEntity(entityID).GetComponent<SpriteRendererComponent>().TilingFactor = v; }
	static uint64_t SpriteRendererComponent_GetTexture(uint64_t entityID)           { return (uint64_t)GetEntity(entityID).GetComponent<SpriteRendererComponent>().Texture; }
	static void SpriteRendererComponent_SetTexture(uint64_t entityID, uint64_t h)   { GetEntity(entityID).GetComponent<SpriteRendererComponent>().Texture = h; }

	#pragma endregion

	#pragma region CircleRendererComponent

	static void CircleRendererComponent_GetColor(uint64_t entityID, glm::vec4* out) { *out = GetEntity(entityID).GetComponent<CircleRendererComponent>().Color; }
	static void CircleRendererComponent_SetColor(uint64_t entityID, glm::vec4* in)  { GetEntity(entityID).GetComponent<CircleRendererComponent>().Color = *in; }
	static float CircleRendererComponent_GetThickness(uint64_t entityID)            { return GetEntity(entityID).GetComponent<CircleRendererComponent>().Thickness; }
	static void CircleRendererComponent_SetThickness(uint64_t entityID, float v)    { GetEntity(entityID).GetComponent<CircleRendererComponent>().Thickness = v; }
	static float CircleRendererComponent_GetFade(uint64_t entityID)                 { return GetEntity(entityID).GetComponent<CircleRendererComponent>().Fade; }
	static void CircleRendererComponent_SetFade(uint64_t entityID, float v)         { GetEntity(entityID).GetComponent<CircleRendererComponent>().Fade = v; }

	#pragma endregion

	#pragma region CameraComponent

	static Coral::Bool32 CameraComponent_GetPrimary(uint64_t entityID)         { return GetEntity(entityID).GetComponent<CameraComponent>().Primary; }
	static void CameraComponent_SetPrimary(uint64_t entityID, Coral::Bool32 v) { GetEntity(entityID).GetComponent<CameraComponent>().Primary = v; }

	#pragma endregion

	#pragma region RigidBody2DComponent

	static void RigidBody2DComponent_ApplyLinearImpulse(uint64_t entityID, glm::vec2* impulse, glm::vec2* point, Coral::Bool32 wake)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
			body->ApplyLinearImpulse(b2Vec2(impulse->x, impulse->y), b2Vec2(point->x, point->y), wake);
	}

	static void RigidBody2DComponent_ApplyLinearImpulseToCenter(uint64_t entityID, glm::vec2* impulse, Coral::Bool32 wake)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
			body->ApplyLinearImpulseToCenter(b2Vec2(impulse->x, impulse->y), wake);
	}

	static void RigidBody2DComponent_ApplyForce(uint64_t entityID, glm::vec2* force, glm::vec2* point, Coral::Bool32 wake)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
			body->ApplyForce(b2Vec2(force->x, force->y), b2Vec2(point->x, point->y), wake);
	}

	static void RigidBody2DComponent_ApplyForceToCenter(uint64_t entityID, glm::vec2* force, Coral::Bool32 wake)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
			body->ApplyForceToCenter(b2Vec2(force->x, force->y), wake);
	}

	static void RigidBody2DComponent_ApplyTorque(uint64_t entityID, float torque, Coral::Bool32 wake)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
			body->ApplyTorque(torque, wake);
	}

	static void RigidBody2DComponent_GetLinearVelocity(uint64_t entityID, glm::vec2* out)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
		{
			const b2Vec2& v = body->GetLinearVelocity();
			*out = { v.x, v.y };
		}
		else *out = { 0.0f, 0.0f };
	}

	static void RigidBody2DComponent_SetLinearVelocity(uint64_t entityID, glm::vec2* v)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
			body->SetLinearVelocity(b2Vec2(v->x, v->y));
	}

	static float RigidBody2DComponent_GetAngularVelocity(uint64_t entityID)
	{
		b2Body* body = GetRuntimeBody(entityID);
		return body ? body->GetAngularVelocity() : 0.0f;
	}

	static void RigidBody2DComponent_SetAngularVelocity(uint64_t entityID, float omega)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
			body->SetAngularVelocity(omega);
	}

	static void RigidBody2DComponent_GetPosition(uint64_t entityID, glm::vec2* out)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
		{
			const b2Vec2& p = body->GetPosition();
			*out = { p.x, p.y };
		}
		else *out = { 0.0f, 0.0f };
	}

	static void RigidBody2DComponent_SetPosition(uint64_t entityID, glm::vec2* position)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
			body->SetTransform(b2Vec2(position->x, position->y), body->GetAngle());
	}

	static float RigidBody2DComponent_GetRotation(uint64_t entityID)
	{
		b2Body* body = GetRuntimeBody(entityID);
		return body ? body->GetAngle() : 0.0f;
	}

	static void RigidBody2DComponent_SetRotation(uint64_t entityID, float angle)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
			body->SetTransform(body->GetPosition(), angle);
	}

	static float RigidBody2DComponent_GetMass(uint64_t entityID)
	{
		b2Body* body = GetRuntimeBody(entityID);
		return body ? body->GetMass() : 0.0f;
	}

	static float RigidBody2DComponent_GetGravityScale(uint64_t entityID)        { return GetEntity(entityID).GetComponent<RigidBody2DComponent>().GravityScale; }
	static void RigidBody2DComponent_SetGravityScale(uint64_t entityID, float v) { GetEntity(entityID).GetComponent<RigidBody2DComponent>().GravityScale = v; }

	static RigidBody2DComponent::Type RigidBody2DComponent_GetType(uint64_t entityID)
	{
		if (b2Body* body = GetRuntimeBody(entityID))
			return Utils::RigidBody2DTypeFromBox2DBody(body->GetType());
		return GetEntity(entityID).GetComponent<RigidBody2DComponent>().BodyType;
	}

	static void RigidBody2DComponent_SetType(uint64_t entityID, RigidBody2DComponent::Type bodyType)
	{
		GetEntity(entityID).GetComponent<RigidBody2DComponent>().BodyType = bodyType;
		if (b2Body* body = GetRuntimeBody(entityID))
			body->SetType(Utils::RigidBody2DTypeToBox2DBody(bodyType));
	}

	#pragma endregion

	#pragma region BoxCollider2DComponent

	static void BoxCollider2DComponent_GetOffset(uint64_t entityID, glm::vec2* out) { *out = GetEntity(entityID).GetComponent<BoxCollider2DComponent>().Offset; }
	static void BoxCollider2DComponent_SetOffset(uint64_t entityID, glm::vec2* in)  { GetEntity(entityID).GetComponent<BoxCollider2DComponent>().Offset = *in; }
	static void BoxCollider2DComponent_GetSize(uint64_t entityID, glm::vec2* out)   { *out = GetEntity(entityID).GetComponent<BoxCollider2DComponent>().Size; }
	static void BoxCollider2DComponent_SetSize(uint64_t entityID, glm::vec2* in)    { GetEntity(entityID).GetComponent<BoxCollider2DComponent>().Size = *in; }
	static float BoxCollider2DComponent_GetDensity(uint64_t entityID)              { return GetEntity(entityID).GetComponent<BoxCollider2DComponent>().Density; }
	static void BoxCollider2DComponent_SetDensity(uint64_t entityID, float v)      { GetEntity(entityID).GetComponent<BoxCollider2DComponent>().Density = v; }
	static float BoxCollider2DComponent_GetFriction(uint64_t entityID)             { return GetEntity(entityID).GetComponent<BoxCollider2DComponent>().Friction; }
	static void BoxCollider2DComponent_SetFriction(uint64_t entityID, float v)     { GetEntity(entityID).GetComponent<BoxCollider2DComponent>().Friction = v; }
	static float BoxCollider2DComponent_GetRestitution(uint64_t entityID)          { return GetEntity(entityID).GetComponent<BoxCollider2DComponent>().Restitution; }
	static void BoxCollider2DComponent_SetRestitution(uint64_t entityID, float v)  { GetEntity(entityID).GetComponent<BoxCollider2DComponent>().Restitution = v; }

	#pragma endregion

	#pragma region CircleCollider2DComponent

	static void CircleCollider2DComponent_GetOffset(uint64_t entityID, glm::vec2* out) { *out = GetEntity(entityID).GetComponent<CircleCollider2DComponent>().Offset; }
	static void CircleCollider2DComponent_SetOffset(uint64_t entityID, glm::vec2* in)  { GetEntity(entityID).GetComponent<CircleCollider2DComponent>().Offset = *in; }
	static float CircleCollider2DComponent_GetRadius(uint64_t entityID)               { return GetEntity(entityID).GetComponent<CircleCollider2DComponent>().Radius; }
	static void CircleCollider2DComponent_SetRadius(uint64_t entityID, float v)       { GetEntity(entityID).GetComponent<CircleCollider2DComponent>().Radius = v; }
	static float CircleCollider2DComponent_GetDensity(uint64_t entityID)              { return GetEntity(entityID).GetComponent<CircleCollider2DComponent>().Density; }
	static void CircleCollider2DComponent_SetDensity(uint64_t entityID, float v)      { GetEntity(entityID).GetComponent<CircleCollider2DComponent>().Density = v; }
	static float CircleCollider2DComponent_GetFriction(uint64_t entityID)             { return GetEntity(entityID).GetComponent<CircleCollider2DComponent>().Friction; }
	static void CircleCollider2DComponent_SetFriction(uint64_t entityID, float v)     { GetEntity(entityID).GetComponent<CircleCollider2DComponent>().Friction = v; }
	static float CircleCollider2DComponent_GetRestitution(uint64_t entityID)          { return GetEntity(entityID).GetComponent<CircleCollider2DComponent>().Restitution; }
	static void CircleCollider2DComponent_SetRestitution(uint64_t entityID, float v)  { GetEntity(entityID).GetComponent<CircleCollider2DComponent>().Restitution = v; }

	#pragma endregion

	#pragma region TextComponent

	static Coral::String TextComponent_GetText(uint64_t entityID) { return Coral::String::New(GetEntity(entityID).GetComponent<TextComponent>().TextString); }
	static void TextComponent_SetText(uint64_t entityID, Coral::String text) { GetEntity(entityID).GetComponent<TextComponent>().TextString = text; }
	static void TextComponent_GetColor(uint64_t entityID, glm::vec4* out) { *out = GetEntity(entityID).GetComponent<TextComponent>().Color; }
	static void TextComponent_SetColor(uint64_t entityID, glm::vec4* in)  { GetEntity(entityID).GetComponent<TextComponent>().Color = *in; }
	static float TextComponent_GetKerning(uint64_t entityID)             { return GetEntity(entityID).GetComponent<TextComponent>().Kerning; }
	static void TextComponent_SetKerning(uint64_t entityID, float v)     { GetEntity(entityID).GetComponent<TextComponent>().Kerning = v; }
	static float TextComponent_GetLineSpacing(uint64_t entityID)         { return GetEntity(entityID).GetComponent<TextComponent>().LineSpacing; }
	static void TextComponent_SetLineSpacing(uint64_t entityID, float v) { GetEntity(entityID).GetComponent<TextComponent>().LineSpacing = v; }
	static float TextComponent_GetMaxWidth(uint64_t entityID)            { return GetEntity(entityID).GetComponent<TextComponent>().MaxWidth; }
	static void TextComponent_SetMaxWidth(uint64_t entityID, float v)    { GetEntity(entityID).GetComponent<TextComponent>().MaxWidth = v; }

	#pragma endregion

	#pragma region RigidBodyComponent (3D)

	static Ref<PhysicsBody> GetPhysicsBody(uint64_t entityID)
	{
		Scene* scene = GetScene();
		if (!scene->GetPhysicsScene())
			return nullptr;
		return scene->GetPhysicsScene()->GetBodyByEntityID(entityID);
	}

	static void RigidBodyComponent_AddForce(uint64_t entityID, glm::vec3* force, int32_t forceMode, Coral::Bool32 wake)
	{
		if (Ref<PhysicsBody> body = GetPhysicsBody(entityID))
			body->AddForce(*force, (EForceMode)forceMode, wake);
	}

	static void RigidBodyComponent_AddForceAtLocation(uint64_t entityID, glm::vec3* force, glm::vec3* location, int32_t forceMode, Coral::Bool32 wake)
	{
		if (Ref<PhysicsBody> body = GetPhysicsBody(entityID))
			body->AddForce(*force, *location, (EForceMode)forceMode, wake);
	}

	static void RigidBodyComponent_AddTorque(uint64_t entityID, glm::vec3* torque, Coral::Bool32 wake)
	{
		if (Ref<PhysicsBody> body = GetPhysicsBody(entityID))
			body->AddTorque(*torque, wake);
	}

	static void RigidBodyComponent_GetLinearVelocity(uint64_t entityID, glm::vec3* out)
	{
		Ref<PhysicsBody> body = GetPhysicsBody(entityID);
		*out = body ? body->GetLinearVelocity() : glm::vec3(0.0f);
	}

	static void RigidBodyComponent_SetLinearVelocity(uint64_t entityID, glm::vec3* v)
	{
		if (Ref<PhysicsBody> body = GetPhysicsBody(entityID))
			body->SetLinearVelocity(*v);
	}

	static void RigidBodyComponent_GetAngularVelocity(uint64_t entityID, glm::vec3* out)
	{
		Ref<PhysicsBody> body = GetPhysicsBody(entityID);
		*out = body ? body->GetAngularVelocity() : glm::vec3(0.0f);
	}

	static void RigidBodyComponent_SetAngularVelocity(uint64_t entityID, glm::vec3* v)
	{
		if (Ref<PhysicsBody> body = GetPhysicsBody(entityID))
			body->SetAngularVelocity(*v);
	}

	static void RigidBodyComponent_GetTranslation(uint64_t entityID, glm::vec3* out)
	{
		Ref<PhysicsBody> body = GetPhysicsBody(entityID);
		*out = body ? body->GetTranslation() : glm::vec3(0.0f);
	}

	static void RigidBodyComponent_SetTranslation(uint64_t entityID, glm::vec3* t)
	{
		if (Ref<PhysicsBody> body = GetPhysicsBody(entityID))
			body->SetTranslation(*t);
	}

	static void RigidBodyComponent_GetRotation(uint64_t entityID, glm::vec3* outEuler)
	{
		Ref<PhysicsBody> body = GetPhysicsBody(entityID);
		*outEuler = body ? glm::eulerAngles(body->GetRotation()) : glm::vec3(0.0f);
	}

	static void RigidBodyComponent_SetRotation(uint64_t entityID, glm::vec3* euler)
	{
		if (Ref<PhysicsBody> body = GetPhysicsBody(entityID))
			body->SetRotation(glm::quat(*euler));
	}

	static float RigidBodyComponent_GetMass(uint64_t entityID)
	{
		Ref<PhysicsBody> body = GetPhysicsBody(entityID);
		return body ? body->GetMass() : 0.0f;
	}

	static void RigidBodyComponent_SetMass(uint64_t entityID, float mass)
	{
		if (Ref<PhysicsBody> body = GetPhysicsBody(entityID))
			body->SetMass(mass);
	}

	static void RigidBodyComponent_SetGravityEnabled(uint64_t entityID, Coral::Bool32 enabled)
	{
		if (Ref<PhysicsBody> body = GetPhysicsBody(entityID))
			body->SetGravityEnabled(enabled);
	}

	static Coral::Bool32 RigidBodyComponent_IsSleeping(uint64_t entityID)
	{
		Ref<PhysicsBody> body = GetPhysicsBody(entityID);
		return body ? (Coral::Bool32)body->IsSleeping() : false;
	}

	static void RigidBodyComponent_SetSleepState(uint64_t entityID, Coral::Bool32 sleep)
	{
		if (Ref<PhysicsBody> body = GetPhysicsBody(entityID))
			body->SetSleepState(sleep);
	}

	#pragma endregion

	#pragma region CharacterControllerComponent

	static Ref<CharacterController> GetCharacterController(uint64_t entityID)
	{
		Scene* scene = GetScene();
		if (!scene->GetPhysicsScene())
			return nullptr;
		return scene->GetPhysicsScene()->GetCharacterControllerByEntityID(entityID);
	}

	static void CharacterControllerComponent_Move(uint64_t entityID, glm::vec3* displacement)
	{
		if (Ref<CharacterController> cc = GetCharacterController(entityID))
			cc->Move(*displacement);
	}

	static void CharacterControllerComponent_Jump(uint64_t entityID, float power)
	{
		if (Ref<CharacterController> cc = GetCharacterController(entityID))
			cc->Jump(power);
	}

	static Coral::Bool32 CharacterControllerComponent_IsGrounded(uint64_t entityID)
	{
		Ref<CharacterController> cc = GetCharacterController(entityID);
		return cc ? (Coral::Bool32)cc->IsGrounded() : false;
	}

	static void CharacterControllerComponent_GetLinearVelocity(uint64_t entityID, glm::vec3* out)
	{
		Ref<CharacterController> cc = GetCharacterController(entityID);
		*out = cc ? cc->GetLinearVelocity() : glm::vec3(0.0f);
	}

	static void CharacterControllerComponent_SetLinearVelocity(uint64_t entityID, glm::vec3* v)
	{
		if (Ref<CharacterController> cc = GetCharacterController(entityID))
			cc->SetLinearVelocity(*v);
	}

	static void CharacterControllerComponent_SetGravityEnabled(uint64_t entityID, Coral::Bool32 enabled)
	{
		if (Ref<CharacterController> cc = GetCharacterController(entityID))
			cc->SetGravityEnabled(enabled);
	}

	#pragma endregion

	#pragma region 3D Colliders

	static void BoxColliderComponent_GetHalfSize(uint64_t entityID, glm::vec3* out) { *out = GetEntity(entityID).GetComponent<BoxColliderComponent>().HalfSize; }
	static void BoxColliderComponent_SetHalfSize(uint64_t entityID, glm::vec3* in)  { GetEntity(entityID).GetComponent<BoxColliderComponent>().HalfSize = *in; }
	static void BoxColliderComponent_GetOffset(uint64_t entityID, glm::vec3* out)   { *out = GetEntity(entityID).GetComponent<BoxColliderComponent>().Offset; }
	static void BoxColliderComponent_SetOffset(uint64_t entityID, glm::vec3* in)    { GetEntity(entityID).GetComponent<BoxColliderComponent>().Offset = *in; }

	static float SphereColliderComponent_GetRadius(uint64_t entityID)          { return GetEntity(entityID).GetComponent<SphereColliderComponent>().Radius; }
	static void SphereColliderComponent_SetRadius(uint64_t entityID, float v)  { GetEntity(entityID).GetComponent<SphereColliderComponent>().Radius = v; }
	static void SphereColliderComponent_GetOffset(uint64_t entityID, glm::vec3* out) { *out = GetEntity(entityID).GetComponent<SphereColliderComponent>().Offset; }
	static void SphereColliderComponent_SetOffset(uint64_t entityID, glm::vec3* in)  { GetEntity(entityID).GetComponent<SphereColliderComponent>().Offset = *in; }

	static float CapsuleColliderComponent_GetRadius(uint64_t entityID)             { return GetEntity(entityID).GetComponent<CapsuleColliderComponent>().Radius; }
	static void CapsuleColliderComponent_SetRadius(uint64_t entityID, float v)     { GetEntity(entityID).GetComponent<CapsuleColliderComponent>().Radius = v; }
	static float CapsuleColliderComponent_GetHalfHeight(uint64_t entityID)         { return GetEntity(entityID).GetComponent<CapsuleColliderComponent>().HalfHeight; }
	static void CapsuleColliderComponent_SetHalfHeight(uint64_t entityID, float v) { GetEntity(entityID).GetComponent<CapsuleColliderComponent>().HalfHeight = v; }
	static void CapsuleColliderComponent_GetOffset(uint64_t entityID, glm::vec3* out) { *out = GetEntity(entityID).GetComponent<CapsuleColliderComponent>().Offset; }
	static void CapsuleColliderComponent_SetOffset(uint64_t entityID, glm::vec3* in)  { GetEntity(entityID).GetComponent<CapsuleColliderComponent>().Offset = *in; }

	#pragma endregion

	// typeid().name() is implementation-defined. MSVC returns a readable
	// "struct Lux::TransformComponent", while the Itanium ABI (GCC/Clang) returns the mangled
	// "N3Lux18TransformComponentE" - which contains no ':', so the namespace strip below would
	// otherwise keep the whole mangled string and no component would ever resolve.
	static std::string DemangleTypeName(const char* name)
	{
#if defined(LUX_PLATFORM_WINDOWS)
		return name;
#else
		int status = 0;
		char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
		if (status != 0 || demangled == nullptr)
		{
			std::free(demangled);
			return name;
		}

		std::string result = demangled;
		std::free(demangled);
		return result;
#endif
	}

	template<typename TComponent>
	static void RegisterManagedComponent(Coral::ManagedAssembly& coreAssembly)
	{
		const std::string typeName = DemangleTypeName(typeid(TComponent).name());
		size_t pos = typeName.find_last_of(':');
		std::string_view structName = std::string_view(typeName).substr(pos + 1);
		std::string managedTypename = std::format("Lux.{}", structName);

		Coral::Type& managedType = coreAssembly.GetLocalType(managedTypename);
		if (!managedType)
		{
			LUX_CORE_TRACE("[ScriptGlue] No managed component type {} (skipping).", managedTypename);
			return;
		}

		Coral::TypeId id = managedType.GetTypeId();
		s_HasComponentFuncs[id] = [](Entity entity) { return entity.HasComponent<TComponent>(); };
		s_AddComponentFuncs[id] = [](Entity entity) { if (!entity.HasComponent<TComponent>()) entity.AddComponent<TComponent>(); };
		s_RemoveComponentFuncs[id] = [](Entity entity) { entity.RemoveComponentIfExists<TComponent>(); };
	}

	static void RegisterComponentTypes(Coral::ManagedAssembly& coreAssembly)
	{
		s_HasComponentFuncs.clear();
		s_AddComponentFuncs.clear();
		s_RemoveComponentFuncs.clear();

		RegisterManagedComponent<TransformComponent>(coreAssembly);
		RegisterManagedComponent<TagComponent>(coreAssembly);
		RegisterManagedComponent<SpriteRendererComponent>(coreAssembly);
		RegisterManagedComponent<CircleRendererComponent>(coreAssembly);
		RegisterManagedComponent<CameraComponent>(coreAssembly);
		RegisterManagedComponent<RigidBody2DComponent>(coreAssembly);
		RegisterManagedComponent<BoxCollider2DComponent>(coreAssembly);
		RegisterManagedComponent<CircleCollider2DComponent>(coreAssembly);
		RegisterManagedComponent<TextComponent>(coreAssembly);
		RegisterManagedComponent<AudioSourceComponent>(coreAssembly);
		RegisterManagedComponent<AudioListenerComponent>(coreAssembly);

		// 3D physics
		RegisterManagedComponent<RigidBodyComponent>(coreAssembly);
		RegisterManagedComponent<CharacterControllerComponent>(coreAssembly);
		RegisterManagedComponent<BoxColliderComponent>(coreAssembly);
		RegisterManagedComponent<SphereColliderComponent>(coreAssembly);
		RegisterManagedComponent<CapsuleColliderComponent>(coreAssembly);
		RegisterManagedComponent<MeshColliderComponent>(coreAssembly);
	}

	static void RegisterInternalCalls(Coral::ManagedAssembly& coreAssembly)
	{
		LUX_ADD_INTERNAL_CALL(NativeLog);

		LUX_ADD_INTERNAL_CALL(Input_IsKeyDown);
		LUX_ADD_INTERNAL_CALL(Input_IsKeyPressed);
		LUX_ADD_INTERNAL_CALL(Input_IsKeyHeld);
		LUX_ADD_INTERNAL_CALL(Input_IsKeyReleased);
		LUX_ADD_INTERNAL_CALL(Input_IsMouseButtonDown);
		LUX_ADD_INTERNAL_CALL(Input_IsMouseButtonPressed);
		LUX_ADD_INTERNAL_CALL(Input_IsMouseButtonHeld);
		LUX_ADD_INTERNAL_CALL(Input_IsMouseButtonReleased);
		LUX_ADD_INTERNAL_CALL(Input_GetMouseX);
		LUX_ADD_INTERNAL_CALL(Input_GetMouseY);
		LUX_ADD_INTERNAL_CALL(Input_GetMousePosition);
		LUX_ADD_INTERNAL_CALL(Input_SetMousePosition);
		LUX_ADD_INTERNAL_CALL(Input_SetCursorMode);
		LUX_ADD_INTERNAL_CALL(Input_IsControllerPresent);
		LUX_ADD_INTERNAL_CALL(Input_IsControllerButtonDown);
		LUX_ADD_INTERNAL_CALL(Input_GetControllerAxis);

		LUX_ADD_INTERNAL_CALL(Scene_CreateEntity);
		LUX_ADD_INTERNAL_CALL(Scene_DestroyEntity);

		LUX_ADD_INTERNAL_CALL(GetScriptInstance);
		LUX_ADD_INTERNAL_CALL(Entity_HasComponent);
		LUX_ADD_INTERNAL_CALL(Entity_AddComponent);
		LUX_ADD_INTERNAL_CALL(Entity_RemoveComponent);
		LUX_ADD_INTERNAL_CALL(Entity_FindEntityByName);
		LUX_ADD_INTERNAL_CALL(Entity_GetName);
		LUX_ADD_INTERNAL_CALL(Entity_SetName);
		LUX_ADD_INTERNAL_CALL(Entity_GetParent);
		LUX_ADD_INTERNAL_CALL(Entity_SetParent);
		LUX_ADD_INTERNAL_CALL(Entity_GetChildren);

		LUX_ADD_INTERNAL_CALL(TransformComponent_GetTranslation);
		LUX_ADD_INTERNAL_CALL(TransformComponent_SetTranslation);
		LUX_ADD_INTERNAL_CALL(TransformComponent_GetRotation);
		LUX_ADD_INTERNAL_CALL(TransformComponent_SetRotation);
		LUX_ADD_INTERNAL_CALL(TransformComponent_GetScale);
		LUX_ADD_INTERNAL_CALL(TransformComponent_SetScale);

		LUX_ADD_INTERNAL_CALL(TagComponent_GetTag);
		LUX_ADD_INTERNAL_CALL(TagComponent_SetTag);

		LUX_ADD_INTERNAL_CALL(SpriteRendererComponent_GetColor);
		LUX_ADD_INTERNAL_CALL(SpriteRendererComponent_SetColor);
		LUX_ADD_INTERNAL_CALL(SpriteRendererComponent_GetTilingFactor);
		LUX_ADD_INTERNAL_CALL(SpriteRendererComponent_SetTilingFactor);
		LUX_ADD_INTERNAL_CALL(SpriteRendererComponent_GetTexture);
		LUX_ADD_INTERNAL_CALL(SpriteRendererComponent_SetTexture);

		LUX_ADD_INTERNAL_CALL(CircleRendererComponent_GetColor);
		LUX_ADD_INTERNAL_CALL(CircleRendererComponent_SetColor);
		LUX_ADD_INTERNAL_CALL(CircleRendererComponent_GetThickness);
		LUX_ADD_INTERNAL_CALL(CircleRendererComponent_SetThickness);
		LUX_ADD_INTERNAL_CALL(CircleRendererComponent_GetFade);
		LUX_ADD_INTERNAL_CALL(CircleRendererComponent_SetFade);

		LUX_ADD_INTERNAL_CALL(CameraComponent_GetPrimary);
		LUX_ADD_INTERNAL_CALL(CameraComponent_SetPrimary);

		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_ApplyLinearImpulse);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_ApplyLinearImpulseToCenter);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_ApplyForce);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_ApplyForceToCenter);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_ApplyTorque);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_GetLinearVelocity);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_SetLinearVelocity);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_GetAngularVelocity);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_SetAngularVelocity);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_GetPosition);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_SetPosition);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_GetRotation);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_SetRotation);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_GetMass);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_GetGravityScale);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_SetGravityScale);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_GetType);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_SetType);

		LUX_ADD_INTERNAL_CALL(BoxCollider2DComponent_GetOffset);
		LUX_ADD_INTERNAL_CALL(BoxCollider2DComponent_SetOffset);
		LUX_ADD_INTERNAL_CALL(BoxCollider2DComponent_GetSize);
		LUX_ADD_INTERNAL_CALL(BoxCollider2DComponent_SetSize);
		LUX_ADD_INTERNAL_CALL(BoxCollider2DComponent_GetDensity);
		LUX_ADD_INTERNAL_CALL(BoxCollider2DComponent_SetDensity);
		LUX_ADD_INTERNAL_CALL(BoxCollider2DComponent_GetFriction);
		LUX_ADD_INTERNAL_CALL(BoxCollider2DComponent_SetFriction);
		LUX_ADD_INTERNAL_CALL(BoxCollider2DComponent_GetRestitution);
		LUX_ADD_INTERNAL_CALL(BoxCollider2DComponent_SetRestitution);

		LUX_ADD_INTERNAL_CALL(CircleCollider2DComponent_GetOffset);
		LUX_ADD_INTERNAL_CALL(CircleCollider2DComponent_SetOffset);
		LUX_ADD_INTERNAL_CALL(CircleCollider2DComponent_GetRadius);
		LUX_ADD_INTERNAL_CALL(CircleCollider2DComponent_SetRadius);
		LUX_ADD_INTERNAL_CALL(CircleCollider2DComponent_GetDensity);
		LUX_ADD_INTERNAL_CALL(CircleCollider2DComponent_SetDensity);
		LUX_ADD_INTERNAL_CALL(CircleCollider2DComponent_GetFriction);
		LUX_ADD_INTERNAL_CALL(CircleCollider2DComponent_SetFriction);
		LUX_ADD_INTERNAL_CALL(CircleCollider2DComponent_GetRestitution);
		LUX_ADD_INTERNAL_CALL(CircleCollider2DComponent_SetRestitution);

		LUX_ADD_INTERNAL_CALL(TextComponent_GetText);
		LUX_ADD_INTERNAL_CALL(TextComponent_SetText);
		LUX_ADD_INTERNAL_CALL(TextComponent_GetColor);
		LUX_ADD_INTERNAL_CALL(TextComponent_SetColor);
		LUX_ADD_INTERNAL_CALL(TextComponent_GetKerning);
		LUX_ADD_INTERNAL_CALL(TextComponent_SetKerning);
		LUX_ADD_INTERNAL_CALL(TextComponent_GetLineSpacing);
		LUX_ADD_INTERNAL_CALL(TextComponent_SetLineSpacing);
		LUX_ADD_INTERNAL_CALL(TextComponent_GetMaxWidth);
		LUX_ADD_INTERNAL_CALL(TextComponent_SetMaxWidth);

		// 3D physics
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_AddForce);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_AddForceAtLocation);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_AddTorque);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_GetLinearVelocity);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_SetLinearVelocity);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_GetAngularVelocity);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_SetAngularVelocity);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_GetTranslation);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_SetTranslation);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_GetRotation);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_SetRotation);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_GetMass);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_SetMass);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_SetGravityEnabled);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_IsSleeping);
		LUX_ADD_INTERNAL_CALL(RigidBodyComponent_SetSleepState);

		LUX_ADD_INTERNAL_CALL(CharacterControllerComponent_Move);
		LUX_ADD_INTERNAL_CALL(CharacterControllerComponent_Jump);
		LUX_ADD_INTERNAL_CALL(CharacterControllerComponent_IsGrounded);
		LUX_ADD_INTERNAL_CALL(CharacterControllerComponent_GetLinearVelocity);
		LUX_ADD_INTERNAL_CALL(CharacterControllerComponent_SetLinearVelocity);
		LUX_ADD_INTERNAL_CALL(CharacterControllerComponent_SetGravityEnabled);

		LUX_ADD_INTERNAL_CALL(BoxColliderComponent_GetHalfSize);
		LUX_ADD_INTERNAL_CALL(BoxColliderComponent_SetHalfSize);
		LUX_ADD_INTERNAL_CALL(BoxColliderComponent_GetOffset);
		LUX_ADD_INTERNAL_CALL(BoxColliderComponent_SetOffset);
		LUX_ADD_INTERNAL_CALL(SphereColliderComponent_GetRadius);
		LUX_ADD_INTERNAL_CALL(SphereColliderComponent_SetRadius);
		LUX_ADD_INTERNAL_CALL(SphereColliderComponent_GetOffset);
		LUX_ADD_INTERNAL_CALL(SphereColliderComponent_SetOffset);
		LUX_ADD_INTERNAL_CALL(CapsuleColliderComponent_GetRadius);
		LUX_ADD_INTERNAL_CALL(CapsuleColliderComponent_SetRadius);
		LUX_ADD_INTERNAL_CALL(CapsuleColliderComponent_GetHalfHeight);
		LUX_ADD_INTERNAL_CALL(CapsuleColliderComponent_SetHalfHeight);
		LUX_ADD_INTERNAL_CALL(CapsuleColliderComponent_GetOffset);
		LUX_ADD_INTERNAL_CALL(CapsuleColliderComponent_SetOffset);
	}

	void ScriptGlue::RegisterGlue(Coral::ManagedAssembly& coreAssembly)
	{
		RegisterComponentTypes(coreAssembly);
		RegisterInternalCalls(coreAssembly);
		coreAssembly.UploadInternalCalls();
	}

}
