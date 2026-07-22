#include "lpch.h"
#include "ScriptGlue.h"
#include "ScriptEngine.h"

#include "Lux/Core/UUID.h"
#include "Lux/Core/KeyCodes.h"
#include "Lux/Core/Input.h"

#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Entity.h"

#include "Lux/Physics2D/ContactListener2D.h"

#include <Coral/Assembly.hpp>
#include <Coral/Type.hpp>
#include <Coral/String.hpp>

#include "box2d/b2_body.h"

#include <format>

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
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);

		auto it = s_HasComponentFuncs.find(componentType.m_TypeID);
		if (it == s_HasComponentFuncs.end())
			return false;
		return it->second(entity);
	}

	static void Entity_AddComponent(uint64_t entityID, Coral::ReflectionType componentType)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);

		auto it = s_AddComponentFuncs.find(componentType.m_TypeID);
		if (it != s_AddComponentFuncs.end())
			it->second(entity);
	}

	static void Entity_RemoveComponent(uint64_t entityID, Coral::ReflectionType componentType)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);

		auto it = s_RemoveComponentFuncs.find(componentType.m_TypeID);
		if (it != s_RemoveComponentFuncs.end())
			it->second(entity);
	}

	static uint64_t Entity_FindEntityByName(Coral::String name)
	{
		std::string nameStr = name;
		Scene* scene = GetScene();
		Entity entity = scene->FindEntityByName(nameStr);
		if (!entity)
			return 0;
		return entity.GetUUID();
	}

	static Coral::String Entity_GetName(uint64_t entityID)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		return Coral::String::New(entity.GetName());
	}

	#pragma endregion

	#pragma region TransformComponent

	static void TransformComponent_GetTranslation(uint64_t entityID, glm::vec3* outTranslation)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		*outTranslation = entity.GetComponent<TransformComponent>().Translation;
	}

	static void TransformComponent_SetTranslation(uint64_t entityID, glm::vec3* translation)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		entity.GetComponent<TransformComponent>().Translation = *translation;
	}

	static void TransformComponent_GetScale(uint64_t entityID, glm::vec3* outScale)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		*outScale = entity.GetComponent<TransformComponent>().Scale;
	}

	static void TransformComponent_SetScale(uint64_t entityID, glm::vec3* scale)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		entity.GetComponent<TransformComponent>().Scale = *scale;
	}

	#pragma endregion

	#pragma region RigidBody2DComponent

	static void RigidBody2DComponent_ApplyLinearImpulse(uint64_t entityID, glm::vec2* impulse, glm::vec2* point, Coral::Bool32 wake)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
		b2Body* body = (b2Body*)rb2d.RuntimeBody;
		body->ApplyLinearImpulse(b2Vec2(impulse->x, impulse->y), b2Vec2(point->x, point->y), wake);
	}

	static void RigidBody2DComponent_ApplyLinearImpulseToCenter(uint64_t entityID, glm::vec2* impulse, Coral::Bool32 wake)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
		b2Body* body = (b2Body*)rb2d.RuntimeBody;
		body->ApplyLinearImpulseToCenter(b2Vec2(impulse->x, impulse->y), wake);
	}

	static void RigidBody2DComponent_GetLinearVelocity(uint64_t entityID, glm::vec2* outLinearVelocity)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
		b2Body* body = (b2Body*)rb2d.RuntimeBody;
		const b2Vec2& linearVelocity = body->GetLinearVelocity();
		*outLinearVelocity = glm::vec2(linearVelocity.x, linearVelocity.y);
	}

	static RigidBody2DComponent::Type RigidBody2DComponent_GetType(uint64_t entityID)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
		b2Body* body = (b2Body*)rb2d.RuntimeBody;
		return Utils::RigidBody2DTypeFromBox2DBody(body->GetType());
	}

	static void RigidBody2DComponent_SetType(uint64_t entityID, RigidBody2DComponent::Type bodyType)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity);
		auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
		b2Body* body = (b2Body*)rb2d.RuntimeBody;
		body->SetType(Utils::RigidBody2DTypeToBox2DBody(bodyType));
	}

	#pragma endregion

	#pragma region TextComponent

	static Coral::String TextComponent_GetText(uint64_t entityID)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity && entity.HasComponent<TextComponent>());
		return Coral::String::New(entity.GetComponent<TextComponent>().TextString);
	}

	static void TextComponent_SetText(uint64_t entityID, Coral::String textString)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity && entity.HasComponent<TextComponent>());
		entity.GetComponent<TextComponent>().TextString = textString;
	}

	static void TextComponent_GetColor(uint64_t entityID, glm::vec4* color)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity && entity.HasComponent<TextComponent>());
		*color = entity.GetComponent<TextComponent>().Color;
	}

	static void TextComponent_SetColor(uint64_t entityID, glm::vec4* color)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity && entity.HasComponent<TextComponent>());
		entity.GetComponent<TextComponent>().Color = *color;
	}

	static float TextComponent_GetKerning(uint64_t entityID)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity && entity.HasComponent<TextComponent>());
		return entity.GetComponent<TextComponent>().Kerning;
	}

	static void TextComponent_SetKerning(uint64_t entityID, float kerning)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity && entity.HasComponent<TextComponent>());
		entity.GetComponent<TextComponent>().Kerning = kerning;
	}

	static float TextComponent_GetLineSpacing(uint64_t entityID)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity && entity.HasComponent<TextComponent>());
		return entity.GetComponent<TextComponent>().LineSpacing;
	}

	static void TextComponent_SetLineSpacing(uint64_t entityID, float lineSpacing)
	{
		Scene* scene = GetScene();
		Entity entity = scene->GetEntityByUUID(entityID);
		LUX_CORE_ASSERT(entity && entity.HasComponent<TextComponent>());
		entity.GetComponent<TextComponent>().LineSpacing = lineSpacing;
	}

	#pragma endregion

	#pragma region Input

	static Coral::Bool32 Input_IsKeyDown(KeyCode keycode)
	{
		return Input::IsKeyPressed(keycode);
	}

	static Coral::Bool32 Input_IsMouseButtonDown(MouseButton button)
	{
		return Input::IsMouseButtonPressed(button);
	}

	#pragma endregion

	template<typename TComponent>
	static void RegisterManagedComponent(Coral::ManagedAssembly& coreAssembly)
	{
		std::string_view typeName = typeid(TComponent).name();
		size_t pos = typeName.find_last_of(':');
		std::string_view structName = typeName.substr(pos + 1);
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
		RegisterManagedComponent<SpriteRendererComponent>(coreAssembly);
		RegisterManagedComponent<CircleRendererComponent>(coreAssembly);
		RegisterManagedComponent<CameraComponent>(coreAssembly);
		RegisterManagedComponent<RigidBody2DComponent>(coreAssembly);
		RegisterManagedComponent<BoxCollider2DComponent>(coreAssembly);
		RegisterManagedComponent<CircleCollider2DComponent>(coreAssembly);
		RegisterManagedComponent<TextComponent>(coreAssembly);
		RegisterManagedComponent<AudioSourceComponent>(coreAssembly);
		RegisterManagedComponent<AudioListenerComponent>(coreAssembly);
	}

	static void RegisterInternalCalls(Coral::ManagedAssembly& coreAssembly)
	{
		LUX_ADD_INTERNAL_CALL(NativeLog);

		LUX_ADD_INTERNAL_CALL(GetScriptInstance);
		LUX_ADD_INTERNAL_CALL(Entity_HasComponent);
		LUX_ADD_INTERNAL_CALL(Entity_AddComponent);
		LUX_ADD_INTERNAL_CALL(Entity_RemoveComponent);
		LUX_ADD_INTERNAL_CALL(Entity_FindEntityByName);
		LUX_ADD_INTERNAL_CALL(Entity_GetName);

		LUX_ADD_INTERNAL_CALL(TransformComponent_GetTranslation);
		LUX_ADD_INTERNAL_CALL(TransformComponent_SetTranslation);
		LUX_ADD_INTERNAL_CALL(TransformComponent_GetScale);
		LUX_ADD_INTERNAL_CALL(TransformComponent_SetScale);

		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_ApplyLinearImpulse);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_ApplyLinearImpulseToCenter);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_GetLinearVelocity);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_GetType);
		LUX_ADD_INTERNAL_CALL(RigidBody2DComponent_SetType);

		LUX_ADD_INTERNAL_CALL(TextComponent_GetText);
		LUX_ADD_INTERNAL_CALL(TextComponent_SetText);
		LUX_ADD_INTERNAL_CALL(TextComponent_GetColor);
		LUX_ADD_INTERNAL_CALL(TextComponent_SetColor);
		LUX_ADD_INTERNAL_CALL(TextComponent_GetKerning);
		LUX_ADD_INTERNAL_CALL(TextComponent_SetKerning);
		LUX_ADD_INTERNAL_CALL(TextComponent_GetLineSpacing);
		LUX_ADD_INTERNAL_CALL(TextComponent_SetLineSpacing);

		LUX_ADD_INTERNAL_CALL(Input_IsKeyDown);
		LUX_ADD_INTERNAL_CALL(Input_IsMouseButtonDown);
	}

	void ScriptGlue::RegisterGlue(Coral::ManagedAssembly& coreAssembly)
	{
		RegisterComponentTypes(coreAssembly);
		RegisterInternalCalls(coreAssembly);
		coreAssembly.UploadInternalCalls();
	}

}
