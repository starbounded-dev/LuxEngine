#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Renderer/Camera.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/RenderPass.h"
#include "Lux/Renderer/Pipeline.h"
#include "Lux/Renderer/Framebuffer.h"
#include "Lux/Renderer/Material.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/UniformBufferSet.h"
#include "Lux/Renderer/StorageBufferSet.h"
#include "Lux/Renderer/SceneEnvironment.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/DebugRenderer.h"
#include "Lux/Project/TieringSettings.h"
#include "Lux/Scene/Scene.h"

#include <glm/glm.hpp>
#include <map>
#include <vector>

namespace Lux {

	// ─────────────────────────────────────────────────────────────────────────
	// Light structures
	// These are NOT yet stored on the Scene; instead they are supplied each
	// frame via SceneRenderer::SetLightEnvironment() / SetEnvironment().
	// ─────────────────────────────────────────────────────────────────────────

	struct DirectionalLight
	{
		glm::vec3 Direction = { 0.0f, -1.0f,  0.0f };
		float     Padding0 = 0.0f;
		glm::vec3 Radiance = { 1.0f,  1.0f,  1.0f };
		float     Intensity = 0.0f;     // 0 = disabled
		float     ShadowAmount = 1.0f;
		bool      CastShadows = true;
		bool      SoftShadows = true;
		float     LightSize = 0.5f;
		float     Padding1 = 0.0f;
	};

	struct PointLight
	{
		glm::vec3 Position = { 0.0f, 0.0f, 0.0f };
		float     Intensity = 0.0f;      // 0 = disabled
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float     MinRadius = 0.001f;
		float     Radius = 25.0f;
		float     Falloff = 1.0f;
		float     LightSize = 0.5f;
		uint32_t  CastsShadows = 0;
	};

	struct SpotLight
	{
		glm::vec3 Position = { 0.0f, 0.0f, 0.0f };
		float     Intensity = 0.0f;
		glm::vec3 Direction = { 0.0f, -1.0f, 0.0f };
		float     AngleAttenuation = 1.0f;
		glm::vec3 Radiance = { 1.0f, 1.0f, 1.0f };
		float     Range = 25.0f;
		float     Angle = 45.0f;
		float     Falloff = 1.0f;
		uint32_t  ShadowIndex = 0;
		uint32_t  SoftShadows = 0;
		uint32_t  CastsShadows = 0;
		float     AtlasOffsetX = 0.0f;
		float     AtlasOffsetY = 0.0f;
		float     AtlasScale = 1.0f;
	};

	static_assert(sizeof(PointLight) == 48, "PointLight must match the GLSL std140 layout.");
	static_assert(sizeof(SpotLight) == 80, "SpotLight must match the GLSL std140 layout.");

	struct LightEnvironment
	{
		static constexpr uint32_t MaxDirectionalLights = 1;

		DirectionalLight        DirectionalLights[MaxDirectionalLights];
		std::vector<PointLight> PointLights;
		std::vector<SpotLight>  SpotLights;

		uint64_t GetPointLightsSize() const
		{
			return PointLights.size() * sizeof(PointLight);
		}

		uint64_t GetSpotLightsSize() const
		{
			return SpotLights.size() * sizeof(SpotLight);
		}
	};

	// ─────────────────────────────────────────────────────────────────────────
	// Renderer options / spec
	// ─────────────────────────────────────────────────────────────────────────

	struct SceneRendererOptions
	{
		bool  ShowGrid = true;
		bool  ShowSelectedInWireframe = false;
		bool  ShowPhysicsColliders = false;
		bool  SoftShadows = true;
		float MaxShadowDistance = 200.0f;
		float ShadowFade = 1.0f;
	};

	struct SceneRendererCamera
	{
		Camera    Camera;
		glm::mat4 ViewMatrix;
		float     Near = 0.1f;
		float     Far = 1000.0f;
		float     FOV = 45.0f;    // vertical FOV in degrees
	};

	struct SceneRendererSpecification
	{
		uint32_t ViewportWidth = 0;   // 0 = use window size
		uint32_t ViewportHeight = 0;
		Tiering::Renderer::RendererTieringSettings Tiering;
	};

	// ─────────────────────────────────────────────────────────────────────────
	// SceneRenderer
	// Handles the full 3-D render pipeline for a Scene.
	//
	// Minimal usage pattern:
	//   renderer->BeginScene(camera);
	//   renderer->SetLightEnvironment(lights);
	//   renderer->SetEnvironment(env, intensity);
	//   renderer->SubmitStaticMesh(...);    // repeat for every mesh
	//   renderer->EndScene();
	// ─────────────────────────────────────────────────────────────────────────

