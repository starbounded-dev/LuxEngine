// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ScriptGlue.h"
#include "AnimationScriptBindings.h"
#include "AudioScriptBindings.h"
#include "ComponentScriptBindings.h"
#include "Lux/UI/UIScriptBindings.h"
#include "ScriptEngine.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/Events/ApplicationEvent.h"
#include "Lux/Core/UUID.h"
#include "Lux/Core/KeyCodes.h"
#include "Lux/Core/Input.h"

#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Prefab.h"
#include "Lux/Asset/AssetManager.h"

#include "Lux/Physics2D/ContactListener2D.h"

#include "Lux/Physics/PhysicsScene.h"
#include "Lux/Physics2D/PhysicsScene2D.h"
#include "Lux/Scene/Tilemap.h"
#include "Lux/Physics/PhysicsBody.h"
#include "Lux/Physics/CharacterController.h"
#include "Lux/Physics/PhysicsTypes.h"
#include "Lux/Physics/SceneQueries.h"
#include "Lux/Project/Project.h"

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
	// Only for components with an on/off switch (HasEnabledFlag).
	static std::unordered_map<Coral::TypeId, std::function<bool(Entity)>> s_GetEnabledFuncs;
	static std::unordered_map<Coral::TypeId, std::function<void(Entity, bool)>> s_SetEnabledFuncs;
	// Every entity with the component, for Entity.FindAllWithComponent<T>: appends (creation index, UUID).
	using EntityHit = std::pair<uint32_t, uint64_t>;
	static std::unordered_map<Coral::TypeId, std::function<void(Scene*, std::vector<EntityHit>&)>> s_FindComponentFuncs;

#define LUX_ADD_INTERNAL_CALL(Name) coreAssembly.AddInternalCall("Lux.InternalCalls", #Name, reinterpret_cast<void*>(&Name))

	static Scene* GetScene()
	{
		Scene* scene = ScriptEngine::GetInstance().GetCurrentScene().Raw();
		LUX_CORE_ASSERT(scene, "No active scene bound to ScriptEngine");
		return scene;
	}

	// A script can keep an Entity after it was destroyed. Every entity binding is registered through
	// this check, so a stale ID logs an error and returns a default value instead of the binding
	// dereferencing an empty Entity (a crash in Release, where GetEntity's assert is compiled out).
	template<auto Function>
	struct EntityCall;
	template<typename Result, typename... Args, Result(*Function)(uint64_t, Args...)>
	struct EntityCall<Function>
	{
		static Result Invoke(uint64_t entityID, Args... args)
		{
			Scene* scene = ScriptEngine::GetInstance().GetCurrentScene().Raw();
			if (!scene || !scene->GetEntityByUUID(entityID))
			{
				LUX_CONSOLE_LOG_ERROR("A script used entity {} after it was destroyed (or it never existed)", entityID);
				if constexpr (std::is_void_v<Result>)
					return;
				else
					return Result{};
			}
			return Function(entityID, args...);
		}
	};
