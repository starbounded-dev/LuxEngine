// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

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
#include "Lux/Renderer/StorageImageClear.h"
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
#include "Lux/Renderer/DebugPalette.h"
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
		uint32_t  ShadowIndex = 0;   // first of six spot-atlas tiles (+X, -X, +Y, -Y, +Z, -Z); set by SceneRenderer
		float     Radius = 25.0f;    // range: the falloff window reaches zero here
		float     AtlasScale = 1.0f; // atlas tile size in UV; set by SceneRenderer
		float     LightSize = 0.5f;
		uint32_t  ShadowFlags = 0;   // PointLightShadowFlags
	};

	// PointLight::ShadowFlags; mirrors POINT_LIGHT_SHADOW_* in Buffers.glslh.
	enum PointLightShadowFlags : uint32_t
	{
		PointLightShadowCasts = 1u << 0,
		PointLightShadowSoft = 1u << 1,
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
		float     LightSize = 1.0f; // emitter diameter in metres (SpotLightComponent::Falloff), sets the PCSS penumbra
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
		// Tints collider debug items by their DebugCategoryPalette index (the acoustic material
		// view) with a translucent fill plus outline. Takes over the collider draw list while on.
		bool  ShowDebugCategories = false;
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
		// PCSS taps per pixel, 1..SceneRenderer::ShadowKernelSize. Set by the shadow quality tier.
		uint32_t ShadowBlockerSamples = 16;
		uint32_t ShadowFilterSamples = 24;
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
		// XeGTAO sampling: SliceCount * StepsPerSlice * 2 depth samples per pixel, and
		// the dominant cost of the GTAO pass. Driven by the AO quality tier - see
		// ApplyAmbientOcclusionQuality - using XeGTAO's own preset values.
		uint32_t GTAOSliceCount = 9;
		uint32_t GTAOStepsPerSlice = 3;
		// Visibility-bitmask GTAO: occluders are GTAOThickness metres deep instead of infinitely
		// thick, so thin objects darken less. Off = XeGTAO's horizon integral.
		bool  GTAOVisibilityBitmask = false;
		float GTAOThickness = 0.5f;
		// 1..3 sampling passes per pixel with shifted noise, averaged (spatial, no history).
		uint32_t GTAONoisePasses = 1;
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
		// Entries in RendererData.ShadowKernel, mirrored by SHADOW_KERNEL_SIZE in Buffers.glslh.
		static constexpr uint32_t ShadowKernelSize = 32;

		// The editor floor grid's look (Grid.glsl). Colours are display-space RGBA, since the grid
		// draws over the composited image; widths are in pixels.
		struct GridSettings
		{
			glm::vec4 XAxisColor = { 0.86f, 0.30f, 0.30f, 0.90f };
			glm::vec4 ZAxisColor = { 0.30f, 0.50f, 0.92f, 0.90f };
			glm::vec4 MinorColor = { 0.55f, 0.55f, 0.55f, 0.30f };
			glm::vec4 MajorColor = { 0.70f, 0.70f, 0.70f, 0.55f };
			float CellSize = 1.0f;       // finest cell, metres
			float FadeDistance = 150.0f; // the grid fades out by this distance from the camera
			float AxisLineWidth = 2.0f;
			float MinorLineWidth = 1.0f;
			float MajorLineWidth = 1.5f;
		};
		static constexpr uint32_t MaxSpotShadows = 16;
		// Point lights take six spot-atlas tiles each (one per cube face).
		static constexpr uint32_t MaxPointShadows = 8;
		// Tiles in the shared spot/point shadow atlas; mirrors SHADOW_ATLAS_MAX_TILES in Buffers.glslh.
		static constexpr uint32_t MaxShadowAtlasTiles = 64;
		static_assert(MaxSpotShadows + 6 * MaxPointShadows <= MaxShadowAtlasTiles);
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

			// The retained render scene: what this frame changed, and what is kept between frames.
			struct RetainedSceneStatistics
			{
				uint32_t Proxies = 0;
				uint32_t ChangedProxies = 0;      // created or changed in this frame's sync
				uint32_t RemovedProxies = 0;
				uint32_t CachedSubmitEntries = 0; // proxies whose submit data is cached and current
				uint32_t CullGroups = 0;
				uint32_t CullGroupInstances = 0;
				uint32_t ShadowCullGroups = 0;
				uint32_t StaticShadowCasters = 0;
				bool     GPUCullGroups = false;   // the main view and static shadows are culled on the GPU
				bool     CullGroupsKept = false;  // kept from last frame: no per-instance CPU work
				bool     ShadowCullRan = false;   // a static shadow cache re-rendered from the GPU cull
			} RetainedScene;

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
			TextureDimension Dimension = TextureDimension::Unknown;
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
			// The state the image is in between command buffers.
			ResourceState RestingState = ResourceState::Unknown;
			uint32_t FirstWriter = UINT32_MAX;
			uint32_t LastReader = UINT32_MAX;
			std::vector<uint32_t> Consumers;
			uint32_t DiagnosticCount = 0;
			uint32_t ErrorCount = 0;
			uint32_t WarningCount = 0;
		};

		struct RenderGraphBufferDebugInfo
		{
			uint32_t Resource = RenderGraph::InvalidResource;
			std::string Name;
			uint32_t FirstPass = UINT32_MAX;
			uint32_t LastPass = UINT32_MAX;
			uint32_t FirstWriter = UINT32_MAX;
			uint32_t LastReader = UINT32_MAX;
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
			std::vector<RenderGraphBufferDebugInfo> Buffers;
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
		// Main thread. Values are clamped to a usable range.
		void SetGridSettings(const GridSettings& settings);
		// Records a CPU stage of the frame (render-scene sync, submit, flush) as a pass profile, between
		// BeginScene and the end of EndScene. Stages enclose pass zones, so they are kept out of
		// Statistics::TotalCPUTime. Main thread.
		void RecordStageCPUProfile(const char* name, float milliseconds);
		const GridSettings& GetGridSettings() const { return m_GridSettings; }
		void RefreshRenderResolutionScale();
		void UpdateGTAOData();
		// Rebuilds RendererData.ShadowKernel when the PCSS sample counts change.
		void UpdateShadowKernel(uint32_t blockerSamples, uint32_t filterSamples);

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
		void SetEnvironment(Ref<Environment> environment, float intensity = 1.0f, float skyboxLod = 0.0f, float rotationDegrees = 0.0f);
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
			bool isSimpleCollider = true,
			int32_t debugCategory = -1);

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
			// A draw expanded from a GPU cull group (the GPU picks its instances and their LOD), kept
			// apart from the CPU-bucketed draw of the same mesh, submesh and LOD.
			bool        GPUCullGroup = false;
			// The material's raster variant (MaterialScene::ResolveRasterState), so each draw has one
			// pipeline: a two-sided material's depth-prepass and G-buffer draws take those passes'
			// no-cull pipelines (a two-sided transparent draws its back faces, then its front faces), and
			// a Cutout material's shadow draws the alpha-tested shadow ones.
			bool        TwoSided = false;
			bool        AlphaMasked = false;

			bool operator<(const MeshKey& o) const
			{
				if (MeshHandle != o.MeshHandle)     return MeshHandle < o.MeshHandle;
				if (SubmeshIndex != o.SubmeshIndex)   return SubmeshIndex < o.SubmeshIndex;
				if (LODIndex != o.LODIndex)       return LODIndex < o.LODIndex;
				if (MaterialHandle != o.MaterialHandle) return MaterialHandle < o.MaterialHandle;
				if (StaticShadowCaster != o.StaticShadowCaster) return StaticShadowCaster < o.StaticShadowCaster;
				if (IsSelected != o.IsSelected) return IsSelected < o.IsSelected;
				if (GPUCullGroup != o.GPUCullGroup) return GPUCullGroup < o.GPUCullGroup;
				if (TwoSided != o.TwoSided) return TwoSided < o.TwoSided;
				return AlphaMasked < o.AlphaMasked;
			}

			bool operator==(const MeshKey& o) const
			{
				return MeshHandle == o.MeshHandle
					&& MaterialHandle == o.MaterialHandle
					&& SubmeshIndex == o.SubmeshIndex
					&& LODIndex == o.LODIndex
					&& IsSelected == o.IsSelected
					&& StaticShadowCaster == o.StaticShadowCaster
					&& GPUCullGroup == o.GPUCullGroup
					&& TwoSided == o.TwoSided
					&& AlphaMasked == o.AlphaMasked;
			}
		};

		struct MeshKeyHasher
		{
			size_t operator()(const MeshKey& key) const
			{
				// Multiply-xorshift mixing: this runs several times per submitted instance, and MSVC's
				// std::hash<uint64_t> is byte-wise FNV-1a. Draw order never depends on map order
				// (BuildSortedDrawCommandOrder sorts on every key field), so the hash is free to change.
				auto mix = [](uint64_t value)
					{
						value ^= value >> 32;
						value *= 0xd6e8feb86659fd93ull;
						value ^= value >> 32;
						value *= 0xd6e8feb86659fd93ull;
						value ^= value >> 32;
						return value;
					};

				uint64_t hash = mix((uint64_t)key.MeshHandle);
				hash = mix(hash ^ (uint64_t)key.MaterialHandle);
				hash = mix(hash ^ ((uint64_t)key.SubmeshIndex | ((uint64_t)key.LODIndex << 32)));
				hash = mix(hash ^ ((uint64_t)key.IsSelected | ((uint64_t)key.StaticShadowCaster << 1) | ((uint64_t)key.GPUCullGroup << 2)
					| ((uint64_t)key.TwoSided << 3) | ((uint64_t)key.AlphaMasked << 4)));
				return (size_t)hash;
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
			// Cull-group draws only: the early cull's instances (in ObjectIndexes) and indirect args,
			// which the depth prepass draws.
			uint32_t              EarlyObjectIndexBase = 0;
			uint32_t              EarlyIndirectDrawOffsetBytes = std::numeric_limits<uint32_t>::max();
			// This bucket's draw in each pass, set by its first instance: later instances only count.
			// Valid for the frame (the draw lists and this map are cleared together).
			std::array<StaticDrawCommand*, MeshPassTypeCount> PassCommands{};
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

			// The depth prepass's view of a cull-group draw: the early cull's instances and args.
			static MeshDrawParams EarlyCull(const TransformMapData& tmd)
			{
				MeshDrawParams params;
				params.ObjectIndexBase = tmd.EarlyObjectIndexBase;
				params.IndirectDrawOffsetBytes = tmd.EarlyIndirectDrawOffsetBytes;
				return params;
			}
		};

		// One workgroup of MeshCulling.glsl. Mirrors the three uvec4 its MeshCullDrawData block reads per
		// draw: edit both in the same change.
		struct MeshCullDrawData
		{
			uint32_t ObjectIndexBase = 0;
			uint32_t InstanceCount = 0;
			uint32_t VisibleObjectIndexBase = 0; // LOD l's visible instances at + l * InstanceCount
			uint32_t IndirectArgsBase = 0;       // LOD l's args at + l; the early cull's at + LOD count + l
			uint32_t LODCountAndFlags = 1;       // low byte LOD count, MeshCullEarlyDrawFlag when the early cull draws it
			float    LODDistanceMultipliers[3] = {}; // SubmeshLOD::DistanceMultiplier of LOD 1..3
			uint32_t EarlyObjectIndexBase = 0;   // the early cull writes LOD l's instances at + l * InstanceCount
			uint32_t Padding[3] = {};
		};
		static_assert(sizeof(MeshCullDrawData) == 48, "MeshCullDrawData is three uvec4 in MeshCulling.glsl");
		static constexpr uint32_t MeshCullEarlyDrawFlag = 0x100u;
		static constexpr uint32_t MaxCullGroupLODs = 4;

		// A main-view GPU cull group: every proxy instance of one mesh submesh (and, when transparent,
		// one material key). MeshCulling.glsl culls the instances and sorts them into one draw per LOD,
		// so no per-instance visibility or LOD work runs on the CPU for the main view. Groups persist
		// across frames and are rebuilt only when the submitted scene's static meshes change.
		struct CullGroup
		{
			MeshKey Key{};                // LODIndex 0; the per-LOD draws differ only in LODIndex
			Ref<StaticMesh> StaticMesh;
			Ref<MeshSource> MeshSource;
			Ref<MaterialTable> MaterialTable; // transparent draws only
			AssetHandle MaterialHandle = 0;   // transparent draws only
			Ref<Material> OverrideMaterial;   // transparent draws only
			uint64_t ShaderSortKey = 0;
			uint64_t MaterialSortKey = 0;
			uint64_t MeshSortKey = 0;
			uint32_t SubmeshIndex = 0;
			bool Transparent = false;
			std::vector<uint32_t> ObjectIndices; // persistent GPUScene rows
			// This frame's layout, set by FlushDrawList.
			uint32_t LODCount = 1;
			uint32_t ObjectIndexBase = 0;
			uint32_t VisibleObjectIndexBase = 0;
			uint32_t EarlyObjectIndexBase = 0;
		};

		// A shadow cull group: every static shadow caster (unmoved for StaticShadowCasterStableFrames) among
		// the cull-group instances of one mesh submesh. ShadowCulling.glsl culls them per cascade and for the
		// spot atlas and sorts them into one draw per (target, LOD) for the static shadow caches. A caster
		// still moving takes the CPU shadow path: its lists are exact, and it is re-synced anyway.
		// Built and kept with the main cull groups.
		struct ShadowCullGroup
		{
			MeshKey Key{}; // LODIndex 0, StaticShadowCaster true; the draws set LODIndex
			Ref<StaticMesh> StaticMesh;
			Ref<MeshSource> MeshSource;
			uint64_t MeshSortKey = 0;
			uint32_t SubmeshIndex = 0;
			std::vector<uint32_t> ObjectIndices;
			// This frame's layout, set by FlushDrawList.
			uint32_t LODCount = 1;
			uint32_t ObjectIndexBase = 0;
			uint32_t OutputBase = 0;
			uint32_t IndirectArgsBase = 0;
		};
		static constexpr uint32_t ShadowCullTargetCount = 5; // the cascades, then the spot atlas (ShadowCulling.glsl)
		static_assert(ShadowCascadeCount == 4, "ShadowCulling.glsl has four cascade targets");

		// Internal helper for the debug-mesh submission path.
		void SubmitStaticDebugMesh(MeshPassType passType,
			Ref<StaticMesh>  staticMesh,
			Ref<MeshSource>  meshSource,
			const glm::mat4& transform,
			Ref<Material>    material);

		// Camera-independent submit data of one submesh: which material it draws with, how it sorts,
		// whether it is transparent or casts shadows. A proxy's is cached (m_ProxySubmitCache).
		struct SubmeshSubmitData
		{
			AssetHandle MaterialHandle = 0;
			Ref<MaterialAsset> MaterialAsset;
			Ref<Material> OverrideMaterial;      // the explicit override, or the default material when the asset is missing
			Ref<Material> MaterialSceneOverride; // what a transient material row is built from
			AssetHandle MeshKeyHandle = 0;
			AssetHandle KeyMaterialHandle = 0;
			uint64_t MeshSortKey = 0;
			uint64_t MaterialSortKey = 0;
			uint64_t ShaderSortKey = 0;
			bool Transparent = false;
			bool CastsShadows = false;
			// MaterialScene::ResolveRasterState
			bool TwoSided = false;
			bool AlphaMasked = false;
		};

		// A proxy's SubmeshSubmitData, parallel to its SubmeshInstances. Valid while the proxy's
		// Revision and m_ProxySubmitCacheEpoch are unchanged.
		struct ProxySubmitCacheEntry
		{
			uint64_t Epoch = 0;
			uint64_t ProxyRevision = 0;
			UUID EntityID = 0;
			std::vector<SubmeshSubmitData> Submeshes;
		};

		// Sort keys of the fixed per-pass materials and pipelines, read once per frame instead of
		// once per submesh.
		struct PassSortKeys
		{
			uint32_t Frame = std::numeric_limits<uint32_t>::max();
			uint64_t GeometryPipeline = 0;
			uint64_t GeometryTwoSidedPipeline = 0;
			uint64_t TransparentGeometryPipeline = 0;
			uint64_t PreDepthPipeline = 0;
			uint64_t PreDepthTwoSidedPipeline = 0;
			uint64_t PreDepthShader = 0;
			uint64_t PreDepthMaterial = 0;
			uint64_t SelectedPipeline = 0;
			uint64_t SelectedShader = 0;
			uint64_t SelectedMaterial = 0;
			uint64_t WireframePipeline = 0;
			uint64_t WireframeShader = 0;
			uint64_t WireframeMaterial = 0;
			uint64_t ShadowPipeline = 0;
			uint64_t ShadowMaskedPipeline = 0;
			uint64_t ShadowShader = 0;
			uint64_t ShadowMaterial = 0;
		};

		void SubmitStaticMeshProxy(const StaticMeshRenderProxy& proxy);
		void SubmitStaticMeshInternal(const Ref<StaticMesh>& staticMesh,
			const Ref<MeshSource>& meshSource,
			const Ref<MaterialTable>& materialTable,
			const glm::mat4& transform,
			const Ref<Material>& overrideMaterial,
			bool               isSelected,
			const StaticMeshRenderProxy* renderProxy = nullptr);
		static SubmeshSubmitData BuildSubmeshSubmitData(const Ref<StaticMesh>& staticMesh,
			const Ref<MeshSource>& meshSource,
			const Ref<MaterialTable>& materialTable,
			const Ref<Material>& overrideMaterial,
			uint32_t submeshIndex);
		// The proxy's cached submit data, rebuilt when the proxy or any material input changed.
		const ProxySubmitCacheEntry& GetProxySubmitData(const StaticMeshRenderProxy& proxy);
		const PassSortKeys& GetPassSortKeys();
		TransformMapData& GetMeshTransformBucket(const MeshKey& key);
		// True when the main view is culled and LOD-selected on the GPU through cull groups this frame:
		// GPU-driven rendering on and the mesh-shader prepass (which needs CPU LOD buckets) not in use.
		bool CanUseGPUCullGroups() const;
		// staticCaster: the instance has not moved for StaticShadowCasterStableFrames; only such a caster
		// joins a shadow cull group.
		void AddToCullGroup(const SubmeshSubmitData& data, const Ref<StaticMesh>& staticMesh, const Ref<MeshSource>& meshSource,
			const Ref<MaterialTable>& materialTable, uint32_t submeshIndex, uint32_t sceneInstanceIndex, bool staticCaster);
		void ClearCullGroups();
		// Adds every cull group's per-LOD draws to the pass draw lists (FlushDrawList, before sorting).
		void ExpandCullGroupDraws();
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
		GPUTextureIndex ResolveTransientGPUTextureIndex(const GPUTextureSlot& slot);
		StaticDrawCommand& SubmitMeshPassDraw(MeshPassType passType,
			const MeshKey& key,
			const Ref<StaticMesh>& staticMesh,
			const Ref<MeshSource>& meshSource,
			const Ref<MaterialTable>& materialTable,
			uint32_t submeshIndex,
			uint32_t lodIndex,
			AssetHandle materialHandle,
			const Ref<Material>& overrideMaterial,
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
		void EarlyMeshCullingPass();
		void ShadowMeshCullingPass();
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
			float EnvironmentRotation = 0.0f; // degrees about +Y
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
			std::vector<DrawIndexedIndirectCommand>& drawCommands);

		// Times a stage for RecordStageCPUProfile.
		struct ScopedStageProfile
		{
			ScopedStageProfile(SceneRenderer& renderer, const char* name) : Renderer(renderer), Name(name) {}
			~ScopedStageProfile() { Renderer.RecordStageCPUProfile(Name, ProfileTimer.ElapsedMillis()); }

			SceneRenderer& Renderer;
			const char* Name = "";
			Timer ProfileTimer;
		};

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
			// Non-zero for an orthographic projection; DepthUnpackConsts is then affine.
			int32_t   IsOrthographic = 0;
			int32_t   Padding[3]{};
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
			float       EnvironmentMapRotation = 0.0f; // degrees about +Y
			glm::vec3   Padding{};
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
			glm::mat4 ViewProjection[MaxShadowAtlasTiles];
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
			uint32_t  ShadowBlockerSampleCount = 16;
			glm::vec4 ShadowFilterParams = { 1.25f, 1.5f, 0.0f, 0.0f }; // x=dir PCF texels, y=spot PCF texels
			uint32_t  ShadowFilterSampleCount = 24;
			uint32_t  ShadowKernelPadding[3] = { 0, 0, 0 };
			// xy = filter tap i, zw = blocker-search tap i; each a Vogel disk over its own count.
			glm::vec4 ShadowKernel[ShadowKernelSize] = {};
		} m_RendererDataUB;
		static_assert(sizeof(UBRendererData) == 656, "UBRendererData must match the GLSL std140 RendererData block.");

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
			float Thickness = 0.5f;
			uint32_t VisibilityBitmask = 0;
			uint32_t NoisePassCount = 1;
			glm::vec2 Padding = { 0.0f, 0.0f };
		} m_GTAODataCB;
		static_assert(sizeof(CBGTAOData) == 88, "CBGTAOData must match GTAOConstants in GTAO.hlsl.");

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
			float               EnvironmentRotation = 0.0f; // degrees about +Y
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
		// Never begun: alpha-masked casters (MeshKey::AlphaMasked) switch to its alpha-tested pipeline
		// and sets inside whichever cascade pass is open.
		Ref<RenderPass>  m_ShadowMapMaskedPass;

		// ── Spot shadow atlas ───────────────────────────────────────────────
		Ref<Image2D>     m_SpotShadowMapImage;
		Ref<RenderPass>  m_SpotShadowMapPass;
		Ref<Image2D>     m_SpotShadowStaticCacheImage;
		Ref<RenderPass>  m_SpotShadowStaticCachePass;
		Ref<RenderPass>  m_SpotShadowDynamicPass;
		// The spot atlas's m_ShadowMapMaskedPass.
		Ref<RenderPass>  m_SpotShadowMaskedPass;
		Ref<Material>    m_SpotShadowPassMaterial;
		uint32_t         m_SpotShadowMapSize = 2048;
		uint32_t         m_SpotShadowAtlasGridSize = 1;
		uint32_t         m_SpotShadowTileSize = 2048;
		uint32_t         m_SpotShadowCount = 0;      // atlas tiles in use: spot lights + 6 per point light
		uint32_t         m_SpotShadowLightCount = 0; // spot lights with shadows

		// ── Pre-depth pass ────────────────────────────────────────────────────
		Ref<Pipeline>    m_PreDepthPipeline;
		// Without back-face culling, for two-sided materials' draws (MeshKey::TwoSided).
		Ref<Pipeline>    m_PreDepthTwoSidedPipeline;
		Ref<Material>    m_PreDepthMaterial;
		Ref<RenderPass>  m_PreDepthPass;
		// Mesh-shader variant (created only when VK_EXT_mesh_shader is available).
		Ref<Pipeline>    m_PreDepthMeshletPipeline;
		Ref<Pipeline>    m_PreDepthMeshletTwoSidedPipeline; // without back-face culling (MeshKey::TwoSided)
		Ref<RenderPass>  m_PreDepthMeshletPass;

		// ── Tiled light culling ──────────────────────────────────────────────
		Ref<ComputePass> m_MeshCullingPass;
		// The same shader in early mode: frustum and LOD only, before the depth prepass, writing into
		// ObjectIndexes (its VisibleObjectIndexes binding is the ObjectIndexes buffer).
		Ref<ComputePass> m_MeshEarlyCullingPass;
		// ShadowCulling.glsl: per cascade and for the spot atlas, before the shadow passes; its output binding
		// is the ObjectIndexes buffer too.
		Ref<ComputePass> m_ShadowCullingPass;
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
		// Mip 0 of the visibility texture, cleared to fully visible every frame; rebuilt with the
		// texture's views.
		Ref<StorageImageClear> m_PreIntegrationVisibilityClear;
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
		Ref<Pipeline>    m_GeometryTwoSidedPipeline;    // opaque GBuffer without culling (MeshKey::TwoSided)
		Ref<Pipeline>    m_TransparentGeometryPipeline; // transparent forward PBR
		Ref<Pipeline>    m_TransparentBackFacesPipeline; // the same, back faces only: a two-sided transparent's first draw
		Ref<Pipeline>    m_DeferredLightingPipeline;    // fullscreen deferred lighting
		Ref<RenderPass>  m_GeometryPass;                // opaque GBuffer
		Ref<RenderPass>  m_GeometryPassTransparent;     // transparent forward
		Ref<RenderPass>  m_DeferredLightingPass;        // fullscreen deferred lighting
		Ref<Material>    m_DeferredLightingMaterial;
		Ref<RenderPass>  m_GBufferDebugPass;
		Ref<Material>    m_GBufferDebugMaterial;
		std::vector<Ref<Texture2D>> m_GPUMaterialTextures;
		Ref<BindlessTextureTable> m_BindlessTextures;
		uint32_t m_ResolvedTextureSlotCount = 0;

		// ── Selected / wireframe ──────────────────────────────────────────────
		Ref<RenderPass>  m_SelectedGeometryPass;
		Ref<Material>    m_SelectedGeometryMaterial;
		Ref<RenderPass>  m_GeometryWireframePass;
		Ref<RenderPass>  m_PhysicsColliderPass;
		Ref<RenderPass>  m_DebugCategoryFillPass;
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
		GridSettings     m_GridSettings;

		// ── Physics collider debug ────────────────────────────────────────────
		Ref<Material>    m_SimpleColliderMaterial;
		Ref<Material>    m_ComplexColliderMaterial;
		std::array<Ref<Material>, DebugCategoryPalette.size()> m_DebugCategoryMaterials;

		// ── Mesh passes ───────────────────────────────────────────────────────
		std::array<MeshPassState, MeshPassTypeCount> m_MeshPasses;
		std::unordered_map<MeshDrawCommandCacheKey, CachedStaticDrawCommand, MeshDrawCommandCacheKeyHasher> m_MeshDrawCommandCache;
		uint32_t m_MeshDrawCommandCacheFrame = 0;

		// GPUScene indirection for all submitted meshes this frame.
		std::unordered_map<MeshKey, TransformMapData, MeshKeyHasher>  m_MeshTransformMap;
		// ObjectIndices vectors of last frame's buckets, handed to this frame's new buckets so the
		// per-frame maps do not regrow their vectors from empty every frame.
		std::vector<std::vector<uint32_t>> m_ObjectIndexVectorPool;

		// Submit data per proxy, indexed by RenderPrimitiveID, for m_ProxySubmitCacheScene. The epoch
		// moves (dropping every entry) when the scene, any MaterialTable or any material asset changes.
		std::vector<ProxySubmitCacheEntry> m_ProxySubmitCache;
		const RenderScene* m_ProxySubmitCacheScene = nullptr;
		uint64_t m_ProxySubmitCacheEpoch = 1;
		uint64_t m_ProxySubmitCacheSceneGeneration = 0;
		uint64_t m_ProxySubmitCacheTableRevision = 0;
		uint64_t m_ProxySubmitCacheMaterialRevision = 0;
		PassSortKeys m_PassSortKeys;

		// Main-view cull groups (CanUseGPUCullGroups). Rebuilt during a submit only when the scene's
		// static meshes changed since the frame they were built for; otherwise kept ("frozen").
		std::unordered_map<MeshKey, CullGroup, MeshKeyHasher> m_CullGroups;
		bool m_UseGPUCullGroups = false;        // this frame (BeginScene)
		bool m_CullGroupsFrozen = false;        // this frame keeps last frame's groups
		bool m_CullGroupsSubmitted = false;     // a render scene was submitted this frame
		bool m_CullGroupsValid = false;
		const RenderScene* m_CullGroupsScene = nullptr;
		uint64_t m_CullGroupsSceneGeneration = 0;
		uint32_t m_CullGroupsSceneFrame = 0;
		uint64_t m_CullGroupsSubmitEpoch = 0;
		uint32_t m_EarlyCullDrawCount = 0;

		// Shadow cull groups, rebuilt and kept with m_CullGroups. Static casters change set when a dynamic one
		// settles, so the groups are also rebuilt at m_NextStaticCasterFrame (a RenderScene frame index),
		// which every submit recomputes from the casters still moving or settling.
		std::unordered_map<MeshKey, ShadowCullGroup, MeshKeyHasher> m_ShadowCullGroups;
		uint32_t m_NextStaticCasterFrame = std::numeric_limits<uint32_t>::max();
		// By RenderPrimitiveID: 1 when the proxy is in a shadow cull group. Moving one of these changes the
		// groups, so it rebuilds them; moving any other proxy keeps them.
		std::vector<uint8_t> m_ShadowCullGroupPrimitives;
		// Order-independent hash of the static casters: the static shadow caches re-render when it moves.
		uint64_t m_ShadowStaticCasterSetHash = 0;
		uint64_t m_LastShadowStaticCasterSetHash = 0;
		// This frame's shadow cull dispatch: its records and the frustum planes in m_SBSMeshCullDrawData.
		uint32_t m_ShadowCullDrawCount = 0;
		bool m_ShadowCullDispatched = false; // statistics: the shadow cull ran this frame
		uint32_t m_ShadowCullRecordBase = 0;
		uint32_t m_ShadowCullFrustumBase = 0;
		// What the GPU shadow cull's LOD and tiny-caster tests read: the static caches re-render when it changes.
		struct ShadowCullCameraState
		{
			glm::vec3 CameraPosition{ 0.0f };
			float TinyCasterPixelsPerRadian = 0.0f;
			float LODDistanceScale = 0.0f;
			bool LODEnabled = false;
			bool ShadowCullingEnabled = false;

			bool operator==(const ShadowCullCameraState& other) const = default;
		};
		ShadowCullCameraState GetShadowCullCameraState() const;
		ShadowCullCameraState m_ShadowCullCameraState;
		bool m_ShadowCullCameraStateValid = false;
		std::vector<glm::vec4> m_ScratchShadowCullPlanes;
		std::vector<GPUSceneInstanceData>    m_TransientGPUSceneInstances;
		std::vector<GPUMaterialData>         m_TransientGPUMaterials;
		std::unordered_map<uint64_t, uint32_t> m_TransientGPUMaterialIndexByKey;
		std::vector<GPUTextureSlot> m_TransientGPUTextureSlots;
		std::unordered_map<GPUTextureSlot, GPUTextureIndex, GPUTextureSlotHasher> m_TransientGPUTextureIndexBySlot;
		GPUTextureIndex m_NextTransientGPUTextureIndex = 0;

		// Per-frame scratch buffers reused across frames (cleared, capacity retained)
		// so FlushDrawList does not reallocate them every frame.
		std::vector<uint32_t>                          m_ScratchObjectIndexData;
		std::vector<uint32_t>                          m_ScratchVisibleObjectIndexData;
		std::vector<MeshCullDrawData>                  m_ScratchMeshCullDrawData;
		std::vector<DrawIndexedIndirectCommand> m_ScratchIndirectDrawData;
		std::vector<GPUTextureSlot>                    m_ScratchTextureSlots;
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
			StaticDrawCommand* ShadowCommand = nullptr; // as TransformMapData::PassCommands
		};
		std::unordered_map<MeshKey, ShadowTransformMapData, MeshKeyHasher> m_ShadowMeshTransformMap;
		ShadowTransformMapData& GetShadowTransformBucket(const MeshKey& key);
		// Whether a shadow bucket may draw in a target: a cull group's lists only a candidate count.
		static bool HasShadowCasterCandidates(const MeshKey& key, const TransformMapData& tmd);
		std::array<Frustum, ShadowCascadeCount> m_ShadowCascadeFrustums;
		std::array<Frustum, MaxShadowAtlasTiles> m_SpotShadowFrustums;
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
		// Where the current static caster depth lives. With no dynamic casters the static casters render
		// straight into the shadow map and the cache image goes stale; it is filled from the map the first
		// frame dynamic casters need it. HoldsStaticOnly: the shadow map has no dynamic depth in it.
		bool  m_StaticShadowCacheImageStale = false;
		bool  m_ShadowMapHoldsStaticOnly = false;
		bool  m_SpotShadowMapCacheValid = false;
		bool  m_SpotShadowMapNeedsRender = true;
		bool  m_StaticSpotShadowMapCacheValid = false;
		bool  m_StaticSpotShadowCacheImageStale = false;
		bool  m_SpotShadowMapHoldsStaticOnly = false;
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
			glm::vec4 TransformRows[3]{};
			uint32_t StableFrames = 0;
			uint32_t LastTouchedFrame = 0;
			bool Tracked = false;
		};
		// Indexed by GPUScene row (dense), so tracking a caster is an array access, not a hash lookup.
		std::vector<ShadowCasterMotionState> m_ShadowCasterMotion;
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
