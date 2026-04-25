#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Timestep.h"
#include "Lux/Core/UUID.h"
#include "Lux/Editor/EditorCamera.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/SceneEnvironment.h"

#include "entt/entt.hpp"

#include <glm/glm.hpp>
#include <functional>

class b2World;

namespace Lux {

	class Entity;
	class Framebuffer;
	class Prefab;
	class SceneRenderer;

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

		void SetTargetFramebuffer(Ref<Framebuffer> framebuffer);

		Entity DuplicateEntity(Entity entity);
		Entity InstantiatePrefab(Ref<Prefab> prefab);

		Entity FindEntityByName(std::string_view name);
		Entity GetEntityByUUID(UUID uuid) const;
		glm::mat4 GetWorldSpaceTransformMatrix(Entity entity) const;

		Entity GetPrimaryCameraEntity();

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

		friend class Entity;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;
	};

}
