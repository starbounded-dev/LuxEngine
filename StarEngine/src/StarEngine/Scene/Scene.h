#pragma once

#include <unordered_set>

#include "StarEngine/Asset/Asset.h"
#include "StarEngine/Core/Timestep.h"
#include "StarEngine/Core/UUID.h"
#include "StarEngine/Renderer/EditorCamera.h"
#include "Entity.h"

#include "StarEngine/Core/Application.h"

#include "entt.hpp"
#include "StarEngine/Scripting/ScriptEntityStorage.hpp"

class b2World;

namespace StarEngine {

	class Entity;

	using EntityMap = std::unordered_map<UUID, Entity>;

	class Scene : public Asset
	{
	public:
		struct PerformanceTimers
		{
			float ScriptUpdate = 0.0f;
			float ScriptLateUpdate = 0.0f;
			float PhysicsStep = 0.0f;
		};
	public:
		Scene();
		~Scene();

		static Ref<Scene> Copy(Ref<Scene> other);

		virtual AssetType GetType() const { return AssetType::Scene; }

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		Entity CreateEntityWithID(UUID uuid, const std::string& name = "", bool shouldSort = true);
		void DestroyEntity(Entity entity);

		void OnRuntimeStart();
		void OnRuntimeStop();

		void OnSimulationStart();
		void OnSimulationStop();

		void OnUpdateRuntime(Timestep ts);
		void OnUpdateSimulation(Timestep ts, EditorCamera& camera);
		void OnUpdateEditor(Timestep ts, EditorCamera& camera);

		void SetViewportBounds(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom);

		Entity DuplicateEntity(Entity entity);

		void ParentEntity(Entity entity, Entity parent);
		void UnparentEntity(Entity entity, bool convertToWorldSpace = true);

		void SetSceneTransitionCallback(const std::function<void(AssetHandle)>& callback) { m_OnSceneTransitionCallback = callback; }

		void ConvertToLocalSpace(Entity entity);
		void ConvertToWorldSpace(Entity entity);
		glm::mat4 GetWorldSpaceTransformMatrix(Entity entity);
		void SetWorldSpaceTransformMatrix(Entity entity, const glm::mat4& transform);
		TransformComponent GetWorldSpaceTransform(Entity entity);
		void SetWorldSpaceTransform(Entity entity, const TransformComponent& transform);

		Entity FindEntityByTag(const std::string& tag);
		Entity FindEntityByName(std::string_view name);
		Entity GetEntityByID(uint64_t id);
		Entity TryGetEntityWithID(uint64_t id) const;

		void OnSceneTransition(AssetHandle handle);

		glm::vec2 GetPhysics2DGravity();
		void SetPhysics2DGravity(const glm::vec2& gravity);

		void RenderHoveredEntityOutline(Entity entity, glm::vec4 color);
		void RenderSelectedEntityOutline(Entity entity, glm::vec4 color);
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

		const EntityMap& GetEntityMap() const { return m_EntityIDMap; }
		std::pair<std::unordered_set<AssetHandle>, std::unordered_set<AssetHandle>> GetAssetList(); // returns assetList, missingAssetList

		template<typename TComponent>
		void CopyComponentIfExists(entt::entity dst, entt::registry& dstRegistry, entt::entity src)
		{
			if (m_Registry.has<TComponent>(src))
			{
				auto& srcComponent = m_Registry.get<TComponent>(src);
				dstRegistry.emplace_or_replace<TComponent>(dst, srcComponent);
			}
		}

		UUID GetUUID() const { return m_SceneID; }

		Entity GetEntityWithUUID(UUID id) const;

		// return entity with id as specified, or empty entity if cannot be found - caller must check
		Entity TryGetEntityWithUUID(UUID id) const;

		void SetName(const std::string& name) { m_Name = name; }
		const std::string& GetName() const { return m_Name; }

		const PerformanceTimers& GetPerformanceTimers() const { return m_PerformanceTimers; }

		ScriptStorage& GetScriptStorage() { return m_ScriptStorage; }
		const ScriptStorage& GetScriptStorage() const { return m_ScriptStorage; }

	private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

		void OnPhysics2DStart();
		void OnPhysics2DStop();

		void SortEntities();

		void RenderScene(EditorCamera& camera);
	private:
		UUID m_SceneID;

		entt::registry m_Registry;
		std::size_t m_LastSerializeHash = 0; // used by auto-save to determine if scene has changed

		std::function<void(AssetHandle)> m_OnSceneTransitionCallback;

		std::string m_Name;
		bool m_IsEditorScene = false;
		uint32_t m_ViewportTop = 0;
		uint32_t m_ViewportLeft = 0;
		uint32_t m_ViewportRight = 0;
		uint32_t m_ViewportBottom = 0;

		bool m_IsRunning = false;
		bool m_IsPaused = false;

		static bool s_SetPaused;
		
		int m_StepFrames = 0;

		static glm::vec2 s_Gravity;

		b2World* m_PhysicsWorld = nullptr;

		EntityMap m_EntityIDMap; // maps UUID to entity handle

		ScriptStorage m_ScriptStorage;

		PerformanceTimers m_PerformanceTimers;

		friend class Entity;
		friend class Prefab;
		friend class Physics2D;
		friend class SceneRenderer;
		friend class SceneSerializer;
		friend class PrefabSerializer;
		friend class SceneHierarchyPanel;
		friend class ECSDebugPanel;
	};

}

#include "EntityTemplates.h"