	class SceneRenderer : public RefCounted
	{
	public:
		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t Meshes = 0;
			uint32_t Instances = 0;
			uint32_t SavedDraws = 0;
			float    TotalGPUTime = 0.0f;
		};

	public:
		SceneRenderer() = default;
		SceneRenderer(Ref<Scene> scene,
			SceneRendererSpecification specification = SceneRendererSpecification());
		virtual ~SceneRenderer();

		void Init();
		void Shutdown();

		void SetScene(Ref<Scene> scene);
		void SetViewportSize(uint32_t width, uint32_t height);

		// ── Per-frame API ────────────────────────────────────────────────────

		void BeginScene(const SceneRendererCamera& camera);

		// Call before BeginScene to update the scene state consumed by the render passes.
		void SetLightEnvironment(const LightEnvironment& lightEnvironment);
		void SetEnvironment(Ref<Environment> environment, float intensity = 1.0f, float skyboxLod = 0.0f);

		// Submit a static (non-animated) mesh for rendering this frame.
		void SubmitStaticMesh(Ref<StaticMesh>    staticMesh,
			Ref<MeshSource>    meshSource,
			Ref<MaterialTable> materialTable,
			const glm::mat4& transform = glm::mat4(1.0f),
			Ref<Material>      overrideMaterial = nullptr);

		void SubmitSelectedStaticMesh(Ref<StaticMesh>    staticMesh,
			Ref<MeshSource>    meshSource,
			Ref<MaterialTable> materialTable,
			const glm::mat4& transform = glm::mat4(1.0f),
			Ref<Material>      overrideMaterial = nullptr);

		// Submit a debug mesh (wireframe collider, etc.) with an explicit material.
		void SubmitPhysicsStaticDebugMesh(Ref<StaticMesh> staticMesh,
			Ref<MeshSource> meshSource,
			const glm::mat4& transform,
			bool isSimpleCollider = true);

		void EndScene();
		static void WaitForThreads();

		// ── Output ────────────────────────────────────────────────────────────

		Ref<Image2D>     GetFinalPassImage();
		Ref<Pipeline>    GetFinalPipeline();
		Ref<RenderPass>  GetFinalRenderPass();
		Ref<RenderPass>  GetCompositeRenderPass() { return m_CompositePass; }
		Ref<Framebuffer> GetExternalCompositeFramebuffer() { return m_CompositingFramebuffer; }
		Ref<RenderCommandBuffer> GetCommandBuffer() { return m_CommandBuffer; }

		Ref<Renderer2D>    GetRenderer2D() { return m_Renderer2D; }
		Ref<DebugRenderer> GetDebugRenderer() { return m_DebugRenderer; }

		// ── Settings ─────────────────────────────────────────────────────────

		SceneRendererOptions& GetOptions() { return m_Options; }
		const SceneRendererSpecification& GetSpecification()  const { return m_Specification; }

		void SetLineWidth(float width);

		uint32_t GetViewportWidth()  const { return m_ViewportWidth; }
		uint32_t GetViewportHeight() const { return m_ViewportHeight; }

		float GetOpacity() const { return m_Opacity; }
		void  SetOpacity(float opacity) { m_Opacity = opacity; }

		const glm::mat4& GetScreenSpaceProjectionMatrix() const { return m_ScreenSpaceProjectionMatrix; }
		const Statistics& GetStatistics() const { return m_Statistics; }

		bool IsReady() const { return m_ResourcesCreatedGPU; }

	private:
		// ── Internal draw-list key & commands ────────────────────────────────

		struct MeshKey
		{
			AssetHandle MeshHandle;
			AssetHandle MaterialHandle;
			uint32_t    SubmeshIndex;
			bool        IsSelected;

			bool operator<(const MeshKey& o) const
			{
				if (MeshHandle != o.MeshHandle)     return MeshHandle < o.MeshHandle;
				if (SubmeshIndex != o.SubmeshIndex)   return SubmeshIndex < o.SubmeshIndex;
				if (MaterialHandle != o.MaterialHandle) return MaterialHandle < o.MaterialHandle;
				return IsSelected < o.IsSelected;
			}
		};

		struct StaticDrawCommand
		{
			Ref<StaticMesh>    StaticMesh;
			Ref<MeshSource>    MeshSource;
			uint32_t           SubmeshIndex = 0;
			Ref<MaterialTable> MaterialTable;
			Ref<Material>      OverrideMaterial;
			uint32_t           InstanceCount = 0;
		};

