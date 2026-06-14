#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Renderer/Camera.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/RenderPass.h"
#include "Lux/Renderer/ComputePass.h"
#include "Lux/Renderer/RenderGraph.h"
#include "Lux/Renderer/Atmosphere.h"
#include "Lux/Renderer/RenderVolumes.h"
#include "Lux/Renderer/Pipeline.h"
#include "Lux/Renderer/PipelineCompute.h"
#include "Lux/Renderer/Framebuffer.h"
#include "Lux/Renderer/GPUScene.h"
#include "Lux/Renderer/Material.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Renderer/UniformBufferSet.h"
#include "Lux/Renderer/StorageBufferSet.h"
#include "Lux/Renderer/SceneEnvironment.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/DebugRenderer.h"
#include "Lux/Renderer/RendererTypes.h"
#include "Lux/Renderer/ShaderDefs.h"
#include "Lux/Core/Math/Frustum.h"
#include "Lux/Core/Timer.h"
#include "Lux/Project/TieringSettings.h"
#include "Lux/Scene/Scene.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lux {

	struct ProjectSceneRendererSettings;
	class RenderScene;
	struct StaticMeshRenderProxy;

	// ─────────────────────────────────────────────────────────────────────────
	// Quality presets
	// ─────────────────────────────────────────────────────────────────────────

	enum class QualityPreset : uint32_t
	{
		Low = 0,
		Medium = 1,
		High = 2,
		Ultra = 3,
		Cinematic = 4
	};

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
		float     ShadowDistance = 0.0f; // 0 = use renderer MaxShadowDistance
		uint32_t  ShadowResolutionTier = 2; // 0=1K, 1=2K, 2=4K, 3=8K
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
		float     ShadowDistance = 0.0f; // 0 = use Range
		uint32_t  ShadowResolutionTier = 1; // 0=1K, 1=2K, 2=4K, 3=8K
		glm::vec2 Padding2 = { 0.0f, 0.0f };
	};

	static_assert(sizeof(PointLight) == 48, "PointLight must match the GLSL std140 layout.");
	static_assert(sizeof(SpotLight) == 96, "SpotLight must match the GLSL std140 layout.");

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
		enum class RenderResolutionScaleMode : uint32_t
		{
			Native = 0,
			Scale75 = 1,
			Scale50 = 2,
			Dynamic = 3
		};

		enum class EffectResolutionScale : uint32_t
		{
			Full = 1,
			Half = 2,
			Quarter = 4
		};

		enum class SSRQualityPreset : uint32_t
		{
			Full = 0,
			HalfBilateral = 1,
			QuarterDebug = 2
		};

		enum class ShadowResolutionTier : uint32_t
		{
			Tier_1K = 0,
			Tier_2K = 1,
			Tier_4K = 2,
			Tier_8K = 3
		};

		bool  ShowGrid = true;
		bool  ShowSelectedInWireframe = false;
		bool  ShowPhysicsColliders = false;
		enum class PhysicsColliderView
		{
			SelectedEntity = 0, All = 1
		};
		PhysicsColliderView PhysicsColliderMode = PhysicsColliderView::SelectedEntity;
		bool  ShowPhysicsCollidersOnTop = false;
		glm::vec4 SimplePhysicsCollidersColor = { 0.2f, 1.0f, 0.2f, 1.0f };
		glm::vec4 ComplexPhysicsCollidersColor = { 0.5f, 0.5f, 1.0f, 1.0f };
		bool  ShowShadowCascades = false;
		bool  ShowCascadeFrustums = false;
		bool  ShowLightComplexity = false;
		bool  ShowMaterialComplexity = false;
		bool  SoftShadows = true;
		bool  EnableShadowCulling = true;
		bool  EnableMainViewCulling = true;
		float MaxShadowDistance = 200.0f;
		float ShadowFade = 25.0f;
		float ShadowCascadeSplitLambda = 0.92f;
		float ShadowCascadeNearPlaneOffset = 0.0f;
		float ShadowCascadeFarPlaneOffset = 50.0f;
		float ShadowCascadeTransitionFade = 1.0f;
		ShadowResolutionTier ShadowResolution = ShadowResolutionTier::Tier_4K;
		QualityPreset Quality = QualityPreset::Medium;
		bool  EnableGTAO = true;
		bool  GTAOBentNormals = false;
		float AOShadowTolerance = 1.0f;
		int   GTAODenoisePasses = 4;
		bool  EnableSSR = true;
		ShaderDef::AOMethod ReflectionOcclusionMethod = ShaderDef::AOMethod::None;
		bool  EnableJumpFlood = true;
		bool  EnableFrustumCulling = true;
		bool  EnableOcclusionCulling = false;
		float OcclusionDepthBias = 0.003f;
		float OcclusionBoundsScale = 1.15f;
		bool  EnableGPUDrivenRendering = true;
		EffectResolutionScale GTAOResolutionScale = EffectResolutionScale::Half;
		SSRQualityPreset SSRQuality = SSRQualityPreset::HalfBilateral;
		EffectResolutionScale SSRResolutionScale = EffectResolutionScale::Half;
		bool  EnableGTAOTemporalAccumulation = false;
		float GTAOTemporalBlend = 0.85f;
		bool  EnableSSRTemporalAccumulation = false;
		float SSRTemporalBlend = 0.90f;
		bool  EnableTAA = false;
		float TAAHistoryBlend = 0.90f; // fraction of history kept per frame
		RenderResolutionScaleMode ResolutionScaleMode = RenderResolutionScaleMode::Native;
		float DynamicResolutionScale = 1.0f;
		float DynamicResolutionMinScale = 0.5f;
		float DynamicResolutionMaxScale = 1.0f;
		float DynamicResolutionTargetGPUTime = 16.67f;
		float TextureMipBias = 0.0f;
		bool  EnableDistanceMipBias = false;
		float DistanceMipBiasStart = 50.0f;
		float DistanceMipBiasEnd = 250.0f;
		float DistanceMipBiasMax = 2.0f;
	};

	struct BloomSettings
	{
		bool  Enabled = true;
		SceneRendererOptions::EffectResolutionScale ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
		float Threshold = 1.0f;
		float Knee = 0.1f;
		float UpsampleScale = 1.0f;
		float Intensity = 1.0f;
		float DirtIntensity = 1.0f;
	};

	struct DOFSettings
	{
		bool  Enabled = false;
		SceneRendererOptions::EffectResolutionScale ResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
		float FocusDistance = 0.0f;
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
		uint32_t ResolutionScale = 2;
		uint32_t TemporalAccumulation = 0;
		float TemporalBlend = 0.0f;
		float Padding2 = 0.0f;
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
		struct PassProfile
		{
			const char* Name = "";
			float CPUTime = 0.0f;
			float GPUTime = 0.0f;
			bool Active = false;
			bool GPUActive = false;
		};

		struct Statistics
		{
			struct MemoryStatistics
			{
				uint64_t BudgetBytes = 0;
				uint64_t UsedBytes = 0;
				uint64_t TextureBytes = 0;
				uint64_t BufferBytes = 0;
				uint64_t RenderTargetBytes = 0;
				uint32_t TextureCount = 0;
				uint32_t BufferCount = 0;
				uint32_t RenderTargetCount = 0;
				uint32_t FramebufferCount = 0;
				uint32_t DescriptorSetCount = 0;
				uint64_t RenderGraphTransientBytes = 0;
				uint64_t RenderGraphAliasedBytes = 0;
				uint64_t RenderGraphSavedBytes = 0;
				uint32_t RenderGraphPassCount = 0;
				uint32_t RenderGraphTransientCount = 0;
				uint32_t RenderGraphAliasGroupCount = 0;
			};

			uint32_t DrawCalls = 0;
			uint32_t Meshes = 0;
			uint32_t SubmittedInstances = 0;
			uint32_t Instances = 0;
			uint32_t VisibleInstances = 0;
			uint32_t GPUVisibleInstances = 0;
			uint32_t CulledInstances = 0;
			uint32_t FrustumCulledInstances = 0;
			uint32_t MainViewCulledInstances = 0;
			uint32_t ShadowCulledInstances = 0;
			uint32_t OcclusionCulledInstances = 0;
			uint32_t FullyCulledInstances = 0;
			uint32_t IndirectDraws = 0;
			uint32_t SavedDraws = 0;
			uint32_t SpotlightShadowcasters = 0;
			uint32_t SpotlightShadowsCulled = 0;
			float    TotalCPUTime = 0.0f;
			float    TotalGPUTime = 0.0f;
			PipelineStatistics PipelineStats;
			MemoryStatistics MemoryStats;
			std::vector<PassProfile> PassProfiles;
		};

		struct RenderGraphResourceAccessDebugInfo
		{
			uint32_t Resource = RenderGraph::InvalidResource;
			std::string State;
		};

		struct RenderGraphTextureDebugInfo
		{
			uint32_t Resource = RenderGraph::InvalidResource;
			std::string Name;
			ImageFormat Format = ImageFormat::None;
			ImageUsage Usage = ImageUsage::None;
			nvrhi::TextureDimension Dimension = nvrhi::TextureDimension::Unknown;
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t Mips = 0;
			uint32_t Layers = 0;
			uint64_t EstimatedBytes = 0;
			uint32_t FirstPass = UINT32_MAX;
			uint32_t LastPass = UINT32_MAX;
			uint32_t AliasGroup = UINT32_MAX;
			bool Transient = false;
			bool AllowAlias = false;
			bool AliasedNow = false;
			nvrhi::ResourceStates CurrentState = nvrhi::ResourceStates::Unknown;
			uint32_t FirstWriter = UINT32_MAX;
			uint32_t LastReader = UINT32_MAX;
			std::vector<uint32_t> Consumers;
			uint32_t DiagnosticCount = 0;
			uint32_t ErrorCount = 0;
			uint32_t WarningCount = 0;
		};

		struct RenderGraphPassDebugInfo
		{
			uint32_t Index = UINT32_MAX;
			std::string Name;
			std::vector<RenderGraphResourceAccessDebugInfo> Inputs;
			std::vector<RenderGraphResourceAccessDebugInfo> Outputs;
			uint32_t Flags = 0;
			bool Executable = false;
			bool Culled = false;
			float CPUTime = 0.0f;
			float GPUTime = 0.0f;
			std::vector<uint32_t> Diagnostics;
		};

		struct RenderGraphDiagnosticDebugInfo
		{
			RenderGraph::DiagnosticSeverity Severity = RenderGraph::DiagnosticSeverity::Info;
			RenderGraph::DiagnosticCode Code = RenderGraph::DiagnosticCode::InvalidResource;
			uint32_t PassIndex = UINT32_MAX;
			std::string PassName;
			uint32_t Resource = RenderGraph::InvalidResource;
			std::string ResourceName;
			std::string Message;
		};

		struct RenderGraphAliasGroupDebugInfo
		{
			uint32_t AliasGroup = UINT32_MAX;
			std::vector<uint32_t> Resources;
			uint64_t EstimatedBytes = 0;
			uint64_t BackingBytes = 0;
			uint64_t SavedBytes = 0;
			bool Compatible = true;
		};

		struct RenderGraphDebugSnapshot
		{
			std::vector<RenderGraphPassDebugInfo> Passes;
			std::vector<RenderGraphTextureDebugInfo> Textures;
			std::vector<RenderGraphDiagnosticDebugInfo> Diagnostics;
			std::vector<RenderGraphAliasGroupDebugInfo> AliasGroups;
			uint32_t ErrorCount = 0;
			uint32_t WarningCount = 0;
			uint32_t InfoCount = 0;
			uint32_t ExecutedPassCount = 0;
			uint32_t CulledPassCount = 0;
			uint64_t TransientBytes = 0;
			uint64_t AliasedBytes = 0;
			uint64_t SavedBytes = 0;
		};

		struct RendererFrameDebugSnapshot
		{
			RenderingTechnique Technique = RenderingTechnique::Forward;
			bool DeferredPath = false;
			bool HasRenderScene = false;
			bool HasRenderVolumeEnvironment = false;
			bool SkyAtmosphereEnabled = false;
			bool VolumetricCloudsEnabled = false;
			bool HeightFogEnabled = false;
			bool LocalFogEnabled = false;
			bool BloomEnabled = false;
			bool DOFEnabled = false;
			uint32_t ActiveVolumeCount = 0;
			uint32_t ActivePostProcessVolumeCount = 0;
			uint32_t ActiveAtmosphereVolumeCount = 0;
			uint32_t LocalFogVolumeCount = 0;
			uint32_t CulledLocalFogVolumeCount = 0;
			uint32_t DroppedLocalFogVolumeCount = 0;
		};

		struct GPUSceneDebugSnapshot
		{
			uint32_t PersistentInstanceCount = 0;
			uint32_t TransientInstanceCount = 0;
			uint32_t TotalUploadedInstanceCount = 0;
			uint32_t ActivePrimitiveCount = 0;
			uint32_t VisiblePrimitiveCount = 0;
			uint32_t ObjectIndexCount = 0;
			uint32_t VisibleObjectIndexCount = 0;
			uint32_t MeshCullDrawCount = 0;
			uint32_t IndirectDrawCount = 0;
			uint32_t DirtyInstanceCount = 0;
			uint32_t DirtyRangeCount = 0;
			uint32_t PersistentMaterialCount = 0;
			uint32_t TransientMaterialCount = 0;
			uint32_t UploadedMaterialCount = 0;
			uint32_t DirtyMaterialCount = 0;
			uint32_t DirtyMaterialRangeCount = 0;
			uint32_t PersistentTextureCount = 0;
			uint32_t TransientTextureCount = 0;
			uint32_t UploadedTextureCount = 0;
			uint32_t DirtyTextureCount = 0;
			uint32_t DirtyTextureRangeCount = 0;
			uint32_t InvalidTextureIndexCount = 0;
			uint32_t MissingTextureDescriptorCount = 0;
			uint32_t TextureTableOverflowCount = 0;
			uint32_t InvalidObjectIndexCount = 0;
			uint32_t InvalidVisibleObjectIndexCount = 0;
			uint32_t InvalidMaterialIDCount = 0;
			uint32_t InvalidBoundsCount = 0;
			uint32_t InvalidPreviousTransformCount = 0;
			uint32_t InvalidStoredInstanceIDCount = 0;
			uint32_t PersistentInvalidPrimitiveIDCount = 0;
			uint32_t MissingPersistentObjectIDCount = 0;
			uint32_t MissingMaterialCount = 0;
			uint32_t MissingTextureCount = 0;
			uint32_t MaxMaterialIndex = 0;
			std::vector<std::string> Diagnostics;
		};

	public:
		SceneRenderer() = default;
		SceneRenderer(Ref<Scene> scene,
			SceneRendererSpecification specification = SceneRendererSpecification());
		virtual ~SceneRenderer();

		void Init();
		void Shutdown();
		void InitOptions();

		void SetScene(Ref<Scene> scene);
		void SetViewportSize(uint32_t width, uint32_t height);
		void RefreshRenderResolutionScale();
		void UpdateGTAOData();

		// ── Per-frame API ────────────────────────────────────────────────────

		void BeginScene(const SceneRendererCamera& camera);

		// Call before BeginScene to update the scene state consumed by the render passes.
		void SetLightEnvironment(const LightEnvironment& lightEnvironment);
		void SetEnvironment(Ref<Environment> environment, float intensity = 1.0f, float skyboxLod = 0.0f);
		void SetAtmosphereEnvironment(const AtmosphereEnvironment& atmosphereEnvironment);
		void SetRenderVolumeEnvironment(const RenderVolumeEnvironment& renderVolumeEnvironment);

		// Submit a static (non-animated) mesh for rendering this frame.
		void SubmitRenderScene(const Ref<RenderScene>& renderScene);

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

		enum class DebugViewMode : uint32_t
		{
			Final = 0,
			Geometry,
			Depth,
			Normals,
			SSR,
			AO,
			Bloom,
			Composite,
			LocalFogDensity,
			GBufferBaseColor,
			GBufferNormal,
			GBufferMetalRough,
			GBufferMaterialID,
			GBufferObjectID,
			DeferredLighting,
			GPUScenePrimitiveID,
			GPUSceneMaterialIndex,
			GPUSceneObjectID,
			GPUSceneBounds,
			GPUSceneMotion,
			GPUMaterialTextureValidity,
			GPUMaterialAlphaMode,
			GPUMaterialRoughness,
			GPUMaterialMetalness,
			GPUMaterialMissing
		};

		Ref<Image2D>     GetFinalPassImage();
		Ref<Image2D>     GetDebugViewImage(DebugViewMode mode);
		DebugViewMode    GetDebugViewMode() const { return m_DebugViewMode; }
		void             SetDebugViewMode(DebugViewMode mode) { m_DebugViewMode = mode; }
		Ref<Pipeline>    GetFinalPipeline();
		Ref<RenderPass>  GetFinalRenderPass();
		Ref<RenderPass>  GetCompositeRenderPass() { return m_CompositePass; }
		Ref<Framebuffer> GetDepthCompositeFramebuffer();
		Ref<Framebuffer> GetExternalCompositeFramebuffer();
		Ref<RenderCommandBuffer> GetCommandBuffer() { return m_CommandBuffer; }
		void SetWorldOverlayRenderCallback(std::function<void()> callback) { m_WorldOverlayRenderCallback = std::move(callback); }

		Ref<Renderer2D>    GetRenderer2D() { return m_Renderer2D; }
		Ref<Renderer2D>    GetScreenSpaceRenderer2D() { return m_Renderer2DScreenSpace ? m_Renderer2DScreenSpace : m_Renderer2D; }
		Ref<DebugRenderer> GetDebugRenderer() { return m_DebugRenderer; }

		// ── Settings ─────────────────────────────────────────────────────────

		SceneRendererOptions& GetOptions() { return m_Options; }
		BloomSettings& GetBloomSettings() { return m_BloomSettings; }
		DOFSettings& GetDOFSettings() { return m_DOFSettings; }
		RenderVolumeBaseSettings GetBaseRenderVolumeSettings(float cameraExposure) const;
		const RenderVolumeEnvironment& GetRenderVolumeEnvironment() const { return m_RenderVolumeEnvironment; }
		SSROptionsUB& GetSSROptions() { return m_SSROptions; }
		RenderingTechnique GetRenderingTechnique() const { return m_RenderingTechnique; }
		void SetRenderingTechnique(RenderingTechnique technique) { m_RenderingTechnique = technique; }
	void ApplyProjectSettings(const ProjectSceneRendererSettings& settings);
	void WriteProjectSettings(ProjectSceneRendererSettings& settings) const;
	void RefreshScreenSpaceEffectResources();
	void SetQualityPreset(QualityPreset preset);
	const SceneRendererSpecification& GetSpecification()  const { return m_Specification; }
	void SetShadowSettings(float nearPlane, float farPlane, float lambda, float scaleShadowToOrigin = 0.0f)
	{
		m_Options.ShadowCascadeNearPlaneOffset = nearPlane;
		m_Options.ShadowCascadeFarPlaneOffset = farPlane;
		m_Options.ShadowCascadeSplitLambda = lambda;
		m_ScaleShadowCascadesToOrigin = scaleShadowToOrigin;
	}

		void SetShadowCascades(float a, float b, float c, float d)
		{
			m_UseManualCascadeSplits = true;
			m_ShadowCascadeSplits[0] = a;
			m_ShadowCascadeSplits[1] = b;
			m_ShadowCascadeSplits[2] = c;
			m_ShadowCascadeSplits[3] = d;
		}

	void SetLineWidth(float width);
	void ApplyQualityPreset(QualityPreset preset);

	uint32_t GetViewportWidth()  const { return m_ViewportWidth; }
	uint32_t GetViewportHeight() const { return m_ViewportHeight; }
	uint32_t GetOutputViewportWidth()  const { return m_OutputViewportWidth; }
	uint32_t GetOutputViewportHeight() const { return m_OutputViewportHeight; }
	float GetRenderResolutionScale() const;
	uint32_t GetVolumetricCloudRenderScale() const { return m_CloudRenderScale; }
	const glm::uvec2& GetVolumetricCloudRenderSize() const { return m_CloudRenderSize; }
	const AtmosphereEnvironment& GetAtmosphereEnvironment() const { return m_FrameEnvironment.Atmosphere; }

		float GetOpacity() const { return m_Opacity; }
		void  SetOpacity(float opacity) { m_Opacity = opacity; }

		const glm::mat4& GetScreenSpaceProjectionMatrix() const { return m_ScreenSpaceProjectionMatrix; }
		const Statistics& GetStatistics() const { return m_Statistics; }
		RenderGraphDebugSnapshot GetRenderGraphDebugSnapshot();
		RendererFrameDebugSnapshot GetRendererFrameDebugSnapshot() const;
		const GPUSceneDebugSnapshot& GetGPUSceneDebugSnapshot() const { return m_GPUSceneDebugSnapshot; }
		const Frustum& GetCameraFrustum() const { return m_SceneData.CameraFrustum; }

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

			bool operator==(const MeshKey& o) const
			{
				return MeshHandle == o.MeshHandle
					&& MaterialHandle == o.MaterialHandle
					&& SubmeshIndex == o.SubmeshIndex
					&& IsSelected == o.IsSelected;
			}
		};

		struct MeshKeyHasher
		{
			size_t operator()(const MeshKey& key) const
			{
				size_t seed = std::hash<uint64_t>{}((uint64_t)key.MeshHandle);
				seed ^= std::hash<uint64_t>{}((uint64_t)key.MaterialHandle) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				seed ^= std::hash<uint32_t>{}(key.SubmeshIndex) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				seed ^= std::hash<bool>{}(key.IsSelected) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				return seed;
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
			uint64_t           PipelineSortKey = 0;
			uint64_t           ShaderSortKey = 0;
			uint64_t           MaterialSortKey = 0;
			uint64_t           MeshSortKey = 0;
		};

		using DrawCommandList = std::unordered_map<MeshKey, StaticDrawCommand, MeshKeyHasher>;
		using DrawCommandOrder = std::vector<MeshKey>;

		enum class MeshPassType : uint8_t
		{
			DepthPrepass = 0,
			ShadowDepth,
			Opaque,
			Transparent,
			SelectedMask,
			Wireframe,
			PhysicsCollider,
			Count
		};

		static constexpr size_t MeshPassTypeCount = static_cast<size_t>(MeshPassType::Count);
		static constexpr uint32_t MeshDrawCommandCacheRetireAge = 300;

		struct MeshPassState
		{
			MeshPassType Type = MeshPassType::Opaque;
			DrawCommandList DrawList;
			DrawCommandOrder DrawOrder;
		};

		struct MeshDrawCommandCacheKey
		{
			MeshPassType PassType = MeshPassType::Opaque;
			MeshKey Key{};
			uint64_t StaticMeshPtr = 0;
			uint64_t MeshSourcePtr = 0;
			uint64_t MaterialTablePtr = 0;
			uint64_t OverrideMaterialPtr = 0;
			uint64_t PipelineSortKey = 0;
			uint64_t ShaderSortKey = 0;
			uint64_t MaterialSortKey = 0;
			uint64_t MeshSortKey = 0;

			bool operator==(const MeshDrawCommandCacheKey& o) const
			{
				return PassType == o.PassType
					&& Key == o.Key
					&& StaticMeshPtr == o.StaticMeshPtr
					&& MeshSourcePtr == o.MeshSourcePtr
					&& MaterialTablePtr == o.MaterialTablePtr
					&& OverrideMaterialPtr == o.OverrideMaterialPtr
					&& PipelineSortKey == o.PipelineSortKey
					&& ShaderSortKey == o.ShaderSortKey
					&& MaterialSortKey == o.MaterialSortKey
					&& MeshSortKey == o.MeshSortKey;
			}
		};

		struct MeshDrawCommandCacheKeyHasher
		{
			size_t operator()(const MeshDrawCommandCacheKey& key) const
			{
				auto combine = [](size_t& seed, uint64_t value)
					{
						seed ^= std::hash<uint64_t>{}(value) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
					};

				size_t seed = std::hash<uint8_t>{}(static_cast<uint8_t>(key.PassType));
				seed ^= MeshKeyHasher{}(key.Key) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				combine(seed, key.StaticMeshPtr);
				combine(seed, key.MeshSourcePtr);
				combine(seed, key.MaterialTablePtr);
				combine(seed, key.OverrideMaterialPtr);
				combine(seed, key.PipelineSortKey);
				combine(seed, key.ShaderSortKey);
				combine(seed, key.MaterialSortKey);
				combine(seed, key.MeshSortKey);
				return seed;
			}
		};

		struct CachedStaticDrawCommand
		{
			StaticDrawCommand Command;
			uint32_t LastUsedFrame = 0;
		};

		struct TransformMapData
		{
			std::vector<uint32_t> ObjectIndices; // persistent GPUScene IDs or encoded transient IDs
			uint32_t              ObjectIndexBase = 0;  // offset into ObjectIndexes SSBO
			uint32_t              VisibleObjectIndexBase = 0;
			uint32_t              VisibleInstanceCount = 0;
			uint32_t              IndirectDrawOffsetBytes = std::numeric_limits<uint32_t>::max();
		};

		struct MeshCullDrawData
		{
			uint32_t ObjectIndexBase = 0;
			uint32_t InstanceCount = 0;
			uint32_t VisibleObjectIndexBase = 0;
			uint32_t Padding = 0;
		};

		// Internal helper for the debug-mesh submission path.
		void SubmitStaticDebugMesh(MeshPassType passType,
			Ref<StaticMesh>  staticMesh,
			Ref<MeshSource>  meshSource,
			const glm::mat4& transform,
			Ref<Material>    material);

		void SubmitStaticMeshProxy(const StaticMeshRenderProxy& proxy);
		void SubmitStaticMeshInternal(Ref<StaticMesh>    staticMesh,
			Ref<MeshSource>    meshSource,
			Ref<MaterialTable> materialTable,
			const glm::mat4& transform,
			Ref<Material>      overrideMaterial,
			bool               isSelected,
			const StaticMeshRenderProxy* renderProxy = nullptr);
		bool IsMainViewVisible(const BoundingSphere& bounds) const;
		bool IsShadowCasterVisible(const BoundingSphere& bounds) const;
		void BuildSortedDrawCommandOrder(const DrawCommandList& drawList, DrawCommandOrder& drawOrder) const;
		MeshPassState& GetMeshPass(MeshPassType passType);
		const MeshPassState& GetMeshPass(MeshPassType passType) const;
		RenderMaterialID GetOrCreateTransientRenderMaterialID(AssetHandle materialHandle, const Ref<MaterialAsset>& materialAsset, const Ref<Material>& overrideMaterial, bool transparent);
		GPUTextureIndex ResolveTransientGPUTextureIndex(AssetHandle textureHandle);
		StaticDrawCommand& SubmitMeshPassDraw(MeshPassType passType,
			const MeshKey& key,
			Ref<StaticMesh> staticMesh,
			Ref<MeshSource> meshSource,
			Ref<MaterialTable> materialTable,
			uint32_t submeshIndex,
			AssetHandle materialHandle,
			Ref<Material> overrideMaterial,
			uint64_t pipelineSortKey,
			uint64_t shaderSortKey,
			uint64_t materialSortKey,
			uint64_t meshSortKey);
		void ClearFrameMeshPasses();
		void PruneMeshDrawCommandCache();
		uint64_t CalculateShadowCasterHash() const;

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
		void SkyAtmospherePass();
		void VolumetricCloudPass();
		void VolumetricCloudCompositePass();
		void AtmosphericFogPass();
		void SelectedGeometryPass();
		void GBufferPass();
		void ForwardGeometryPass();
		void DeferredLightingPass();
		void TransparentForwardPass();
		void GeometryWireframePass();
		void GBufferDebugPass();
		void GTAOCompute();
		void GTAODenoiseCompute();
		void GTAOTemporalAccumulationCompute();
		void AOComposite();
		void AODebugPass();
		void PreConvolutionCompute();
		void SSRCompute();
		void SSRTemporalAccumulationCompute();
		void SSRCompositePass();
		void BloomCompute();
		void AutoExposurePass();
		void TAAResolvePass();
		float ComputeFinalExposure(const RenderVolumePostProcessSettings& settings) const;
		void CompositePass();
		void DOFPass();
		bool CanCompositeDOFIntoFinalTarget();
		void JumpFloodPass();
		void JumpFloodCompositePass();
		void GridPass();

		void UpdateStatistics();
		void ResetProfilingData();
		PassProfile& GetOrCreatePassProfile(const char* name);
		void RecordCPUProfile(const char* name, float cpuTime);
		void BeginProfiledGPU(const char* name);
		void EndProfiledGPU();
		void UpdateGPUProfileTimes();
		void UpdateMemoryStatistics();
		void UpdateRenderGraphStatistics();
		bool UpdateDynamicRenderResolution();
		float ResolveRenderResolutionScale() const;
		void ResizeLightCullingResources();
		void ResizeBloomResources();
		void CreateBloomPassMaterials();
		void ResizeScreenSpaceEffectResources();
		void ResizeVolumetricCloudResources(bool forceRecreate = false);
		glm::uvec2 CalculateVolumetricCloudRenderSize() const;
		enum SceneRenderPassInput : uint32_t
		{
			PassInputNone = 0,
			PassInputCamera = 1u << 0,
			PassInputScene = 1u << 1,
			PassInputScreen = 1u << 2,
			PassInputRenderer = 1u << 3,
			PassInputAtmosphere = 1u << 4,
			PassInputShadowData = 1u << 5,
			PassInputLights = 1u << 6,
			PassInputSamplers = 1u << 7,
			PassInputDepth = 1u << 8,
			PassInputEnvironment = 1u << 9,
			PassInputShadowMaps = 1u << 10,
			PassInputMaterialScene = 1u << 11,
			PassInputGBuffer = 1u << 12,
			PassInputSceneColor = 1u << 13
		};
		static constexpr uint32_t PassInputFrameUniforms = PassInputCamera | PassInputScene | PassInputScreen | PassInputRenderer | PassInputAtmosphere;
		static constexpr uint32_t PassInputLightingBuffers = PassInputShadowData | PassInputLights;
		static constexpr uint32_t PassInputCommonScene = PassInputFrameUniforms | PassInputLightingBuffers | PassInputSamplers;
		static constexpr uint32_t PassInputPBRLighting = PassInputCommonScene | PassInputEnvironment | PassInputShadowMaps;
		void BindSceneRenderPassInputs(Ref<RenderPass> renderPass, uint32_t inputMask);
		void BindCommonSceneRenderPassInputs(Ref<RenderPass> renderPass, bool bindDepth = false);
		bool UsesDeferredPath() const;
		bool UsesDeferredPath(RenderingTechnique technique) const;
		Ref<Image2D> GetSceneColorOutput() const;
		Ref<Image2D> GetGeometryBaseColorOutput() const;
		Ref<Image2D> GetGeometryNormalOutput() const;
		Ref<Image2D> GetGeometryMetalRoughOutput() const;
		Ref<Image2D> GetGeometryMaterialIDOutput() const;
		Ref<Image2D> GetGeometryObjectIDOutput() const;
		Ref<Image2D> GetGeometryVelocityOutput() const;
		void CreateHZBPassMaterials();
		void CreatePreIntegrationPassMaterials();
		void CreatePreConvolutionPassMaterials();
		void BuildRenderGraph(bool executable = false);
		void ApplyRenderTargetAliasing();
		void ClearRenderTargetAliasing(bool recreateResources);
		void RecreateRenderTargetFramebuffers();
		void RefreshRenderTargetImageViews();
		bool IsRenderGraphAliasCandidate(const Ref<Image2D>& image);
		struct ResolvedFrameEnvironment
		{
			RenderingTechnique Technique = RenderingTechnique::Forward;
			bool DeferredPath = false;
			Ref<Environment> Environment;
			float EnvironmentIntensity = 1.0f;
			float SkyboxLod = 0.0f;
			AtmosphereEnvironment Atmosphere;
			RenderVolumeEnvironment Volumes;
			RenderVolumePostProcessSettings PostProcess;
			bool HasRenderVolumeEnvironment = false;
			bool SkyAtmosphereEnabled = false;
			bool VolumetricCloudsEnabled = false;
			bool HeightFogEnabled = false;
			bool LocalFogEnabled = false;
			bool BloomEnabled = false;
			bool DOFEnabled = false;
		};
		ResolvedFrameEnvironment ResolveFrameEnvironment() const;
		void RefreshFrameEnvironment();
		RenderVolumePostProcessSettings GetResolvedPostProcessSettings() const;

		struct CascadeData
		{
			glm::mat4 ViewProj{ 1.0f };
			float SplitDepth = 0.0f;
		};
		void CalculateCascades(CascadeData* cascades, const SceneRendererCamera& sceneCamera, const glm::vec3& lightDirection, float maxShadowDistance) const;

		void BuildIndirectDrawCommand(const StaticDrawCommand& dc,
			const TransformMapData& tmd,
			std::vector<nvrhi::DrawIndexedIndirectArguments>& drawCommands);

		struct ScopedCPUProfile
		{
			ScopedCPUProfile(SceneRenderer& renderer, const char* name);
			~ScopedCPUProfile();

			SceneRenderer& Renderer;
			const char* Name = "";
			Timer ProfileTimer;
		};

		// Render-thread draw helper (must be called inside Renderer::Submit).
		void RT_DrawStaticMesh(Ref<RenderCommandBuffer> cmd,
			const StaticDrawCommand& dc,
			const TransformMapData& tmd,
			bool                     bindMaterial,
			uint32_t                 lightIndex = 0,
			bool                     useVisibleObjectIndexes = false,
			bool                     useIndirect = false,
			Ref<Shader>              pipelineShader = nullptr);

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
			// TAA: unjittered current + previous VP for motion-vector reprojection,
			// and the sub-pixel clip-space jitter applied to the rasterized VP above.
			glm::mat4 UnjitteredViewProjection = glm::mat4(1.0f);
			glm::mat4 PreviousViewProjection = glm::mat4(1.0f);
			glm::vec2 Jitter = { 0.0f, 0.0f };
			glm::vec2 PreviousJitter = { 0.0f, 0.0f };
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
			bool      ShowMaterialComplexity = false;
			char      Pad4[3] = { 0, 0, 0 };
			float     TextureMipBias = 0.0f;
			bool      EnableDistanceMipBias = false;
			char      Pad5[3] = { 0, 0, 0 };
			float     DistanceMipBiasStart = 50.0f;
			float     DistanceMipBiasEnd = 250.0f;
			float     DistanceMipBiasMax = 2.0f;
			uint32_t  GPUSceneDebugMode = 0;
		} m_RendererDataUB;

		struct UBScreenData
		{
			glm::vec2 InvFullResolution = { 1.0f, 1.0f };
			glm::vec2 FullResolution = { 1.0f, 1.0f };
			glm::vec2 InvHalfResolution = { 1.0f, 1.0f };
			glm::vec2 HalfResolution = { 1.0f, 1.0f };
			glm::vec2 InvQuarterResolution = { 1.0f, 1.0f };
			glm::vec2 QuarterResolution = { 1.0f, 1.0f };
		} m_ScreenDataUB;

		struct UBAtmosphere
		{
			struct LocalFogVolumeData
			{
				glm::mat4 WorldToLocal = glm::mat4(1.0f);
				glm::vec4 ColorDensity = { 0.52f, 0.62f, 0.72f, 0.0f };
				glm::vec4 Params0 = { 0.2f, 0.0f, 0.025f, 0.0f };
				glm::vec4 Params1 = { 120.0f, 1.0f, 0.0f, 0.0f };
				glm::uvec4 Metadata = { 0u, 0u, 0u, 0u };
			};

			glm::vec4 RayleighScattering = { 5.802f, 13.558f, 33.100f, 0.0025f };
			glm::vec4 MieScattering = { 3.996f, 3.996f, 3.996f, 0.0015f };
			glm::vec4 MieAbsorption = { 4.400f, 4.400f, 4.400f, 0.76f };
			glm::vec4 Absorption = { 0.650f, 1.881f, 0.085f, 0.00065f };
			glm::vec4 GroundAlbedo = { 0.18f, 0.18f, 0.18f, 0.15f };
			glm::vec4 AtmosphereParams = { 6360.0f, 100.0f, 8.0f, 1.2f };
			glm::vec4 SunParams = { 20.0f, 0.00935f, 0.35f, 1.0f };
			glm::vec4 CloudParams0 = { 0.58f, 0.35f, 1800.0f, 650.0f };
			glm::vec4 CloudParams1 = { 1.0f, 0.15f, 15.0f, 0.00065f };
			glm::vec4 CloudParams2 = { 0.0045f, 0.35f, 0.85f, 0.35f };
			glm::vec4 CloudColor = { 1.0f, 0.98f, 0.92f, 0.35f };
			glm::vec4 CloudRenderParams = { 6000.0f, 2500.0f, 2000.0f, 2500.0f };
			glm::vec4 CloudRenderParams2 = { 2.0f, 0.0f, 0.0f, 0.0f };
			glm::vec4 FogColorDensity = { 0.52f, 0.62f, 0.72f, 0.015f };
			glm::vec4 FogParams0 = { 0.12f, 0.0f, 0.85f, 10000.0f };
			glm::vec4 FogDirectionalInscattering = { 1.0f, 0.88f, 0.65f, 8.0f };
			glm::vec4 FogParams1 = { 50.0f, 1.0f, 0.2f, 0.0f };
			glm::uvec4 Flags = { 1u, 0u, 0u, 0u };
			glm::uvec4 Steps = { 24u, 1u, 0u, 16u };
			glm::uvec4 LocalFogParams = { 0u, 0u, 0u, 0u };
			std::array<LocalFogVolumeData, MaxVisibleLocalFogVolumes> LocalFogVolumes{};
		} m_AtmosphereUB;

		struct CBGTAOData
		{
			glm::vec2 NDCToViewMul_x_PixelSize = { 1.0f, 1.0f };
			float EffectRadius = 0.5f;
			float EffectFalloffRange = 0.62f;
			float RadiusMultiplier = 1.46f;
			float FinalValuePower = 2.2f;
			float DenoiseBlurBeta = 1.2f;
			uint32_t ResolutionScale = 2;
			float SampleDistributionPower = 2.0f;
			float ThinOccluderCompensation = 0.0f;
			float DepthMIPSamplingOffset = 3.3f;
			int NoiseIndex = 0;
			glm::vec2 HZBUVFactor = { 1.0f, 1.0f };
			float ShadowTolerance = 0.0f;
			uint32_t SliceCount = 9;
			uint32_t StepsPerSlice = 3;
			uint32_t TemporalAccumulation = 0;
			float TemporalBlend = 0.0f;
			float Padding = 0.0f;
		} m_GTAODataCB;

		struct GTAODenoiseConstants
		{
			float DenoiseBlurBeta = 1.2f;
			uint32_t ResolutionScale = 2;
		} m_GTAODenoiseConstants;

		struct TemporalAccumulationConstants
		{
			glm::mat4 PreviousViewProjection = glm::mat4(1.0f);
			float Blend = 0.0f;
			uint32_t HasHistory = 0;
			uint32_t BentNormals = 0;
			uint32_t ResolutionScale = 1;
		};

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
		RenderGraph                m_RenderGraph;
		std::vector<Ref<Image2D>>   m_RenderGraphAliasedImages;
		bool                       m_RenderTargetAliasingApplied = false;
		size_t                     m_LastRenderGraphDiagnosticHash = 0;

		Ref<Renderer2D>    m_Renderer2D;
		Ref<Renderer2D>    m_Renderer2DScreenSpace;
		Ref<DebugRenderer> m_DebugRenderer;
		Ref<RenderScene>   m_SubmittedRenderScene;
		GPUSceneDebugSnapshot m_GPUSceneDebugSnapshot;
		std::function<void()> m_WorldOverlayRenderCallback;

		glm::mat4 m_ScreenSpaceProjectionMatrix{ 1.0f };

		// Scene data populated each frame in BeginScene / SetLightEnvironment.
		struct SceneInfo
		{
			SceneRendererCamera SceneCamera;
			Frustum             CameraFrustum;
			Ref<Environment>    SceneEnvironment;
			float               SceneEnvironmentIntensity = 1.0f;
			float               SkyboxLod = 0.0f;
			AtmosphereEnvironment Atmosphere;
			LightEnvironment    SceneLightEnvironment;
		} m_SceneData;
		ResolvedFrameEnvironment m_FrameEnvironment;

		// ── Uniform / Storage buffer sets ─────────────────────────────────────
		Ref<UniformBufferSet> m_UBSCamera;
		Ref<UniformBufferSet> m_UBSScene;
		Ref<UniformBufferSet> m_UBSShadow;
		Ref<UniformBufferSet> m_UBSSpotShadow;
		Ref<UniformBufferSet> m_UBSRendererData;
		Ref<UniformBufferSet> m_UBSScreenData;
		Ref<UniformBufferSet> m_UBSAtmosphere;
		Ref<UniformBufferSet> m_UBSPointLights;
		Ref<UniformBufferSet> m_UBSSpotLights;

		Ref<StorageBufferSet> m_SBSObjectIndexes;       // uint32_t[] - maps draw instance to GPUScene instance
		Ref<StorageBufferSet> m_SBSVisibleObjectIndexes;
		Ref<StorageBufferSet> m_SBSGPUSceneInstances;
		Ref<StorageBufferSet> m_SBSGPUMaterials;
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
		Ref<ComputePass> m_GTAOTemporalPass;
		Ref<Material>    m_GTAODenoiseMaterial[2];
		Ref<Image2D>     m_GTAOOutputImage;
		Ref<Image2D>     m_GTAODenoiseImage;
		Ref<Image2D>     m_GTAOFinalImage;
		Ref<Image2D>     m_GTAOEdgesOutputImage;
		Ref<Image2D>     m_GTAOHistoryImages[2];
		glm::uvec3       m_GTAOWorkGroups{ 1 };
		glm::uvec3       m_GTAODenoiseWorkGroups{ 1 };
		glm::uvec3       m_GTAOTemporalWorkGroups{ 1 };
		uint32_t         m_GTAOHistoryIndex = 0;

		Ref<RenderPass>  m_AOCompositePass;
		Ref<Material>    m_AOCompositeMaterial;
		Ref<RenderPass>  m_AODebugPass;
		Ref<Material>    m_AODebugMaterial;

		// ── SSR ──────────────────────────────────────────────────────────────
		Ref<Image2D>     m_SSRImage;
		Ref<Image2D>     m_SSRFinalImage;
		Ref<Image2D>     m_SSRHistoryImages[2];
		Ref<ComputePass> m_SSRPass;
		Ref<ComputePass> m_SSRTemporalPass;
		Ref<RenderPass>  m_SSRCompositePass;
		Ref<Material>    m_SSRCompositeMaterial;
		glm::uvec3       m_SSRWorkGroups{ 1 };
		glm::uvec3       m_SSRTemporalWorkGroups{ 1 };
		uint32_t         m_SSRHistoryIndex = 0;

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
		Ref<Framebuffer> m_GeometryPassFramebuffer;     // GBuffer attachments
		Ref<Framebuffer> m_SceneColorFramebuffer;       // HDR scene color shared by deferred/forward lighting
		Ref<Pipeline>    m_GeometryPipeline;            // opaque GBuffer
		Ref<Pipeline>    m_ForwardGeometryPipeline;     // opaque forward fallback
		Ref<Pipeline>    m_TransparentGeometryPipeline; // transparent forward PBR
		Ref<Pipeline>    m_DeferredLightingPipeline;    // fullscreen deferred lighting
		Ref<RenderPass>  m_GeometryPass;                // opaque GBuffer
		Ref<RenderPass>  m_ForwardGeometryPass;         // opaque forward fallback
		Ref<RenderPass>  m_GeometryPassTransparent;     // transparent forward
		Ref<RenderPass>  m_DeferredLightingPass;        // fullscreen deferred lighting
		Ref<Material>    m_DeferredLightingMaterial;
		Ref<RenderPass>  m_GBufferDebugPass;
		Ref<Material>    m_GBufferDebugMaterial;
		std::vector<Ref<Texture2D>> m_GPUMaterialTextures;

		// ── Selected / wireframe ──────────────────────────────────────────────
		Ref<RenderPass>  m_SelectedGeometryPass;
		Ref<Material>    m_SelectedGeometryMaterial;
		Ref<RenderPass>  m_GeometryWireframePass;
		Ref<Material>    m_WireframeMaterial;

		// ── Skybox ────────────────────────────────────────────────────────────
		Ref<Pipeline>    m_SkyboxPipeline;
		Ref<Material>    m_SkyboxMaterial;
		Ref<RenderPass>  m_SkyboxPass;
		Ref<Pipeline>    m_SkyAtmospherePipeline;
		Ref<Material>    m_SkyAtmosphereMaterial;
		Ref<RenderPass>  m_SkyAtmospherePass;
		Ref<Pipeline>    m_VolumetricCloudPipeline;
		Ref<Material>    m_VolumetricCloudMaterial;
		Ref<RenderPass>  m_VolumetricCloudPass;
		Ref<Pipeline>    m_VolumetricCloudCompositePipeline;
		Ref<Material>    m_VolumetricCloudCompositeMaterial;
		Ref<RenderPass>  m_VolumetricCloudCompositePass;
		Ref<Pipeline>    m_AtmosphericFogPipeline;
		Ref<Material>    m_AtmosphericFogMaterial;
		Ref<RenderPass>  m_AtmosphericFogPass;
		uint32_t         m_CloudRenderScale = 2;
		glm::uvec2       m_CloudRenderSize = { 1, 1 };

		// ── Composite (tone-map + opacity) ────────────────────────────────────
		Ref<Framebuffer> m_CompositingFramebuffer;
		Ref<Material>    m_CompositeMaterial;
		Ref<RenderPass>  m_CompositePass;
		DebugViewMode    m_DebugViewMode = DebugViewMode::Final;

		// Latest histogram auto-exposure result (linear multiplier). Driven by the
		// auto-exposure passes; consumed when ExposureMode::Automatic is active.
		float            m_AutoExposure = 1.0f;
		bool             m_AutoExposureValid = false;

		// ── Histogram auto-exposure ───────────────────────────────────────────
		Ref<ComputePass> m_LuminanceHistogramPass;
		Ref<ComputePass> m_LuminanceAveragePass;
		Ref<StorageBufferSet> m_SBSLuminanceHistogram;   // 256-bin histogram (per-frame)
		Ref<StorageBufferSet> m_SBSExposureState;        // { adapted luminance, exposure }

		// ── TAA resolve ───────────────────────────────────────────────────────
		Ref<ComputePass> m_TAAResolvePass;
		Ref<Image2D>     m_TAAHistoryImages[2]; // ping-pong resolved-color history
		uint32_t         m_TAAHistoryIndex = 0;
		static constexpr uint32_t s_LuminanceHistogramBins = 256;
		static constexpr float s_AutoExposureMinLogLuminance = -10.0f; // log2 luminance for bin 1
		static constexpr float s_AutoExposureMaxLogLuminance = 2.0f;   // log2 luminance for bin 255

		// ── Editor grid ───────────────────────────────────────────────────────
		Ref<RenderPass>  m_GridRenderPass;
		Ref<Material>    m_GridMaterial;

		// ── Physics collider debug ────────────────────────────────────────────
		Ref<Material>    m_SimpleColliderMaterial;
		Ref<Material>    m_ComplexColliderMaterial;

		// ── Mesh passes ───────────────────────────────────────────────────────
		std::array<MeshPassState, MeshPassTypeCount> m_MeshPasses;
		std::unordered_map<MeshDrawCommandCacheKey, CachedStaticDrawCommand, MeshDrawCommandCacheKeyHasher> m_MeshDrawCommandCache;
		uint32_t m_MeshDrawCommandCacheFrame = 0;

		// GPUScene indirection for all submitted meshes this frame.
		std::unordered_map<MeshKey, TransformMapData, MeshKeyHasher>  m_MeshTransformMap;
		std::vector<GPUSceneInstanceData>    m_TransientGPUSceneInstances;
		std::vector<GPUMaterialData>         m_TransientGPUMaterials;
		std::unordered_map<uint64_t, uint32_t> m_TransientGPUMaterialIndexByKey;
		std::vector<AssetHandle> m_TransientGPUTextureHandles;
		std::unordered_map<AssetHandle, GPUTextureIndex> m_TransientGPUTextureIndexByHandle;
		GPUTextureIndex m_NextTransientGPUTextureIndex = 0;

		// Shadow-specific per-cascade transform tracking.
		// Index 0 is the only cascade we use currently.
		struct ShadowTransformMapData
		{
			TransformMapData Cascade; // single cascade
		};
		std::unordered_map<MeshKey, ShadowTransformMapData, MeshKeyHasher> m_ShadowMeshTransformMap;
		std::array<Frustum, ShadowCascadeCount> m_ShadowCascadeFrustums;
		std::array<Frustum, MaxSpotShadows> m_SpotShadowFrustums;
		uint32_t m_ShadowCascadeFrustumCount = 0;
		uint32_t m_SpotShadowFrustumCount = 0;

		struct FrameCullingStats
		{
			uint32_t SubmittedInstances = 0;
			uint32_t MainViewCulledInstances = 0;
			uint32_t ShadowCulledInstances = 0;
			uint32_t FullyCulledInstances = 0;
		} m_FrameCullingStats;

		// ── Viewport / frame state ────────────────────────────────────────────
		uint32_t m_OutputViewportWidth = 0;
		uint32_t m_OutputViewportHeight = 0;
		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;
		float    m_InvViewportWidth = 0.0f;
		float    m_InvViewportHeight = 0.0f;
		bool     m_NeedsResize = false;
		bool     m_Active = false;
		bool     m_ResourcesCreatedGPU = false;
		bool     m_ResourcesCreated = false;
		bool     m_HZBPrimed = false;
		bool     m_TemporalHistoryValid = false;
		glm::mat4 m_CurrentViewProjection = glm::mat4(1.0f); // unjittered
		glm::mat4 m_PreviousViewProjection = glm::mat4(1.0f); // unjittered
		glm::vec2 m_CurrentJitter = { 0.0f, 0.0f };  // clip-space sub-pixel offset this frame
		glm::vec2 m_PreviousJitter = { 0.0f, 0.0f };
		uint32_t  m_TAAJitterIndex = 0;              // Halton sequence index

		float m_LineWidth = 2.0f;
		float m_Opacity = 1.0f;
		float m_ScaleShadowCascadesToOrigin = 0.0f;
		float m_ShadowCascadeSplits[ShadowCascadeCount] = { 0.1f, 0.2f, 0.3f, 1.0f };
		bool  m_UseManualCascadeSplits = false;
		bool  m_ShadowCascadeCacheValid = false;
		bool  m_DirectionalShadowMapCacheValid = false;
		bool  m_DirectionalShadowMapNeedsRender = true;
		bool  m_SpotShadowMapCacheValid = false;
		bool  m_SpotShadowMapNeedsRender = true;
		uint64_t m_LastShadowCasterHash = 0;
		uint64_t m_LastSpotShadowStateHash = 0;
		glm::vec3 m_CachedShadowCameraPosition{ 0.0f };
		glm::vec3 m_CachedShadowCameraForward{ 0.0f, 0.0f, -1.0f };
		glm::vec3 m_CachedShadowLightDirection{ 0.0f, -1.0f, 0.0f };
		float m_CachedShadowFOV = 0.0f;
		float m_CachedShadowNear = 0.0f;
		float m_CachedShadowFar = 0.0f;
		float m_CachedMaxShadowDistance = 0.0f;
		float m_CachedShadowCascadeSplitLambda = 0.0f;
		float m_CachedShadowCascadeNearPlaneOffset = 0.0f;
		float m_CachedShadowCascadeFarPlaneOffset = 0.0f;
		float m_CachedScaleShadowCascadesToOrigin = 0.0f;
		float m_CachedShadowCascadeSplits[ShadowCascadeCount] = {};
		bool  m_CachedUseManualCascadeSplits = false;
		uint32_t m_CachedShadowMapResolution = 0;
		BloomSettings m_BloomSettings;
		DOFSettings m_DOFSettings;
		RenderVolumeEnvironment m_RenderVolumeEnvironment;
		bool m_HasRenderVolumeEnvironment = false;
		SSROptionsUB m_SSROptions;

		SceneRendererOptions m_Options;
		Statistics           m_Statistics;
	};

} // namespace Lux
