#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Renderer/Camera.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/RenderPass.h"
#include "Lux/Renderer/ComputePass.h"
#include "Lux/Renderer/RenderGraph.h"
#include "Lux/Renderer/PostProcessSettings.h"
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
#include "Lux/Debug/Profiler.h"
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

	// One quality tier. Used both for the overall preset and for each individual
	// category, so a project can say "High overall, but Shadows at Ultra".
	//
	// Custom means "this category's knobs were edited by hand" - the values are then
	// owned by the project file rather than by a tier, and nothing will overwrite them.
	// It is only ever entered explicitly; no tier resolves to it.
	enum class QualityPreset : uint32_t
	{
		Low = 0,
		Medium = 1,
		High = 2,
		Ultra = 3,
		Cinematic = 4,
		Custom = 5
	};

	// The groups a user picks a tier for, mirroring a game's graphics menu. Each group
	// owns a disjoint set of SceneRendererOptions fields; see the Apply*Quality methods
	// on SceneRenderer for exactly which.
	enum class QualityCategory : uint32_t
	{
		Shadows = 0,
		AmbientOcclusion,
		Reflections,
		AntiAliasing,
		Textures,
		ResolutionScale,
		PostProcessing,
		COUNT
	};

	// Per-category tiers. The overall preset is just "set every category to this tier",
	// so the two can never disagree in a way the UI cannot show.
	struct RendererQualityCategories
	{
		QualityPreset Shadows = QualityPreset::Medium;
		QualityPreset AmbientOcclusion = QualityPreset::Medium;
		QualityPreset Reflections = QualityPreset::Medium;
		QualityPreset AntiAliasing = QualityPreset::Medium;
		QualityPreset Textures = QualityPreset::Medium;
		QualityPreset ResolutionScale = QualityPreset::Medium;
		QualityPreset PostProcessing = QualityPreset::Medium;

		QualityPreset& Get(QualityCategory category);
		QualityPreset Get(QualityCategory category) const;
		void SetAll(QualityPreset level);

		// The tier every category shares, or Custom when they differ - which is what the
		// overall "Quality" dropdown displays.
		QualityPreset Unified() const;
	};

	const char* QualityPresetToDisplayString(QualityPreset preset);
	const char* QualityCategoryToDisplayString(QualityCategory category);

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
			Dynamic = 3,
			// Render at an absolute resolution (FixedRenderWidth x FixedRenderHeight) regardless of
			// how large the output viewport is, then scale the result to fit. Lets a 4K editor
			// display show the exact pixel count - and therefore the cost - of a 1080p target.
			// Unlike the modes above this is not a fraction of the output, so the render aspect can
			// differ from the viewport's; the presenter is expected to letterbox rather than stretch.
			FixedResolution = 4
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

		enum class ShadowFilterMode : uint32_t
		{
			TunedPCF = 0,
			PCSS = 1,
			Hybrid = 2
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
		uint32_t ActiveShadowCascadeCount = 3;
		float ShadowCascadeSplitLambda = 0.82f;
		float ShadowCascadeNearPlaneOffset = 0.0f;
		float ShadowCascadeFarPlaneOffset = 50.0f;
		float ShadowCascadeTransitionFade = 1.0f;
		ShadowFilterMode ShadowFilter = ShadowFilterMode::Hybrid;
		uint32_t DirectionalPCSSCascadeCount = 1;
		float ShadowPCFRadiusTexels = 1.25f;
		float SpotShadowPCFRadiusTexels = 1.5f;
		ShadowResolutionTier ShadowResolution = ShadowResolutionTier::Tier_2K;
		// Overall tier. Kept in sync with QualityCategories: it is whatever every
		// category agrees on, or Custom when they differ.
		QualityPreset Quality = QualityPreset::Medium;
		RendererQualityCategories QualityCategories;
		bool  EnableGTAO = true;
		bool  GTAOBentNormals = false;
		float AOShadowTolerance = 1.0f;
		int   GTAODenoisePasses = 4;
		bool  EnableSSR = true;
		ShaderDef::AOMethod ReflectionOcclusionMethod = ShaderDef::AOMethod::None;
		bool  EnableJumpFlood = true;
		bool  EnableFrustumCulling = true;
		bool  EnableOcclusionCulling = true;
		float OcclusionDepthBias = 0.003f;
		float OcclusionBoundsScale = 1.15f;
		bool  EnableGPUDrivenRendering = true;
		bool  EnableMeshLODs = true;
		float MeshLODDistanceScale = 1.0f;
		bool  EnableVariableRateShading = true;
		// Experimental: task/mesh-shader PreDepth with per-meshlet culling.
		// Requires VK_EXT_mesh_shader; ignored (classic path) when unsupported.
		bool  EnableMeshShaders = false;
		EffectResolutionScale GTAOResolutionScale = EffectResolutionScale::Half;
		SSRQualityPreset SSRQuality = SSRQualityPreset::HalfBilateral;
		EffectResolutionScale SSRResolutionScale = EffectResolutionScale::Half;
		// NOTE: this engine ships no temporal/reprojection techniques. TAA, SMAA T2x, and
		// GTAO/SSR/cloud temporal accumulation were removed outright rather than left
		// off-by-default, because every one of them trades ghosting and smearing behind
		// moving geometry for its performance win. Quality is bought spatially instead:
		// more denoise passes, higher effect resolution, more ray steps, lower SMAA
		// threshold. Do not reintroduce them.
		//
		// SMAA 1x. Runs on the tone-mapped image at the very end of the post chain. Purely
		// spatial: it cleans up staircase edges within a frame, and cannot smear.
		bool  EnableSMAA = false;
		float SMAAThreshold = 0.1f;                        // edge sensitivity; lower catches more
		float SMAALocalContrastAdaptationFactor = 2.0f;    // suppresses doubled edges on silhouettes
		// MSAA sample count: 1 (off), 2, 4, 8 or 16. Unlike SMAA this is not a post
		// process - it multisamples the G-buffer itself, which in a deferred renderer
		// means every attachment plus depth is allocated N times over and the lighting
		// pass has to evaluate (or classify) per sample. See MSAASamplesSupported.
		uint32_t MSAASamples = 1;
		// Schedule independent compute passes (cluster light-cull, GTAO, SSR,
		// bloom) on the async compute queue overlapping graphics work. Off by
		// default while the cross-queue path is brought up one pass at a time;
		// nothing submits async until a pass is wired to honor this flag.
		bool  EnableAsyncCompute = false;
		RenderResolutionScaleMode ResolutionScaleMode = RenderResolutionScaleMode::Native;
		// Only meaningful for RenderResolutionScaleMode::FixedResolution.
		uint32_t FixedRenderWidth = 1920;
		uint32_t FixedRenderHeight = 1080;
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
		// Which byte the GTAO visibility term lives in (high byte for bent normals,
		// low byte otherwise). Decoded at runtime in SSR.glsl so it can't desync
		// from the GTAO producer's packing. Was previously named Padding2.
		// NOTE: mirrors the u_SSRInfo block in SSR.glsl field-for-field - edit both.
		uint32_t BentNormals = 0;
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

		// Editor-only render targets (selection outline, wireframe, AO/GBuffer
		// debug views) cost ~180 MB of full-viewport images. The standalone
		// runtime sets this false so they are never created; their passes
		// null-guard and never execute there.
		bool EnableEditorRenderTargets = true;
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
		// Clustered (froxel) light culling grid — see CLUSTERED_DEFERRED_PLAN.md.
		static constexpr uint32_t ClusterGridX = 16;
		static constexpr uint32_t ClusterGridY = 9;
		static constexpr uint32_t ClusterGridZ = 32;
		static constexpr uint32_t ClusterCount = ClusterGridX * ClusterGridY * ClusterGridZ;
		// Global packed light-index list caps (dynamic packing). Must match the
		// CLUSTER_MAX_*_INDICES defines in Cluster.glslh.
		static constexpr uint32_t ClusterAvgLightsPerCluster = 16;
		static constexpr uint32_t MaxClusterPointIndices = ClusterCount * ClusterAvgLightsPerCluster;
		static constexpr uint32_t MaxClusterSpotIndices = ClusterCount * ClusterAvgLightsPerCluster;
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
			bool DeferredPath = true;
			bool HasRenderScene = false;
			bool BloomEnabled = false;
			bool DOFEnabled = false;
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

		// Per-category tier appliers. Each owns a disjoint slice of SceneRendererOptions
		// (plus the matching m_BloomSettings / m_DOFSettings / m_SSROptions fields) and
		// writes every field it owns on every call, so a tier is fully described by its
		// own branch and never inherits a stray value from the previously-applied tier.
		// A level of Custom is a no-op: the values stay as the project left them.
		void ApplyShadowQuality(QualityPreset level);
		void ApplyAmbientOcclusionQuality(QualityPreset level);
		void ApplyReflectionQuality(QualityPreset level);
		void ApplyAntiAliasingQuality(QualityPreset level);
		void ApplyTextureQuality(QualityPreset level);
		void ApplyResolutionScaleQuality(QualityPreset level);
		void ApplyPostProcessQuality(QualityPreset level);

		// Recomputes the derived SSR/GTAO scratch values the appliers feed into, and
		// refreshes m_Options.Quality from the category tiers.
		void FinalizeQualityChange();
		// Rebinds Camera/samplers/GTAO inputs and rebakes the AO passes after the AO
		// shader recompiles into the GTAO-on variant. No-ops until that variant is live.
		void RebakeAOPassInputs();

		// ── Per-frame API ────────────────────────────────────────────────────

		void BeginScene(const SceneRendererCamera& camera);

		// Call before BeginScene to update the scene state consumed by the render passes.
		void SetLightEnvironment(const LightEnvironment& lightEnvironment);
		void SetEnvironment(Ref<Environment> environment, float intensity = 1.0f, float skyboxLod = 0.0f);
		// Scene-wide post-processing. Replaces the removed per-volume blending: the scene
		// authors one set of values and they apply everywhere.
		void SetPostProcessSettings(const PostProcessSettings& postProcessSettings);

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
		// True only when the SMAA passes exist *and* the reference lookup textures were
		// vendored. Everything SMAA-related is gated on this, so a missing AreaTex/SearchTex
		// degrades to "no antialiasing" rather than a wrong image or a failed validate. Public
		// so the renderer panel can explain why the toggle is having no effect.
		bool IsSMAAReady() const;
		BloomSettings& GetBloomSettings() { return m_BloomSettings; }
		DOFSettings& GetDOFSettings() { return m_DOFSettings; }
		// The authored settings with Bloom / DOF / Exposure overlaid from the renderer's own
		// state, which is what the passes actually consume.
		PostProcessSettings GetEffectivePostProcessSettings(float cameraExposure) const;
		const PostProcessSettings& GetPostProcessSettings() const { return m_PostProcessSettings; }
		SSROptionsUB& GetSSROptions() { return m_SSROptions; }
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

	// Sets every category to `preset` and applies all of them. Custom is ignored here:
	// "set everything to Custom" is meaningless, since Custom means "leave the values
	// alone". Use SetCategoryQuality to move a single group.
	void ApplyQualityPreset(QualityPreset preset);

	// Applies one category's tier, leaving every other category untouched. A tier of
	// Custom applies nothing - the current values simply become the project's own.
	void SetCategoryQuality(QualityCategory category, QualityPreset level);
	QualityPreset GetCategoryQuality(QualityCategory category) const { return m_Options.QualityCategories.Get(category); }

	// Marks a category as hand-edited. Call this from any UI that writes a raw option
	// so the value stops being owned - and overwritten - by a tier.
	void MarkCategoryCustom(QualityCategory category);

	// Re-applies whatever tier each category currently holds. Used after the categories
	// are loaded from a project.
	void ApplyAllCategoryQuality();

	uint32_t GetViewportWidth()  const { return m_ViewportWidth; }
	uint32_t GetViewportHeight() const { return m_ViewportHeight; }
	uint32_t GetOutputViewportWidth()  const { return m_OutputViewportWidth; }
	uint32_t GetOutputViewportHeight() const { return m_OutputViewportHeight; }
	float GetRenderResolutionScale() const;

		float GetOpacity() const { return m_Opacity; }
		void  SetOpacity(float opacity) { m_Opacity = opacity; }

		const glm::mat4& GetScreenSpaceProjectionMatrix() const { return m_ScreenSpaceProjectionMatrix; }
		const Statistics& GetStatistics() const { return m_Statistics; }
		RenderGraphDebugSnapshot GetRenderGraphDebugSnapshot();
		RendererFrameDebugSnapshot GetRendererFrameDebugSnapshot() const;
		const GPUSceneDebugSnapshot& GetGPUSceneDebugSnapshot() const { return m_GPUSceneDebugSnapshot; }
		// The snapshot's validation loops cost O(instances + materials) CPU, so it
		// is only built on frames where a consumer (Renderer Debugger panel) asks.
		void RequestGPUSceneDebugSnapshot() { m_GPUSceneDebugSnapshotRequested = true; }
		const Frustum& GetCameraFrustum() const { return m_SceneData.CameraFrustum; }

		bool IsReady() const { return m_ResourcesCreatedGPU; }

	private:
		// ── Internal draw-list key & commands ────────────────────────────────

		struct MeshKey
		{
			AssetHandle MeshHandle;
			// Zero for passes whose shaders fetch per-instance material data from
			// GPUMaterials or use a fixed pass material; nonzero for material-bound buckets.
			AssetHandle MaterialHandle;
			uint32_t    SubmeshIndex;
			uint32_t    LODIndex;
			bool        IsSelected;
			bool        StaticShadowCaster;

			bool operator<(const MeshKey& o) const
			{
				if (MeshHandle != o.MeshHandle)     return MeshHandle < o.MeshHandle;
				if (SubmeshIndex != o.SubmeshIndex)   return SubmeshIndex < o.SubmeshIndex;
				if (LODIndex != o.LODIndex)       return LODIndex < o.LODIndex;
				if (MaterialHandle != o.MaterialHandle) return MaterialHandle < o.MaterialHandle;
				if (StaticShadowCaster != o.StaticShadowCaster) return StaticShadowCaster < o.StaticShadowCaster;
				return IsSelected < o.IsSelected;
			}

			bool operator==(const MeshKey& o) const
			{
				return MeshHandle == o.MeshHandle
					&& MaterialHandle == o.MaterialHandle
					&& SubmeshIndex == o.SubmeshIndex
					&& LODIndex == o.LODIndex
					&& IsSelected == o.IsSelected
					&& StaticShadowCaster == o.StaticShadowCaster;
			}
		};

		struct MeshKeyHasher
		{
			size_t operator()(const MeshKey& key) const
			{
				size_t seed = std::hash<uint64_t>{}((uint64_t)key.MeshHandle);
				seed ^= std::hash<uint64_t>{}((uint64_t)key.MaterialHandle) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				seed ^= std::hash<uint32_t>{}(key.SubmeshIndex) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				seed ^= std::hash<uint32_t>{}(key.LODIndex) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				seed ^= std::hash<bool>{}(key.IsSelected) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				seed ^= std::hash<bool>{}(key.StaticShadowCaster) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				return seed;
			}
		};

		struct StaticDrawCommand
		{
			Ref<StaticMesh>    StaticMesh;
			Ref<MeshSource>    MeshSource;
			uint32_t           SubmeshIndex = 0;
			uint32_t           LODIndex = 0;
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
			// Fingerprint of the key set DrawOrder was sorted for; when the set
			// is unchanged, last frame's sorted order is reused (DrawOrder is
			// intentionally retained across frames for this).
			uint64_t OrderCacheHash = 0;
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

		// Snapshot of the TransformMapData scalars RT_DrawStaticMesh needs.
		// Captured by value into the render-command lambda instead of the full
		// TransformMapData, which would copy the ObjectIndices heap vector per draw.
		// Must be a snapshot: ClearFrameMeshPasses wipes the transform map on the
		// main thread while the render thread executes a frame behind.
		struct MeshDrawParams
		{
			uint32_t ObjectIndexBase = 0;
			uint32_t VisibleObjectIndexBase = 0;
			uint32_t VisibleInstanceCount = 0;
			uint32_t IndirectDrawOffsetBytes = std::numeric_limits<uint32_t>::max();

			MeshDrawParams() = default;
			explicit MeshDrawParams(const TransformMapData& tmd)
				: ObjectIndexBase(tmd.ObjectIndexBase)
				, VisibleObjectIndexBase(tmd.VisibleObjectIndexBase)
				, VisibleInstanceCount(tmd.VisibleInstanceCount)
				, IndirectDrawOffsetBytes(tmd.IndirectDrawOffsetBytes)
			{
			}
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
		uint32_t GetDirectionalShadowCascadeMask(const BoundingSphere& bounds) const;
		bool IsSpotShadowCasterVisible(const BoundingSphere& bounds) const;
		bool ShouldCullTinyDirectionalShadowCaster(const BoundingSphere& bounds, uint32_t cascade) const;
		uint32_t SelectStaticMeshLOD(const MeshSource& meshSource, uint32_t submeshIndex, const BoundingSphere& bounds) const;
		void ApplyCoarseFragmentShadingRate();
		void BuildSortedDrawCommandOrder(const DrawCommandList& drawList, DrawCommandOrder& drawOrder, uint64_t& orderCacheHash) const;
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
			uint32_t lodIndex,
			AssetHandle materialHandle,
			Ref<Material> overrideMaterial,
			uint64_t pipelineSortKey,
			uint64_t shaderSortKey,
			uint64_t materialSortKey,
			uint64_t meshSortKey);
		void ClearFrameMeshPasses();
		void PruneMeshDrawCommandCache();
		void CalculateShadowCasterHashes(uint64_t& outStaticDirectionalHash, uint64_t& outDynamicDirectionalHash, uint64_t& outStaticSpotHash, uint64_t& outDynamicSpotHash) const;
		bool UpdateShadowCasterMotion(uint32_t sceneInstanceIndex, const GPUSceneInstanceData* instanceData);
		bool IsShadowCasterStatic(uint32_t sceneInstanceIndex) const;

		// ── Render passes ────────────────────────────────────────────────────

		void FlushDrawList();

		void ShadowMapPass();
		void SpotShadowMapPass();
		void PreDepthPass();
		void HZBCompute();
		void PreIntegration();
		void MeshCullingPass();
		void ClusterBuildPass();
		void ClusterLightCullingPass();
		void SkyboxPass();
		void SelectedGeometryPass();
		void GBufferPass();
		void DeferredLightingPass();
		void TransparentForwardPass();
		void GeometryWireframePass();
		void GBufferDebugPass();
		void GTAOCompute();
		void GTAODenoiseCompute();
		void AOComposite();
		void AODebugPass();
		void PreConvolutionCompute();
		void SSRCompute();
		void SSRCompositePass();
		void BloomCompute();
		void AutoExposurePass();
		float ComputeFinalExposure(const PostProcessSettings& settings) const;
		void CompositePass();
		void DOFPass();
		// One entry point: the three (or four, with T2x) dispatches are sequential and
		// share push constants, so splitting them across graph nodes bought nothing.
		void SMAAPass();
		// Image SMAA reads and replaces: the DOF output when DOF is resolving separately,
		// otherwise the composite output.
		Ref<Image2D> GetPostProcessInputImage();
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
		void ResizeBloomResources();
		void CreateBloomPassMaterials();
		void ResizeScreenSpaceEffectResources();
		enum SceneRenderPassInput : uint32_t
		{
			PassInputNone = 0,
			PassInputCamera = 1u << 0,
			PassInputScene = 1u << 1,
			PassInputScreen = 1u << 2,
			PassInputRenderer = 1u << 3,
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
		static constexpr uint32_t PassInputFrameUniforms = PassInputCamera | PassInputScene | PassInputScreen | PassInputRenderer;
		static constexpr uint32_t PassInputLightingBuffers = PassInputShadowData | PassInputLights;
		static constexpr uint32_t PassInputCommonScene = PassInputFrameUniforms | PassInputLightingBuffers | PassInputSamplers;
		static constexpr uint32_t PassInputPBRLighting = PassInputCommonScene | PassInputEnvironment | PassInputShadowMaps;
		void BindSceneRenderPassInputs(Ref<RenderPass> renderPass, uint32_t inputMask);
		void BindCommonSceneRenderPassInputs(Ref<RenderPass> renderPass, bool bindDepth = false);
		bool UsesDeferredPath() const;
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
		// Creates the SMAA passes and loads the reference lookup textures. Safe to call
		// when the vendored SMAA headers are absent: the lookup textures stay null, the
		// weight-and-blend pass is not created, and IsSMAAReady() reports false.
		void CreateSMAAPasses();
		void BuildRenderGraph(bool executable = false);
		void ApplyRenderTargetAliasing();
		void ClearRenderTargetAliasing(bool recreateResources);
		void RecreateRenderTargetFramebuffers();
		void RefreshRenderTargetImageViews();
		bool IsRenderGraphAliasCandidate(const Ref<Image2D>& image);
		struct ResolvedFrameEnvironment
		{
			bool DeferredPath = true;
			Ref<Environment> Environment;
			float EnvironmentIntensity = 1.0f;
			float SkyboxLod = 0.0f;
			PostProcessSettings PostProcess;
			bool BloomEnabled = false;
			bool DOFEnabled = false;
		};
		ResolvedFrameEnvironment ResolveFrameEnvironment() const;
		void RefreshFrameEnvironment();
		PostProcessSettings GetResolvedPostProcessSettings() const;

		struct CascadeData
		{
			glm::mat4 ViewProj{ 1.0f };
			float SplitDepth = 0.0f;
		};
		void CalculateCascades(CascadeData* cascades, const SceneRendererCamera& sceneCamera, const glm::vec3& lightDirection, float maxShadowDistance, uint32_t activeCascadeCount) const;

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
#if LUX_ENABLE_PROFILING
			TracyCZoneCtx ProfileZone;   // Tracy zone spanning this object's lifetime
#endif
		};

		// Render-thread draw helper (must be called inside Renderer::Submit).
		void RT_DrawStaticMesh(Ref<RenderCommandBuffer> cmd,
			const StaticDrawCommand& dc,
			MeshDrawParams           params,
			bool                     bindMaterial,
			uint32_t                 lightIndex = 0,
			bool                     useVisibleObjectIndexes = false,
			bool                     useIndirect = false,
			Ref<Shader>              pipelineShader = nullptr);

		// Mesh-shader PreDepth (task/mesh culled meshlets).
		void PreDepthMeshletPass();
		void RT_DrawStaticMeshMeshlets(Ref<RenderCommandBuffer> cmd,
			const StaticDrawCommand& dc,
			MeshDrawParams           params);

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

		// Shadow UBO upload gate: last uploaded contents + how many more uploads
		// remain (one per frame-in-flight buffer after a change; starts above any
		// realistic frames-in-flight count so startup initializes every buffer).
		UBShadow m_LastUploadedShadowUB{};
		uint32_t m_ShadowUBUploadsRemaining = 8;

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
			glm::vec4 ClusterZParams = { 0.1f, 1000.0f, 0.0f, 0.0f }; // x=zNear, y=zFar
			uint32_t  ActiveShadowCascadeCount = 3;
			uint32_t  ShadowFilterMode = (uint32_t)SceneRendererOptions::ShadowFilterMode::Hybrid;
			uint32_t  DirectionalPCSSCascadeCount = 1;
			uint32_t  ShadowFilterPadding = 0;
			glm::vec4 ShadowFilterParams = { 1.25f, 1.5f, 0.0f, 0.0f }; // x=dir PCF texels, y=spot PCF texels
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
			float Padding = 0.0f;
		} m_GTAODataCB;

		struct GTAODenoiseConstants
		{
			float DenoiseBlurBeta = 1.2f;
			uint32_t ResolutionScale = 2;
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
		Ref<RenderCommandBuffer>   m_CommandBuffer;       // render commands (graphics queue)
		Ref<RenderCommandBuffer>   m_UploadCommandBuffer; // UB/SB data uploads
		Ref<RenderCommandBuffer>   m_ComputeCommandBuffer; // async-compute queue (EnableAsyncCompute)
		RenderGraph                m_RenderGraph;
		std::vector<Ref<Image2D>>   m_RenderGraphAliasedImages;
		bool                       m_RenderTargetAliasingApplied = false;
		size_t                     m_LastRenderGraphDiagnosticHash = 0;
		// Cached render-graph compilation. The graph is rebuilt every frame (cheap),
		// but Compile() (the lifetime/alias analysis) is skipped and the cached result
		// reused while the graph's structure hash is unchanged.
		RenderGraph::CompileResult m_CachedRenderGraphResult;
		uint64_t                   m_RenderGraphStructureHash = 0;
		bool                       m_RenderGraphResultValid = false;

		// Display-only memory statistics are refreshed every N frames, not every frame.
		static constexpr uint32_t  MemoryStatsRefreshFrameInterval = 8;
		uint32_t                   m_MemoryStatsCountdown = 0;

		Ref<Renderer2D>    m_Renderer2D;
		Ref<Renderer2D>    m_Renderer2DScreenSpace;
		Ref<DebugRenderer> m_DebugRenderer;
		Ref<RenderScene>   m_SubmittedRenderScene;
		GPUSceneDebugSnapshot m_GPUSceneDebugSnapshot;
		bool m_GPUSceneDebugSnapshotRequested = false;
		std::function<void()> m_WorldOverlayRenderCallback;

		glm::mat4 m_ScreenSpaceProjectionMatrix{ 1.0f };

		// Scene data populated each frame in BeginScene / SetLightEnvironment.
		struct SceneInfo
		{
			SceneRendererCamera SceneCamera;
			Frustum             CameraFrustum;
			glm::vec3           CameraPosition{ 0.0f };
			Ref<Environment>    SceneEnvironment;
			float               SceneEnvironmentIntensity = 1.0f;
			float               SkyboxLod = 0.0f;
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
		Ref<UniformBufferSet> m_UBSPointLights;
		Ref<UniformBufferSet> m_UBSSpotLights;

		Ref<StorageBufferSet> m_SBSObjectIndexes;       // uint32_t[] - maps draw instance to GPUScene instance
		Ref<StorageBufferSet> m_SBSVisibleObjectIndexes;
		Ref<StorageBufferSet> m_SBSGPUSceneInstances;
		Ref<StorageBufferSet> m_SBSGPUMaterials;
		Ref<StorageBufferSet> m_SBSMeshCullDrawData;
		Ref<StorageBufferSet> m_SBSIndirectDrawCommands;
		Ref<StorageBufferSet> m_SBSClusterAABBs; // per-cluster view-space AABB grid
		// Clustered light assignment outputs (parallel point/spot grids + packed
		// index lists, dynamically allocated via m_SBSClusterLightCounter).
		Ref<StorageBufferSet> m_SBSPointLightGrid;
		Ref<StorageBufferSet> m_SBSSpotLightGrid;
		Ref<StorageBufferSet> m_SBSPointLightIndexList;
		Ref<StorageBufferSet> m_SBSSpotLightIndexList;
		Ref<StorageBufferSet> m_SBSClusterLightCounter;

		// ── Directional shadow maps ─────────────────────────────────────────
		Ref<Image2D>     m_ShadowMapImage;
		std::array<Ref<RenderPass>, ShadowCascadeCount> m_ShadowMapPasses;
		Ref<RenderPass>  m_ShadowMapPass; // Alias for cascade 0, used for shared shadow texture binding
		Ref<Material>    m_ShadowPassMaterial;
		// Static shadow caching: static casters render into a cached depth array
		// only when the static set changes; per frame the cache is copied into the
		// live map and only dynamic casters render on top (no-clear passes).
		Ref<Image2D>     m_ShadowMapStaticCacheImage;
		std::array<Ref<RenderPass>, ShadowCascadeCount> m_ShadowMapStaticCachePasses;
		std::array<Ref<RenderPass>, ShadowCascadeCount> m_ShadowMapDynamicPasses;

		// ── Spot shadow atlas ───────────────────────────────────────────────
		Ref<Image2D>     m_SpotShadowMapImage;
		Ref<RenderPass>  m_SpotShadowMapPass;
		Ref<Image2D>     m_SpotShadowStaticCacheImage;
		Ref<RenderPass>  m_SpotShadowStaticCachePass;
		Ref<RenderPass>  m_SpotShadowDynamicPass;
		Ref<Material>    m_SpotShadowPassMaterial;
		uint32_t         m_SpotShadowMapSize = 2048;
		uint32_t         m_SpotShadowAtlasGridSize = 1;
		uint32_t         m_SpotShadowTileSize = 2048;
		uint32_t         m_SpotShadowCount = 0;

		// ── Pre-depth pass ────────────────────────────────────────────────────
		Ref<Pipeline>    m_PreDepthPipeline;
		Ref<Material>    m_PreDepthMaterial;
		Ref<RenderPass>  m_PreDepthPass;
		// Mesh-shader variant (created only when VK_EXT_mesh_shader is available).
		Ref<Pipeline>    m_PreDepthMeshletPipeline;
		Ref<RenderPass>  m_PreDepthMeshletPass;

		// ── Tiled light culling ──────────────────────────────────────────────
		Ref<ComputePass> m_MeshCullingPass;
		Ref<ComputePass> m_ClusterBuildPass;        // builds m_SBSClusterAABBs
		Ref<ComputePass> m_ClusterLightCullingPass; // assigns lights to clusters
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
		Ref<RenderPass>  m_AODebugPass;
		Ref<Material>    m_AODebugMaterial;

		// Tracks the AO shader variant (__HZ_AO_METHOD) currently applied. Toggling
		// GTAO on recompiles AO-Composite/AO-Debug into a variant that newly declares
		// Camera/samplers/GTAO textures; the reload preserves inputs by name but can't
		// restore ones absent from the previous variant, so we must rebind + rebake
		// the full set once the new variant is live. See RebakeAOPassInputs().
		int  m_AppliedAOMethod = -1;
		int  m_AppliedGTAOBentNormals = -1;
		bool m_AOPassInputsDirty = false;

		// ── SSR ──────────────────────────────────────────────────────────────
		Ref<Image2D>     m_SSRImage;
		Ref<Image2D>     m_SSRFinalImage;
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

		// SMAA - compute passes writing storage images, matching the GTAO/SSR pattern:
		// edge detection -> blending weights -> neighbourhood blending (-> T2x resolve).
		Ref<ComputePass> m_SMAAEdgeComputePass;
		// Weight calculation and neighbourhood blending are one dispatch: the blending
		// weights stay in shared memory instead of a full-screen intermediate target.
		Ref<ComputePass> m_SMAAWeightAndBlendComputePass;
		Ref<Image2D>     m_SMAAEdgesImage;         // RG8, storage
		Ref<Image2D>     m_SMAAOutputImage;        // RGBA8, storage
		// Precomputed lookup tables from the SMAA reference implementation. Null until the
		// vendored headers are present, in which case no SMAA resources are created at all
		// and the renderer reports SMAA unavailable rather than producing garbage.
		Ref<Texture2D>   m_SMAAAreaTexture;
		Ref<Texture2D>   m_SMAASearchTexture;
		glm::uvec3       m_SMAAWorkGroups{ 1 };

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
		Ref<Pipeline>    m_TransparentGeometryPipeline; // transparent forward PBR
		Ref<Pipeline>    m_DeferredLightingPipeline;    // fullscreen deferred lighting
		Ref<RenderPass>  m_GeometryPass;                // opaque GBuffer
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

		// Per-frame scratch buffers reused across frames (cleared, capacity retained)
		// so FlushDrawList does not reallocate them every frame.
		std::vector<uint32_t>                          m_ScratchObjectIndexData;
		std::vector<uint32_t>                          m_ScratchVisibleObjectIndexData;
		std::vector<MeshCullDrawData>                  m_ScratchMeshCullDrawData;
		std::vector<nvrhi::DrawIndexedIndirectArguments> m_ScratchIndirectDrawData;
		std::vector<AssetHandle>                       m_ScratchTextureHandles;
		std::vector<GPUMaterialData>                   m_ScratchMaterialData;
		std::vector<GPUMaterialData>                   m_ScratchTransientMaterialData;

		// Render-thread-only scratch for RT_DrawStaticMesh push constants. RT_*
		// helpers execute serially inside render-command execution (only lambda
		// construction happens on the main thread), so no synchronization is needed.
		// Never touch this from the main thread.
		std::vector<uint8_t>                           m_RTPushConstantScratch;

		// Bindless texture resolve: slots waiting on streaming textures retry per
		// frame; everything else re-resolves only on table changes or the
		// periodic safety sweep (hot-reload coverage).
		std::vector<uint32_t> m_PendingTextureResolveSlots;
		std::vector<uint32_t> m_PendingTextureResolveScratch;
		uint32_t m_TextureResolveSweepCountdown = 0;
		uint32_t m_MissingTextureDescriptorCount = 0;

		// Dirty-range GPUScene uploads: each sync's dirty ranges replay once per
		// frame-in-flight buffer ("epochs"); full uploads run on scene switch /
		// instance-count growth (high-water tracked, which also covers buffer
		// resizes) and start above any realistic frames-in-flight count.
		struct GPUSceneRangeUploadEpoch
		{
			std::vector<GPUSceneDirtyRange> Ranges;
			uint32_t RemainingUploads = 0;
		};
		std::vector<GPUSceneRangeUploadEpoch> m_PendingGPUSceneRangeUploads;
		uint32_t    m_GPUSceneFullUploadsRemaining = 8;
		const void* m_LastGPUSceneKey = nullptr;
		uint32_t    m_LastGPUSceneInstanceCount = 0;
		uint32_t    m_GPUSceneMaxTotalInstancesSeen = 0;

		// Change tracking for the texture/material table scratches above: the copy
		// from the submitted scene is skipped when the same scene instance is
		// submitted with an unchanged version. Version sentinels start at max so
		// the first frame always copies.
		const void* m_ScratchTextureSceneKey = nullptr;
		uint64_t    m_ScratchTextureSceneVersion = std::numeric_limits<uint64_t>::max();
		uint32_t    m_ScratchPersistentTextureCount = 0;
		const void* m_ScratchMaterialSceneKey = nullptr;
		uint64_t    m_ScratchMaterialSceneVersion = std::numeric_limits<uint64_t>::max();

		// Shadow-specific transform tracking. Directional shadows are bucketed
		// per cascade; spot shadows keep one atlas-wide list because a caster can
		// affect any selected spot tile.
		struct ShadowTransformMapData
		{
			std::array<TransformMapData, ShadowCascadeCount> Cascades;
			TransformMapData Spot;
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
		glm::mat4 m_CurrentViewProjection = glm::mat4(1.0f); // unjittered
		glm::mat4 m_PreviousViewProjection = glm::mat4(1.0f); // unjittered
		glm::vec2 m_CurrentJitter = { 0.0f, 0.0f };  // clip-space sub-pixel offset this frame
		glm::vec2 m_PreviousJitter = { 0.0f, 0.0f };

		float m_LineWidth = 2.0f;
		float m_Opacity = 1.0f;
		float m_ScaleShadowCascadesToOrigin = 0.0f;
		float m_ShadowCascadeSplits[ShadowCascadeCount] = { 0.1f, 0.2f, 0.3f, 1.0f };
		bool  m_UseManualCascadeSplits = false;
		bool  m_ShadowCascadeCacheValid = false;
		bool  m_DirectionalShadowMapCacheValid = false;
		bool  m_DirectionalShadowMapNeedsRender = true;
		bool  m_StaticShadowMapCacheValid = false;
		bool  m_SpotShadowMapCacheValid = false;
		bool  m_SpotShadowMapNeedsRender = true;
		bool  m_StaticSpotShadowMapCacheValid = false;
		uint64_t m_LastStaticShadowCasterHash = 0;
		uint64_t m_LastDynamicShadowCasterHash = 0;
		uint64_t m_LastStaticSpotShadowCasterHash = 0;
		uint64_t m_LastDynamicSpotShadowCasterHash = 0;

		// Per-caster motion tracking (keyed by GPUScene row). A caster is static
		// once its transform is unchanged for StaticShadowCasterStableFrames
		// consecutive frames; transient instances are always dynamic.
		static constexpr uint32_t StaticShadowCasterStableFrames = 4;
		struct ShadowCasterMotionState
		{
			uint64_t TransformHash = 0;
			uint32_t StableFrames = 0;
			uint32_t LastTouchedFrame = 0;
		};
		std::unordered_map<uint32_t, ShadowCasterMotionState> m_ShadowCasterMotion;
		uint32_t m_ShadowMotionFrameIndex = 0;
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
		uint32_t m_CachedActiveShadowCascadeCount = 0;
		uint32_t m_CachedShadowMapResolution = 0;
		BloomSettings m_BloomSettings;
		DOFSettings m_DOFSettings;
		PostProcessSettings m_PostProcessSettings;
		SSROptionsUB m_SSROptions;

		SceneRendererOptions m_Options;
		Statistics           m_Statistics;
	};

} // namespace Lux