		// Row-major 3×4 transform stored in the InstanceTransforms SSBO.
		struct TransformVertexData
		{
			glm::vec4 MRow[3];
		};

		struct TransformMapData
		{
			std::vector<uint32_t> ObjectIndices;
			uint32_t              ObjectIndexBase = 0;  // offset into ObjectIndexes SSBO
		};

		// Internal helper for the debug-mesh submission path.
		void SubmitStaticDebugMesh(std::map<MeshKey, StaticDrawCommand>& drawList,
			Ref<StaticMesh>  staticMesh,
			Ref<MeshSource>  meshSource,
			const glm::mat4& transform,
			Ref<Material>    material);

		void SubmitStaticMeshInternal(Ref<StaticMesh>    staticMesh,
			Ref<MeshSource>    meshSource,
			Ref<MaterialTable> materialTable,
			const glm::mat4& transform,
			Ref<Material>      overrideMaterial,
			bool               isSelected);

		// ── Render passes ────────────────────────────────────────────────────

		void FlushDrawList();

		void ShadowMapPass();
		void PreDepthPass();
		void SkyboxPass();
		void GeometryPass();
		void CompositePass();
		void GridPass();

		void UpdateStatistics();

		// Render-thread draw helper (must be called inside Renderer::Submit).
		void RT_DrawStaticMesh(Ref<RenderCommandBuffer> cmd,
			const StaticDrawCommand& dc,
			const TransformMapData& tmd,
			bool                     bindMaterial);

		// ── Uniform buffer GPU structs ────────────────────────────────────────

		struct UBCamera
		{
			glm::mat4 ViewProjection;
			glm::mat4 InverseViewProjection;
			glm::mat4 Projection;
			glm::mat4 InverseProjection;
			glm::mat4 View;
			glm::mat4 InverseView;
			glm::vec2 NDCToViewMul;
			glm::vec2 NDCToViewAdd;
			glm::vec2 DepthUnpackConsts;
			glm::vec2 CameraTanHalfFOV;
		} m_CameraUB;

		struct DirLightGPU
		{
			glm::vec3 Direction;
			float     ShadowAmount;
			glm::vec3 Radiance;
			float     Intensity;
		};

		struct UBScene
		{
			DirLightGPU Lights;
			glm::vec3   CameraPosition;
			float       EnvironmentMapIntensity = 1.0f;
		} m_SceneUB;

		// Supports up to 4 cascades; we only use [0] (single ortho shadow map).
		struct UBShadow
		{
			glm::mat4 ViewProjection[4];
		} m_ShadowUB;

		struct UBSpotShadow
		{
			glm::mat4 ViewProjection[16];
			uint32_t Count = 0;
			glm::vec3 Padding{};
		};

		struct UBRendererData
		{
			glm::vec4 CascadeSplits;
			uint32_t  TilesCountX = 0;
			bool      ShowCascades = false;
			char      Pad0[3] = { 0, 0, 0 };
			bool      SoftShadows = true;
			char      Pad1[3] = { 0, 0, 0 };
			float     LightSize = 0.5f;
			float     MaxShadowDistance = 200.0f;
			float     ShadowFade = 1.0f;
			bool      CascadeFading = false;
			char      Pad2[3] = { 0, 0, 0 };
			float     CascadeTransitionFade = 1.0f;
			bool      ShowLightComplexity = false;
			char      Pad3[3] = { 0, 0, 0 };
		} m_RendererDataUB;

		struct UBPointLights
		{
			uint32_t   Count = 0;
			glm::vec3  Padding{};
			PointLight PointLights[256]{};
		} m_PointLightsUB;

		struct UBSpotLights
		{
			uint32_t Count = 0;
			glm::vec3 Padding{};
			SpotLight SpotLights[256]{};
		} m_SpotLightsUB;

		// ── Private data ──────────────────────────────────────────────────────

		Ref<Scene>                 m_Scene;
		SceneRendererSpecification m_Specification;
		Ref<RenderCommandBuffer>   m_CommandBuffer;       // render commands
		Ref<RenderCommandBuffer>   m_UploadCommandBuffer; // UB/SB data uploads

		Ref<Renderer2D>    m_Renderer2D;
		Ref<DebugRenderer> m_DebugRenderer;

		glm::mat4 m_ScreenSpaceProjectionMatrix{ 1.0f };

		// Scene data populated each frame in BeginScene / SetLightEnvironment.
		struct SceneInfo
		{
			SceneRendererCamera SceneCamera;
			Ref<Environment>    SceneEnvironment;
			float               SceneEnvironmentIntensity = 1.0f;
			float               SkyboxLod = 0.0f;
			LightEnvironment    SceneLightEnvironment;
		} m_SceneData;

