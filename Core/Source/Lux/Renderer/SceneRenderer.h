#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Renderer/Camera.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/RenderPass.h"
#include "Lux/Renderer/ComputePass.h"
#include "Lux/Renderer/Pipeline.h"
#include "Lux/Renderer/PipelineCompute.h"
#include "Lux/Renderer/Framebuffer.h"
#include "Lux/Renderer/Material.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Renderer/UniformBufferSet.h"
#include "Lux/Renderer/StorageBufferSet.h"
#include "Lux/Renderer/SceneEnvironment.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/DebugRenderer.h"
#include "Lux/Renderer/RendererTypes.h"
#include "Lux/Project/TieringSettings.h"
#include "Lux/Scene/Scene.h"

#include <glm/glm.hpp>
#include <array>
#include <limits>
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
		bool  ShowShadowCascades = false;
		bool  ShowLightComplexity = false;
		bool  SoftShadows = true;
		float MaxShadowDistance = 200.0f;
		float ShadowFade = 25.0f;
		float ShadowCascadeSplitLambda = 0.92f;
		float ShadowCascadeNearPlaneOffset = 0.0f;
		float ShadowCascadeFarPlaneOffset = 50.0f;
		float ShadowCascadeTransitionFade = 1.0f;
		bool  EnableGTAO = true;
		bool  GTAOBentNormals = false;
		uint32_t GTAODenoisePasses = 4;
		bool  EnableSSR = true;
		bool  EnableJumpFlood = true;
		bool  EnableFrustumCulling = true;
		bool  EnableGPUDrivenRendering = true;
	};

	struct BloomSettings
	{
		bool  Enabled = true;
		float Threshold = 1.0f;
		float Knee = 0.1f;
		float Intensity = 1.0f;
		float DirtIntensity = 0.0f;
	};

	struct DOFSettings
	{
		bool  Enabled = false;
		float FocusDistance = 10.0f;
		float BlurSize = 1.0f;
	};

	struct SSROptionsUB
	{
		glm::vec2 HZBUvFactor = { 1.0f, 1.0f };
		glm::vec2 FadeIn = { 0.1f, 0.15f };
		float Brightness = 0.7f;
		float DepthTolerance = 0.8f;
		float FacingReflectionsFading = 0.1f;
		int MaxSteps = 70;
		uint32_t NumDepthMips = 1;
		float RoughnessDepthTolerance = 1.0f;
		bool HalfRes = true;
		char Padding[3]{ 0, 0, 0 };
		bool EnableConeTracing = true;
		char Padding1[3]{ 0, 0, 0 };
		float LuminanceFactor = 1.0f;
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
		static constexpr uint32_t ShadowCascadeCount = 4;
		static constexpr uint32_t MaxSpotShadows = 16;
		static constexpr uint32_t LightCullingTileSize = 16;
		static constexpr uint32_t MaxVisibleLightsPerTile = 256;
		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t Meshes = 0;
			uint32_t Instances = 0;
			uint32_t VisibleInstances = 0;
			uint32_t CulledInstances = 0;
			uint32_t IndirectDraws = 0;
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
		BloomSettings& GetBloomSettings() { return m_BloomSettings; }
		DOFSettings& GetDOFSettings() { return m_DOFSettings; }
		SSROptionsUB& GetSSROptions() { return m_SSROptions; }
		RenderingTechnique GetRenderingTechnique() const { return m_RenderingTechnique; }
		void SetRenderingTechnique(RenderingTechnique technique) { m_RenderingTechnique = technique; }
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
			AssetHandle        MaterialHandle = 0;
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
			uint32_t              VisibleObjectIndexBase = 0;
			uint32_t              VisibleInstanceCount = 0;
			uint32_t              IndirectDrawOffsetBytes = std::numeric_limits<uint32_t>::max();
		};

		struct InstanceBoundsData
		{
			glm::vec4 Sphere; // xyz = world center, w = world radius
		};

		struct MeshCullDrawData
		{
			uint32_t ObjectIndexBase = 0;
			uint32_t InstanceCount = 0;
			uint32_t VisibleObjectIndexBase = 0;
			uint32_t Padding = 0;
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
		void SpotShadowMapPass();
		void PreDepthPass();
		void HZBCompute();
		void PreIntegration();
		void MeshCullingPass();
		void LightCullingPass();
		void SkyboxPass();
		void GeometryPass();
		void GTAOCompute();
		void GTAODenoiseCompute();
		void AOComposite();
		void PreConvolutionCompute();
		void SSRCompute();
		void SSRCompositePass();
		void BloomCompute();
		void CompositePass();
		void DOFPass();
		void JumpFloodPass();
		void JumpFloodCompositePass();
		void GridPass();

		void UpdateStatistics();
		void ResizeLightCullingResources();
		void ResizeBloomResources();
		void CreateBloomPassMaterials();
		void ResizeScreenSpaceEffectResources();
		void CreateHZBPassMaterials();
		void CreatePreIntegrationPassMaterials();
		void CreatePreConvolutionPassMaterials();

		struct CascadeData
		{
			glm::mat4 ViewProj{ 1.0f };
			float SplitDepth = 0.0f;
		};
		void CalculateCascades(CascadeData* cascades, const SceneRendererCamera& sceneCamera, const glm::vec3& lightDirection) const;

		void BuildIndirectDrawCommand(const StaticDrawCommand& dc,
			const TransformMapData& tmd,
			std::vector<nvrhi::DrawIndexedIndirectArguments>& drawCommands);

		// Render-thread draw helper (must be called inside Renderer::Submit).
		void RT_DrawStaticMesh(Ref<RenderCommandBuffer> cmd,
			const StaticDrawCommand& dc,
			const TransformMapData& tmd,
			bool                     bindMaterial,
			uint32_t                 lightIndex = 0,
			bool                     useVisibleObjectIndexes = false,
			bool                     useIndirect = false);

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

		struct UBShadow
		{
			glm::mat4 ViewProjection[ShadowCascadeCount];
		} m_ShadowUB;

		struct UBSpotShadow
		{
			glm::mat4 ViewProjection[MaxSpotShadows];
			uint32_t Count = 0;
			glm::vec3 Padding{};
		} m_SpotShadowUB;

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

		struct UBScreenData
		{
			glm::vec2 InvFullResolution = { 1.0f, 1.0f };
			glm::vec2 FullResolution = { 1.0f, 1.0f };
			glm::vec2 InvHalfResolution = { 1.0f, 1.0f };
			glm::vec2 HalfResolution = { 1.0f, 1.0f };
		} m_ScreenDataUB;

		struct CBGTAOData
		{
			glm::vec2 NDCToViewMul_x_PixelSize = { 1.0f, 1.0f };
			float EffectRadius = 0.5f;
			float EffectFalloffRange = 0.62f;
			float RadiusMultiplier = 1.46f;
			float FinalValuePower = 2.2f;
			float DenoiseBlurBeta = 1.2f;
			bool HalfRes = false;
			char Padding0[3]{ 0, 0, 0 };
			float SampleDistributionPower = 2.0f;
			float ThinOccluderCompensation = 0.0f;
			float DepthMIPSamplingOffset = 3.3f;
			int NoiseIndex = 0;
			glm::vec2 HZBUVFactor = { 1.0f, 1.0f };
			float ShadowTolerance = 0.0f;
			float Padding = 0.0f;
		} m_GTAODataCB;

		struct GTAODenoiseConstants
		{
			float DenoiseBlurBeta = 1.2f;
			bool HalfRes = false;
			char Padding[3]{ 0, 0, 0 };
		} m_GTAODenoiseConstants;

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
		RenderingTechnique         m_RenderingTechnique = RenderingTechnique::Forward;
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
		Ref<UniformBufferSet> m_UBSScreenData;
		Ref<UniformBufferSet> m_UBSPointLights;
		Ref<UniformBufferSet> m_UBSSpotLights;

		Ref<StorageBufferSet> m_SBSInstanceTransforms;  // TransformVertexData[]
		Ref<StorageBufferSet> m_SBSObjectIndexes;       // uint32_t[] – maps draw → transform
		Ref<StorageBufferSet> m_SBSVisibleObjectIndexes;
		Ref<StorageBufferSet> m_SBSInstanceBounds;
		Ref<StorageBufferSet> m_SBSMeshCullDrawData;
		Ref<StorageBufferSet> m_SBSIndirectDrawCommands;
		Ref<StorageBufferSet> m_SBSVisiblePointLightIndices;
		Ref<StorageBufferSet> m_SBSVisibleSpotLightIndices;
		uint32_t              m_LightTilesCountX = 1;
		uint32_t              m_LightTilesCountY = 1;
		uint32_t              m_VisibleLightIndexBufferSize = 0;

		// ── Directional shadow maps ─────────────────────────────────────────
		Ref<Image2D>     m_ShadowMapImage;
		std::array<Ref<RenderPass>, ShadowCascadeCount> m_ShadowMapPasses;
		Ref<RenderPass>  m_ShadowMapPass; // Alias for cascade 0, used for shared shadow texture binding
		Ref<Material>    m_ShadowPassMaterial;

		// ── Spot shadow atlas ───────────────────────────────────────────────
		Ref<Image2D>     m_SpotShadowMapImage;
		Ref<RenderPass>  m_SpotShadowMapPass;
		Ref<Material>    m_SpotShadowPassMaterial;
		uint32_t         m_SpotShadowMapSize = 2048;
		uint32_t         m_SpotShadowAtlasGridSize = 1;
		uint32_t         m_SpotShadowTileSize = 2048;
		uint32_t         m_SpotShadowCount = 0;

		// ── Pre-depth pass ────────────────────────────────────────────────────
		Ref<Pipeline>    m_PreDepthPipeline;
		Ref<Material>    m_PreDepthMaterial;
		Ref<RenderPass>  m_PreDepthPass;

		// ── Tiled light culling ──────────────────────────────────────────────
		Ref<ComputePass> m_MeshCullingPass;
		Ref<ComputePass> m_LightCullingPass;
		uint32_t         m_MeshCullDrawCount = 0;

		struct MippedTexture
		{
			Ref<Texture2D> Texture;
			std::vector<Ref<ImageView>> ImageViews;
		};

		// ── HZB / SSR prepasses ───────────────────────────────────────────────
		Ref<ComputePass> m_HierarchicalDepthPass;
		MippedTexture    m_HierarchicalDepthTexture;
		std::vector<Ref<Material>> m_HZBMaterials;

		Ref<ComputePass> m_PreIntegrationPass;
		MippedTexture    m_PreIntegrationVisibilityTexture;
		std::vector<Ref<Material>> m_PreIntegrationMaterials;

		Ref<ComputePass> m_PreConvolutionComputePass;
		MippedTexture    m_PreConvolutedTexture;
		std::vector<Ref<Material>> m_PreConvolutionMaterials;

		// ── GTAO / AO ────────────────────────────────────────────────────────
		Ref<ComputePass> m_GTAOComputePass;
		Ref<ComputePass> m_GTAODenoisePass[2];
		Ref<Material>    m_GTAODenoiseMaterial[2];
		Ref<Image2D>     m_GTAOOutputImage;
		Ref<Image2D>     m_GTAODenoiseImage;
		Ref<Image2D>     m_GTAOFinalImage;
		Ref<Image2D>     m_GTAOEdgesOutputImage;
		glm::uvec3       m_GTAOWorkGroups{ 1 };
		glm::uvec3       m_GTAODenoiseWorkGroups{ 1 };

		Ref<RenderPass>  m_AOCompositePass;
		Ref<Material>    m_AOCompositeMaterial;

		// ── SSR ──────────────────────────────────────────────────────────────
		Ref<Image2D>     m_SSRImage;
		Ref<ComputePass> m_SSRPass;
		Ref<RenderPass>  m_SSRCompositePass;
		Ref<Material>    m_SSRCompositeMaterial;
		glm::uvec3       m_SSRWorkGroups{ 1 };

		// ── Bloom compute ────────────────────────────────────────────────────
		Ref<ComputePass>     m_BloomComputePass;
		Ref<PipelineCompute> m_BloomComputePipeline;
		uint32_t             m_BloomComputeWorkgroupSize = 4;

		struct BloomComputeTextures
		{
			Ref<Texture2D> Texture;
			std::vector<Ref<ImageView>> ImageViews;
		};
		std::vector<BloomComputeTextures> m_BloomComputeTextures{ 3 };

		struct BloomComputeMaterials
		{
			Ref<Material> PrefilterMaterial;
			std::vector<Ref<Material>> DownsampleAMaterials;
			std::vector<Ref<Material>> DownsampleBMaterials;
			Ref<Material> FirstUpsampleMaterial;
			std::vector<Ref<Material>> UpsampleMaterials;
		} m_BloomComputeMaterials;
		Ref<Texture2D> m_BloomDirtTexture;

		// ── DOF ──────────────────────────────────────────────────────────────
		Ref<RenderPass> m_DOFPass;
		Ref<Material>   m_DOFMaterial;

		// ── Jump flood selected outline ──────────────────────────────────────
		Ref<RenderPass> m_JumpFloodInitPass;
		Ref<RenderPass> m_JumpFloodPasses[2];
		Ref<RenderPass> m_JumpFloodCompositePass;
		Ref<Material>   m_JumpFloodInitMaterial;
		Ref<Material>   m_JumpFloodPassMaterials[2];
		Ref<Material>   m_JumpFloodCompositeMaterial;

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
		std::vector<InstanceBoundsData>      m_InstanceBoundsData;

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
		BloomSettings m_BloomSettings;
		DOFSettings m_DOFSettings;
		SSROptionsUB m_SSROptions;

		SceneRendererOptions m_Options;
		Statistics           m_Statistics;
	};

} // namespace Lux
