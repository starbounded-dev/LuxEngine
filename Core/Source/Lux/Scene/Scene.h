#pragma once

#include "Entity.h"

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Base.h"
#include "Lux/Core/Timestep.h"
#include "Lux/Core/UUID.h"
#include "Lux/Editor/EditorCamera.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/RenderVolumes.h"
#include "Lux/Renderer/SceneEnvironment.h"

#include "Lux/Scripting/ScriptEntityStorage.hpp"
#include "Lux/Scripting/CSharpObject.h"

#include "entt/entt.hpp"

#include <glm/glm.hpp>
#include <functional>
#include <unordered_set>

namespace Lux {

	class Entity;
	class Framebuffer;
	class Prefab;
	class PhysicsScene2D;
	class PhysicsScene;
	class RenderScene;
	class SceneRenderer;
	class AudioSource;
	class Mesh;
	class StaticMesh;

	// Forward declare light structures (defined in SceneRenderer.h)
	struct LightEnvironment;
	struct DirectionalLight;
	struct PointLight;
	struct SpotLight;
	struct AtmosphereEnvironment;

	// Captured render state for one frame (defined in Renderer/FrameRenderPacket.h)
	struct FrameRenderPacket;

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
		bool HasScripts() const;

		// ============================================================================
		// 3D Rendering Support
		// ============================================================================

		// Collect light data from DirectionalLightComponent, PointLightComponent and SpotLightComponent entities
		LightEnvironment CollectLightEnvironment() const;

		// Collect environment/skybox from the first SkyLightComponent entity.
		// Falls back to the renderer default environment when no explicit skylight is configured.
		Ref<Environment> CollectEnvironment(float& outIntensity, float& outLod) const;

		// Collect atmosphere, volumetric cloud and fog settings from scene components.
		AtmosphereEnvironment CollectAtmosphereEnvironment() const;

		// Resolve rendering volumes for the camera using the scene atmosphere and renderer post-process defaults as the base layer.
		RenderVolumeEnvironment CollectRenderVolumeEnvironment(
			const glm::vec3& cameraPosition,
			const Frustum* cameraFrustum,
			const RenderVolumeBaseSettings& baseSettings,
			const std::function<bool(Entity)>& isSelected = nullptr) const;

		// Submit all StaticMeshComponent entities to the SceneRenderer
		// Optional predicate to determine if an entity is selected (for highlight rendering)
		void SubmitStaticMeshes(Ref<SceneRenderer> renderer,
			const std::function<bool(Entity)>& isSelected = nullptr) const;

		// Synchronize ECS renderable state into the persistent render-side scene.
		Ref<::Lux::RenderScene> SyncRenderScene(const std::function<bool(Entity)>& isSelected = nullptr) const;

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

		// --- Frame render packet (decouples render submission from the live ECS) -----------------
		// Build* capture all renderer-relevant ECS state into a packet (read-only on the registry);
		// SubmitRenderPacket replays that packet to the SceneRenderer without touching the registry.
		// Build* read the ECS (non-const: EnTT owning groups reorder the registry); SubmitRenderPacket
		// only touches the packet + renderer and stays const.
		void BuildRenderPacketEditor(FrameRenderPacket& packet, const EditorCamera& camera,
			Ref<SceneRenderer> renderer, const std::function<bool(Entity)>& isSelected);
		bool BuildRenderPacketRuntime(FrameRenderPacket& packet, Ref<SceneRenderer> renderer);
		void SubmitRenderPacket(Ref<SceneRenderer> renderer, const FrameRenderPacket& packet) const;
		void CaptureDraw2D(FrameRenderPacket& packet);
		void CaptureColliderDebug(FrameRenderPacket& packet,
			Ref<SceneRenderer> renderer, const std::function<bool(Entity)>& isSelected);
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
		void OnPhysics3DStart();
		void OnPhysics3DStop();
		void StepPhysics(Timestep ts);
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

		Scope<PhysicsScene2D> m_PhysicsScene2D;
		Ref<PhysicsScene> m_PhysicsScene;

		Ref<Renderer2D> m_Renderer2D;
		mutable Ref<::Lux::RenderScene> m_RenderScene;
		std::string m_Name = "Untitled";
		mutable Ref<Environment> m_DynamicSkyEnvironment;
		mutable glm::vec3 m_DynamicSkyParameters = { 2.0f, 0.0f, 0.0f };
		mutable bool m_DynamicSkyEnvironmentValid = false;

		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::vector<std::function<void()>> m_PostUpdateQueue;
		std::unordered_map<UUID, Ref<AudioSource>> m_RuntimeAudioSources;
		std::unordered_map<UUID, std::vector<Ref<AudioSource>>> m_RuntimeAudioPlaylists;

		// Per-entity C# script field values (serialized with the scene) and live instances.
		ScriptStorage m_ScriptStorage;
		std::unordered_map<UUID, CSharpObject> m_ScriptInstances;

	public:
		ScriptStorage& GetScriptStorage() { return m_ScriptStorage; }
		const ScriptStorage& GetScriptStorage() const { return m_ScriptStorage; }

	private:
		friend class Entity;
		friend class SceneSerializer;
		friend class SceneHierarchyPanel;
	};

}

#include "EntityTemplates.h"