		// ── Uniform / Storage buffer sets ─────────────────────────────────────
		Ref<UniformBufferSet> m_UBSCamera;
		Ref<UniformBufferSet> m_UBSScene;
		Ref<UniformBufferSet> m_UBSShadow;
		Ref<UniformBufferSet> m_UBSSpotShadow;
		Ref<UniformBufferSet> m_UBSRendererData;
		Ref<UniformBufferSet> m_UBSPointLights;
		Ref<UniformBufferSet> m_UBSSpotLights;

		Ref<StorageBufferSet> m_SBSInstanceTransforms;  // TransformVertexData[]
		Ref<StorageBufferSet> m_SBSObjectIndexes;       // uint32_t[] – maps draw → transform
		Ref<StorageBufferSet> m_SBSVisiblePointLightIndices;
		Ref<StorageBufferSet> m_SBSVisibleSpotLightIndices;

		// ── Shadow map (single ortho cascade) ────────────────────────────────
		Ref<Image2D>     m_ShadowMapImage;
		Ref<RenderPass>  m_ShadowMapPass;
		Ref<Material>    m_ShadowPassMaterial;

		// ── Pre-depth pass ────────────────────────────────────────────────────
		Ref<Pipeline>    m_PreDepthPipeline;
		Ref<Material>    m_PreDepthMaterial;
		Ref<RenderPass>  m_PreDepthPass;

		// ── Geometry pass ─────────────────────────────────────────────────────
		Ref<Framebuffer> m_GeometryPassFramebuffer;     // owns the attachments
		Ref<Pipeline>    m_GeometryPipeline;            // opaque PBR
		Ref<Pipeline>    m_TransparentGeometryPipeline; // transparent PBR
		Ref<RenderPass>  m_GeometryPass;                // opaque
		Ref<RenderPass>  m_GeometryPassTransparent;     // transparent (same FB, load)

		// ── Selected / wireframe ──────────────────────────────────────────────
		Ref<RenderPass>  m_SelectedGeometryPass;
		Ref<Material>    m_SelectedGeometryMaterial;
		Ref<RenderPass>  m_GeometryWireframePass;
		Ref<Material>    m_WireframeMaterial;

		// ── Skybox ────────────────────────────────────────────────────────────
		Ref<Pipeline>    m_SkyboxPipeline;
		Ref<Material>    m_SkyboxMaterial;
		Ref<RenderPass>  m_SkyboxPass;

		// ── Composite (tone-map + opacity) ────────────────────────────────────
		Ref<Framebuffer> m_CompositingFramebuffer;
		Ref<Material>    m_CompositeMaterial;
		Ref<RenderPass>  m_CompositePass;

		// ── Editor grid ───────────────────────────────────────────────────────
		Ref<RenderPass>  m_GridRenderPass;
		Ref<Material>    m_GridMaterial;

		// ── Physics collider debug ────────────────────────────────────────────
		Ref<Material>    m_SimpleColliderMaterial;
		Ref<Material>    m_ComplexColliderMaterial;

		// ── Draw lists ────────────────────────────────────────────────────────
		std::map<MeshKey, StaticDrawCommand> m_StaticMeshDrawList;
		std::map<MeshKey, StaticDrawCommand> m_TransparentStaticMeshDrawList;
		std::map<MeshKey, StaticDrawCommand> m_SelectedStaticMeshDrawList;
		std::map<MeshKey, StaticDrawCommand> m_StaticMeshShadowPassDrawList;
		std::map<MeshKey, StaticDrawCommand> m_StaticColliderDrawList;

		// Transform storage for all submitted meshes this frame.
		std::map<MeshKey, TransformMapData>  m_MeshTransformMap;
		std::vector<TransformVertexData>     m_TransformData;

		// Shadow-specific per-cascade transform tracking.
		// Index 0 is the only cascade we use currently.
		struct ShadowTransformMapData
		{
			TransformMapData Cascade; // single cascade
		};
		std::map<MeshKey, ShadowTransformMapData> m_ShadowMeshTransformMap;

		// ── Viewport / frame state ────────────────────────────────────────────
		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;
		float    m_InvViewportWidth = 0.0f;
		float    m_InvViewportHeight = 0.0f;
		bool     m_NeedsResize = false;
		bool     m_Active = false;
		bool     m_ResourcesCreatedGPU = false;
		bool     m_ResourcesCreated = false;

		float m_LineWidth = 2.0f;
		float m_Opacity = 1.0f;

		SceneRendererOptions m_Options;
		Statistics           m_Statistics;
	};

} // namespace Lux
