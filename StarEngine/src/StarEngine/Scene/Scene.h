#pragma once

#include "StarEngine/Asset/Asset.h"
#include "StarEngine/Core/Timestep.h"
#include "StarEngine/Core/UUID.h"
#include "StarEngine/Renderer/EditorCamera.h"

#include "entt.hpp"

class b2World;

namespace StarEngine {

	class Entity;

	class Scene : public Asset
	{
	public:
		Scene();
		~Scene();

		static Ref<Scene> Copy(Ref<Scene> other);

		virtual AssetType GetType() const { return AssetType::Scene; }

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		void DestroyEntity(Entity entity);

		void OnRuntimeStart();
		void OnRuntimeStop();

		void OnSimulationStart();
		void OnSimulationStop();

		void OnUpdateRuntime(Timestep ts);
		void OnUpdateSimulation(Timestep ts, EditorCamera& camera);
		void OnUpdateEditor(Timestep ts, EditorCamera& camera);
		void OnViewportResize(uint32_t width, uint32_t height);

		Entity DuplicateEntity(Entity entity);

		void SetSceneTransitionCallback(const std::function<void(AssetHandle)>& callback) { m_OnSceneTransitionCallback = callback; }

		Entity FindEntityByTag(const std::string& tag);
		Entity FindEntityByName(std::string_view name);
		Entity GetEntityByID(uint64_t id);
		Entity TryGetEntityWithID(uint64_t id) const;

		void OnSceneTransition(AssetHandle handle);

		glm::vec2 GetPhysics2DGravity();
		void SetPhysics2DGravity(const glm::vec2& gravity);

		/*
		void RenderHoveredEntityOutline(Entity entity, glm::vec4 color);
		void RenderSelectedEntityOutline(Entity entity, glm::vec4 color);*/
		Entity GetEntityByUUID(UUID uuid);

		Entity GetPrimaryCameraEntity();

		bool IsRunning() const { return m_IsRunning; }

		bool IsPaused() const { return m_IsPaused; }

		void SetPaused(bool paused) { m_IsPaused = paused; }

		void Step(int frames = 1);

		template<typename... Components>
		auto GetAllEntitiesWith()
		{
			return m_Registry.view<Components...>();
		}

		void SetName(const std::string& name) { m_Name = name; }
		const std::string& GetName() const { return m_Name; }

		void ShouldGameBePaused(bool shouldPause) { s_SetPaused = shouldPause; }
		bool IsGamePaused() { return s_SetPaused; }

		ScriptStorage& GetScriptStorage() { return m_ScriptStorage; }
		const ScriptStorage& GetScriptStorage() const { return m_ScriptStorage; }

	private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

		void OnPhysics2DStart();
		void OnPhysics2DStop();

		void RenderScene(EditorCamera& camera);
	private:
		entt::registry m_Registry;

		std::function<void(AssetHandle)> m_OnSceneTransitionCallback;

		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
		std::string m_Name = "Untitled";

		bool m_IsRunning = false;
		bool m_IsPaused = false;

		static bool s_SetPaused;
		
		int m_StepFrames = 0;

		static glm::vec2 s_Gravity;

		b2World* m_PhysicsWorld = nullptr;

		std::unordered_map<UUID, entt::entity> m_EntityMap;

		ScriptStorage m_ScriptStorage; // Ensure ScriptStorage is fully declared

		friend class Entity;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;
	};

}
