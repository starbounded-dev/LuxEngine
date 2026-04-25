#pragma once

#include "Entity.h"

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Timestep.h"
#include "Lux/Core/UUID.h"
#include "Lux/Editor/EditorCamera.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/SceneEnvironment.h"

#include "entt/entt.hpp"

#include <glm/glm.hpp>
#include <functional>
#include <unordered_set>

class b2World;

namespace Lux {

	class Entity;
	class Framebuffer;
	class Prefab;
	class SceneRenderer;
	class AudioSource;
	class Mesh;
	class StaticMesh;

	// Forward declare light structures (defined in SceneRenderer.h)
	struct LightEnvironment;
	struct DirectionalLight;
	struct PointLight;
	struct SpotLight;

	class Scene : public Asset
	{
	public:
		Scene();
		~Scene();

		static Ref<Scene> Copy(Ref<Scene> other);

		static AssetType GetStaticType() { return AssetType::Scene; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }
		virtual AssetType GetType() const { return GetStaticType(); }

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateChildEntity(Entity parent, const std::string& name = std::string());
		Entity CreateEntityWithID(UUID uuid, const std::string& name = std::string(), bool shouldSort = true);
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		void SubmitToDestroyEntity(Entity entity);
		void DestroyEntity(Entity entity, bool excludeChildren = false, bool first = true);
		void DestroyEntity(UUID entityID, bool excludeChildren = false, bool first = true);

		void OnRuntimeStart();
		void OnRuntimeStop();

		void OnSimulationStart();
		void OnSimulationStop();

		void OnUpdateRuntime(Timestep ts);
		void OnUpdateSimulation(Timestep ts, EditorCamera& camera);
		void OnUpdateEditor(Timestep ts, EditorCamera& camera);
		void OnViewportResize(uint32_t width, uint32_t height);

		void SetTargetFramebuffer(Ref<Framebuffer> framebuffer);

		Entity DuplicateEntity(Entity entity);
		Entity Instantiate(Ref<Prefab> prefab, const glm::vec3* translation = nullptr, const glm::vec3* rotation = nullptr, const glm::vec3* scale = nullptr);
		Entity InstantiateChild(Ref<Prefab> prefab, Entity parent, const glm::vec3* translation = nullptr, const glm::vec3* rotation = nullptr, const glm::vec3* scale = nullptr);
		Entity InstantiatePrefab(Ref<Prefab> prefab);
		Entity InstantiateMesh(Ref<Mesh> mesh);
		Entity InstantiateStaticMesh(Ref<StaticMesh> mesh);

		Entity FindEntityByName(std::string_view name);
		Entity GetEntityByUUID(UUID uuid) const;
		Entity GetEntityWithUUID(UUID uuid) const;
		Entity TryGetEntityWithUUID(UUID uuid) const;
		Entity TryGetEntityWithTag(const std::string& tag);
		glm::mat4 GetWorldSpaceTransformMatrix(Entity entity) const;
		TransformComponent GetWorldSpaceTransform(Entity entity) const;
		void ConvertToLocalSpace(Entity entity);
		void ConvertToWorldSpace(Entity entity);
		void ParentEntity(Entity entity, Entity parent);
		void UnparentEntity(Entity entity, bool convertToWorldSpace = true);
		std::vector<UUID> GetAllChildren(Entity entity) const;
		void CopyTo(Ref<Scene>& target);
		void SortEntities();

		Entity GetPrimaryCameraEntity();
		Entity GetMainCameraEntity() { return GetPrimaryCameraEntity(); }

		bool IsRunning() const { return m_IsRunning; }
		bool IsPaused() const { return m_IsPaused; }
		void SetPaused(bool paused) { m_IsPaused = paused; }
		void Step(int frames = 1);

		// ============================================================================
		// 3D Rendering Support
		// ============================================================================

		// Collect light data from DirectionalLightComponent, PointLightComponent and SpotLightComponent entities
		LightEnvironment CollectLightEnvironment() const;

		// Collect environment/skybox from the first SkyLightComponent entity.
		// Falls back to the renderer default environment when no explicit skylight is configured.
		Ref<Environment> CollectEnvironment(float& outIntensity, float& outLod) const;

		// Submit all StaticMeshComponent entities to the SceneRenderer
		// Optional predicate to determine if an entity is selected (for highlight rendering)
		void SubmitStaticMeshes(Ref<SceneRenderer> renderer,
			const std::function<bool(Entity)>& isSelected = nullptr) const;

		// High-level 3D rendering method that orchestrates the full 3D pipeline:
		// - Sets up camera
		// - Collects and applies light environment
		// - Collects and applies skybox/IBL environment
		// - Submits all static meshes
		// Call this from EditorLayer to render 3D content
		void Render3D(const EditorCamera& camera, Ref<SceneRenderer> renderer,
			const std::function<bool(Entity)>& isSelected = nullptr);

		// Render 3D content using a runtime camera (for Play mode)
		void Render3DRuntime(Ref<SceneRenderer> renderer);
		void OnRenderEditor(Ref<SceneRenderer> renderer, const EditorCamera& camera, const std::function<bool(Entity)>& isSelected = nullptr);
		void OnRenderSimulation(Ref<SceneRenderer> renderer, const EditorCamera& camera, const std::function<bool(Entity)>& isSelected = nullptr);
		void OnRenderRuntime(Ref<SceneRenderer> renderer);
		std::unordered_set<AssetHandle> GetAssetList();

		// ============================================================================

		template<typename... Components>
		auto GetAllEntitiesWith()
		{
			return m_Registry.view<Components...>();
		}

		template<typename... Components>
		auto GetAllEntitiesWith() const
		{
			return m_Registry.view<const Components...>();
		}
	private:
		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

		void OnPhysics2DStart();
		void OnPhysics2DStop();
		void RenderScene(EditorCamera& camera);
		Ref<AudioSource> GetOrCreateRuntimeAudioSource(Entity entity, AssetHandle audioHandle);
		Ref<AudioSource> GetOrCreateRuntimePlaylistSource(Entity entity, uint32_t index, AssetHandle audioHandle);
		void ReleaseRuntimeAudio(Entity entity);
		void ReleaseAllRuntimeAudio();
		Entity CreatePrefabEntity(Entity entity, Entity parent, const glm::vec3* translation = nullptr, const glm::vec3* rotation = nullptr, const glm::vec3* scale = nullptr);

	private:
		entt::registry m_Registry;
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

		bool m_IsRunning = false;
		bool m_IsPaused = false;
		int m_StepFrames = 0;

		b2World* m_PhysicsWorld = nullptr;

		Ref<Renderer2D> m_Renderer2D;
		std::string m_Name = "Untitled";
		mutable Ref<Environment> m_DynamicSkyEnvironment;
		mutable glm::vec3 m_DynamicSkyParameters = { 2.0f, 0.0f, 0.0f };
		mutable bool m_DynamicSkyEnvironmentValid = false;

		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::vector<std::function<void()>> m_PostUpdateQueue;
		std::unordered_map<UUID, Ref<AudioSource>> m_RuntimeAudioSources;
		std::unordered_map<UUID, std::vector<Ref<AudioSource>>> m_RuntimeAudioPlaylists;

		friend class Entity;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;
	};

}

#include "EntityTemplates.h"