#define LUX_ADD_ENTITY_CALL(Name) coreAssembly.AddInternalCall("Lux.InternalCalls", #Name, reinterpret_cast<void*>(&EntityCall<&Name>::Invoke))

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
			case 0: LUX_CONSOLE_LOG_TRACE("{}", str); break;
			case 1: LUX_CONSOLE_LOG_INFO("{}", str); break;
			case 2: LUX_CONSOLE_LOG_WARN("{}", str); break;
			default: LUX_CONSOLE_LOG_ERROR("{}", str); break;
		}
	}

	#pragma endregion

	#pragma region Application

	// Ends the game at the next safe point (the window-close path every other quit takes). The
	// editor ignores it: stopping Play is the editor's own command.
	static void Application_Quit()
	{
		if (!Application::IsRuntime())
		{
			LUX_CONSOLE_LOG_WARN("Application.Quit() is ignored in the editor; it ends the exported game");
			return;
		}
		Application::Get().QueueEvent([]() { Application::Get().DispatchEvent<WindowCloseEvent, true>(); });
	}

	#pragma endregion

	#pragma region Time

	// One call for every clock value, in the order of the C# `Time.Field` enum. Doubles keep the elapsed
	// seconds and the frame count exact over long sessions.
	static double Time_Get(int32_t field)
	{
		Scene* scene = GetScene();
		const Scene::ScriptClock& clock = scene->GetClock();
		switch (field)
		{
			case 0: return clock.InFixedStep ? clock.FixedDelta : clock.Delta;
			case 1: return clock.UnscaledDelta;
			case 2: return clock.Elapsed;
			case 3: return clock.UnscaledElapsed;
			case 4: return clock.TimeScale;
			case 5: return scene->GetPhysicsScene() ? scene->GetPhysicsScene()->GetFixedTimestep() : clock.FixedDelta;
			case 6: return static_cast<double>(clock.FrameCount);
			default: return 0.0;
		}
	}

	static void Time_SetScale(float scale)
	{
		GetScene()->GetClock().TimeScale = std::isfinite(scale) ? std::max(scale, 0.0f) : 1.0f;
	}

	#pragma endregion

	#pragma region Input

	static Coral::Bool32 Application_IsFocused() { return Application::Get().IsFocused(); }
	static Coral::Bool32 Application_GetPaused() { return GetScene()->IsPaused(); }
	static void Application_SetPaused(Coral::Bool32 paused) { GetScene()->SetPaused(paused); }
	static int32_t Application_GetWidth() { return static_cast<int32_t>(GetScene()->GetViewportWidth()); }
	static int32_t Application_GetHeight() { return static_cast<int32_t>(GetScene()->GetViewportHeight()); }

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

	static Coral::Bool32 Input_IsGamepadConnected(int32_t id) { return Input::IsGamepadConnected(id); }
	static Coral::String Input_GetGamepadName(int32_t id) { return Coral::String::New(Input::GetGamepadName(id)); }
	static Coral::Bool32 Input_IsGamepadButtonDown(GamepadButton button, int32_t id)     { return Input::IsGamepadButtonDown(button, id); }
	static Coral::Bool32 Input_IsGamepadButtonPressed(GamepadButton button, int32_t id)  { return Input::IsGamepadButtonPressed(button, id); }
	static Coral::Bool32 Input_IsGamepadButtonReleased(GamepadButton button, int32_t id) { return Input::IsGamepadButtonReleased(button, id); }
	static float Input_GetGamepadAxis(GamepadAxis axis, int32_t id) { return Input::GetGamepadAxis(axis, id); }
	static float Input_GetGamepadDeadzone() { return Input::GetGamepadDeadzone(); }
	static void Input_SetGamepadDeadzone(float deadzone) { Input::SetGamepadDeadzone(deadzone); }
	static_assert(sizeof(TriggerEffect) == 5 * sizeof(int32_t), "TriggerEffect must match the C# Lux.TriggerEffect layout");
	static Coral::Bool32 Input_SupportsTriggerEffects(int32_t id) { return Input::SupportsTriggerEffects(id); }
	static void Input_SetGamepadTriggerEffect(GamepadTrigger trigger, TriggerEffect* effect, int32_t id) { Input::SetGamepadTriggerEffect(trigger, *effect, id); }
	static void Input_ResetGamepadTriggerEffects() { Input::ResetGamepadTriggerEffects(); }
	static Coral::Bool32 Input_SupportsRumble(int32_t id) { return Input::SupportsRumble(id); }
	static void Input_RumbleGamepad(float low, float high, float durationSeconds, int32_t id) { Input::RumbleGamepad(low, high, durationSeconds, id); }
	static void Input_StopGamepadRumble(int32_t id) { Input::StopGamepadRumble(id); }
	static GamepadType Input_GetGamepadType(int32_t id) { return Input::GetGamepadType(id); }
	static Coral::Bool32 Input_SupportsLightbar(int32_t id) { return Input::SupportsLightbar(id); }
	static void Input_SetGamepadLightColor(float red, float green, float blue, int32_t id) { Input::SetGamepadLightColor(red, green, blue, id); }
	static void Input_SetGamepadPlayerLights(int32_t player, int32_t id) { Input::SetGamepadPlayerLights(player, id); }
	static void Input_ResetGamepadLights() { Input::ResetGamepadLights(); }

	static Coral::Type* s_InputType = nullptr;
	static Coral::Type* s_SchedulerType = nullptr;
	static Coral::Type* s_ApplicationType = nullptr;
	static bool s_ApplicationKnown = false, s_KnownFocused = false, s_KnownPaused = false;
	static uint32_t s_KnownWidth = 0, s_KnownHeight = 0;
	static uint32_t s_KnownControllerMask = 0;
	static bool s_InputEventsPrimed = false;

	#pragma endregion

	#pragma region Scene

	static uint64_t Scene_CreateEntity(Coral::String name)
	{
		std::string nameStr = name;
		return GetScene()->CreateEntity(nameStr).GetUUID();
	}

	// The new hierarchy's root, or 0. Translation and rotation (Euler, radians) are optional: null keeps
	// the prefab root's own. During Play the instance's physics and scripts start at once (OnCreate runs
	// before this returns).
	static uint64_t Scene_InstantiatePrefab(uint64_t prefabHandle, glm::vec3* translation, glm::vec3* rotation)
	{
		if (!AssetManager::IsAssetHandleValid(prefabHandle) || AssetManager::GetAssetType(prefabHandle) != AssetType::Prefab)
		{
			LUX_CONSOLE_LOG_ERROR("Instantiate: {} is not a prefab", prefabHandle);
			return 0;
		}
		Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(prefabHandle);
		Entity root = prefab ? GetScene()->Instantiate(prefab, translation, rotation) : Entity{};
		return root ? (uint64_t)root.GetUUID() : 0;
	}

	// Scene changes are requests: the layer that owns the running scene performs them (Scene::RequestSceneLoad), and
	// an additive load or unload runs when this frame's update ends.
	static void Scene_Load(uint64_t sceneHandle)
	{
		if (!sceneHandle)
		{
			LUX_CONSOLE_LOG_ERROR("Scene.Load: the scene is not set");
			return;
		}
		GetScene()->RequestSceneLoad(sceneHandle);
	}

	static void Scene_LoadAdditive(uint64_t sceneHandle)
	{
		if (!sceneHandle)
		{
			LUX_CONSOLE_LOG_ERROR("Scene.LoadAdditive: the scene is not set");
			return;
		}
		GetScene()->RequestAdditiveScene(sceneHandle);
	}

	static void Scene_UnloadAdditive(uint64_t sceneHandle)
	{
		if (sceneHandle)
			GetScene()->RequestUnloadAdditiveScene(sceneHandle);
	}

	static uint64_t Scene_GetCurrent()
	{
		return static_cast<uint64_t>(GetScene()->Handle);
	}

	static Coral::Bool32 Entity_GetPersistent(uint64_t entityID)
	{
		return GetScene()->IsEntityPersistent(GetEntity(entityID));
	}

	// Timers and coroutines ask these every frame; unlike the entity calls they stay quiet about an entity that is gone.
	static Coral::Bool32 Entity_IsAlive(uint64_t entityID)
	{
		Scene* scene = ScriptEngine::GetInstance().GetCurrentScene().Raw();
		return scene && scene->GetEntityByUUID(entityID);
	}

	static Coral::Bool32 Entity_IsRunning(uint64_t entityID)
	{
		Scene* scene = ScriptEngine::GetInstance().GetCurrentScene().Raw();
		const Entity entity = scene ? scene->GetEntityByUUID(entityID) : Entity{};
		return entity && scene->IsEntityActiveInHierarchy(entity);
	}

	static void Entity_SetPersistent(uint64_t entityID, Coral::Bool32 persistent)
	{
		GetScene()->SetEntityPersistent(GetEntity(entityID), persistent);
	}

	static void Gizmos_DrawLine(glm::vec3* start, glm::vec3* end, glm::vec4* color, float duration, Coral::Bool32 depthTest)
	{
		GetScene()->AddDebugLine(*start, *end, *color, duration, depthTest);
	}

	static void Scene_DestroyEntity(uint64_t entityID)
	{
		// Deferred to the end of the update: the caller may be the script being destroyed, inside the
		// loop over script instances.
		GetScene()->SubmitToDestroyEntity(GetEntity(entityID));
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

	// An entity's own switch (the inspector's checkbox); a parent switched off still switches it off in effect.
	static Coral::Bool32 Entity_GetActive(uint64_t entityID)
	{
		return GetEntity(entityID).GetComponent<TagComponent>().Active;
	}

	static void Entity_SetActive(uint64_t entityID, Coral::Bool32 active)
	{
		GetScene()->SetEntityActive(GetEntity(entityID), active);
	}

	static Coral::Bool32 Entity_IsActiveInHierarchy(uint64_t entityID)
	{
		return GetScene()->IsEntityActiveInHierarchy(GetEntity(entityID));
	}

	// Whether the entity's script component runs: a switched-off script gets no OnUpdate or OnFixedUpdate.
	static Coral::Bool32 Entity_GetScriptEnabled(uint64_t entityID)
	{
		Entity entity = GetEntity(entityID);
		return entity.HasComponent<ScriptComponent>() && entity.GetComponent<ScriptComponent>().Enabled;
	}

	static void Entity_SetScriptEnabled(uint64_t entityID, Coral::Bool32 enabled)
	{
		Entity entity = GetEntity(entityID);
		if (entity.HasComponent<ScriptComponent>())
			entity.GetComponent<ScriptComponent>().Enabled = enabled;
	}

	static Coral::Bool32 Component_GetEnabled(uint64_t entityID, Coral::ReflectionType componentType)
	{
		const auto it = s_GetEnabledFuncs.find(componentType.m_TypeID);
		if (it != s_GetEnabledFuncs.end())
			return it->second(GetEntity(entityID));
		// No switch: the component is always on, as long as the entity still has it.
		const auto has = s_HasComponentFuncs.find(componentType.m_TypeID);
		return has != s_HasComponentFuncs.end() && has->second(GetEntity(entityID));
	}

	static void Component_SetEnabled(uint64_t entityID, Coral::ReflectionType componentType, Coral::Bool32 enabled)
	{
		const auto it = s_SetEnabledFuncs.find(componentType.m_TypeID);
		if (it != s_SetEnabledFuncs.end())
			it->second(GetEntity(entityID), enabled);
		else
			LUX_CONSOLE_LOG_WARN("Component.Enabled was set on a component that has no enabled switch; it stays on");
	}

	static Coral::String Entity_GetUserTag(uint64_t entityID)
	{
		return Coral::String::New(GetEntity(entityID).GetComponent<TagComponent>().UserTag);
	}

	static void Entity_SetUserTag(uint64_t entityID, Coral::String tag)
	{
		GetEntity(entityID).GetComponent<TagComponent>().UserTag = tag;
	}

	// The finders return entity IDs in creation order (the order of the scene file), so a result never depends on
	// the layout of the registry. Switched-off entities, and children of one, are left out unless asked for.
	static Coral::Array<uint64_t> ToIDArray(std::vector<EntityHit>& hits, Scene* scene, bool includeInactive)
	{
		std::sort(hits.begin(), hits.end());
		std::vector<uint64_t> ids;
		ids.reserve(hits.size());
		for (const EntityHit& hit : hits)
		{
			if (includeInactive || scene->IsEntityActiveInHierarchy(scene->GetEntityByUUID(hit.second)))
				ids.push_back(hit.second);
		}
		Coral::Array<uint64_t> result = Coral::Array<uint64_t>::New(static_cast<int32_t>(ids.size()));
		for (size_t i = 0; i < ids.size(); i++)
			result[i] = ids[i];
		return result;
	}

	template<typename Matches>
	static Coral::Array<uint64_t> FindEntities(bool includeInactive, Matches&& matches)
	{
		Scene* scene = GetScene();
		std::vector<EntityHit> hits;
		for (auto handle : scene->GetAllEntitiesWith<IDComponent, TagComponent>())
		{
			Entity entity{ handle, scene };
			if (matches(entity.GetComponent<TagComponent>()))
				hits.emplace_back(static_cast<uint32_t>(handle), static_cast<uint64_t>(entity.GetUUID()));
		}
		return ToIDArray(hits, scene, includeInactive);
	}

	static Coral::Array<uint64_t> Scene_FindEntitiesByName(Coral::String name, Coral::Bool32 includeInactive)
	{
		const std::string wanted = name;
		return FindEntities(includeInactive, [&](const TagComponent& tag) { return tag.Tag == wanted; });
	}

	static Coral::Array<uint64_t> Scene_FindEntitiesByTag(Coral::String tag, Coral::Bool32 includeInactive)
	{
		const std::string wanted = tag;
		if (wanted.empty())
			return Coral::Array<uint64_t>::New(0);
		return FindEntities(includeInactive, [&](const TagComponent& candidate) { return candidate.UserTag == wanted; });
	}

	static Coral::Array<uint64_t> Scene_FindEntitiesWithComponent(Coral::ReflectionType componentType, Coral::Bool32 includeInactive)
	{
		Scene* scene = GetScene();
		std::vector<EntityHit> hits;
		if (const auto it = s_FindComponentFuncs.find(componentType.m_TypeID); it != s_FindComponentFuncs.end())
			it->second(scene, hits);
		return ToIDArray(hits, scene, includeInactive);
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

	// Quaternions cross the boundary as (x, y, z, w), not glm's constructor order.
	static void TransformComponent_GetRotationQuat(uint64_t entityID, glm::vec4* out)
	{
		const glm::quat q = GetEntity(entityID).GetComponent<TransformComponent>().GetRotation();
		*out = { q.x, q.y, q.z, q.w };
	}

	static void TransformComponent_SetRotationQuat(uint64_t entityID, glm::vec4* in)
	{
		const glm::quat q = glm::normalize(glm::quat(in->w, in->x, in->y, in->z));
		GetEntity(entityID).GetComponent<TransformComponent>().SetRotation(q);
	}

	// Position and rotation in world space, through every parent.
	static void TransformComponent_GetWorld(uint64_t entityID, glm::vec3* outTranslation, glm::vec4* outRotation)
	{
		const TransformComponent world = GetScene()->GetWorldSpaceTransform(GetEntity(entityID));
		const glm::quat q = world.GetRotation();
		*outTranslation = world.Translation;
		*outRotation = { q.x, q.y, q.z, q.w };
	}

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
	static int32_t SpriteRendererComponent_GetSpriteIndex(uint64_t entityID)        { return GetEntity(entityID).GetComponent<SpriteRendererComponent>().SpriteIndex; }
	static void SpriteRendererComponent_SetSpriteIndex(uint64_t entityID, int32_t v){ GetEntity(entityID).GetComponent<SpriteRendererComponent>().SpriteIndex = v; }
	static int32_t SpriteRendererComponent_GetOrderInLayer(uint64_t entityID)       { return GetEntity(entityID).GetComponent<SpriteRendererComponent>().OrderInLayer; }
	static void SpriteRendererComponent_SetOrderInLayer(uint64_t entityID, int32_t v){ GetEntity(entityID).GetComponent<SpriteRendererComponent>().OrderInLayer = v; }

	// Sorting layers are named in the project; a script names one, the component stores its ID.
	static Coral::String SpriteRendererComponent_GetSortingLayer(uint64_t entityID)
	{
		const uint32_t layerID = GetEntity(entityID).GetComponent<SpriteRendererComponent>().SortingLayer;
		for (const ProjectSortingLayer& layer : Project::GetActive()->GetConfig().SortingLayers)
		{
			if (layer.ID == layerID)
				return Coral::String::New(layer.Name);
		}
		return Coral::String::New("Default");
	}

	static void SpriteRendererComponent_SetSortingLayer(uint64_t entityID, Coral::String name)
	{
		const std::string wanted = name;
		for (const ProjectSortingLayer& layer : Project::GetActive()->GetConfig().SortingLayers)
		{
			if (layer.Name == wanted)
			{
				GetEntity(entityID).GetComponent<SpriteRendererComponent>().SortingLayer = layer.ID;
				return;
			}
		}
		LUX_CORE_WARN_TAG("ScriptEngine", "SpriteRenderer.SortingLayer: the project has no sorting layer named '{}'", wanted);
	}

	#pragma endregion

	#pragma region TilemapComponent

	static int32_t TilemapComponent_GetLayerCount(uint64_t entityID) { return static_cast<int32_t>(GetEntity(entityID).GetComponent<TilemapComponent>().Layers.size()); }

	static int32_t TilemapComponent_GetTile(uint64_t entityID, int32_t x, int32_t y, int32_t layer)
	{
		const auto& tilemap = GetEntity(entityID).GetComponent<TilemapComponent>();
		if (layer < 0 || static_cast<size_t>(layer) >= tilemap.Layers.size())
			return -1;
		return Tilemap::GetTile(tilemap.Layers[static_cast<size_t>(layer)], x, y);
	}

	static void TilemapComponent_SetTile(uint64_t entityID, int32_t x, int32_t y, int32_t tile, int32_t layer)
	{
		Entity entity = GetEntity(entityID);
		auto& tilemap = entity.GetComponent<TilemapComponent>();
		if (layer < 0 || static_cast<size_t>(layer) >= tilemap.Layers.size())
		{
			LUX_CORE_WARN_TAG("ScriptEngine", "Tilemap.SetTile: the tilemap has no layer {} ({} layers)", layer, tilemap.Layers.size());
			return;
		}
		// The solid-tile shapes follow the change while the scene plays.
		if (Tilemap::SetTile(tilemap.Layers[static_cast<size_t>(layer)], x, y, tile) && tilemap.Layers[static_cast<size_t>(layer)].Collides)
		{
			if (PhysicsScene2D* physics = GetScene()->GetPhysicsScene2D())
				physics->RebuildTilemap(entity);
		}
	}

	// The cell under a world point, and the centre of a cell in world space, through the entity's transform and cell size.
	static void TilemapComponent_WorldToCell(uint64_t entityID, glm::vec2* world, int32_t* x, int32_t* y)
	{
		Entity entity = GetEntity(entityID);
		const auto& tilemap = entity.GetComponent<TilemapComponent>();
		const glm::vec2 local = glm::vec2(glm::inverse(GetScene()->GetWorldSpaceTransformMatrix(entity)) * glm::vec4(world->x, world->y, 0.0f, 1.0f)) / tilemap.CellSize;
		*x = static_cast<int32_t>(std::floor(local.x));
		*y = static_cast<int32_t>(std::floor(local.y));
	}

	static void TilemapComponent_CellToWorld(uint64_t entityID, int32_t x, int32_t y, glm::vec2* out)
	{
		Entity entity = GetEntity(entityID);
		const auto& tilemap = entity.GetComponent<TilemapComponent>();
		const glm::vec2 local = (glm::vec2(static_cast<float>(x), static_cast<float>(y)) + glm::vec2(0.5f)) * tilemap.CellSize;
		*out = glm::vec2(GetScene()->GetWorldSpaceTransformMatrix(entity) * glm::vec4(local, 0.0f, 1.0f));
	}

	#pragma endregion

	#pragma region ParticleSystemComponent

	// The system's live particles, made on the first call; a script may play a system before the scene's first step.
	static Particles::State& ParticlesOf(uint64_t entityID)
	{
		const Entity entity = GetEntity(entityID);
		return *GetScene()->GetOrCreateParticleState(entity, entity.GetComponent<ParticleSystemComponent>().PlayOnAwake);
	}

	static void ParticleSystemComponent_Play(uint64_t entityID)
	{
		Particles::Play(ParticlesOf(entityID), GetEntity(entityID).GetComponent<ParticleSystemComponent>().Settings);
	}

	static void ParticleSystemComponent_Stop(uint64_t entityID)  { Particles::Stop(ParticlesOf(entityID)); }
	static void ParticleSystemComponent_Pause(uint64_t entityID) { ParticlesOf(entityID).Playing = false; }
	static void ParticleSystemComponent_Clear(uint64_t entityID)
	{
		Particles::State& state = ParticlesOf(entityID);
		const bool emitting = state.Emitting;
		Particles::Clear(state);
		state.Emitting = emitting;
	}

	static void ParticleSystemComponent_Emit(uint64_t entityID, int32_t count)
	{
		const Entity entity = GetEntity(entityID);
		Particles::Emit(ParticlesOf(entityID), entity.GetComponent<ParticleSystemComponent>().Settings, GetScene()->GetWorldSpaceTransformMatrix(entity), static_cast<uint32_t>(std::max(count, 0)));
	}

	static Coral::Bool32 ParticleSystemComponent_IsPlaying(uint64_t entityID)
	{
		const Particles::State& state = ParticlesOf(entityID);
		const Particles::Settings& settings = GetEntity(entityID).GetComponent<ParticleSystemComponent>().Settings;
		return state.Playing && ((state.Emitting && (settings.Looping || state.Time < settings.Duration)) || state.Count() > 0);
	}

	static int32_t ParticleSystemComponent_GetParticleCount(uint64_t entityID)       { return static_cast<int32_t>(ParticlesOf(entityID).Count()); }
	static Coral::Bool32 ParticleSystemComponent_GetLooping(uint64_t entityID)       { return GetEntity(entityID).GetComponent<ParticleSystemComponent>().Settings.Looping; }
	static void ParticleSystemComponent_SetLooping(uint64_t entityID, Coral::Bool32 looping) { GetEntity(entityID).GetComponent<ParticleSystemComponent>().Settings.Looping = looping; }
	static float ParticleSystemComponent_GetEmissionRate(uint64_t entityID)          { return GetEntity(entityID).GetComponent<ParticleSystemComponent>().Settings.Rate; }
	static void ParticleSystemComponent_SetEmissionRate(uint64_t entityID, float rate) { GetEntity(entityID).GetComponent<ParticleSystemComponent>().Settings.Rate = std::max(rate, 0.0f); }

	#pragma endregion

	#pragma region SpriteAnimatorComponent

	static void SpriteAnimatorComponent_Play(uint64_t entityID, Coral::String clip, Coral::Bool32 restart)
	{
		auto& animator = GetEntity(entityID).GetComponent<SpriteAnimatorComponent>();
		const std::string name = clip;
		// Playing the clip that is already running leaves it running; anything else starts it from the top.
		if (restart || animator.Clip != name || !animator.Playing)
		{
			animator.Time = 0.0f;
			animator.Finished = false;
		}
		animator.Clip = name;
		animator.Playing = true;
	}

	static void SpriteAnimatorComponent_Stop(uint64_t entityID)                    { GetEntity(entityID).GetComponent<SpriteAnimatorComponent>().Playing = false; }
	static Coral::String SpriteAnimatorComponent_GetClip(uint64_t entityID)        { return Coral::String::New(GetEntity(entityID).GetComponent<SpriteAnimatorComponent>().Clip); }
	static Coral::Bool32 SpriteAnimatorComponent_IsPlaying(uint64_t entityID)      { return GetEntity(entityID).GetComponent<SpriteAnimatorComponent>().Playing; }
	static Coral::Bool32 SpriteAnimatorComponent_IsFinished(uint64_t entityID)     { return GetEntity(entityID).GetComponent<SpriteAnimatorComponent>().Finished; }
	static float SpriteAnimatorComponent_GetSpeed(uint64_t entityID)               { return GetEntity(entityID).GetComponent<SpriteAnimatorComponent>().Speed; }
	static void SpriteAnimatorComponent_SetSpeed(uint64_t entityID, float speed)   { GetEntity(entityID).GetComponent<SpriteAnimatorComponent>().Speed = speed; }

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
	static Coral::Bool32 BoxCollider2DComponent_GetIsTrigger(uint64_t entityID)    { return GetEntity(entityID).GetComponent<BoxCollider2DComponent>().IsTrigger; }
	static void BoxCollider2DComponent_SetIsTrigger(uint64_t entityID, Coral::Bool32 v)
	{
		auto& collider = GetEntity(entityID).GetComponent<BoxCollider2DComponent>();
		collider.IsTrigger = v;
		if (collider.RuntimeFixture) // while playing, the fixture follows at once
			static_cast<b2Fixture*>(collider.RuntimeFixture)->SetSensor(v);
	}
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
	static Coral::Bool32 CircleCollider2DComponent_GetIsTrigger(uint64_t entityID)    { return GetEntity(entityID).GetComponent<CircleCollider2DComponent>().IsTrigger; }
	static void CircleCollider2DComponent_SetIsTrigger(uint64_t entityID, Coral::Bool32 v)
	{
		auto& collider = GetEntity(entityID).GetComponent<CircleCollider2DComponent>();
		collider.IsTrigger = v;
		if (collider.RuntimeFixture)
			static_cast<b2Fixture*>(collider.RuntimeFixture)->SetSensor(v);
	}
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

	#pragma region Physics2D queries

	// One 2D hit, as the C# Lux.RaycastHit2D lays it out.
	struct NativeRaycastHit2D
	{
		uint64_t Entity;
		glm::vec2 Point;
		glm::vec2 Normal;
		float Distance;
	};
	static_assert(sizeof(NativeRaycastHit2D) == 32, "NativeRaycastHit2D must match the C# Lux.RaycastHit2D layout");

	static void FillHit2D(const PhysicsScene2D::RayHit& hit, NativeRaycastHit2D* out)
	{
		out->Entity = static_cast<uint64_t>(hit.EntityID);
		out->Point = hit.Point;
		out->Normal = hit.Normal;
		out->Distance = hit.Distance;
	}

	// Like the 3D queries: main thread, between steps; without a 2D world (not playing) nothing is hit.
	static Coral::Bool32 Physics2D_Raycast(glm::vec2* origin, glm::vec2* direction, float maxDistance, uint64_t ignore, Coral::Bool32 includeTriggers, NativeRaycastHit2D* outHit)
	{
		PhysicsScene2D* physics = GetScene()->GetPhysicsScene2D();
		PhysicsScene2D::RayHit hit;
		if (!physics || !physics->RayCast(*origin, *direction, maxDistance, UUID(ignore), includeTriggers, hit))
			return false;
		FillHit2D(hit, outHit);
		return true;
	}

	// Returns the number found, which may exceed `capacity`; only `capacity` are written.
	static int32_t Physics2D_RaycastAll(glm::vec2* origin, glm::vec2* direction, float maxDistance, uint64_t ignore, Coral::Bool32 includeTriggers, NativeRaycastHit2D* buffer, int32_t capacity)
	{
		PhysicsScene2D* physics = GetScene()->GetPhysicsScene2D();
		if (!physics)
			return 0;

		std::vector<PhysicsScene2D::RayHit> hits;
		physics->RayCastAll(*origin, *direction, maxDistance, UUID(ignore), includeTriggers, hits);
		for (int32_t i = 0; i < static_cast<int32_t>(hits.size()) && i < capacity; i++)
			FillHit2D(hits[static_cast<size_t>(i)], &buffer[i]);
		return static_cast<int32_t>(hits.size());
	}

	// shape 0: circle (radius in dimensions.x; radius 0 is a point), 1: box (half extents, turned by angle).
	static int32_t Physics2D_Overlap(int32_t shape, glm::vec2* center, glm::vec2* dimensions, float angle, uint64_t ignore, Coral::Bool32 includeTriggers, uint64_t* buffer, int32_t capacity)
	{
		PhysicsScene2D* physics = GetScene()->GetPhysicsScene2D();
		if (!physics)
			return 0;

		std::vector<UUID> entities;
		physics->Overlap(shape == 1 ? PhysicsScene2D::QueryShape::Box : PhysicsScene2D::QueryShape::Circle, *center, *dimensions, angle, UUID(ignore), includeTriggers, entities);
		for (int32_t i = 0; i < static_cast<int32_t>(entities.size()) && i < capacity; i++)
			buffer[i] = static_cast<uint64_t>(entities[static_cast<size_t>(i)]);
		return static_cast<int32_t>(entities.size());
	}

	#pragma endregion

	#pragma region Physics queries

	// One hit, as the C# Lux.RaycastHit lays it out.
	struct NativeRaycastHit
	{
		uint64_t Entity;
		glm::vec3 Point;
		glm::vec3 Normal;
		float Distance;
	};
	static_assert(sizeof(NativeRaycastHit) == 40, "NativeRaycastHit must match the C# Lux.RaycastHit layout");

	// The shape codes of the C# Physics class: sphere (radius in x), box (half extents), capsule (radius in x,
	// half height of the straight part in y).
	enum class QueryShape : int32_t { Sphere = 0, Box = 1, Capsule = 2 };

	static void FillHit(const SceneQueryHit& hit, NativeRaycastHit* out)
	{
		out->Entity = static_cast<uint64_t>(hit.HitEntity);
		out->Point = hit.Position;
		out->Normal = hit.Normal;
		out->Distance = hit.Distance;
	}

	static glm::quat ToQuat(const glm::vec4* xyzw)
	{
		const glm::quat q(xyzw->w, xyzw->x, xyzw->y, xyzw->z);
		return glm::dot(q, q) > 1e-12f ? glm::normalize(q) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	}

	// Queries run on the main thread between physics steps; without a physics scene (not playing) nothing is hit.
	static Coral::Bool32 Physics_Raycast(glm::vec3* origin, glm::vec3* direction, float maxDistance, uint32_t layerMask, uint64_t ignore,
		Coral::Bool32 includeTriggers, NativeRaycastHit* outHit)
	{
		Ref<PhysicsScene> physics = GetScene()->GetPhysicsScene();
		if (!physics)
			return false;

		RayCastInfo ray;
		ray.Origin = *origin;
		ray.Direction = *direction;
		ray.MaxDistance = maxDistance;
		ray.LayerMask = layerMask;
		ray.IncludeTriggers = includeTriggers;
		if (ignore)
			ray.ExcludedEntities.insert(UUID(ignore));

		SceneQueryHit hit;
		if (!physics->CastRay(&ray, hit))
			return false;
		FillHit(hit, outHit);
		return true;
	}

	// Every body the ray enters, nearest first, one hit per surface the ray goes into: the back faces
	// CastRayAll also reports (where the ray leaves a solid, or starts inside one) are dropped. Returns the
	// number found, which may exceed `capacity`; only `capacity` are written.
	static int32_t Physics_RaycastAll(glm::vec3* origin, glm::vec3* direction, float maxDistance, uint32_t layerMask, uint64_t ignore,
		Coral::Bool32 includeTriggers, NativeRaycastHit* buffer, int32_t capacity)
	{
		Ref<PhysicsScene> physics = GetScene()->GetPhysicsScene();
		if (!physics)
			return 0;

		RayCastInfo ray;
		ray.Origin = *origin;
		ray.Direction = *direction;
		ray.MaxDistance = maxDistance;
		ray.LayerMask = layerMask;
		ray.IncludeTriggers = includeTriggers;
		if (ignore)
			ray.ExcludedEntities.insert(UUID(ignore));

		std::vector<SceneQueryHit> hits;
		physics->CastRayAll(&ray, hits);
		const glm::vec3 unitDirection = glm::normalize(*direction);

		int32_t count = 0;
		for (const SceneQueryHit& hit : hits)
		{
			if (glm::dot(hit.Normal, unitDirection) >= 0.0f)
				continue;
			if (count < capacity)
				FillHit(hit, &buffer[count]);
			count++;
		}
		return count;
	}

	static Coral::Bool32 Physics_ShapeCast(int32_t shape, glm::vec3* origin, glm::vec4* rotation, glm::vec3* dimensions, glm::vec3* direction,
		float maxDistance, uint32_t layerMask, uint64_t ignore, Coral::Bool32 includeTriggers, NativeRaycastHit* outHit)
	{
		Ref<PhysicsScene> physics = GetScene()->GetPhysicsScene();
		if (!physics)
			return false;

		SceneQueryHit hit;
		bool found = false;
		const auto run = [&](auto& info)
		{
			info.Origin = *origin;
			info.Rotation = ToQuat(rotation);
			info.Direction = *direction;
			info.MaxDistance = maxDistance;
			info.LayerMask = layerMask;
			info.IncludeTriggers = includeTriggers;
			if (ignore)
				info.ExcludedEntities.insert(UUID(ignore));
			found = physics->CastShape(&info, hit);
		};
		switch (static_cast<QueryShape>(shape))
		{
			case QueryShape::Sphere: { SphereCastInfo info; info.Radius = dimensions->x; run(info); break; }
			case QueryShape::Box: { BoxCastInfo info; info.HalfExtent = *dimensions; run(info); break; }
			case QueryShape::Capsule: { CapsuleCastInfo info; info.Radius = dimensions->x; info.HalfHeight = dimensions->y; run(info); break; }
			default: return false;
		}
		if (found)
			FillHit(hit, outHit);
		return found;
	}

	// The entities with a body touching the shape, each once, in the order Jolt found them. Returns the number
	// found, which may exceed `capacity`; only `capacity` are written.
	static int32_t Physics_OverlapShape(int32_t shape, glm::vec3* origin, glm::vec4* rotation, glm::vec3* dimensions, uint32_t layerMask,
		uint64_t ignore, Coral::Bool32 includeTriggers, uint64_t* buffer, int32_t capacity)
	{
		Ref<PhysicsScene> physics = GetScene()->GetPhysicsScene();
		if (!physics)
			return 0;

		SceneQueryHit* hits = nullptr;
		int32_t hitCount = 0;
		const auto run = [&](auto& info)
		{
			info.Origin = *origin;
			info.Rotation = ToQuat(rotation);
			info.LayerMask = layerMask;
			info.IncludeTriggers = includeTriggers;
			if (ignore)
				info.ExcludedEntities.insert(UUID(ignore));
			hitCount = physics->OverlapShape(&info, &hits);
		};
		switch (static_cast<QueryShape>(shape))
		{
			case QueryShape::Sphere: { SphereOverlapInfo info; info.Radius = dimensions->x; run(info); break; }
			case QueryShape::Box: { BoxOverlapInfo info; info.HalfExtent = *dimensions; run(info); break; }
			case QueryShape::Capsule: { CapsuleOverlapInfo info; info.Radius = dimensions->x; info.HalfHeight = dimensions->y; run(info); break; }
			default: return 0;
		}

		std::vector<uint64_t> unique;
		for (int32_t i = 0; i < hitCount; i++)
		{
			const uint64_t id = static_cast<uint64_t>(hits[i].HitEntity);
			if (std::find(unique.begin(), unique.end(), id) == unique.end())
				unique.push_back(id);
		}
		for (size_t i = 0; i < unique.size() && static_cast<int32_t>(i) < capacity; i++)
			buffer[i] = unique[i];
		return static_cast<int32_t>(unique.size());
	}

	// The bit a physics layer owns in a layer mask: its index in the project list. -1 for a name the project lacks.
	static int32_t Physics_GetLayerIndex(Coral::String name)
	{
		const std::string wanted = name;
		const Ref<Project> project = Project::GetActive();
		const std::vector<ProjectPhysicsLayer>* layers = project ? &project->GetConfig().Physics.Layers : nullptr;
		if (!layers || layers->empty())
			return wanted == "Default" ? 0 : -1;
		for (size_t i = 0; i < layers->size(); i++)
		{
			if ((*layers)[i].Name == wanted)
				return static_cast<int32_t>(i);
		}
		return -1;
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
		s_FindComponentFuncs[id] = [](Scene* scene, std::vector<EntityHit>& hits)
		{
			for (auto handle : scene->GetAllEntitiesWith<IDComponent, TComponent>())
				hits.emplace_back(static_cast<uint32_t>(handle), static_cast<uint64_t>(Entity{ handle, scene }.GetUUID()));
		};
		if constexpr (HasEnabledFlag<TComponent>)
		{
			s_GetEnabledFuncs[id] = [](Entity entity) { return entity.HasComponent<TComponent>() && GetEnabledFlag(entity.GetComponent<TComponent>()); };
			s_SetEnabledFuncs[id] = [](Entity entity, bool enabled)
			{
				if (entity.HasComponent<TComponent>())
					GetEnabledFlag(entity.GetComponent<TComponent>()) = enabled;
			};
		}
	}

	static void RegisterComponentTypes(Coral::ManagedAssembly& coreAssembly)
	{
		s_HasComponentFuncs.clear();
		s_AddComponentFuncs.clear();
		s_RemoveComponentFuncs.clear();
		s_GetEnabledFuncs.clear();
		s_SetEnabledFuncs.clear();
		s_FindComponentFuncs.clear();

		RegisterManagedComponent<TransformComponent>(coreAssembly);
		RegisterManagedComponent<TagComponent>(coreAssembly);
		RegisterManagedComponent<SpriteRendererComponent>(coreAssembly);
		RegisterManagedComponent<SpriteAnimatorComponent>(coreAssembly);
		RegisterManagedComponent<ParticleSystemComponent>(coreAssembly);
		RegisterManagedComponent<TilemapComponent>(coreAssembly);
		RegisterManagedComponent<UICanvasComponent>(coreAssembly);
		RegisterManagedComponent<UIElementComponent>(coreAssembly);
		RegisterManagedComponent<UIImageComponent>(coreAssembly);
		RegisterManagedComponent<UITextComponent>(coreAssembly);
		RegisterManagedComponent<UIWidgetComponent>(coreAssembly);
		RegisterManagedComponent<CircleRendererComponent>(coreAssembly);
		RegisterManagedComponent<CameraComponent>(coreAssembly);
		RegisterManagedComponent<RigidBody2DComponent>(coreAssembly);
		RegisterManagedComponent<BoxCollider2DComponent>(coreAssembly);
		RegisterManagedComponent<CircleCollider2DComponent>(coreAssembly);
		RegisterManagedComponent<TextComponent>(coreAssembly);
		RegisterManagedComponent<AudioSourceComponent>(coreAssembly);
		RegisterManagedComponent<AudioListenerComponent>(coreAssembly);
		RegisterManagedComponent<AudioSurfaceComponent>(coreAssembly);
		RegisterManagedComponent<AudioZoneComponent>(coreAssembly);
		RegisterManagedComponent<AudioPortalComponent>(coreAssembly);
		RegisterManagedComponent<SkeletonComponent>(coreAssembly);
		RegisterManagedComponent<StaticMeshComponent>(coreAssembly);
		RegisterManagedComponent<MusicDirectorComponent>(coreAssembly);
		RegisterManagedComponent<DirectionalLightComponent>(coreAssembly);
		RegisterManagedComponent<PointLightComponent>(coreAssembly);
		RegisterManagedComponent<SpotLightComponent>(coreAssembly);
		RegisterManagedComponent<AreaLightComponent>(coreAssembly);
		RegisterManagedComponent<SkyLightComponent>(coreAssembly);

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
		LUX_ADD_INTERNAL_CALL(Application_Quit);
		LUX_ADD_INTERNAL_CALL(Application_IsFocused);
		LUX_ADD_INTERNAL_CALL(Application_GetPaused);
		LUX_ADD_INTERNAL_CALL(Application_SetPaused);
		LUX_ADD_INTERNAL_CALL(Application_GetWidth);
		LUX_ADD_INTERNAL_CALL(Application_GetHeight);

		LUX_ADD_INTERNAL_CALL(Time_Get);
		LUX_ADD_INTERNAL_CALL(Time_SetScale);

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
		LUX_ADD_INTERNAL_CALL(Input_IsGamepadConnected);
		LUX_ADD_INTERNAL_CALL(Input_GetGamepadName);
		LUX_ADD_INTERNAL_CALL(Input_IsGamepadButtonDown);
		LUX_ADD_INTERNAL_CALL(Input_IsGamepadButtonPressed);
		LUX_ADD_INTERNAL_CALL(Input_IsGamepadButtonReleased);
		LUX_ADD_INTERNAL_CALL(Input_GetGamepadAxis);
		LUX_ADD_INTERNAL_CALL(Input_GetGamepadDeadzone);
		LUX_ADD_INTERNAL_CALL(Input_SetGamepadDeadzone);
		LUX_ADD_INTERNAL_CALL(Input_SupportsTriggerEffects);
		LUX_ADD_INTERNAL_CALL(Input_SetGamepadTriggerEffect);
		LUX_ADD_INTERNAL_CALL(Input_ResetGamepadTriggerEffects);
		LUX_ADD_INTERNAL_CALL(Input_SupportsRumble);
		LUX_ADD_INTERNAL_CALL(Input_RumbleGamepad);
		LUX_ADD_INTERNAL_CALL(Input_StopGamepadRumble);
		LUX_ADD_INTERNAL_CALL(Input_GetGamepadType);
		LUX_ADD_INTERNAL_CALL(Input_SupportsLightbar);
		LUX_ADD_INTERNAL_CALL(Input_SetGamepadLightColor);
		LUX_ADD_INTERNAL_CALL(Input_SetGamepadPlayerLights);
		LUX_ADD_INTERNAL_CALL(Input_ResetGamepadLights);

		LUX_ADD_INTERNAL_CALL(Scene_CreateEntity);
		LUX_ADD_ENTITY_CALL(Scene_DestroyEntity);
		LUX_ADD_INTERNAL_CALL(Scene_InstantiatePrefab);
		LUX_ADD_INTERNAL_CALL(Gizmos_DrawLine);
		LUX_ADD_INTERNAL_CALL(Scene_Load);
		LUX_ADD_INTERNAL_CALL(Scene_LoadAdditive);
		LUX_ADD_INTERNAL_CALL(Scene_UnloadAdditive);
		LUX_ADD_INTERNAL_CALL(Scene_GetCurrent);
		LUX_ADD_ENTITY_CALL(Entity_GetPersistent);
		LUX_ADD_INTERNAL_CALL(Entity_IsAlive);
		LUX_ADD_INTERNAL_CALL(Entity_IsRunning);
		LUX_ADD_ENTITY_CALL(Entity_SetPersistent);

		LUX_ADD_ENTITY_CALL(GetScriptInstance);
		LUX_ADD_ENTITY_CALL(Entity_HasComponent);
		LUX_ADD_ENTITY_CALL(Entity_AddComponent);
		LUX_ADD_ENTITY_CALL(Entity_RemoveComponent);
		LUX_ADD_INTERNAL_CALL(Entity_FindEntityByName);
		LUX_ADD_ENTITY_CALL(Entity_GetName);
		LUX_ADD_ENTITY_CALL(Entity_SetName);
		LUX_ADD_ENTITY_CALL(Entity_GetParent);
		LUX_ADD_ENTITY_CALL(Entity_SetParent);
		LUX_ADD_ENTITY_CALL(Entity_GetChildren);
		LUX_ADD_ENTITY_CALL(Entity_GetActive);
		LUX_ADD_ENTITY_CALL(Entity_SetActive);
		LUX_ADD_ENTITY_CALL(Entity_IsActiveInHierarchy);
		LUX_ADD_ENTITY_CALL(Entity_GetUserTag);
		LUX_ADD_ENTITY_CALL(Entity_SetUserTag);
		LUX_ADD_INTERNAL_CALL(Scene_FindEntitiesByName);
		LUX_ADD_INTERNAL_CALL(Scene_FindEntitiesByTag);
		LUX_ADD_INTERNAL_CALL(Scene_FindEntitiesWithComponent);
		LUX_ADD_ENTITY_CALL(Entity_GetScriptEnabled);
		LUX_ADD_ENTITY_CALL(Entity_SetScriptEnabled);
		LUX_ADD_ENTITY_CALL(Component_GetEnabled);
		LUX_ADD_ENTITY_CALL(Component_SetEnabled);

		LUX_ADD_ENTITY_CALL(TransformComponent_GetTranslation);
		LUX_ADD_ENTITY_CALL(TransformComponent_SetTranslation);
		LUX_ADD_ENTITY_CALL(TransformComponent_GetRotation);
		LUX_ADD_ENTITY_CALL(TransformComponent_SetRotation);
		LUX_ADD_ENTITY_CALL(TransformComponent_GetScale);
		LUX_ADD_ENTITY_CALL(TransformComponent_SetScale);
		LUX_ADD_ENTITY_CALL(TransformComponent_GetRotationQuat);
		LUX_ADD_ENTITY_CALL(TransformComponent_SetRotationQuat);
		LUX_ADD_ENTITY_CALL(TransformComponent_GetWorld);

		LUX_ADD_ENTITY_CALL(TagComponent_GetTag);
		LUX_ADD_ENTITY_CALL(TagComponent_SetTag);

		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_GetColor);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_SetColor);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_GetTilingFactor);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_SetTilingFactor);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_GetTexture);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_SetTexture);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_GetSpriteIndex);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_SetSpriteIndex);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_GetOrderInLayer);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_SetOrderInLayer);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_GetSortingLayer);
		LUX_ADD_ENTITY_CALL(SpriteRendererComponent_SetSortingLayer);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_GetIsTrigger);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_SetIsTrigger);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_GetIsTrigger);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_SetIsTrigger);
		LUX_ADD_ENTITY_CALL(TilemapComponent_GetLayerCount);
		LUX_ADD_ENTITY_CALL(TilemapComponent_GetTile);
		LUX_ADD_ENTITY_CALL(TilemapComponent_SetTile);
		LUX_ADD_ENTITY_CALL(TilemapComponent_WorldToCell);
		LUX_ADD_ENTITY_CALL(TilemapComponent_CellToWorld);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_Play);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_Stop);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_Pause);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_Clear);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_Emit);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_IsPlaying);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_GetParticleCount);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_GetLooping);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_SetLooping);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_GetEmissionRate);
		LUX_ADD_ENTITY_CALL(ParticleSystemComponent_SetEmissionRate);
		LUX_ADD_ENTITY_CALL(SpriteAnimatorComponent_Play);
		LUX_ADD_ENTITY_CALL(SpriteAnimatorComponent_Stop);
		LUX_ADD_ENTITY_CALL(SpriteAnimatorComponent_GetClip);
		LUX_ADD_ENTITY_CALL(SpriteAnimatorComponent_IsPlaying);
		LUX_ADD_ENTITY_CALL(SpriteAnimatorComponent_IsFinished);
		LUX_ADD_ENTITY_CALL(SpriteAnimatorComponent_GetSpeed);
		LUX_ADD_ENTITY_CALL(SpriteAnimatorComponent_SetSpeed);

		LUX_ADD_ENTITY_CALL(CircleRendererComponent_GetColor);
		LUX_ADD_ENTITY_CALL(CircleRendererComponent_SetColor);
		LUX_ADD_ENTITY_CALL(CircleRendererComponent_GetThickness);
		LUX_ADD_ENTITY_CALL(CircleRendererComponent_SetThickness);
		LUX_ADD_ENTITY_CALL(CircleRendererComponent_GetFade);
		LUX_ADD_ENTITY_CALL(CircleRendererComponent_SetFade);

		LUX_ADD_ENTITY_CALL(CameraComponent_GetPrimary);
		LUX_ADD_ENTITY_CALL(CameraComponent_SetPrimary);

		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_ApplyLinearImpulse);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_ApplyLinearImpulseToCenter);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_ApplyForce);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_ApplyForceToCenter);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_ApplyTorque);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_GetLinearVelocity);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_SetLinearVelocity);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_GetAngularVelocity);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_SetAngularVelocity);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_GetPosition);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_SetPosition);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_GetRotation);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_SetRotation);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_GetMass);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_GetGravityScale);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_SetGravityScale);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_GetType);
		LUX_ADD_ENTITY_CALL(RigidBody2DComponent_SetType);

		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_GetOffset);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_SetOffset);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_GetSize);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_SetSize);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_GetDensity);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_SetDensity);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_GetFriction);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_SetFriction);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_GetRestitution);
		LUX_ADD_ENTITY_CALL(BoxCollider2DComponent_SetRestitution);

		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_GetOffset);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_SetOffset);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_GetRadius);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_SetRadius);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_GetDensity);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_SetDensity);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_GetFriction);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_SetFriction);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_GetRestitution);
		LUX_ADD_ENTITY_CALL(CircleCollider2DComponent_SetRestitution);

		LUX_ADD_ENTITY_CALL(TextComponent_GetText);
		LUX_ADD_ENTITY_CALL(TextComponent_SetText);
		LUX_ADD_ENTITY_CALL(TextComponent_GetColor);
		LUX_ADD_ENTITY_CALL(TextComponent_SetColor);
		LUX_ADD_ENTITY_CALL(TextComponent_GetKerning);
		LUX_ADD_ENTITY_CALL(TextComponent_SetKerning);
		LUX_ADD_ENTITY_CALL(TextComponent_GetLineSpacing);
		LUX_ADD_ENTITY_CALL(TextComponent_SetLineSpacing);
		LUX_ADD_ENTITY_CALL(TextComponent_GetMaxWidth);
		LUX_ADD_ENTITY_CALL(TextComponent_SetMaxWidth);

		// 3D physics
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_AddForce);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_AddForceAtLocation);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_AddTorque);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_GetLinearVelocity);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_SetLinearVelocity);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_GetAngularVelocity);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_SetAngularVelocity);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_GetTranslation);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_SetTranslation);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_GetRotation);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_SetRotation);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_GetMass);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_SetMass);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_SetGravityEnabled);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_IsSleeping);
		LUX_ADD_ENTITY_CALL(RigidBodyComponent_SetSleepState);

		LUX_ADD_INTERNAL_CALL(Physics2D_Raycast);
		LUX_ADD_INTERNAL_CALL(Physics2D_RaycastAll);
		LUX_ADD_INTERNAL_CALL(Physics2D_Overlap);
		LUX_ADD_INTERNAL_CALL(Physics_Raycast);
		LUX_ADD_INTERNAL_CALL(Physics_RaycastAll);
		LUX_ADD_INTERNAL_CALL(Physics_ShapeCast);
		LUX_ADD_INTERNAL_CALL(Physics_OverlapShape);
		LUX_ADD_INTERNAL_CALL(Physics_GetLayerIndex);

		LUX_ADD_ENTITY_CALL(CharacterControllerComponent_Move);
		LUX_ADD_ENTITY_CALL(CharacterControllerComponent_Jump);
		LUX_ADD_ENTITY_CALL(CharacterControllerComponent_IsGrounded);
		LUX_ADD_ENTITY_CALL(CharacterControllerComponent_GetLinearVelocity);
		LUX_ADD_ENTITY_CALL(CharacterControllerComponent_SetLinearVelocity);
		LUX_ADD_ENTITY_CALL(CharacterControllerComponent_SetGravityEnabled);

		LUX_ADD_ENTITY_CALL(BoxColliderComponent_GetHalfSize);
		LUX_ADD_ENTITY_CALL(BoxColliderComponent_SetHalfSize);
		LUX_ADD_ENTITY_CALL(BoxColliderComponent_GetOffset);
		LUX_ADD_ENTITY_CALL(BoxColliderComponent_SetOffset);
		LUX_ADD_ENTITY_CALL(SphereColliderComponent_GetRadius);
		LUX_ADD_ENTITY_CALL(SphereColliderComponent_SetRadius);
		LUX_ADD_ENTITY_CALL(SphereColliderComponent_GetOffset);
		LUX_ADD_ENTITY_CALL(SphereColliderComponent_SetOffset);
		LUX_ADD_ENTITY_CALL(CapsuleColliderComponent_GetRadius);
		LUX_ADD_ENTITY_CALL(CapsuleColliderComponent_SetRadius);
		LUX_ADD_ENTITY_CALL(CapsuleColliderComponent_GetHalfHeight);
		LUX_ADD_ENTITY_CALL(CapsuleColliderComponent_SetHalfHeight);
		LUX_ADD_ENTITY_CALL(CapsuleColliderComponent_GetOffset);
		LUX_ADD_ENTITY_CALL(CapsuleColliderComponent_SetOffset);
	}

	void ScriptGlue::RegisterGlue(Coral::ManagedAssembly& coreAssembly)
	{
		RegisterComponentTypes(coreAssembly);
		RegisterInternalCalls(coreAssembly);
		AudioScriptBindings::Register(coreAssembly);
		AnimationScriptBindings::Register(coreAssembly);
		ComponentScriptBindings::Register(coreAssembly);
		UIScriptBindings::Register(coreAssembly);
		coreAssembly.UploadInternalCalls();
		UIScriptBindings::Bind(coreAssembly);

		s_InputType = &coreAssembly.GetLocalType("Lux.Input");
		s_InputEventsPrimed = false;
		s_ApplicationType = &coreAssembly.GetLocalType("Lux.Application");
		s_ApplicationKnown = false;
		s_SchedulerType = &coreAssembly.GetLocalType("Lux.Scheduler");
		if (!*s_SchedulerType)
			LUX_CORE_ERROR_TAG("ScriptEngine", "ScriptCore has no Lux.Scheduler: timers and coroutines will not run");
	}

	void ScriptGlue::UpdateInput()
	{
		uint32_t connectedMask = 0;
		for (const auto& [id, controller] : Input::GetControllers())
			connectedMask |= 1u << id;

		if (!s_InputEventsPrimed)
		{
			// Pads already connected when Play starts are not "new"; scripts query them directly.
			s_KnownControllerMask = connectedMask;
			s_InputEventsPrimed = true;
			return;
		}

		const uint32_t changed = connectedMask ^ s_KnownControllerMask;
		s_KnownControllerMask = connectedMask;
		if (changed == 0 || !s_InputType)
			return;

		for (int32_t id = 0; id < 32; id++)
		{
			if (changed & (1u << id))
				s_InputType->InvokeStaticMethod("DispatchGamepadConnection", id, (int32_t)((connectedMask >> id) & 1u));
		}
	}

	void ScriptGlue::ResetInput()
	{
		s_InputEventsPrimed = false;
		if (s_InputType)
			s_InputType->InvokeStaticMethod("ResetGamepadEvents");
	}

	void ScriptGlue::ShutdownInput()
	{
		ResetInput();
		UIScriptBindings::Shutdown();
		s_InputType = nullptr;
		s_SchedulerType = nullptr;
		s_ApplicationType = nullptr;
		s_ApplicationKnown = false;
	}

	void ScriptGlue::UpdateApplication(bool paused, uint32_t width, uint32_t height)
	{
		if (!s_ApplicationType || !*s_ApplicationType)
			return;

		const bool focused = Application::Get().IsFocused();
		if (!s_ApplicationKnown)
		{
			s_ApplicationKnown = true;
			s_KnownFocused = focused;
			s_KnownPaused = paused;
			s_KnownWidth = width;
			s_KnownHeight = height;
			return;
		}

		if (focused != s_KnownFocused)
		{
			s_KnownFocused = focused;
			s_ApplicationType->InvokeStaticMethod("DispatchFocus", static_cast<int32_t>(focused));
		}
		if (paused != s_KnownPaused)
		{
			s_KnownPaused = paused;
			s_ApplicationType->InvokeStaticMethod("DispatchPause", static_cast<int32_t>(paused));
		}
		if ((width != s_KnownWidth || height != s_KnownHeight) && width > 0 && height > 0)
		{
			s_KnownWidth = width;
			s_KnownHeight = height;
			s_ApplicationType->InvokeStaticMethod("DispatchResize", static_cast<int32_t>(width), static_cast<int32_t>(height));
		}
	}

	void ScriptGlue::EndApplication(bool quitting)
	{
		s_ApplicationKnown = false;
		if (!s_ApplicationType || !*s_ApplicationType)
			return;
		if (quitting)
			s_ApplicationType->InvokeStaticMethod("DispatchQuitting");
		s_ApplicationType->InvokeStaticMethod("ResetEvents");
	}

	void ScriptGlue::UpdateScheduler(float scaledDelta, float unscaledDelta)
	{
		if (s_SchedulerType && *s_SchedulerType)
			s_SchedulerType->InvokeStaticMethod("Update", scaledDelta, unscaledDelta);
	}

	void ScriptGlue::StepScheduler()
	{
		if (s_SchedulerType && *s_SchedulerType)
			s_SchedulerType->InvokeStaticMethod("FixedUpdate");
	}

	void ScriptGlue::ResetScheduler()
	{
		if (s_SchedulerType && *s_SchedulerType)
			s_SchedulerType->InvokeStaticMethod("Reset");
	}

}
