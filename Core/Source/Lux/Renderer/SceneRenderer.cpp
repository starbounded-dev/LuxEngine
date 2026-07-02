#include "lpch.h"
#include "SceneRenderer.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/RenderScene.h"
#include "Lux/Renderer/Exposure.h"
#include "Lux/Core/Application.h"
#include "Lux/Core/Math/Frustum.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Lux {

	// Push-constant layout that every mesh-draw shader in this engine expects.
	// Must match the push_constant block declared in HazelPBR_Static, PreDepth,
	// DirShadowMap, SelectedGeometry, and Wireframe shaders.
	struct MeshDrawPushConstants
	{
		uint32_t ObjectIndexBase = 0; // first index into ObjectIndexes SSBO
		uint32_t LightIndex = 0; // shadow cascade index (0 = single cascade)
		uint32_t BoneTransformBase = 0; // unused (no animation)
		uint32_t BoneTransformStride = 0; // unused
	};

	namespace
	{
		uint64_t HashCombine(uint64_t seed, uint64_t value)
		{
			return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
		}

		uint64_t HashFloat(float value)
		{
			uint32_t bits = 0;
			std::memcpy(&bits, &value, sizeof(float));
			return bits;
		}

		uint64_t HashVec3(uint64_t seed, const glm::vec3& value)
		{
			seed = HashCombine(seed, HashFloat(value.x));
			seed = HashCombine(seed, HashFloat(value.y));
			seed = HashCombine(seed, HashFloat(value.z));
			return seed;
		}

		uint64_t HashVec4(uint64_t seed, const glm::vec4& value)
		{
			seed = HashCombine(seed, HashFloat(value.x));
			seed = HashCombine(seed, HashFloat(value.y));
			seed = HashCombine(seed, HashFloat(value.z));
			seed = HashCombine(seed, HashFloat(value.w));
			return seed;
		}

		uint64_t SortKeyFromRef(const void* object)
		{
			return reinterpret_cast<uint64_t>(object);
		}

		constexpr uint32_t TransientGPUSceneInstanceFlag = 0x80000000u;
		constexpr uint32_t TransientGPUSceneInstanceMask = ~TransientGPUSceneInstanceFlag;
		constexpr uint32_t TransientRenderMaterialFlag = 0x80000000u;
		constexpr uint32_t TransientRenderMaterialMask = ~TransientRenderMaterialFlag;
		constexpr uint32_t TransientGPUTextureFlag = 0x80000000u;
		constexpr uint32_t TransientGPUTextureMask = ~TransientGPUTextureFlag;
		constexpr uint32_t MaxGPUTextureSceneTextures = 1024;

		uint32_t EncodeTransientGPUSceneInstanceIndex(uint32_t index)
		{
			LUX_CORE_ASSERT((index & TransientGPUSceneInstanceFlag) == 0, "Transient GPUScene instance index overflow");
			return TransientGPUSceneInstanceFlag | index;
		}

		bool IsTransientGPUSceneInstanceIndex(uint32_t index)
		{
			return (index & TransientGPUSceneInstanceFlag) != 0;
		}

		uint32_t DecodeTransientGPUSceneInstanceIndex(uint32_t index)
		{
			return index & TransientGPUSceneInstanceMask;
		}

		RenderMaterialID EncodeTransientRenderMaterialIndex(uint32_t index)
		{
			LUX_CORE_ASSERT((index & TransientRenderMaterialFlag) == 0, "Transient material index overflow");
			return TransientRenderMaterialFlag | index;
		}

		bool IsTransientRenderMaterialID(RenderMaterialID materialID)
		{
			return (materialID & TransientRenderMaterialFlag) != 0;
		}

		uint32_t DecodeTransientRenderMaterialIndex(RenderMaterialID materialID)
		{
			return materialID & TransientRenderMaterialMask;
		}

		GPUTextureIndex EncodeTransientGPUTextureIndex(uint32_t index)
		{
			LUX_CORE_ASSERT((index & TransientGPUTextureFlag) == 0, "Transient GPU texture index overflow");
			return TransientGPUTextureFlag | index;
		}

		bool IsTransientGPUTextureIndex(GPUTextureIndex textureIndex)
		{
			return (textureIndex & TransientGPUTextureFlag) != 0;
		}

		uint32_t DecodeTransientGPUTextureIndex(GPUTextureIndex textureIndex)
		{
			return textureIndex & TransientGPUTextureMask;
		}

		void WriteGPUSceneTransformRows(glm::vec4 (&rows)[3], const glm::mat4& transform)
		{
			rows[0] = { transform[0][0], transform[1][0], transform[2][0], transform[3][0] };
			rows[1] = { transform[0][1], transform[1][1], transform[2][1], transform[3][1] };
			rows[2] = { transform[0][2], transform[1][2], transform[2][2], transform[3][2] };
		}

		GPUSceneInstanceData BuildTransientGPUSceneInstanceData(const glm::mat4& transform, const glm::vec4& boundsSphere, uint32_t submeshIndex, RenderMaterialID materialID)
		{
			GPUSceneInstanceData data;
			WriteGPUSceneTransformRows(data.TransformRows, transform);
			WriteGPUSceneTransformRows(data.PreviousTransformRows, transform);
			data.BoundsSphere = boundsSphere;
			data.Metadata = glm::uvec4(InvalidRenderPrimitiveID, submeshIndex, materialID, (uint32_t)GPUSceneInstanceFlags::Visible);
			return data;
		}

		bool IsFiniteVec4(const glm::vec4& value)
		{
			return std::isfinite(value.x)
				&& std::isfinite(value.y)
				&& std::isfinite(value.z)
				&& std::isfinite(value.w);
		}

		bool IsFiniteTransformRows(const glm::vec4 (&rows)[3])
		{
			return IsFiniteVec4(rows[0]) && IsFiniteVec4(rows[1]) && IsFiniteVec4(rows[2]);
		}

		uint32_t ResolveGPUSceneDebugMode(SceneRenderer::DebugViewMode mode)
		{
			switch (mode)
			{
				case SceneRenderer::DebugViewMode::GPUScenePrimitiveID: return 1;
				case SceneRenderer::DebugViewMode::GPUSceneMaterialIndex: return 2;
				case SceneRenderer::DebugViewMode::GPUSceneObjectID: return 3;
				case SceneRenderer::DebugViewMode::GPUSceneBounds: return 4;
				case SceneRenderer::DebugViewMode::GPUSceneMotion: return 5;
				case SceneRenderer::DebugViewMode::GPUMaterialTextureValidity: return 6;
				case SceneRenderer::DebugViewMode::GPUMaterialAlphaMode: return 7;
				case SceneRenderer::DebugViewMode::GPUMaterialRoughness: return 8;
				case SceneRenderer::DebugViewMode::GPUMaterialMetalness: return 9;
				case SceneRenderer::DebugViewMode::GPUMaterialMissing: return 10;
				default: return 0;
			}
		}

		uint32_t ResolveGBufferDebugMode(SceneRenderer::DebugViewMode mode)
		{
			switch (mode)
			{
				case SceneRenderer::DebugViewMode::GBufferBaseColor: return 1;
				case SceneRenderer::DebugViewMode::GBufferNormal: return 2;
				case SceneRenderer::DebugViewMode::GBufferMetalRough: return 3;
				case SceneRenderer::DebugViewMode::GBufferMaterialID:
				case SceneRenderer::DebugViewMode::GPUSceneMaterialIndex: return 4;
				case SceneRenderer::DebugViewMode::GBufferObjectID:
				case SceneRenderer::DebugViewMode::GPUScenePrimitiveID:
				case SceneRenderer::DebugViewMode::GPUSceneObjectID: return 5;
				case SceneRenderer::DebugViewMode::DeferredLighting: return 6;
				case SceneRenderer::DebugViewMode::GPUMaterialTextureValidity: return 7;
				case SceneRenderer::DebugViewMode::GPUMaterialAlphaMode: return 8;
				case SceneRenderer::DebugViewMode::GPUMaterialRoughness: return 9;
				case SceneRenderer::DebugViewMode::GPUMaterialMetalness: return 10;
				case SceneRenderer::DebugViewMode::GPUMaterialMissing: return 11;
				default: return 0;
			}
		}

		bool UsesGBufferDebugPass(SceneRenderer::DebugViewMode mode)
		{
			return ResolveGBufferDebugMode(mode) != 0;
		}

		glm::vec3 NormalizeOrFallback(const glm::vec3& value, const glm::vec3& fallback)
		{
			const float lengthSquared = glm::dot(value, value);
			if (lengthSquared <= 0.000001f)
				return fallback;

			return value * glm::inversesqrt(lengthSquared);
		}

		Ref<TextureCube> GetEnvironmentRadianceMap(const Ref<Environment>& environment)
		{
			return environment && environment->RadianceMap ? environment->RadianceMap : Renderer::GetBlackCubeTexture();
		}

		Ref<TextureCube> GetEnvironmentIrradianceMap(const Ref<Environment>& environment)
		{
			return environment && environment->IrradianceMap ? environment->IrradianceMap : Renderer::GetBlackCubeTexture();
		}

		AssetHandle GetStaticMeshKeyHandle(const Ref<StaticMesh>& staticMesh)
		{
			if (!staticMesh)
				return 0;

			return staticMesh->Handle ? staticMesh->Handle : staticMesh->GetMeshSource();
		}

		glm::vec4 CalculateWorldBoundsSphere(const AABB& localBounds, const glm::mat4& transform)
		{
			const BoundingSphere localSphere = localBounds.ToBoundingSphere();
			const glm::vec3 worldCenter = glm::vec3(transform * glm::vec4(localSphere.Center, 1.0f));
			const float maxScale = glm::max(
				glm::length(glm::vec3(transform[0])),
				glm::max(glm::length(glm::vec3(transform[1])), glm::length(glm::vec3(transform[2]))));

			return { worldCenter, localSphere.Radius * maxScale };
		}

		uint32_t AlignUp(uint32_t value, uint32_t alignment)
		{
			if (alignment == 0)
				return value;

			return ((value + alignment - 1u) / alignment) * alignment;
		}

		uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
		{
			return divisor == 0 ? value : (value + divisor - 1u) / divisor;
		}

		glm::uvec2 DivideRoundUp(const glm::uvec2& value, uint32_t divisor)
		{
			return { DivideRoundUp(value.x, divisor), DivideRoundUp(value.y, divisor) };
		}

		SceneRendererOptions::RenderResolutionScaleMode SanitizeRenderResolutionScaleMode(uint32_t mode)
		{
			if (mode > static_cast<uint32_t>(SceneRendererOptions::RenderResolutionScaleMode::Dynamic))
				return SceneRendererOptions::RenderResolutionScaleMode::Native;

			return static_cast<SceneRendererOptions::RenderResolutionScaleMode>(mode);
		}

		QualityPreset SanitizeQualityPreset(uint32_t preset)
		{
			if (preset > static_cast<uint32_t>(QualityPreset::Cinematic))
				return QualityPreset::Medium;

			return static_cast<QualityPreset>(preset);
		}

		SceneRendererOptions::EffectResolutionScale SanitizeEffectResolutionScale(uint32_t scale)
		{
			switch (scale)
			{
				case static_cast<uint32_t>(SceneRendererOptions::EffectResolutionScale::Full):
				case static_cast<uint32_t>(SceneRendererOptions::EffectResolutionScale::Half):
				case static_cast<uint32_t>(SceneRendererOptions::EffectResolutionScale::Quarter):
					return static_cast<SceneRendererOptions::EffectResolutionScale>(scale);
				default:
					return SceneRendererOptions::EffectResolutionScale::Half;
			}
		}

		uint32_t SanitizeCloudRenderScale(uint32_t scale)
		{
			switch (scale)
			{
				case static_cast<uint32_t>(SceneRendererOptions::EffectResolutionScale::Full):
				case static_cast<uint32_t>(SceneRendererOptions::EffectResolutionScale::Half):
				case static_cast<uint32_t>(SceneRendererOptions::EffectResolutionScale::Quarter):
					return scale;
				default:
					return static_cast<uint32_t>(SceneRendererOptions::EffectResolutionScale::Half);
			}
		}

		SceneRendererOptions::EffectResolutionScale CloudRenderScaleToEffectScale(uint32_t scale)
		{
			return static_cast<SceneRendererOptions::EffectResolutionScale>(SanitizeCloudRenderScale(scale));
		}

		template<typename TInput>
		void SetRenderPassInputIfValid(Ref<RenderPass> renderPass, std::string_view name, TInput&& input)
		{
			if (renderPass && renderPass->IsInputValid(name))
				renderPass->SetInput(name, std::forward<TInput>(input));
		}

		SceneRendererOptions::ShadowResolutionTier SanitizeShadowResolutionTier(uint32_t tier)
		{
			if (tier > static_cast<uint32_t>(SceneRendererOptions::ShadowResolutionTier::Tier_8K))
				return SceneRendererOptions::ShadowResolutionTier::Tier_4K;

			return static_cast<SceneRendererOptions::ShadowResolutionTier>(tier);
		}

		SceneRendererOptions::SSRQualityPreset SanitizeSSRQualityPreset(uint32_t quality)
		{
			switch (quality)
			{
				case static_cast<uint32_t>(SceneRendererOptions::SSRQualityPreset::Full):
				case static_cast<uint32_t>(SceneRendererOptions::SSRQualityPreset::HalfBilateral):
				case static_cast<uint32_t>(SceneRendererOptions::SSRQualityPreset::QuarterDebug):
					return static_cast<SceneRendererOptions::SSRQualityPreset>(quality);
				default:
					return SceneRendererOptions::SSRQualityPreset::HalfBilateral;
			}
		}

		SceneRendererOptions::EffectResolutionScale GetSSRQualityResolutionScale(SceneRendererOptions::SSRQualityPreset quality)
		{
			switch (SanitizeSSRQualityPreset(static_cast<uint32_t>(quality)))
			{
				case SceneRendererOptions::SSRQualityPreset::Full:
					return SceneRendererOptions::EffectResolutionScale::Full;
				case SceneRendererOptions::SSRQualityPreset::QuarterDebug:
					return SceneRendererOptions::EffectResolutionScale::Quarter;
				case SceneRendererOptions::SSRQualityPreset::HalfBilateral:
				default:
					return SceneRendererOptions::EffectResolutionScale::Half;
			}
		}

		bool UsesSSRBilateralUpscale(SceneRendererOptions::SSRQualityPreset quality)
		{
			return SanitizeSSRQualityPreset(static_cast<uint32_t>(quality)) != SceneRendererOptions::SSRQualityPreset::Full;
		}

		uint32_t GetEffectResolutionDivisor(SceneRendererOptions::EffectResolutionScale scale)
		{
			return static_cast<uint32_t>(SanitizeEffectResolutionScale(static_cast<uint32_t>(scale)));
		}

		glm::uvec2 GetScaledExtent(const glm::uvec2& fullExtent, SceneRendererOptions::EffectResolutionScale scale)
		{
			const uint32_t divisor = GetEffectResolutionDivisor(scale);
			const glm::uvec2 extent = DivideRoundUp(fullExtent, divisor);
			return { glm::max(1u, extent.x), glm::max(1u, extent.y) };
		}

		uint32_t ResolveShadowResolutionTier(uint32_t tier)
		{
			constexpr std::array<uint32_t, 4> resolutions = { 1024u, 2048u, 4096u, 8192u };
			return resolutions[glm::min(tier, (uint32_t)resolutions.size() - 1u)];
		}

		float ResolveShadowDistance(float perLightDistance, float fallbackDistance)
		{
			const float resolved = perLightDistance > 0.0f ? perLightDistance : fallbackDistance;
			return glm::max(0.1f, resolved);
		}

		float CalculateLightLuminance(const glm::vec3& radiance)
		{
			return glm::dot(glm::max(radiance, glm::vec3(0.0f)), glm::vec3(0.2126f, 0.7152f, 0.0722f));
		}

		constexpr std::array<const char*, 29> s_ProfiledSceneRendererPasses = {
			"ShadowMapPass",
			"SpotShadowMapPass",
			"MeshCullingPass",
			"PreDepthPass",
			"HZB",
			"PreIntegration",
			"ClusterBuildPass",
			"ClusterLightCullingPass",
			"SkyboxPass",
			"SkyAtmospherePass",
			"VolumetricCloudPass",
			"VolumetricCloudCompositePass",
			"AtmosphericFogPass",
			"GeometryPass",
			"GTAO",
			"GTAO-Denoise",
			"GTAO-Temporal",
			"AOComposite",
			"PreConvolution",
			"SSR",
			"SSR-Temporal",
			"SSRComposite",
			"JumpFlood",
			"BloomCompute",
			"CompositePass",
			"JumpFloodComposite",
			"GridPass",
			"Renderer2D",
			"DOF"
		};

		uint32_t NextPowerOfTwo(uint32_t value)
		{
			if (value <= 1)
				return 1;

			value--;
			value |= value >> 1;
			value |= value >> 2;
			value |= value >> 4;
			value |= value >> 8;
			value |= value >> 16;
			return value + 1;
		}

		AssetHandle ResolveStaticMeshMaterialHandle(
			const Ref<MaterialTable>& materialTable,
			const Ref<StaticMesh>& staticMesh,
			const Ref<MeshSource>& meshSource,
			uint32_t materialIndex)
		{
			if (materialTable)
			{
				if (materialTable->HasMaterial(materialIndex))
					return materialTable->GetMaterial(materialIndex);

				const auto& overrides = materialTable->GetMaterials();
				if (overrides.size() == 1 && materialTable->HasMaterial(0))
					return materialTable->GetMaterial(0);
			}

			Ref<MaterialTable> staticMeshMaterials = staticMesh ? staticMesh->GetMaterials() : nullptr;
			if (staticMeshMaterials && staticMeshMaterials->HasMaterial(materialIndex))
				return staticMeshMaterials->GetMaterial(materialIndex);

			if (meshSource && materialIndex < meshSource->GetMaterials().size())
				return meshSource->GetMaterials()[materialIndex];

			return 0;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Construction / destruction
	// ─────────────────────────────────────────────────────────────────────────

	SceneRenderer::SceneRenderer(Ref<Scene> scene, SceneRendererSpecification specification)
		: m_Scene(scene), m_Specification(specification)
	{
		static const bool renderVolumeSelfTestsPassed = RenderVolumeEvaluator::RunSelfTests();
		(void)renderVolumeSelfTestsPassed;
		Init();
	}

	SceneRenderer::~SceneRenderer()
	{
		Shutdown();
	}

	void SceneRenderer::Shutdown()
	{
		// Ref<> handles GPU resource lifetimes automatically.
	}

	void SceneRenderer::SetScene(Ref<Scene> scene)
	{
		LUX_CORE_ASSERT(!m_Active, "Cannot change scene while rendering");
		m_Scene = scene;
	}

	void SceneRenderer::InitOptions()
	{
		using namespace Tiering::Renderer;

		const auto& tiering = m_Specification.Tiering;
		m_RendererDataUB.SoftShadows = tiering.ShadowQuality == ShadowQualitySetting::High;
		m_Options.SoftShadows = m_RendererDataUB.SoftShadows;
		m_Options.EnableGTAO = false;

		if (tiering.EnableAO && tiering.AOType == AmbientOcclusionTypeSetting::GTAO)
		{
			switch (tiering.AOQuality)
			{
				case AmbientOcclusionQualitySetting::High:
					m_Options.EnableGTAO = true;
					m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
					m_GTAODataCB.ResolutionScale = 2;
					break;
				case AmbientOcclusionQualitySetting::Ultra:
					m_Options.EnableGTAO = true;
					m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
					m_GTAODataCB.ResolutionScale = 1;
					break;
				case AmbientOcclusionQualitySetting::None:
					break;
			}
		}

		m_Options.EnableSSR = false;
		switch (tiering.SSRQuality)
		{
			case SSRQualitySetting::Medium:
				m_Options.EnableSSR = true;
				m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::HalfBilateral;
				m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
				m_SSROptions.HalfRes = true;
				m_SSROptions.ResolutionScale = 2;
				break;
			case SSRQualitySetting::High:
				m_Options.EnableSSR = true;
				m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::Full;
				m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
				m_SSROptions.HalfRes = false;
				m_SSROptions.ResolutionScale = 1;
				break;
			case SSRQualitySetting::Off:
				break;
		}

		m_BloomSettings.Enabled = tiering.EnableBloom;

		if (Ref<Project> project = Project::GetActive())
			ApplyProjectSettings(project->GetConfig().SceneRenderer);

		UpdateGTAOData();
	}

	void SceneRenderer::ApplyQualityPreset(QualityPreset preset)
	{
		preset = SanitizeQualityPreset(static_cast<uint32_t>(preset));
		m_Options.Quality = preset;

		m_Options.EnableSSR = true;
		m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::HalfBilateral;
		m_Options.EnableSSRTemporalAccumulation = false;
		m_Options.SSRTemporalBlend = 0.90f;
		m_Options.EnableGTAO = true;
		m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
		m_Options.GTAOBentNormals = false;
		m_Options.EnableGTAOTemporalAccumulation = false;
		m_Options.GTAOTemporalBlend = 0.85f;
		m_Options.GTAODenoisePasses = 4;
		m_Options.AOShadowTolerance = 1.0f;
		m_BloomSettings.Enabled = true;
		m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
		m_BloomSettings.Threshold = 1.0f;
		m_BloomSettings.Knee = 0.1f;
		m_BloomSettings.UpsampleScale = 1.0f;
		m_BloomSettings.Intensity = 1.0f;
		m_BloomSettings.DirtIntensity = 1.0f;
		m_DOFSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
		m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Native;
		m_Options.DynamicResolutionMinScale = 0.5f;
		m_Options.DynamicResolutionMaxScale = 1.0f;
		m_Options.DynamicResolutionTargetGPUTime = 16.67f;
		m_Options.TextureMipBias = 0.0f;
		m_Options.EnableDistanceMipBias = true;
		m_Options.DistanceMipBiasStart = 50.0f;
		m_Options.DistanceMipBiasEnd = 250.0f;
		m_Options.DistanceMipBiasMax = 2.0f;
		m_Options.SoftShadows = true;
		m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_4K;
		m_Options.MaxShadowDistance = 200.0f;
		m_Options.ShadowFade = 25.0f;
		m_SSROptions.MaxSteps = 70;
		m_SSROptions.Brightness = 0.7f;
		m_SSROptions.DepthTolerance = 0.8f;

		switch (preset)
		{
		case QualityPreset::Low:
			m_Options.EnableSSR = false;
			m_Options.EnableGTAO = false;
			m_Options.GTAODenoisePasses = 0;
			m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Quarter;
			m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Scale50;
			m_Options.TextureMipBias = 1.0f;
			m_Options.DistanceMipBiasStart = 25.0f;
			m_Options.DistanceMipBiasEnd = 120.0f;
			m_Options.DistanceMipBiasMax = 3.0f;
			m_Options.SoftShadows = false;
			m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_1K;
			m_Options.MaxShadowDistance = 100.0f;
			m_Options.ShadowFade = 15.0f;
			m_SSROptions.MaxSteps = 32;
			break;
		case QualityPreset::Medium:
			m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::HalfBilateral;
			m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
			m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
			m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Scale75;
			m_Options.TextureMipBias = 0.25f;
			m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_2K;
			m_Options.MaxShadowDistance = 150.0f;
			m_SSROptions.MaxSteps = 48;
			break;
		case QualityPreset::High:
			m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::Full;
			m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
			m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
			m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Native;
			m_Options.TextureMipBias = -0.5f;
			m_Options.DistanceMipBiasMax = 1.5f;
			m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_4K;
			break;
		case QualityPreset::Ultra:
			m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::Full;
			m_Options.EnableSSRTemporalAccumulation = true;
			m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
			m_Options.GTAOBentNormals = true;
			m_Options.EnableGTAOTemporalAccumulation = true;
			m_Options.GTAOTemporalBlend = 0.85f;
			m_Options.GTAODenoisePasses = 6;
			m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
			m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Native;
			m_Options.TextureMipBias = -1.0f;
			m_Options.DistanceMipBiasStart = 25.0f;
			m_Options.DistanceMipBiasEnd = 150.0f;
			m_Options.DistanceMipBiasMax = 1.0f;
			m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_8K;
			m_Options.MaxShadowDistance = 300.0f;
			m_SSROptions.MaxSteps = 96;
			break;
		case QualityPreset::Cinematic:
			m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::Full;
			m_Options.EnableSSRTemporalAccumulation = true;
			m_Options.SSRTemporalBlend = 0.95f;
			m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
			m_Options.GTAOBentNormals = true;
			m_Options.EnableGTAOTemporalAccumulation = true;
			m_Options.GTAOTemporalBlend = 0.95f;
			m_Options.GTAODenoisePasses = 8;
			m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
			m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Native;
			m_Options.TextureMipBias = -1.5f;
			m_Options.DistanceMipBiasStart = 10.0f;
			m_Options.DistanceMipBiasEnd = 100.0f;
			m_Options.DistanceMipBiasMax = 0.5f;
			m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_8K;
			m_Options.MaxShadowDistance = 450.0f;
			m_Options.ShadowFade = 50.0f;
			m_DOFSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
			m_SSROptions.MaxSteps = 128;
			break;
		}

		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
		m_GTAODataCB.ResolutionScale = GetEffectResolutionDivisor(m_Options.GTAOResolutionScale);
		m_SSROptions.HalfRes = GetEffectResolutionDivisor(m_Options.SSRResolutionScale) > 1u;
		m_SSROptions.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);
		m_SSROptions.TemporalAccumulation = m_Options.EnableSSRTemporalAccumulation ? 1u : 0u;
		m_SSROptions.TemporalBlend = m_Options.SSRTemporalBlend;
		UpdateGTAOData();
	}

	void SceneRenderer::UpdateGTAOData()
	{
		const bool gtaoEnabled = m_Options.EnableGTAO;
		Renderer::SetGlobalMacroInShaders("__HZ_AO_METHOD", std::to_string((int)ShaderDef::GetAOMethod(gtaoEnabled)));
		Renderer::SetGlobalMacroInShaders("__HZ_GTAO_COMPUTE_BENT_NORMALS", m_Options.GTAOBentNormals ? "1" : "0");

		m_Options.ReflectionOcclusionMethod = ShaderDef::AOMethod::None;
		Renderer::SetGlobalMacroInShaders("__HZ_REFLECTION_OCCLUSION_METHOD", std::to_string((int)m_Options.ReflectionOcclusionMethod));
	}

	void SceneRenderer::ApplyProjectSettings(const ProjectSceneRendererSettings& settings)
	{
		const auto previousScaleMode = m_Options.ResolutionScaleMode;
		const float previousMinScale = m_Options.DynamicResolutionMinScale;
		const float previousMaxScale = m_Options.DynamicResolutionMaxScale;
		const float previousTargetGPUTime = m_Options.DynamicResolutionTargetGPUTime;
		const auto previousGTAOScale = m_Options.GTAOResolutionScale;
		const auto previousSSRQuality = m_Options.SSRQuality;
		const auto previousSSRScale = m_Options.SSRResolutionScale;
		const bool previousGTAOTemporal = m_Options.EnableGTAOTemporalAccumulation;
		const bool previousSSRTemporal = m_Options.EnableSSRTemporalAccumulation;
		const bool previousGTAOBentNormals = m_Options.GTAOBentNormals;
		const auto previousBloomScale = m_BloomSettings.ResolutionScale;
		const auto previousDOFScale = m_DOFSettings.ResolutionScale;

		m_Options.Quality = SanitizeQualityPreset(settings.QualityPreset);
		m_Options.EnableFrustumCulling = settings.EnableFrustumCulling;
		m_Options.EnableOcclusionCulling = settings.EnableOcclusionCulling;
		m_Options.OcclusionDepthBias = std::clamp(settings.OcclusionDepthBias, 0.0f, 0.1f);
		m_Options.OcclusionBoundsScale = std::clamp(settings.OcclusionBoundsScale, 1.0f, 2.0f);
		m_Options.EnableGPUDrivenRendering = settings.EnableGPUDrivenRendering;
		m_Options.EnableGTAO = settings.EnableGTAO;
		m_Options.GTAOBentNormals = settings.GTAOBentNormals;
		m_Options.GTAODenoisePasses = settings.GTAODenoisePasses;
		m_Options.AOShadowTolerance = settings.AOShadowTolerance;
		m_Options.EnableSSR = settings.EnableSSR;
		m_Options.GTAOResolutionScale = SanitizeEffectResolutionScale(settings.GTAOResolutionScale);
		m_Options.EnableGTAOTemporalAccumulation = settings.GTAOTemporalAccumulation;
		m_Options.GTAOTemporalBlend = std::clamp(settings.GTAOTemporalBlend, 0.0f, 0.98f);
		m_Options.SSRQuality = SanitizeSSRQualityPreset(settings.SSRQuality);
		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
		m_Options.EnableSSRTemporalAccumulation = settings.SSRTemporalAccumulation;
		m_Options.SSRTemporalBlend = std::clamp(settings.SSRTemporalBlend, 0.0f, 0.98f);
		m_Options.EnableJumpFlood = settings.EnableJumpFlood;
		m_Options.ResolutionScaleMode = SanitizeRenderResolutionScaleMode(settings.RenderScaleMode);
		m_Options.DynamicResolutionMinScale = std::clamp(settings.DynamicResolutionMinScale, 0.25f, 1.0f);
		m_Options.DynamicResolutionMaxScale = std::clamp(settings.DynamicResolutionMaxScale, m_Options.DynamicResolutionMinScale, 1.0f);
		m_Options.DynamicResolutionTargetGPUTime = std::max(1.0f, settings.DynamicResolutionTargetGPUTime);
		m_Options.DynamicResolutionScale = std::clamp(m_Options.DynamicResolutionScale, m_Options.DynamicResolutionMinScale, m_Options.DynamicResolutionMaxScale);
		m_Options.TextureMipBias = std::clamp(settings.TextureMipBias, -4.0f, 8.0f);
		m_Options.EnableDistanceMipBias = settings.EnableDistanceMipBias;
		m_Options.DistanceMipBiasStart = std::max(0.0f, settings.DistanceMipBiasStart);
		m_Options.DistanceMipBiasEnd = std::max(m_Options.DistanceMipBiasStart + 1.0f, settings.DistanceMipBiasEnd);
		m_Options.DistanceMipBiasMax = std::clamp(settings.DistanceMipBiasMax, 0.0f, 8.0f);

		m_Options.SoftShadows = settings.SoftShadows;
		m_Options.EnableShadowCulling = settings.EnableShadowCulling;
		m_Options.MaxShadowDistance = settings.MaxShadowDistance;
		m_Options.ShadowFade = settings.ShadowFade;
		m_Options.ShadowCascadeSplitLambda = settings.ShadowCascadeSplitLambda;
		m_Options.ShadowCascadeNearPlaneOffset = settings.ShadowCascadeNearPlaneOffset;
		m_Options.ShadowCascadeFarPlaneOffset = settings.ShadowCascadeFarPlaneOffset;
		m_Options.ShadowCascadeTransitionFade = settings.ShadowCascadeTransitionFade;
		m_Options.ShadowResolution = SanitizeShadowResolutionTier(settings.ShadowResolution);

		m_BloomSettings.Enabled = settings.BloomEnabled;
		m_BloomSettings.ResolutionScale = SanitizeEffectResolutionScale(settings.BloomResolutionScale);
		m_BloomSettings.Threshold = settings.BloomThreshold;
		m_BloomSettings.Knee = settings.BloomKnee;
		m_BloomSettings.UpsampleScale = settings.BloomUpsampleScale;
		m_BloomSettings.Intensity = settings.BloomIntensity;
		m_BloomSettings.DirtIntensity = settings.BloomDirtIntensity;

		m_DOFSettings.Enabled = settings.DOFEnabled;
		m_DOFSettings.ResolutionScale = SanitizeEffectResolutionScale(settings.DOFResolutionScale);
		m_DOFSettings.FocusDistance = settings.DOFFocusDistance;
		m_DOFSettings.BlurSize = settings.DOFBlurSize;

		m_SSROptions.HalfRes = GetEffectResolutionDivisor(m_Options.SSRResolutionScale) > 1u;
		m_SSROptions.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);
		m_SSROptions.MaxSteps = settings.SSRMaxSteps;
		m_SSROptions.Brightness = settings.SSRBrightness;
		m_SSROptions.DepthTolerance = settings.SSRDepthTolerance;
		m_SSROptions.TemporalAccumulation = m_Options.EnableSSRTemporalAccumulation ? 1u : 0u;
		m_SSROptions.TemporalBlend = m_Options.SSRTemporalBlend;

		UpdateGTAOData();

		if (previousScaleMode != m_Options.ResolutionScaleMode
			|| previousMinScale != m_Options.DynamicResolutionMinScale
			|| previousMaxScale != m_Options.DynamicResolutionMaxScale
			|| previousTargetGPUTime != m_Options.DynamicResolutionTargetGPUTime)
		{
			RefreshRenderResolutionScale();
		}

		if (previousGTAOScale != m_Options.GTAOResolutionScale
			|| previousSSRQuality != m_Options.SSRQuality
			|| previousSSRScale != m_Options.SSRResolutionScale
			|| previousBloomScale != m_BloomSettings.ResolutionScale
			|| previousDOFScale != m_DOFSettings.ResolutionScale)
		{
			RefreshScreenSpaceEffectResources();
		}

		if (previousGTAOTemporal != m_Options.EnableGTAOTemporalAccumulation
			|| previousSSRTemporal != m_Options.EnableSSRTemporalAccumulation
			|| previousGTAOBentNormals != m_Options.GTAOBentNormals
			|| previousGTAOScale != m_Options.GTAOResolutionScale
			|| previousSSRQuality != m_Options.SSRQuality
			|| previousSSRScale != m_Options.SSRResolutionScale)
		{
			m_TemporalHistoryValid = false;
		m_CloudHistoryValid = false;
		}
	}

	void SceneRenderer::WriteProjectSettings(ProjectSceneRendererSettings& settings) const
	{
		settings.QualityPreset = static_cast<uint32_t>(SanitizeQualityPreset(static_cast<uint32_t>(m_Options.Quality)));
		settings.EnableFrustumCulling = m_Options.EnableFrustumCulling;
		settings.EnableOcclusionCulling = m_Options.EnableOcclusionCulling;
		settings.OcclusionDepthBias = m_Options.OcclusionDepthBias;
		settings.OcclusionBoundsScale = m_Options.OcclusionBoundsScale;
		settings.EnableGPUDrivenRendering = m_Options.EnableGPUDrivenRendering;
		settings.EnableGTAO = m_Options.EnableGTAO;
		settings.GTAOBentNormals = m_Options.GTAOBentNormals;
		settings.GTAODenoisePasses = m_Options.GTAODenoisePasses;
		settings.AOShadowTolerance = m_Options.AOShadowTolerance;
		settings.EnableSSR = m_Options.EnableSSR;
		settings.GTAOResolutionScale = GetEffectResolutionDivisor(m_Options.GTAOResolutionScale);
		settings.GTAOTemporalAccumulation = m_Options.EnableGTAOTemporalAccumulation;
		settings.GTAOTemporalBlend = m_Options.GTAOTemporalBlend;
		const SceneRendererOptions::SSRQualityPreset ssrQuality = SanitizeSSRQualityPreset(static_cast<uint32_t>(m_Options.SSRQuality));
		const SceneRendererOptions::EffectResolutionScale ssrResolutionScale = GetSSRQualityResolutionScale(ssrQuality);
		settings.SSRQuality = static_cast<uint32_t>(ssrQuality);
		settings.SSRResolutionScale = GetEffectResolutionDivisor(ssrResolutionScale);
		settings.SSRTemporalAccumulation = m_Options.EnableSSRTemporalAccumulation;
		settings.SSRTemporalBlend = m_Options.SSRTemporalBlend;
		settings.EnableJumpFlood = m_Options.EnableJumpFlood;
		settings.RenderScaleMode = static_cast<uint32_t>(m_Options.ResolutionScaleMode);
		settings.DynamicResolutionMinScale = m_Options.DynamicResolutionMinScale;
		settings.DynamicResolutionMaxScale = m_Options.DynamicResolutionMaxScale;
		settings.DynamicResolutionTargetGPUTime = m_Options.DynamicResolutionTargetGPUTime;
		settings.TextureMipBias = m_Options.TextureMipBias;
		settings.EnableDistanceMipBias = m_Options.EnableDistanceMipBias;
		settings.DistanceMipBiasStart = m_Options.DistanceMipBiasStart;
		settings.DistanceMipBiasEnd = m_Options.DistanceMipBiasEnd;
		settings.DistanceMipBiasMax = m_Options.DistanceMipBiasMax;

		settings.SoftShadows = m_Options.SoftShadows;
		settings.EnableShadowCulling = m_Options.EnableShadowCulling;
		settings.MaxShadowDistance = m_Options.MaxShadowDistance;
		settings.ShadowFade = m_Options.ShadowFade;
		settings.ShadowCascadeSplitLambda = m_Options.ShadowCascadeSplitLambda;
		settings.ShadowCascadeNearPlaneOffset = m_Options.ShadowCascadeNearPlaneOffset;
		settings.ShadowCascadeFarPlaneOffset = m_Options.ShadowCascadeFarPlaneOffset;
		settings.ShadowCascadeTransitionFade = m_Options.ShadowCascadeTransitionFade;
		settings.ShadowResolution = static_cast<uint32_t>(SanitizeShadowResolutionTier(static_cast<uint32_t>(m_Options.ShadowResolution)));

		settings.BloomEnabled = m_BloomSettings.Enabled;
		settings.BloomResolutionScale = GetEffectResolutionDivisor(m_BloomSettings.ResolutionScale);
		settings.BloomThreshold = m_BloomSettings.Threshold;
		settings.BloomKnee = m_BloomSettings.Knee;
		settings.BloomUpsampleScale = m_BloomSettings.UpsampleScale;
		settings.BloomIntensity = m_BloomSettings.Intensity;
		settings.BloomDirtIntensity = m_BloomSettings.DirtIntensity;

		settings.DOFEnabled = m_DOFSettings.Enabled;
		settings.DOFResolutionScale = GetEffectResolutionDivisor(m_DOFSettings.ResolutionScale);
		settings.DOFFocusDistance = m_DOFSettings.FocusDistance;
		settings.DOFBlurSize = m_DOFSettings.BlurSize;

		settings.SSRHalfRes = GetEffectResolutionDivisor(ssrResolutionScale) > 1u;
		settings.SSRMaxSteps = m_SSROptions.MaxSteps;
		settings.SSRBrightness = m_SSROptions.Brightness;
		settings.SSRDepthTolerance = m_SSROptions.DepthTolerance;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Init
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::Init()
	{
		InitOptions();

		for (size_t passIndex = 0; passIndex < m_MeshPasses.size(); passIndex++)
			m_MeshPasses[passIndex].Type = static_cast<MeshPassType>(passIndex);

		m_CommandBuffer = RenderCommandBuffer::Create(0, "SceneRenderer",       /*queries=*/true);
		m_UploadCommandBuffer = RenderCommandBuffer::Create(0, "SceneRenderer-Upload", /*queries=*/false);

		m_Renderer2D = Ref<Renderer2D>::Create(Renderer2DSpecification{});
		m_Renderer2DScreenSpace = Ref<Renderer2D>::Create(Renderer2DSpecification{});
		m_DebugRenderer = Ref<DebugRenderer>::Create();

		// Use window size if none specified
		if (m_Specification.ViewportWidth == 0) m_Specification.ViewportWidth = Application::Get().GetWindow().GetWidth();
		if (m_Specification.ViewportHeight == 0) m_Specification.ViewportHeight = Application::Get().GetWindow().GetHeight();
		SetViewportSize(m_Specification.ViewportWidth, m_Specification.ViewportHeight);

		// ── Uniform buffer sets ───────────────────────────────────────────────
		m_UBSCamera = UniformBufferSet::Create(sizeof(UBCamera));
		m_UBSScene = UniformBufferSet::Create(sizeof(UBScene));
		m_UBSShadow = UniformBufferSet::Create(sizeof(UBShadow));
		m_UBSRendererData = UniformBufferSet::Create(sizeof(UBRendererData));
		m_UBSPointLights = UniformBufferSet::Create(sizeof(UBPointLights));
		m_UBSSpotLights = UniformBufferSet::Create(sizeof(UBSpotLights));
		m_UBSSpotShadow = UniformBufferSet::Create(sizeof(UBSpotShadow));
		m_UBSScreenData = UniformBufferSet::Create(sizeof(UBScreenData));
		m_UBSAtmosphere = UniformBufferSet::Create(sizeof(UBAtmosphere));

		// ── Storage buffer sets (start with generous initial capacity) ─────────
		{
			StorageBufferSpecification spec;
			spec.GPUOnly = false;
			spec.DebugName = "ObjectIndexes";
			m_SBSObjectIndexes = StorageBufferSet::Create(spec, sizeof(uint32_t) * 4096);

			spec.DebugName = "VisibleObjectIndexes";
			m_SBSVisibleObjectIndexes = StorageBufferSet::Create(spec, sizeof(uint32_t) * 4096);

			spec.DebugName = "GPUSceneInstances";
			m_SBSGPUSceneInstances = StorageBufferSet::Create(spec, sizeof(GPUSceneInstanceData) * 4096);

			spec.DebugName = "GPUMaterials";
			m_SBSGPUMaterials = StorageBufferSet::Create(spec, sizeof(GPUMaterialData) * 1024);

			spec.DebugName = "MeshCullDrawData";
			m_SBSMeshCullDrawData = StorageBufferSet::Create(spec, sizeof(MeshCullDrawData) * 4096);

			spec.DrawIndirect = true;
			spec.DebugName = "IndirectDrawCommands";
			m_SBSIndirectDrawCommands = StorageBufferSet::Create(spec, sizeof(nvrhi::DrawIndexedIndirectArguments) * 4096);
		}
		{
			StorageBufferSpecification indexSpec;
			indexSpec.GPUOnly = true;

			// Clustered light culling: per-cluster view-space AABB grid. Fixed size
			// (independent of viewport); rebuilt by ClusterBuildPass on the GPU.
			indexSpec.DebugName = "ClusterAABBs";
			m_SBSClusterAABBs = StorageBufferSet::Create(indexSpec, sizeof(glm::vec4) * 2 * ClusterCount);

			// Clustered light assignment outputs (parallel point/spot). Grids are
			// (offset,count) per cluster; index lists are dynamically packed.
			indexSpec.DebugName = "PointLightGrid";
			m_SBSPointLightGrid = StorageBufferSet::Create(indexSpec, sizeof(glm::uvec2) * ClusterCount);
			indexSpec.DebugName = "SpotLightGrid";
			m_SBSSpotLightGrid = StorageBufferSet::Create(indexSpec, sizeof(glm::uvec2) * ClusterCount);
			indexSpec.DebugName = "PointLightIndexList";
			m_SBSPointLightIndexList = StorageBufferSet::Create(indexSpec, sizeof(uint32_t) * MaxClusterPointIndices);
			indexSpec.DebugName = "SpotLightIndexList";
			m_SBSSpotLightIndexList = StorageBufferSet::Create(indexSpec, sizeof(uint32_t) * MaxClusterSpotIndices);
			indexSpec.DebugName = "ClusterLightCounter";
			m_SBSClusterLightCounter = StorageBufferSet::Create(indexSpec, sizeof(uint32_t) * 4);
		}
		{
			// Auto-exposure: a 256-bin luminance histogram and a tiny persistent
			// exposure state buffer ({ adapted luminance, exposure }).
			StorageBufferSpecification histogramSpec;
			histogramSpec.GPUOnly = true;
			histogramSpec.DebugName = "LuminanceHistogram";
			m_SBSLuminanceHistogram = StorageBufferSet::Create(histogramSpec, sizeof(uint32_t) * s_LuminanceHistogramBins);

			StorageBufferSpecification exposureSpec;
			exposureSpec.GPUOnly = true;
			exposureSpec.DebugName = "ExposureState";
			m_SBSExposureState = StorageBufferSet::Create(exposureSpec, sizeof(float) * 2);
		}

		// Common vertex layout for all opaque mesh pipelines
		VertexBufferLayout vertexLayout = {
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal"   },
			{ ShaderDataType::Float3, "a_Tangent"  },
			{ ShaderDataType::Float3, "a_Binormal" },
			{ ShaderDataType::Float2, "a_TexCoord" }
		};

		// ── Directional shadow maps ───────────────────────────────────────────
		{
			ImageSpecification shadowMapSpec;
			shadowMapSpec.DebugName = "ShadowMapArray";
			shadowMapSpec.Dimension = nvrhi::TextureDimension::Texture2DArray;
			shadowMapSpec.Format = ImageFormat::Depth;
			shadowMapSpec.Usage = ImageUsage::Attachment;
			shadowMapSpec.Width = 4096;
			shadowMapSpec.Height = 4096;
			shadowMapSpec.Layers = 4;
			m_ShadowMapImage = Image2D::Create(shadowMapSpec);
			m_ShadowMapImage->RT_Invalidate();

			Ref<Shader> shadowPassShader = Renderer::GetShaderLibrary()->Get("DirShadowMap");
			for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
			{
				FramebufferSpecification fbSpec;
				fbSpec.Width = 4096;
				fbSpec.Height = 4096;
				fbSpec.Attachments = { ImageFormat::Depth };
				fbSpec.DepthClearValue = 1.0f;
				fbSpec.DebugName = "ShadowMap-Cascade" + std::to_string(cascade);
				fbSpec.ExistingImage = m_ShadowMapImage;
				fbSpec.ExistingImageLayer = cascade;

				PipelineSpecification pipelineSpec;
				pipelineSpec.DebugName = "DirShadowMap-Cascade" + std::to_string(cascade);
				pipelineSpec.Shader = shadowPassShader;
				pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
				pipelineSpec.Layout = vertexLayout;
				pipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
				pipelineSpec.BackfaceCulling = false; // avoid peter-panning

				RenderPassSpecification rpSpec;
				rpSpec.DebugName = "ShadowMapPass-Cascade" + std::to_string(cascade);
				rpSpec.Pipeline = Pipeline::Create(pipelineSpec);

				m_ShadowMapPasses[cascade] = RenderPass::Create(rpSpec);
				m_ShadowMapPasses[cascade]->SetInput("ShadowData", m_UBSShadow);
				m_ShadowMapPasses[cascade]->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
				m_ShadowMapPasses[cascade]->SetInput("ObjectIndexes", m_SBSObjectIndexes);
				LUX_CORE_VERIFY(m_ShadowMapPasses[cascade]->Validate());
				m_ShadowMapPasses[cascade]->Bake();
			}

			m_ShadowMapPass = m_ShadowMapPasses[0];
			m_ShadowPassMaterial = Material::Create(shadowPassShader, "ShadowPass");
		}

		// ── Spot shadow atlas (single depth atlas) ───────────────────────────
		{
			m_SpotShadowMapSize = 2048;
			m_SpotShadowAtlasGridSize = 1;
			m_SpotShadowTileSize = m_SpotShadowMapSize;

			ImageSpecification spotShadowSpec;
			spotShadowSpec.DebugName = "SpotShadowAtlas";
			spotShadowSpec.Dimension = nvrhi::TextureDimension::Texture2D;
			spotShadowSpec.Format = ImageFormat::Depth;
			spotShadowSpec.Usage = ImageUsage::Attachment;
			spotShadowSpec.Width = m_SpotShadowMapSize;
			spotShadowSpec.Height = m_SpotShadowMapSize;
			m_SpotShadowMapImage = Image2D::Create(spotShadowSpec);
			m_SpotShadowMapImage->RT_Invalidate();

			FramebufferSpecification fbSpec;
			fbSpec.Width = m_SpotShadowMapSize;
			fbSpec.Height = m_SpotShadowMapSize;
			fbSpec.Attachments = { ImageFormat::Depth };
			fbSpec.DepthClearValue = 1.0f;
			fbSpec.DebugName = "SpotShadowAtlas";
			fbSpec.ExistingImage = m_SpotShadowMapImage;

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "SpotShadowMap";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("SpotShadowMap");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			pipelineSpec.BackfaceCulling = false;

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "SpotShadowMapPass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);

			m_SpotShadowMapPass = RenderPass::Create(rpSpec);
			m_SpotShadowMapPass->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_SpotShadowMapPass->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
			m_SpotShadowMapPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_SpotShadowMapPass->Validate());
			m_SpotShadowMapPass->Bake();

			m_SpotShadowPassMaterial = Material::Create(pipelineSpec.Shader, "SpotShadowPass");
		}

		// ── Pre-depth pass ────────────────────────────────────────────────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::DEPTH32FSTENCIL8UINT };
			fbSpec.DepthClearValue = 0.0f;
			fbSpec.ClearDepthOnLoad = true;
			fbSpec.DebugName = "PreDepth";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "PreDepth";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("PreDepth");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::GreaterOrEqual;

			m_PreDepthPipeline = Pipeline::Create(pipelineSpec);
			m_PreDepthMaterial = Material::Create(pipelineSpec.Shader, "PreDepth");

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "PreDepthPass";
			rpSpec.Pipeline = m_PreDepthPipeline;

			m_PreDepthPass = RenderPass::Create(rpSpec);
			m_PreDepthPass->SetInput("Camera", m_UBSCamera);
			m_PreDepthPass->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
			m_PreDepthPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_PreDepthPass->Validate());
			m_PreDepthPass->Bake();
		}

		// ── Hierarchical depth + SSR pre-integration ──────────────────────────
		{
			TextureSpecification hzbSpec;
			hzbSpec.Format = ImageFormat::RED32F;
			hzbSpec.Width = 1;
			hzbSpec.Height = 1;
			hzbSpec.SamplerWrap = TextureWrap::Clamp;
			hzbSpec.SamplerFilter = TextureFilter::Nearest;
			hzbSpec.Storage = true;
			hzbSpec.GenerateMips = true;
			hzbSpec.DebugName = "HierarchicalZ";
			m_HierarchicalDepthTexture.Texture = Texture2D::Create(hzbSpec);

			ComputePassSpecification hzbPassSpec;
			hzbPassSpec.DebugName = "HierarchicalDepth";
			hzbPassSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("HZB"));
			m_HierarchicalDepthPass = ComputePass::Create(hzbPassSpec);
			m_HierarchicalDepthPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_HierarchicalDepthPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_HierarchicalDepthPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_HierarchicalDepthPass->Validate());
			m_HierarchicalDepthPass->Bake();

			TextureSpecification visibilitySpec;
			visibilitySpec.Format = ImageFormat::RED8UN;
			visibilitySpec.Width = 1;
			visibilitySpec.Height = 1;
			visibilitySpec.SamplerWrap = TextureWrap::Clamp;
			visibilitySpec.SamplerFilter = TextureFilter::Linear;
			visibilitySpec.Storage = true;
			visibilitySpec.GenerateMips = true;
			visibilitySpec.DebugName = "Pre-Integration";
			m_PreIntegrationVisibilityTexture.Texture = Texture2D::Create(visibilitySpec);

			ComputePassSpecification preIntegrationSpec;
			preIntegrationSpec.DebugName = "Pre-Integration";
			preIntegrationSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("Pre-Integration"));
			m_PreIntegrationPass = ComputePass::Create(preIntegrationSpec);
			m_PreIntegrationPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_PreIntegrationPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_PreIntegrationPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_PreIntegrationPass->Validate());
			m_PreIntegrationPass->Bake();

			TextureSpecification preConvolutionSpec;
			preConvolutionSpec.Format = ImageFormat::RGBA32F;
			preConvolutionSpec.Width = 1;
			preConvolutionSpec.Height = 1;
			preConvolutionSpec.SamplerWrap = TextureWrap::Clamp;
			preConvolutionSpec.Storage = true;
			preConvolutionSpec.GenerateMips = true;
			preConvolutionSpec.DebugName = "Pre-Convoluted";
			m_PreConvolutedTexture.Texture = Texture2D::Create(preConvolutionSpec);

			ComputePassSpecification preConvolutionPassSpec;
			preConvolutionPassSpec.DebugName = "Pre-Convolution";
			preConvolutionPassSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("Pre-Convolution"));
			m_PreConvolutionComputePass = ComputePass::Create(preConvolutionPassSpec);
			m_PreConvolutionComputePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_PreConvolutionComputePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_PreConvolutionComputePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_PreConvolutionComputePass->Validate());
			m_PreConvolutionComputePass->Bake();
		}

		// ── Mesh culling / GPU-driven indirect pass ─────────────────────────
		{
			ComputePassSpecification computeSpec;
			computeSpec.DebugName = "MeshCulling";
			computeSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("MeshCulling"));

			m_MeshCullingPass = ComputePass::Create(computeSpec);
			m_MeshCullingPass->SetInput("MeshCullDrawData", m_SBSMeshCullDrawData);
			m_MeshCullingPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			m_MeshCullingPass->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
			m_MeshCullingPass->SetInput("VisibleObjectIndexes", m_SBSVisibleObjectIndexes);
			m_MeshCullingPass->SetInput("IndirectDrawCommands", m_SBSIndirectDrawCommands);
			m_MeshCullingPass->SetInput("u_HZB", m_HierarchicalDepthTexture.Texture);
			m_MeshCullingPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			LUX_CORE_VERIFY(m_MeshCullingPass->Validate());
			m_MeshCullingPass->Bake();
		}

		// ── Cluster build pass (froxel AABB grid) ────────────────────────────
		{
			ComputePassSpecification computeSpec;
			computeSpec.DebugName = "ClusterBuild";
			computeSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("ClusterBuild"));

			m_ClusterBuildPass = ComputePass::Create(computeSpec);
			m_ClusterBuildPass->SetInput("Camera", m_UBSCamera);
			m_ClusterBuildPass->SetInput("ClusterAABBBuffer", m_SBSClusterAABBs);
			LUX_CORE_VERIFY(m_ClusterBuildPass->Validate());
			m_ClusterBuildPass->Bake();
		}

		// ── Cluster light assignment pass ────────────────────────────────────
		{
			ComputePassSpecification computeSpec;
			computeSpec.DebugName = "ClusterLightCulling";
			computeSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("ClusterLightCulling"));

			m_ClusterLightCullingPass = ComputePass::Create(computeSpec);
			m_ClusterLightCullingPass->SetInput("Camera", m_UBSCamera);
			m_ClusterLightCullingPass->SetInput("PointLightData", m_UBSPointLights);
			m_ClusterLightCullingPass->SetInput("SpotLightData", m_UBSSpotLights);
			m_ClusterLightCullingPass->SetInput("ClusterAABBBuffer", m_SBSClusterAABBs);
			m_ClusterLightCullingPass->SetInput("ClusterCounterBuffer", m_SBSClusterLightCounter);
			m_ClusterLightCullingPass->SetInput("PointLightGridBuffer", m_SBSPointLightGrid);
			m_ClusterLightCullingPass->SetInput("SpotLightGridBuffer", m_SBSSpotLightGrid);
			m_ClusterLightCullingPass->SetInput("PointLightIndexListBuffer", m_SBSPointLightIndexList);
			m_ClusterLightCullingPass->SetInput("SpotLightIndexListBuffer", m_SBSSpotLightIndexList);
			LUX_CORE_VERIFY(m_ClusterLightCullingPass->Validate());
			m_ClusterLightCullingPass->Bake();
		}

		// ── Scene color, GBuffer, forward fallback, and deferred lighting ─────
		{
			FramebufferSpecification sceneColorSpec;
			sceneColorSpec.Width = m_ViewportWidth;
			sceneColorSpec.Height = m_ViewportHeight;
			sceneColorSpec.Attachments = { ImageFormat::RGBA16F };
			sceneColorSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			sceneColorSpec.DebugName = "SceneColorHDR";
			m_SceneColorFramebuffer = Framebuffer::Create(sceneColorSpec);

			FramebufferTextureSpecification gbufferBaseColor = ImageFormat::RGBA16F;
			FramebufferTextureSpecification gbufferNormal = ImageFormat::RGBA16F;
			FramebufferTextureSpecification gbufferMetalRough = ImageFormat::RGBA;
			FramebufferTextureSpecification gbufferMaterialID = ImageFormat::RED32UI;
			FramebufferTextureSpecification gbufferObjectID = ImageFormat::RED32UI;
			FramebufferTextureSpecification gbufferVelocity = ImageFormat::RG16F;
			gbufferBaseColor.Blend = false;
			gbufferNormal.Blend = false;
			gbufferMetalRough.Blend = false;
			gbufferMaterialID.Blend = false;
			gbufferObjectID.Blend = false;
			gbufferVelocity.Blend = false;

			FramebufferSpecification gbufferSpec;
			gbufferSpec.Width = m_ViewportWidth;
			gbufferSpec.Height = m_ViewportHeight;
			gbufferSpec.Attachments = {
				gbufferBaseColor,
				gbufferNormal,
				gbufferMetalRough,
				gbufferMaterialID,
				gbufferObjectID,
				gbufferVelocity,
				ImageFormat::DEPTH32FSTENCIL8UINT
			};
			gbufferSpec.ExistingImages[6] = m_PreDepthPass->GetDepthOutput();
			gbufferSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			gbufferSpec.ClearDepthOnLoad = false;
			gbufferSpec.Blend = false;
			gbufferSpec.DebugName = "GBuffer";
			m_GeometryPassFramebuffer = Framebuffer::Create(gbufferSpec);

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "GBuffer-Static";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("GBuffer_Static");
			pipelineSpec.TargetFramebuffer = m_GeometryPassFramebuffer;
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::Equal;
			pipelineSpec.DepthWrite = false;
			m_GeometryPipeline = Pipeline::Create(pipelineSpec);

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "GBufferPass";
			rpSpec.Pipeline = m_GeometryPipeline;
			m_GeometryPass = RenderPass::Create(rpSpec);
			BindSceneRenderPassInputs(m_GeometryPass, PassInputCamera | PassInputRenderer | PassInputMaterialScene);
			LUX_CORE_VERIFY(m_GeometryPass->Validate());
			m_GeometryPass->Bake();

			FramebufferTextureSpecification forwardSceneColor = ImageFormat::RGBA16F;
			FramebufferTextureSpecification forwardNormal = ImageFormat::RGBA16F;
			FramebufferTextureSpecification forwardMetalRough = ImageFormat::RGBA;
			FramebufferTextureSpecification forwardVelocity = ImageFormat::RG16F;
			forwardSceneColor.Blend = false;
			forwardNormal.Blend = false;
			forwardMetalRough.Blend = false;
			forwardVelocity.Blend = false;

			FramebufferSpecification forwardSpec;
			forwardSpec.Width = m_ViewportWidth;
			forwardSpec.Height = m_ViewportHeight;
			forwardSpec.Attachments = {
				forwardSceneColor,
				forwardNormal,
				forwardMetalRough,
				forwardVelocity,
				ImageFormat::DEPTH32FSTENCIL8UINT
			};
			forwardSpec.ExistingImages[0] = m_SceneColorFramebuffer->GetImage(0);
			forwardSpec.ExistingImages[1] = m_GeometryPassFramebuffer->GetImage(1);
			forwardSpec.ExistingImages[2] = m_GeometryPassFramebuffer->GetImage(2);
			forwardSpec.ExistingImages[3] = m_GeometryPassFramebuffer->GetImage(5); // shared velocity buffer
			forwardSpec.ExistingImages[4] = m_PreDepthPass->GetDepthOutput();
			forwardSpec.ClearColorOnLoad = false;
			forwardSpec.ClearDepthOnLoad = false;
			forwardSpec.Blend = false;
			forwardSpec.DebugName = "TransparentForwardShared";

			// Transparent does not write motion vectors (layering/blending make them
			// ill-defined), so drop the velocity attachment; transparent pixels keep the
			// opaque-behind velocity and fall back to camera reprojection in TAA.
			FramebufferSpecification transparentSpec = forwardSpec;
			transparentSpec.Attachments = {
				forwardSceneColor,
				forwardNormal,
				forwardMetalRough,
				ImageFormat::DEPTH32FSTENCIL8UINT
			};
			transparentSpec.ExistingImages.erase(4);
			transparentSpec.ExistingImages[3] = m_PreDepthPass->GetDepthOutput();
			transparentSpec.Blend = true;
			transparentSpec.BlendMode = FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha;
			transparentSpec.DebugName = "TransparentForward";

			pipelineSpec.DebugName = "TransparentForward";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("HazelPBR_Transparent");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(transparentSpec);
			pipelineSpec.DepthOperator = DepthCompareOperator::GreaterOrEqual;
			pipelineSpec.DepthWrite = false;
			m_TransparentGeometryPipeline = Pipeline::Create(pipelineSpec);

			rpSpec.DebugName = "TransparentForwardPass";
			rpSpec.Pipeline = m_TransparentGeometryPipeline;
			m_GeometryPassTransparent = RenderPass::Create(rpSpec);
			BindSceneRenderPassInputs(m_GeometryPassTransparent, PassInputPBRLighting | PassInputMaterialScene | PassInputDepth);
			LUX_CORE_VERIFY(m_GeometryPassTransparent->Validate());
			m_GeometryPassTransparent->Bake();

			FramebufferSpecification deferredSpec;
			deferredSpec.Width = m_ViewportWidth;
			deferredSpec.Height = m_ViewportHeight;
			deferredSpec.Attachments = { ImageFormat::RGBA16F };
			deferredSpec.ExistingImages[0] = m_SceneColorFramebuffer->GetImage(0);
			deferredSpec.ClearColorOnLoad = false;
			deferredSpec.Blend = false;
			deferredSpec.DebugName = "DeferredLighting";

			PipelineSpecification deferredPipelineSpec;
			deferredPipelineSpec.DebugName = "DeferredLighting";
			deferredPipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("DeferredLighting");
			deferredPipelineSpec.TargetFramebuffer = Framebuffer::Create(deferredSpec);
			deferredPipelineSpec.DepthTest = false;
			deferredPipelineSpec.DepthWrite = false;
			deferredPipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" },
			};
			m_DeferredLightingPipeline = Pipeline::Create(deferredPipelineSpec);

			rpSpec.DebugName = "DeferredLightingPass";
			rpSpec.Pipeline = m_DeferredLightingPipeline;
			m_DeferredLightingPass = RenderPass::Create(rpSpec);
			BindSceneRenderPassInputs(m_DeferredLightingPass, PassInputPBRLighting | PassInputMaterialScene | PassInputDepth | PassInputGBuffer | PassInputSceneColor);
			m_DeferredLightingPass->SetInput("u_DepthTexture", m_PreDepthPass->GetDepthOutput());
			m_DeferredLightingPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_DeferredLightingPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_DeferredLightingPass->Validate());
			m_DeferredLightingPass->Bake();
			m_DeferredLightingMaterial = Material::Create(deferredPipelineSpec.Shader, "DeferredLighting");

			FramebufferSpecification debugSpec;
			debugSpec.Width = m_ViewportWidth;
			debugSpec.Height = m_ViewportHeight;
			debugSpec.Attachments = { ImageFormat::RGBA16F };
			debugSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			debugSpec.DebugName = "GBufferDebug";

			PipelineSpecification debugPipelineSpec = deferredPipelineSpec;
			debugPipelineSpec.DebugName = "GBufferDebug";
			debugPipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("GBufferDebug");
			debugPipelineSpec.TargetFramebuffer = Framebuffer::Create(debugSpec);

			rpSpec.DebugName = "GBufferDebugPass";
			rpSpec.Pipeline = Pipeline::Create(debugPipelineSpec);
			m_GBufferDebugPass = RenderPass::Create(rpSpec);
			BindSceneRenderPassInputs(m_GBufferDebugPass, PassInputCommonScene | PassInputGBuffer | PassInputMaterialScene);
			m_GBufferDebugPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			LUX_CORE_VERIFY(m_GBufferDebugPass->Validate());
			m_GBufferDebugPass->Bake();
			m_GBufferDebugMaterial = Material::Create(debugPipelineSpec.Shader, "GBufferDebug");
			m_GBufferDebugMaterial->Set("u_Uniforms.Mode", 0u);
		}

		// ── GTAO + AO composite ───────────────────────────────────────────────
		{
			ImageSpecification imageSpec;
			imageSpec.Format = ImageFormat::RED32UI;
			imageSpec.Usage = ImageUsage::Storage;
			imageSpec.DebugName = "GTAO";
			m_GTAOOutputImage = Image2D::Create(imageSpec);

			imageSpec.DebugName = "GTAO-Denoise";
			m_GTAODenoiseImage = Image2D::Create(imageSpec);

			imageSpec.Format = ImageFormat::RED8UN;
			imageSpec.DebugName = "GTAO-Edges";
			m_GTAOEdgesOutputImage = Image2D::Create(imageSpec);

			imageSpec.Format = ImageFormat::RED32UI;
			imageSpec.DebugName = "GTAO-History-A";
			m_GTAOHistoryImages[0] = Image2D::Create(imageSpec);
			imageSpec.DebugName = "GTAO-History-B";
			m_GTAOHistoryImages[1] = Image2D::Create(imageSpec);

			Ref<Shader> gtaoShader = Renderer::GetShaderLibrary()->Get("GTAO");
			ComputePassSpecification gtaoSpec;
			gtaoSpec.DebugName = "GTAO-ComputePass";
			gtaoSpec.Pipeline = PipelineCompute::Create(gtaoShader);
			m_GTAOComputePass = ComputePass::Create(gtaoSpec);
			m_GTAOComputePass->SetInput("u_HiZDepth", m_HierarchicalDepthTexture.Texture);
			m_GTAOComputePass->SetInput("u_HilbertLut", Renderer::GetHilbertLut());
			m_GTAOComputePass->SetInput("u_ViewNormal", GetGeometryNormalOutput());
			m_GTAOComputePass->SetInput("o_AOwBentNormals", m_GTAOOutputImage);
			m_GTAOComputePass->SetInput("o_Edges", m_GTAOEdgesOutputImage);
			m_GTAOComputePass->SetInput("u_samplerPointClamp", Renderer::GetPointSampler());
			m_GTAOComputePass->SetInput("Camera", m_UBSCamera);
			m_GTAOComputePass->SetInput("ScreenData", m_UBSScreenData);
			LUX_CORE_VERIFY(m_GTAOComputePass->Validate());
			m_GTAOComputePass->Bake();

			Ref<Shader> denoiseShader = Renderer::GetShaderLibrary()->Get("GTAO-Denoise");
			m_GTAODenoiseMaterial[0] = Material::Create(denoiseShader, "GTAO-Denoise-Ping");
			m_GTAODenoiseMaterial[1] = Material::Create(denoiseShader, "GTAO-Denoise-Pong");

			ComputePassSpecification denoiseSpec;
			denoiseSpec.DebugName = "GTAO-Denoise";
			denoiseSpec.Pipeline = PipelineCompute::Create(denoiseShader);
			m_GTAODenoisePass[0] = ComputePass::Create(denoiseSpec);
			m_GTAODenoisePass[0]->SetInput("u_Edges", m_GTAOEdgesOutputImage);
			m_GTAODenoisePass[0]->SetInput("u_AOTerm", m_GTAOOutputImage);
			m_GTAODenoisePass[0]->SetInput("o_AOTerm", m_GTAODenoiseImage);
			m_GTAODenoisePass[0]->SetInput("ScreenData", m_UBSScreenData);
			m_GTAODenoisePass[0]->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_GTAODenoisePass[0]->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_GTAODenoisePass[0]->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_GTAODenoisePass[0]->Validate());
			m_GTAODenoisePass[0]->Bake();

			m_GTAODenoisePass[1] = ComputePass::Create(denoiseSpec);
			m_GTAODenoisePass[1]->SetInput("u_Edges", m_GTAOEdgesOutputImage);
			m_GTAODenoisePass[1]->SetInput("u_AOTerm", m_GTAODenoiseImage);
			m_GTAODenoisePass[1]->SetInput("o_AOTerm", m_GTAOOutputImage);
			m_GTAODenoisePass[1]->SetInput("ScreenData", m_UBSScreenData);
			m_GTAODenoisePass[1]->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_GTAODenoisePass[1]->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_GTAODenoisePass[1]->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_GTAODenoisePass[1]->Validate());
			m_GTAODenoisePass[1]->Bake();

			m_GTAOFinalImage = (m_Options.GTAODenoisePasses % 2 != 0) ? m_GTAODenoiseImage : m_GTAOOutputImage;

			Ref<Shader> gtaoTemporalShader = Renderer::GetShaderLibrary()->Get("GTAO-Temporal");
			ComputePassSpecification gtaoTemporalSpec;
			gtaoTemporalSpec.DebugName = "GTAO-Temporal";
			gtaoTemporalSpec.Pipeline = PipelineCompute::Create(gtaoTemporalShader);
			m_GTAOTemporalPass = ComputePass::Create(gtaoTemporalSpec);
			m_GTAOTemporalPass->SetInput("u_CurrentAO", m_GTAOFinalImage);
			m_GTAOTemporalPass->SetInput("u_HistoryAO", m_GTAOHistoryImages[0]);
			m_GTAOTemporalPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_GTAOTemporalPass->SetInput("o_HistoryAO", m_GTAOHistoryImages[1]);
			m_GTAOTemporalPass->SetInput("Camera", m_UBSCamera);
			m_GTAOTemporalPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_GTAOTemporalPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_GTAOTemporalPass->Validate());
			m_GTAOTemporalPass->Bake();

			FramebufferSpecification aoFramebufferSpec;
			aoFramebufferSpec.Width = m_ViewportWidth;
			aoFramebufferSpec.Height = m_ViewportHeight;
			aoFramebufferSpec.Attachments = { ImageFormat::RGBA16F };
			aoFramebufferSpec.ExistingImages[0] = GetSceneColorOutput();
			aoFramebufferSpec.ClearColorOnLoad = false;
			aoFramebufferSpec.Blend = true;
			aoFramebufferSpec.BlendMode = FramebufferBlendMode::Zero_SrcColor;
			aoFramebufferSpec.DebugName = "AO-Composite";

			PipelineSpecification aoPipelineSpec;
			aoPipelineSpec.DebugName = "AO-Composite";
			aoPipelineSpec.TargetFramebuffer = Framebuffer::Create(aoFramebufferSpec);
			aoPipelineSpec.DepthTest = false;
			aoPipelineSpec.DepthWrite = false;
			aoPipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" },
			};
			aoPipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("AO-Composite");

			RenderPassSpecification aoRenderPassSpec;
			aoRenderPassSpec.DebugName = "AO-Composite";
			aoRenderPassSpec.Pipeline = Pipeline::Create(aoPipelineSpec);
			m_AOCompositePass = RenderPass::Create(aoRenderPassSpec);
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
			m_AOCompositePass->SetInput("Camera", m_UBSCamera);
			m_AOCompositePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_AOCompositePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_AOCompositePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_AOCompositePass->Validate());
			m_AOCompositePass->Bake();
			m_AOCompositeMaterial = Material::Create(aoPipelineSpec.Shader, "GTAO-Composite");

			FramebufferSpecification aoDebugFramebufferSpec;
			aoDebugFramebufferSpec.Width = m_ViewportWidth;
			aoDebugFramebufferSpec.Height = m_ViewportHeight;
			aoDebugFramebufferSpec.Attachments = { ImageFormat::RGBA32F };
			aoDebugFramebufferSpec.ClearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
			aoDebugFramebufferSpec.DebugName = "AO-Debug";

			PipelineSpecification aoDebugPipelineSpec = aoPipelineSpec;
			aoDebugPipelineSpec.DebugName = "AO-Debug";
			aoDebugPipelineSpec.TargetFramebuffer = Framebuffer::Create(aoDebugFramebufferSpec);

			RenderPassSpecification aoDebugRenderPassSpec;
			aoDebugRenderPassSpec.DebugName = "AO-Debug";
			aoDebugRenderPassSpec.Pipeline = Pipeline::Create(aoDebugPipelineSpec);
			m_AODebugPass = RenderPass::Create(aoDebugRenderPassSpec);
			m_AODebugPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_AODebugPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_AODebugPass->SetInput("u_Normal", GetGeometryNormalOutput());
			m_AODebugPass->SetInput("Camera", m_UBSCamera);
			m_AODebugPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_AODebugPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_AODebugPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_AODebugPass->Validate());
			m_AODebugPass->Bake();
			m_AODebugMaterial = Material::Create(aoPipelineSpec.Shader, "AO-Debug");
		}

		// ── SSR ────────────────────────────────────────────────────────────────
		{
			ImageSpecification ssrImageSpec;
			ssrImageSpec.Format = ImageFormat::RGBA16F;
			ssrImageSpec.Usage = ImageUsage::Storage;
			ssrImageSpec.DebugName = "SSR";
			m_SSRImage = Image2D::Create(ssrImageSpec);
			ssrImageSpec.DebugName = "SSR-History-A";
			m_SSRHistoryImages[0] = Image2D::Create(ssrImageSpec);
			ssrImageSpec.DebugName = "SSR-History-B";
			m_SSRHistoryImages[1] = Image2D::Create(ssrImageSpec);
			m_SSRFinalImage = m_SSRImage;

			ComputePassSpecification ssrComputeSpec;
			ssrComputeSpec.DebugName = "SSR-Compute";
			ssrComputeSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("SSR"));
			m_SSRPass = ComputePass::Create(ssrComputeSpec);
			m_SSRPass->SetInput("outColor", m_SSRImage);
			m_SSRPass->SetInput("u_InputColor", m_PreConvolutedTexture.Texture);
			m_SSRPass->SetInput("u_Normal", GetGeometryNormalOutput());
			m_SSRPass->SetInput("u_HiZBuffer", m_HierarchicalDepthTexture.Texture);
			m_SSRPass->SetInput("u_MetalnessRoughness", GetGeometryMetalRoughOutput());
			m_SSRPass->SetInput("u_VisibilityBuffer", m_PreIntegrationVisibilityTexture.Texture);
			if (m_SSRPass->IsInputValid("u_GTAOTex"))
				m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_SSRPass->SetInput("Camera", m_UBSCamera);
			m_SSRPass->SetInput("ScreenData", m_UBSScreenData);
			m_SSRPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_SSRPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_SSRPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_SSRPass->Validate());
			m_SSRPass->Bake();

			ComputePassSpecification ssrTemporalSpec;
			ssrTemporalSpec.DebugName = "SSR-Temporal";
			ssrTemporalSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("SSR-Temporal"));
			m_SSRTemporalPass = ComputePass::Create(ssrTemporalSpec);
			m_SSRTemporalPass->SetInput("u_CurrentSSR", m_SSRImage);
			m_SSRTemporalPass->SetInput("u_HistorySSR", m_SSRHistoryImages[0]);
			m_SSRTemporalPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_SSRTemporalPass->SetInput("o_HistorySSR", m_SSRHistoryImages[1]);
			m_SSRTemporalPass->SetInput("Camera", m_UBSCamera);
			m_SSRTemporalPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_SSRTemporalPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_SSRTemporalPass->Validate());
			m_SSRTemporalPass->Bake();

			FramebufferSpecification ssrCompositeFBSpec;
			ssrCompositeFBSpec.Width = m_ViewportWidth;
			ssrCompositeFBSpec.Height = m_ViewportHeight;
			ssrCompositeFBSpec.Attachments = { ImageFormat::RGBA16F };
			ssrCompositeFBSpec.ExistingImages[0] = GetSceneColorOutput();
			ssrCompositeFBSpec.ClearColorOnLoad = false;
			ssrCompositeFBSpec.Blend = true;
			ssrCompositeFBSpec.BlendMode = FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha;
			ssrCompositeFBSpec.DebugName = "SSR-Composite";

			PipelineSpecification ssrCompositePipelineSpec;
			ssrCompositePipelineSpec.DebugName = "SSR-Composite";
			ssrCompositePipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("SSR-Composite");
			ssrCompositePipelineSpec.TargetFramebuffer = Framebuffer::Create(ssrCompositeFBSpec);
			ssrCompositePipelineSpec.DepthTest = false;
			ssrCompositePipelineSpec.DepthWrite = false;
			ssrCompositePipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" },
			};

			RenderPassSpecification ssrRenderPassSpec;
			ssrRenderPassSpec.DebugName = "SSR-Composite";
			ssrRenderPassSpec.Pipeline = Pipeline::Create(ssrCompositePipelineSpec);
			m_SSRCompositePass = RenderPass::Create(ssrRenderPassSpec);
			m_SSRCompositePass->SetInput("u_SSR", m_SSRImage);
			m_SSRCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_SSRCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
			m_SSRCompositePass->SetInput("Camera", m_UBSCamera);
			m_SSRCompositePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_SSRCompositePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_SSRCompositePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_SSRCompositePass->Validate());
			m_SSRCompositePass->Bake();
			m_SSRCompositeMaterial = Material::Create(ssrCompositePipelineSpec.Shader, "SSR-Composite");
		}

		// ── Selected geometry (isolation for outline) ─────────────────────────
		{
			FramebufferTextureSpecification selectedMaskAttachment = ImageFormat::RGBA32F;
			selectedMaskAttachment.Blend = false;

			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { selectedMaskAttachment, ImageFormat::Depth };
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			fbSpec.DepthClearValue = 0.0f;
			fbSpec.DebugName = "SelectedGeometry";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "SelectedGeometry";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("SelectedGeometry");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::GreaterOrEqual;

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "SelectedGeometryPass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_SelectedGeometryPass = RenderPass::Create(rpSpec);
			m_SelectedGeometryPass->SetInput("Camera", m_UBSCamera);
			m_SelectedGeometryPass->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
			m_SelectedGeometryPass->SetInput("ObjectIndexes", m_SBSVisibleObjectIndexes);
			LUX_CORE_VERIFY(m_SelectedGeometryPass->Validate());
			m_SelectedGeometryPass->Bake();

			m_SelectedGeometryMaterial = Material::Create(pipelineSpec.Shader, "SelectedGeometry");
		}

		// ── Jump flood outline buffers ────────────────────────────────────────
		{
			FramebufferTextureSpecification jumpFloodAttachment = ImageFormat::RGBA32F;
			jumpFloodAttachment.Blend = false;

			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { jumpFloodAttachment };
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			fbSpec.DebugName = "JumpFlood-Init";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "JumpFlood-Init";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("JumpFlood_Init");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.DepthTest = false;
			pipelineSpec.DepthWrite = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" },
			};

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "JumpFlood-Init";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_JumpFloodInitPass = RenderPass::Create(rpSpec);
			m_JumpFloodInitPass->SetInput("u_Texture", m_SelectedGeometryPass->GetOutput(0));
			m_JumpFloodInitPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_JumpFloodInitPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_JumpFloodInitPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_JumpFloodInitPass->Validate());
			m_JumpFloodInitPass->Bake();
			m_JumpFloodInitMaterial = Material::Create(pipelineSpec.Shader, "JumpFlood-Init");

			for (uint32_t i = 0; i < 2; i++)
			{
				fbSpec.DebugName = "JumpFlood-Pass" + std::to_string(i);
				pipelineSpec.DebugName = "JumpFlood-Pass" + std::to_string(i);
				pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("JumpFlood_Pass");
				pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);

				rpSpec.DebugName = pipelineSpec.DebugName;
				rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
				m_JumpFloodPasses[i] = RenderPass::Create(rpSpec);
				m_JumpFloodPasses[i]->SetInput("u_Texture", i == 0 ? m_JumpFloodInitPass->GetOutput(0) : m_JumpFloodPasses[0]->GetOutput(0));
				m_JumpFloodPasses[i]->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
				m_JumpFloodPasses[i]->SetInput("r_PointSampler", Renderer::GetPointSampler());
				m_JumpFloodPasses[i]->SetInput("r_LinearSampler", Renderer::GetClampSampler());
				LUX_CORE_VERIFY(m_JumpFloodPasses[i]->Validate());
				m_JumpFloodPasses[i]->Bake();
				m_JumpFloodPassMaterials[i] = Material::Create(pipelineSpec.Shader, pipelineSpec.DebugName);
			}
		}

		// ── Wireframe pass (on top of geometry, for selected meshes) ──────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.ExistingImages[0] = GetSceneColorOutput();
			fbSpec.Attachments = { ImageFormat::RGBA16F };
			fbSpec.ClearColorOnLoad = false;
			fbSpec.DebugName = "GeometryWireframe";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "GeometryWireframe";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("Wireframe");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.Wireframe = true;
			pipelineSpec.DepthTest = false;

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "GeometryWireframePass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_GeometryWireframePass = RenderPass::Create(rpSpec);
			m_GeometryWireframePass->SetInput("Camera", m_UBSCamera);
			m_GeometryWireframePass->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
			m_GeometryWireframePass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_GeometryWireframePass->Validate());
			m_GeometryWireframePass->Bake();

			m_WireframeMaterial = Material::Create(pipelineSpec.Shader, "Wireframe");
			m_WireframeMaterial->Set("u_MaterialUniforms.Color", glm::vec4{ 1.0f, 0.5f, 0.0f, 1.0f });
		}

		// ── Skybox (renders into the HDR scene color before geometry lighting) ─
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.ExistingImages[0] = GetSceneColorOutput();
			fbSpec.Attachments = { ImageFormat::RGBA16F };
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			fbSpec.DebugName = "Skybox";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "Skybox";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("Skybox");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.DepthWrite = false;
			pipelineSpec.DepthTest = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};
			m_SkyboxPipeline = Pipeline::Create(pipelineSpec);
			m_SkyboxMaterial = Material::Create(pipelineSpec.Shader, "Skybox");
			m_SkyboxMaterial->SetFlag(MaterialFlag::DepthTest, false);

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "SkyboxPass";
			rpSpec.Pipeline = m_SkyboxPipeline;
			m_SkyboxPass = RenderPass::Create(rpSpec);
			m_SkyboxPass->SetInput("Camera", m_UBSCamera);
			LUX_CORE_VERIFY(m_SkyboxPass->Validate());
			m_SkyboxPass->Bake();
		}

		// ── Atmosphere, procedural clouds and fog ────────────────────────────
		{
			auto createFullscreenPass = [&](const char* debugName, const char* shaderName, const FramebufferSpecification& framebufferSpec, bool bindDepth, const std::function<void(const Ref<RenderPass>&)>& bindExtra)
			{
				PipelineSpecification pipelineSpec;
				pipelineSpec.DebugName = debugName;
				pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get(shaderName);
				pipelineSpec.TargetFramebuffer = Framebuffer::Create(framebufferSpec);
				pipelineSpec.DepthWrite = false;
				pipelineSpec.DepthTest = false;
				pipelineSpec.Layout = {
					{ ShaderDataType::Float3, "a_Position" },
					{ ShaderDataType::Float2, "a_TexCoord" }
				};

				Ref<Pipeline> pipeline = Pipeline::Create(pipelineSpec);

				RenderPassSpecification rpSpec;
				rpSpec.DebugName = debugName;
				rpSpec.Pipeline = pipeline;
				Ref<RenderPass> pass = RenderPass::Create(rpSpec);
				BindCommonSceneRenderPassInputs(pass, bindDepth);
				if (bindExtra)
					bindExtra(pass);
				LUX_CORE_VERIFY(pass->Validate());
				pass->Bake();

				return std::pair<Ref<Pipeline>, Ref<RenderPass>>{ pipeline, pass };
			};

			auto createSceneColorFramebufferSpec = [&](const char* debugName, bool enableBlending)
			{
				FramebufferSpecification fbSpec;
				fbSpec.Width = m_ViewportWidth;
				fbSpec.Height = m_ViewportHeight;
				fbSpec.ExistingImages[0] = GetSceneColorOutput();
				fbSpec.Attachments = { ImageFormat::RGBA16F };
				fbSpec.ClearColorOnLoad = false;
				fbSpec.ClearDepthOnLoad = false;
				fbSpec.Blend = enableBlending;
				fbSpec.BlendMode = enableBlending ? FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha : FramebufferBlendMode::OneZero;
				fbSpec.DebugName = debugName;
				return fbSpec;
			};

			auto [skyPipeline, skyPass] = createFullscreenPass("SkyAtmosphere", "SkyAtmosphere", createSceneColorFramebufferSpec("SkyAtmosphere", false), false, {});
			m_SkyAtmospherePipeline = skyPipeline;
			m_SkyAtmospherePass = skyPass;
			m_SkyAtmosphereMaterial = Material::Create(skyPipeline->GetShader(), "SkyAtmosphere");

			m_CloudRenderScale = SanitizeCloudRenderScale(ResolveFrameEnvironment().Atmosphere.VolumetricClouds.RenderScale);
			m_CloudRenderSize = CalculateVolumetricCloudRenderSize();

			// Baked tileable 3D noise volumes (generated once on the first frame).
			{
				auto createNoiseVolume = [](const char* debugName, uint32_t size)
				{
					ImageSpecification spec;
					spec.DebugName = debugName;
					spec.Dimension = nvrhi::TextureDimension::Texture3D;
					spec.Format = ImageFormat::RGBA16F;
					spec.Usage = ImageUsage::Storage;
					spec.Width = size;
					spec.Height = size;
					spec.Depth = size;
					spec.Mips = 1;
					spec.Layers = 1;
					spec.CreateSampler = false;
					Ref<Image2D> image = Image2D::Create(spec);
					image->Invalidate();
					return image;
				};
				m_CloudBaseShapeVolume = createNoiseVolume("CloudBaseShape", 128);
				m_CloudDetailVolume = createNoiseVolume("CloudDetail", 32);
				m_CloudCurlVolume = createNoiseVolume("CloudCurl", 32);

				ComputePassSpecification baseBakeSpec;
				baseBakeSpec.DebugName = "CloudNoiseBaseShapeBake";
				baseBakeSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("CloudNoiseBaseShape"));
				m_CloudBaseShapeBakePass = ComputePass::Create(baseBakeSpec);
				m_CloudBaseShapeBakePass->SetInput("o_NoiseVolume", m_CloudBaseShapeVolume);

				ComputePassSpecification detailBakeSpec;
				detailBakeSpec.DebugName = "CloudNoiseDetailBake";
				detailBakeSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("CloudNoiseDetail"));
				m_CloudDetailBakePass = ComputePass::Create(detailBakeSpec);
				m_CloudDetailBakePass->SetInput("o_NoiseVolume", m_CloudDetailVolume);

				ComputePassSpecification curlBakeSpec;
				curlBakeSpec.DebugName = "CloudNoiseCurlBake";
				curlBakeSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("CloudNoiseCurl"));
				m_CloudCurlBakePass = ComputePass::Create(curlBakeSpec);
				m_CloudCurlBakePass->SetInput("o_NoiseVolume", m_CloudCurlVolume);

				if (m_CloudBaseShapeBakePass->Validate())
					m_CloudBaseShapeBakePass->Bake();
				if (m_CloudDetailBakePass->Validate())
					m_CloudDetailBakePass->Bake();
				if (m_CloudCurlBakePass->Validate())
					m_CloudCurlBakePass->Bake();
				m_CloudNoiseBaked = false;
			}

			FramebufferSpecification cloudSpec;
			cloudSpec.Width = m_CloudRenderSize.x;
			cloudSpec.Height = m_CloudRenderSize.y;
			cloudSpec.Attachments = {
				ImageFormat::RGBA16F, // cloud color + transmittance
				ImageFormat::RGBA16F  // front depth, scene depth, trace min/max
			};
			cloudSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			cloudSpec.ClearColorOnLoad = true;
			cloudSpec.ClearDepthOnLoad = false;
			cloudSpec.Blend = false;
			cloudSpec.BlendMode = FramebufferBlendMode::OneZero;
			cloudSpec.DebugName = "VolumetricClouds";

			auto [cloudPipeline, cloudPass] = createFullscreenPass("VolumetricClouds", "VolumetricClouds", cloudSpec, true,
				[&](const Ref<RenderPass>& pass)
				{
					SetRenderPassInputIfValid(pass, "u_CloudBaseShape", m_CloudBaseShapeVolume);
					SetRenderPassInputIfValid(pass, "u_CloudDetail", m_CloudDetailVolume);
					SetRenderPassInputIfValid(pass, "u_CloudCurl", m_CloudCurlVolume);
				});
			m_VolumetricCloudPipeline = cloudPipeline;
			m_VolumetricCloudPass = cloudPass;
			m_VolumetricCloudMaterial = Material::Create(cloudPipeline->GetShader(), "VolumetricClouds");

			// Temporal scattering integration: half-res history (ping-pong) + resolve pass.
			{
				auto createCloudHistory = [&](const char* debugName)
				{
					ImageSpecification spec;
					spec.DebugName = debugName;
					spec.Format = ImageFormat::RGBA16F;
					spec.Usage = ImageUsage::Storage;
					spec.Width = glm::max(m_CloudRenderSize.x, 1u);
					spec.Height = glm::max(m_CloudRenderSize.y, 1u);
					Ref<Image2D> image = Image2D::Create(spec);
					image->Invalidate();
					return image;
				};
				m_CloudHistoryImages[0] = createCloudHistory("CloudHistory-A");
				m_CloudHistoryImages[1] = createCloudHistory("CloudHistory-B");

				ComputePassSpecification temporalSpec;
				temporalSpec.DebugName = "VolumetricCloudTemporal";
				temporalSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("VolumetricCloudTemporal"));
				m_VolumetricCloudTemporalPass = ComputePass::Create(temporalSpec);
				m_VolumetricCloudTemporalPass->SetInput("u_CurrentCloud", m_VolumetricCloudPass->GetOutput(0));
				m_VolumetricCloudTemporalPass->SetInput("u_CurrentCloudDepth", m_VolumetricCloudPass->GetOutput(1));
				m_VolumetricCloudTemporalPass->SetInput("u_HistoryCloud", m_CloudHistoryImages[0]);
				m_VolumetricCloudTemporalPass->SetInput("o_ResolvedCloud", m_CloudHistoryImages[1]);
				m_VolumetricCloudTemporalPass->SetInput("Camera", m_UBSCamera);
				m_VolumetricCloudTemporalPass->SetInput("SceneData", m_UBSScene);
				m_VolumetricCloudTemporalPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
				m_VolumetricCloudTemporalPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
				if (m_VolumetricCloudTemporalPass->Validate())
					m_VolumetricCloudTemporalPass->Bake();
				m_CloudHistoryValid = false;
			}

			auto [cloudCompositePipeline, cloudCompositePass] = createFullscreenPass("VolumetricCloudComposite", "VolumetricCloudComposite", createSceneColorFramebufferSpec("VolumetricCloudComposite", true), true,
				[&](const Ref<RenderPass>& pass)
				{
					SetRenderPassInputIfValid(pass, "u_CloudTexture", m_CloudHistoryImages[1]);
					SetRenderPassInputIfValid(pass, "u_CloudDepthTexture", m_VolumetricCloudPass->GetOutput(1));
				});
			m_VolumetricCloudCompositePipeline = cloudCompositePipeline;
			m_VolumetricCloudCompositePass = cloudCompositePass;
			m_VolumetricCloudCompositeMaterial = Material::Create(cloudCompositePipeline->GetShader(), "VolumetricCloudComposite");

			auto [fogPipeline, fogPass] = createFullscreenPass("AtmosphericFog", "AtmosphericFog", createSceneColorFramebufferSpec("AtmosphericFog", true), true,
				[&](const Ref<RenderPass>& pass)
				{
					// Volumetric fog now scatters clustered point/spot lights (FogClusterLights.glslh);
					// the cluster lists + light UBOs already come from PassInputCommonScene, this adds
					// the spot shadow atlas so the in-fog spot shafts are shadowed.
					BindSceneRenderPassInputs(pass, PassInputShadowMaps);
				});
			m_AtmosphericFogPipeline = fogPipeline;
			m_AtmosphericFogPass = fogPass;
			m_AtmosphericFogMaterial = Material::Create(fogPipeline->GetShader(), "AtmosphericFog");
		}

		// ── Bloom compute (feeds the scene composite) ─────────────────────────
		{
			Ref<Shader> shader = Renderer::GetShaderLibrary()->Get("Bloom");
			m_BloomComputePipeline = PipelineCompute::Create(shader);

			TextureSpecification spec;
			spec.Format = ImageFormat::RGBA32F;
			spec.Width = 1;
			spec.Height = 1;
			spec.SamplerWrap = TextureWrap::Clamp;
			spec.Storage = true;
			spec.GenerateMips = true;

			for (uint32_t i = 0; i < (uint32_t)m_BloomComputeTextures.size(); i++)
			{
				spec.DebugName = "BloomCompute-" + std::to_string(i);
				m_BloomComputeTextures[i].Texture = Texture2D::Create(spec);
			}

			ComputePassSpecification computeSpec;
			computeSpec.DebugName = "Bloom-Compute";
			computeSpec.Pipeline = m_BloomComputePipeline;
			m_BloomComputePass = ComputePass::Create(computeSpec);
			m_BloomComputePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_BloomComputePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_BloomComputePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_BloomComputePass->Validate());
			m_BloomComputePass->Bake();

			m_BloomDirtTexture = Renderer::GetBlackTexture();
			ResizeBloomResources();
			CreateBloomPassMaterials();
		}

		// ── Histogram auto-exposure compute passes ────────────────────────────
		{
			ComputePassSpecification histogramSpec;
			histogramSpec.DebugName = "LuminanceHistogram";
			histogramSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("LuminanceHistogram"));
			m_LuminanceHistogramPass = ComputePass::Create(histogramSpec);
			m_LuminanceHistogramPass->SetInput("u_SceneColor", GetSceneColorOutput());
			m_LuminanceHistogramPass->SetInput("LuminanceHistogramBuffer", m_SBSLuminanceHistogram);
			m_LuminanceHistogramPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_LuminanceHistogramPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_LuminanceHistogramPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_LuminanceHistogramPass->Validate());
			m_LuminanceHistogramPass->Bake();

			ComputePassSpecification averageSpec;
			averageSpec.DebugName = "LuminanceAverage";
			averageSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("LuminanceAverage"));
			m_LuminanceAveragePass = ComputePass::Create(averageSpec);
			m_LuminanceAveragePass->SetInput("LuminanceHistogramBuffer", m_SBSLuminanceHistogram);
			m_LuminanceAveragePass->SetInput("ExposureStateBuffer", m_SBSExposureState);
			LUX_CORE_VERIFY(m_LuminanceAveragePass->Validate());
			m_LuminanceAveragePass->Bake();
		}

		// ── TAA resolve (history ping-pong, copied back into scene color) ──────
		{
			ImageSpecification taaSpec;
			taaSpec.Format = ImageFormat::RGBA16F;
			taaSpec.Usage = ImageUsage::Storage;
			taaSpec.DebugName = "TAA-History-A";
			m_TAAHistoryImages[0] = Image2D::Create(taaSpec);
			taaSpec.DebugName = "TAA-History-B";
			m_TAAHistoryImages[1] = Image2D::Create(taaSpec);

			ComputePassSpecification taaPassSpec;
			taaPassSpec.DebugName = "TAA";
			taaPassSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("TAA"));
			m_TAAResolvePass = ComputePass::Create(taaPassSpec);
			m_TAAResolvePass->SetInput("u_SceneColor", GetSceneColorOutput());
			m_TAAResolvePass->SetInput("u_History", m_TAAHistoryImages[0]);
			m_TAAResolvePass->SetInput("u_Velocity", GetGeometryVelocityOutput());
			m_TAAResolvePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_TAAResolvePass->SetInput("o_Resolved", m_TAAHistoryImages[1]);
			m_TAAResolvePass->SetInput("Camera", m_UBSCamera);
			m_TAAResolvePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_TAAResolvePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_TAAResolvePass->Validate());
			m_TAAResolvePass->Bake();
		}

		// ── Scene composite (tone-map + exposure + opacity) ───────────────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::RGBA, ImageFormat::DEPTH32FSTENCIL8UINT };
			fbSpec.ExistingImages[1] = m_PreDepthPass->GetDepthOutput();
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			fbSpec.ClearColorOnLoad = false;
			fbSpec.ClearDepthOnLoad = false;
			fbSpec.DebugName = "SceneComposite";
			m_CompositingFramebuffer = Framebuffer::Create(fbSpec);

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "SceneComposite";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("SceneComposite");
			pipelineSpec.TargetFramebuffer = m_CompositingFramebuffer;
			pipelineSpec.DepthWrite = false;
			pipelineSpec.DepthTest = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			m_CompositeMaterial = Material::Create(pipelineSpec.Shader, "SceneComposite");
			m_CompositeMaterial->Set("u_Uniforms.Exposure", 1.0f);
			m_CompositeMaterial->Set("u_Uniforms.BloomIntensity", 0.0f);
			m_CompositeMaterial->Set("u_Uniforms.BloomDirtIntensity", 0.0f);
			m_CompositeMaterial->Set("u_Uniforms.Opacity", m_Opacity);
			m_CompositeMaterial->Set("u_Uniforms.Time", 0.0f);

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "CompositePass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_CompositePass = RenderPass::Create(rpSpec);
			// SceneColorHDR is produced by either deferred lighting or the forward fallback.
			m_CompositePass->SetInput("u_Texture", GetSceneColorOutput());
			m_CompositePass->SetInput("u_BloomTexture", m_BloomComputeTextures[2].Texture);
			m_CompositePass->SetInput("u_BloomDirtTexture", m_BloomDirtTexture);
			m_CompositePass->SetInput("u_DepthTexture", m_PreDepthPass->GetDepthOutput());
			m_CompositePass->SetInput("u_TransparentDepthTexture", m_GeometryPassTransparent->GetDepthOutput());
			m_CompositePass->SetInput("ExposureStateBuffer", m_SBSExposureState);
			LUX_CORE_VERIFY(m_CompositePass->Validate());
			m_CompositePass->Bake();
		}

		// ── Depth of field and jump-flood composite overlays ─────────────────
		{
			FramebufferSpecification dofFBSpec;
			dofFBSpec.Width = m_ViewportWidth;
			dofFBSpec.Height = m_ViewportHeight;
			dofFBSpec.Attachments = { ImageFormat::RGBA };
			dofFBSpec.ClearColorOnLoad = false;
			dofFBSpec.DebugName = "POST-DepthOfField";

			PipelineSpecification dofPipelineSpec;
			dofPipelineSpec.DebugName = "POST-DepthOfField";
			dofPipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("DOF");
			dofPipelineSpec.TargetFramebuffer = Framebuffer::Create(dofFBSpec);
			dofPipelineSpec.DepthTest = false;
			dofPipelineSpec.DepthWrite = false;
			dofPipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			RenderPassSpecification dofPassSpec;
			dofPassSpec.DebugName = "POST-DepthOfField";
			dofPassSpec.Pipeline = Pipeline::Create(dofPipelineSpec);
			m_DOFPass = RenderPass::Create(dofPassSpec);
			m_DOFPass->SetInput("u_Texture", m_CompositePass->GetOutput(0));
			m_DOFPass->SetInput("u_DepthTexture", m_PreDepthPass->GetDepthOutput());
			m_DOFPass->SetInput("Camera", m_UBSCamera);
			m_DOFPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_DOFPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_DOFPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_DOFPass->Validate());
			m_DOFPass->Bake();
			m_DOFMaterial = Material::Create(dofPipelineSpec.Shader, "DepthOfField");

			FramebufferSpecification jfCompositeFBSpec;
			jfCompositeFBSpec.Width = m_ViewportWidth;
			jfCompositeFBSpec.Height = m_ViewportHeight;
			jfCompositeFBSpec.Attachments = { ImageFormat::RGBA, ImageFormat::DEPTH32FSTENCIL8UINT };
			jfCompositeFBSpec.ExistingImages[0] = m_CompositingFramebuffer->GetImage(0);
			jfCompositeFBSpec.ExistingImages[1] = m_CompositingFramebuffer->GetDepthImage();
			jfCompositeFBSpec.ClearColorOnLoad = false;
			jfCompositeFBSpec.ClearDepthOnLoad = false;
			jfCompositeFBSpec.Blend = true;
			jfCompositeFBSpec.BlendMode = FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha;
			jfCompositeFBSpec.DebugName = "JumpFlood-Composite";

			PipelineSpecification jfCompositePipelineSpec;
			jfCompositePipelineSpec.DebugName = "JumpFlood-Composite";
			jfCompositePipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("JumpFlood_Composite");
			jfCompositePipelineSpec.TargetFramebuffer = Framebuffer::Create(jfCompositeFBSpec);
			jfCompositePipelineSpec.DepthTest = false;
			jfCompositePipelineSpec.DepthWrite = false;
			jfCompositePipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			RenderPassSpecification jfCompositePassSpec;
			jfCompositePassSpec.DebugName = "JumpFlood-Composite";
			jfCompositePassSpec.Pipeline = Pipeline::Create(jfCompositePipelineSpec);
			m_JumpFloodCompositePass = RenderPass::Create(jfCompositePassSpec);
			m_JumpFloodCompositePass->SetInput("u_Texture", m_JumpFloodPasses[0]->GetOutput(0));
			m_JumpFloodCompositePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_JumpFloodCompositePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_JumpFloodCompositePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_JumpFloodCompositePass->Validate());
			m_JumpFloodCompositePass->Bake();
			m_JumpFloodCompositeMaterial = Material::Create(jfCompositePipelineSpec.Shader, "JumpFlood-Composite");
		}

		// ── Editor grid (renders into composite output, preserves depth) ──────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.ExistingImages[0] = m_CompositingFramebuffer->GetImage(0);
			fbSpec.ExistingImages[1] = m_CompositingFramebuffer->GetDepthImage();
			fbSpec.Attachments = { ImageFormat::RGBA, ImageFormat::DEPTH32FSTENCIL8UINT };
			fbSpec.ClearColorOnLoad = false;
			fbSpec.ClearDepthOnLoad = false;
			fbSpec.DebugName = "Grid";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "Grid";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("Grid");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.DepthTest = true;
			pipelineSpec.DepthWrite = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "GridPass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_GridRenderPass = RenderPass::Create(rpSpec);
			m_GridRenderPass->SetInput("Camera", m_UBSCamera);
			LUX_CORE_VERIFY(m_GridRenderPass->Validate());
			m_GridRenderPass->Bake();

			constexpr float gridScale = 16.025f;
			constexpr float gridSize = 0.025f;
			m_GridMaterial = Material::Create(pipelineSpec.Shader, "Grid");
			const static glm::mat4 transform =
				glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f))
				* glm::scale(glm::mat4(1.0f), glm::vec3(8.0f));
			m_GridMaterial->Set("u_Settings.Transform", transform);
			m_GridMaterial->Set("u_Settings.Scale", gridScale);
			m_GridMaterial->Set("u_Settings.Size", gridSize);
		}

		// ── Physics collider debug materials ─────────────────────────────────
		{
			Ref<Shader> wireframeShader = Renderer::GetShaderLibrary()->Get("Wireframe");
			m_SimpleColliderMaterial = Material::Create(wireframeShader, "SimpleCollider");
			m_SimpleColliderMaterial->Set("u_MaterialUniforms.Color", glm::vec4{ 0.2f, 1.0f, 0.2f, 1.0f });
			m_ComplexColliderMaterial = Material::Create(wireframeShader, "ComplexCollider");
			m_ComplexColliderMaterial->Set("u_MaterialUniforms.Color", glm::vec4{ 0.5f, 0.5f, 1.0f, 1.0f });
		}

		ResizeScreenSpaceEffectResources();
		ApplyRenderTargetAliasing();

		// Signal render thread that GPU resources are ready
		Renderer::Submit([instance = Ref<SceneRenderer>(this)]() mutable {
			instance->m_ResourcesCreatedGPU = true;
			});
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Viewport resize
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		m_OutputViewportWidth = width;
		m_OutputViewportHeight = height;

		const float renderScale = ResolveRenderResolutionScale();
		width = glm::max(1u, (uint32_t)std::round((float)width * renderScale));
		height = glm::max(1u, (uint32_t)std::round((float)height * renderScale));

		if (m_ViewportWidth != width || m_ViewportHeight != height)
		{
			m_ViewportWidth = width;
			m_ViewportHeight = height;
			m_InvViewportWidth = width > 0 ? 1.0f / (float)width : 0.0f;
			m_InvViewportHeight = height > 0 ? 1.0f / (float)height : 0.0f;
			m_NeedsResize = true;
		}
	}

	void SceneRenderer::RefreshRenderResolutionScale()
	{
		if (m_OutputViewportWidth == 0 || m_OutputViewportHeight == 0)
			return;

		SetViewportSize(m_OutputViewportWidth, m_OutputViewportHeight);
	}

	void SceneRenderer::RefreshScreenSpaceEffectResources()
	{
		ResizeScreenSpaceEffectResources();
	}

	glm::uvec2 SceneRenderer::CalculateVolumetricCloudRenderSize() const
	{
		const glm::uvec2 viewportSize{ glm::max(1u, m_ViewportWidth), glm::max(1u, m_ViewportHeight) };
		return GetScaledExtent(viewportSize, CloudRenderScaleToEffectScale(ResolveFrameEnvironment().Atmosphere.VolumetricClouds.RenderScale));
	}

	void SceneRenderer::ResizeVolumetricCloudResources(bool forceRecreate)
	{
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		const uint32_t renderScale = SanitizeCloudRenderScale(ResolveFrameEnvironment().Atmosphere.VolumetricClouds.RenderScale);
		const glm::uvec2 cloudSize = CalculateVolumetricCloudRenderSize();
		const glm::uvec2 viewportSize{ glm::max(1u, m_ViewportWidth), glm::max(1u, m_ViewportHeight) };
		const bool cloudSizeChanged = forceRecreate || m_CloudRenderScale != renderScale || m_CloudRenderSize != cloudSize;

		if (m_VolumetricCloudPass && m_VolumetricCloudPass->GetTargetFramebuffer() && cloudSizeChanged)
			m_VolumetricCloudPass->GetTargetFramebuffer()->Resize(cloudSize.x, cloudSize.y, forceRecreate);

		if (m_VolumetricCloudCompositePass && m_VolumetricCloudCompositePass->GetTargetFramebuffer())
		{
			Ref<Framebuffer> framebuffer = m_VolumetricCloudCompositePass->GetTargetFramebuffer();
			if (forceRecreate || framebuffer->GetWidth() != viewportSize.x || framebuffer->GetHeight() != viewportSize.y)
				framebuffer->Resize(viewportSize.x, viewportSize.y, forceRecreate);
		}

		m_CloudRenderScale = renderScale;
		m_CloudRenderSize = cloudSize;

		if (cloudSizeChanged)
		{
			for (Ref<Image2D>& historyImage : m_CloudHistoryImages)
			{
				if (historyImage)
					historyImage->Resize(cloudSize.x, cloudSize.y);
			}
			m_CloudHistoryValid = false;
			if (m_VolumetricCloudTemporalPass && m_VolumetricCloudPass)
			{
				m_VolumetricCloudTemporalPass->SetInput("u_CurrentCloud", m_VolumetricCloudPass->GetOutput(0));
				m_VolumetricCloudTemporalPass->SetInput("u_CurrentCloudDepth", m_VolumetricCloudPass->GetOutput(1));
			}
		}

		if (m_VolumetricCloudCompositePass && m_VolumetricCloudPass)
		{
			SetRenderPassInputIfValid(m_VolumetricCloudCompositePass, "u_CloudTexture", m_VolumetricCloudPass->GetOutput(0));
			SetRenderPassInputIfValid(m_VolumetricCloudCompositePass, "u_CloudDepthTexture", m_VolumetricCloudPass->GetOutput(1));
		}
	}

	void SceneRenderer::BindCommonSceneRenderPassInputs(Ref<RenderPass> renderPass, bool bindDepth)
	{
		BindSceneRenderPassInputs(renderPass, PassInputCommonScene | (bindDepth ? PassInputDepth : PassInputNone));
	}

	void SceneRenderer::BindSceneRenderPassInputs(Ref<RenderPass> renderPass, uint32_t inputMask)
	{
		if (!renderPass)
			return;

		auto hasInput = [inputMask](SceneRenderPassInput input)
			{
				return (inputMask & input) != 0;
			};

		if (hasInput(PassInputCamera))
			SetRenderPassInputIfValid(renderPass, "Camera", m_UBSCamera);
		if (hasInput(PassInputScene))
			SetRenderPassInputIfValid(renderPass, "SceneData", m_UBSScene);
		if (hasInput(PassInputScreen))
			SetRenderPassInputIfValid(renderPass, "ScreenData", m_UBSScreenData);
		if (hasInput(PassInputRenderer))
			SetRenderPassInputIfValid(renderPass, "RendererData", m_UBSRendererData);
		if (hasInput(PassInputAtmosphere))
			SetRenderPassInputIfValid(renderPass, "AtmosphereData", m_UBSAtmosphere);
		if (hasInput(PassInputShadowData))
		{
			SetRenderPassInputIfValid(renderPass, "ShadowData", m_UBSShadow);
			SetRenderPassInputIfValid(renderPass, "SpotShadowData", m_UBSSpotShadow);
		}
		if (hasInput(PassInputLights))
		{
			SetRenderPassInputIfValid(renderPass, "PointLightData", m_UBSPointLights);
			SetRenderPassInputIfValid(renderPass, "SpotLightData", m_UBSSpotLights);
			// Clustered light lists (consumed by Lighting.glslh).
			SetRenderPassInputIfValid(renderPass, "PointLightGridBuffer", m_SBSPointLightGrid);
			SetRenderPassInputIfValid(renderPass, "SpotLightGridBuffer", m_SBSSpotLightGrid);
			SetRenderPassInputIfValid(renderPass, "PointLightIndexListBuffer", m_SBSPointLightIndexList);
			SetRenderPassInputIfValid(renderPass, "SpotLightIndexListBuffer", m_SBSSpotLightIndexList);
		}
		if (hasInput(PassInputSamplers))
		{
			SetRenderPassInputIfValid(renderPass, "r_DefaultSampler", Renderer::GetDefaultSampler());
			SetRenderPassInputIfValid(renderPass, "r_PointSampler", Renderer::GetPointSampler());
			SetRenderPassInputIfValid(renderPass, "r_LinearSampler", Renderer::GetClampSampler());
			SetRenderPassInputIfValid(renderPass, "r_RepeatSampler", Renderer::GetRepeatSampler());
		}
		if (hasInput(PassInputDepth) && m_PreDepthPass)
			SetRenderPassInputIfValid(renderPass, "u_DepthTexture", m_PreDepthPass->GetDepthOutput());
		if (hasInput(PassInputEnvironment))
		{
			SetRenderPassInputIfValid(renderPass, "u_EnvRadianceTex", Renderer::GetBlackCubeTexture());
			SetRenderPassInputIfValid(renderPass, "u_EnvIrradianceTex", Renderer::GetBlackCubeTexture());
			SetRenderPassInputIfValid(renderPass, "u_BRDFLUTTexture", Renderer::GetBRDFLutTexture());
		}
		if (hasInput(PassInputShadowMaps))
		{
			if (m_ShadowMapPass)
				SetRenderPassInputIfValid(renderPass, "u_ShadowMapTexture", m_ShadowMapPass->GetDepthOutput());
			SetRenderPassInputIfValid(renderPass, "u_SpotShadowTexture", m_SpotShadowMapImage);
		}
		if (hasInput(PassInputMaterialScene))
		{
			SetRenderPassInputIfValid(renderPass, "GPUSceneInstances", m_SBSGPUSceneInstances);
			SetRenderPassInputIfValid(renderPass, "GPUMaterials", m_SBSGPUMaterials);
			SetRenderPassInputIfValid(renderPass, "ObjectIndexes", m_SBSVisibleObjectIndexes);
			SetRenderPassInputIfValid(renderPass, "r_MaterialSampler", Renderer::GetRepeatSampler());
			if (renderPass->IsInputValid("u_GPUMaterialTextures"))
			{
				for (uint32_t textureIndex = 0; textureIndex < MaxGPUTextureSceneTextures; textureIndex++)
					renderPass->SetInput("u_GPUMaterialTextures", Renderer::GetWhiteTexture(), textureIndex);
			}
		}
		if (hasInput(PassInputGBuffer) && m_GeometryPass)
		{
			SetRenderPassInputIfValid(renderPass, "u_GBufferBaseColor", m_GeometryPass->GetOutput(0));
			SetRenderPassInputIfValid(renderPass, "u_GBufferNormal", m_GeometryPass->GetOutput(1));
			SetRenderPassInputIfValid(renderPass, "u_GBufferMetalRoughAO", m_GeometryPass->GetOutput(2));
			SetRenderPassInputIfValid(renderPass, "u_GBufferMaterialID", m_GeometryPass->GetOutput(3));
			SetRenderPassInputIfValid(renderPass, "u_GBufferObjectID", m_GeometryPass->GetOutput(4));
			SetRenderPassInputIfValid(renderPass, "u_DeferredLighting", GetSceneColorOutput());
		}
		if (hasInput(PassInputSceneColor))
			SetRenderPassInputIfValid(renderPass, "u_SceneColor", GetSceneColorOutput());
	}

	float SceneRenderer::GetRenderResolutionScale() const
	{
		return ResolveRenderResolutionScale();
	}

	float SceneRenderer::ResolveRenderResolutionScale() const
	{
		switch (m_Options.ResolutionScaleMode)
		{
			case SceneRendererOptions::RenderResolutionScaleMode::Scale75:
				return 0.75f;
			case SceneRendererOptions::RenderResolutionScaleMode::Scale50:
				return 0.50f;
			case SceneRendererOptions::RenderResolutionScaleMode::Dynamic:
				return std::clamp(m_Options.DynamicResolutionScale, m_Options.DynamicResolutionMinScale, m_Options.DynamicResolutionMaxScale);
			case SceneRendererOptions::RenderResolutionScaleMode::Native:
			default:
				return 1.0f;
		}
	}

	bool SceneRenderer::UpdateDynamicRenderResolution()
	{
		if (m_Options.ResolutionScaleMode != SceneRendererOptions::RenderResolutionScaleMode::Dynamic)
			return false;

		if (m_OutputViewportWidth == 0 || m_OutputViewportHeight == 0 || m_Statistics.TotalGPUTime <= 0.0f)
			return false;

		const float targetGPUTime = std::max(1.0f, m_Options.DynamicResolutionTargetGPUTime);
		const float minScale = std::clamp(m_Options.DynamicResolutionMinScale, 0.25f, 1.0f);
		const float maxScale = std::clamp(m_Options.DynamicResolutionMaxScale, minScale, 1.0f);
		const float previousScale = std::clamp(m_Options.DynamicResolutionScale, minScale, maxScale);
		float nextScale = previousScale;

		if (m_Statistics.TotalGPUTime > targetGPUTime * 1.08f)
			nextScale -= 0.05f;
		else if (m_Statistics.TotalGPUTime < targetGPUTime * 0.78f)
			nextScale += 0.05f;

		nextScale = std::clamp(nextScale, minScale, maxScale);
		if (std::abs(nextScale - previousScale) < 0.005f)
			return false;

		m_Options.DynamicResolutionScale = nextScale;
		RefreshRenderResolutionScale();
		return true;
	}

	void SceneRenderer::ResizeBloomResources()
	{
		if (!m_BloomComputePass || m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		glm::uvec2 bloomSize = GetScaledExtent({ glm::max(1u, m_ViewportWidth), glm::max(1u, m_ViewportHeight) }, m_BloomSettings.ResolutionScale);
		bloomSize.x = glm::max(m_BloomComputeWorkgroupSize, AlignUp(bloomSize.x, m_BloomComputeWorkgroupSize));
		bloomSize.y = glm::max(m_BloomComputeWorkgroupSize, AlignUp(bloomSize.y, m_BloomComputeWorkgroupSize));

		ImageViewSpecification imageViewSpec;
		imageViewSpec.MipCount = 1;

		for (uint32_t i = 0; i < (uint32_t)m_BloomComputeTextures.size(); i++)
		{
			auto& bloomTexture = m_BloomComputeTextures[i];
			bloomTexture.Texture->Resize(bloomSize);

			const uint32_t mipCount = bloomTexture.Texture->GetMipLevelCount();
			bloomTexture.ImageViews.resize(mipCount);

			imageViewSpec.Image = bloomTexture.Texture->GetImage();
			imageViewSpec.DebugName = "BloomCompute-" + std::to_string(i);

			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				imageViewSpec.Mip = mip;
				bloomTexture.ImageViews[mip] = ImageView::Create(imageViewSpec);
			}
		}
	}

	void SceneRenderer::CreateBloomPassMaterials()
	{
		if (!m_BloomComputePass || !GetSceneColorOutput() || !m_BloomComputeTextures[0].Texture)
			return;

		Ref<Image2D> inputImage = GetSceneColorOutput();
		const uint32_t mipCount = m_BloomComputeTextures[0].Texture->GetMipLevelCount();
		if (mipCount < 4)
			return;

		const uint32_t mips = mipCount - 2;

		m_BloomComputeMaterials.PrefilterMaterial = Material::Create(m_BloomComputePass->GetShader(), "Bloom-Prefilter");
		m_BloomComputeMaterials.PrefilterMaterial->Set("o_Image", m_BloomComputeTextures[0].ImageViews[0]);
		m_BloomComputeMaterials.PrefilterMaterial->Set("u_Texture", inputImage);
		m_BloomComputeMaterials.PrefilterMaterial->Set("u_BloomTexture", inputImage);

		m_BloomComputeMaterials.DownsampleAMaterials.clear();
		m_BloomComputeMaterials.DownsampleBMaterials.clear();
		m_BloomComputeMaterials.DownsampleAMaterials.resize(mips);
		m_BloomComputeMaterials.DownsampleBMaterials.resize(mips);

		for (uint32_t i = 1; i < mips; i++)
		{
			m_BloomComputeMaterials.DownsampleAMaterials[i] = Material::Create(m_BloomComputePass->GetShader(), "Bloom-DownsampleA");
			m_BloomComputeMaterials.DownsampleAMaterials[i]->Set("o_Image", m_BloomComputeTextures[1].ImageViews[i]);
			m_BloomComputeMaterials.DownsampleAMaterials[i]->Set("u_Texture", m_BloomComputeTextures[0].Texture);
			m_BloomComputeMaterials.DownsampleAMaterials[i]->Set("u_BloomTexture", inputImage);

			m_BloomComputeMaterials.DownsampleBMaterials[i] = Material::Create(m_BloomComputePass->GetShader(), "Bloom-DownsampleB");
			m_BloomComputeMaterials.DownsampleBMaterials[i]->Set("o_Image", m_BloomComputeTextures[0].ImageViews[i]);
			m_BloomComputeMaterials.DownsampleBMaterials[i]->Set("u_Texture", m_BloomComputeTextures[1].Texture);
			m_BloomComputeMaterials.DownsampleBMaterials[i]->Set("u_BloomTexture", inputImage);
		}

		m_BloomComputeMaterials.FirstUpsampleMaterial = Material::Create(m_BloomComputePass->GetShader(), "Bloom-FirstUpsample");
		m_BloomComputeMaterials.FirstUpsampleMaterial->Set("o_Image", m_BloomComputeTextures[2].ImageViews[mips - 2]);
		m_BloomComputeMaterials.FirstUpsampleMaterial->Set("u_Texture", m_BloomComputeTextures[0].Texture);
		m_BloomComputeMaterials.FirstUpsampleMaterial->Set("u_BloomTexture", inputImage);

		m_BloomComputeMaterials.UpsampleMaterials.clear();
		m_BloomComputeMaterials.UpsampleMaterials.resize(mips - 2);

		for (int32_t mip = (int32_t)mips - 3; mip >= 0; mip--)
		{
			m_BloomComputeMaterials.UpsampleMaterials[mip] = Material::Create(m_BloomComputePass->GetShader(), "Bloom-Upsample");
			m_BloomComputeMaterials.UpsampleMaterials[mip]->Set("o_Image", m_BloomComputeTextures[2].ImageViews[mip]);
			m_BloomComputeMaterials.UpsampleMaterials[mip]->Set("u_Texture", m_BloomComputeTextures[0].Texture);
			m_BloomComputeMaterials.UpsampleMaterials[mip]->Set("u_BloomTexture", m_BloomComputeTextures[2].ImageViews[mip + 1]);
		}
	}

	void SceneRenderer::ResizeScreenSpaceEffectResources()
	{
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		const glm::uvec2 viewportSize{ glm::max(1u, m_ViewportWidth), glm::max(1u, m_ViewportHeight) };

		auto resizePass = [&](Ref<RenderPass> pass, const glm::uvec2& size)
		{
			if (pass && pass->GetTargetFramebuffer())
				pass->GetTargetFramebuffer()->Resize(size.x, size.y);
		};

		resizePass(m_AOCompositePass, viewportSize);
		resizePass(m_AODebugPass, viewportSize);
		resizePass(m_SSRCompositePass, viewportSize);
		resizePass(m_DeferredLightingPass, viewportSize);
		resizePass(m_GBufferDebugPass, viewportSize);
		resizePass(m_SkyAtmospherePass, viewportSize);
		resizePass(m_VolumetricCloudCompositePass, viewportSize);
		resizePass(m_AtmosphericFogPass, viewportSize);
		resizePass(m_JumpFloodInitPass, viewportSize);
		resizePass(m_JumpFloodPasses[0], viewportSize);
		resizePass(m_JumpFloodPasses[1], viewportSize);
		resizePass(m_JumpFloodCompositePass, viewportSize);
		resizePass(m_DOFPass, GetScaledExtent(viewportSize, m_DOFSettings.ResolutionScale));
		ResizeVolumetricCloudResources();

		// HZB uses a power-of-two texture with UV factor back to the real viewport.
		if (m_HierarchicalDepthTexture.Texture)
		{
			const uint32_t hzbWidth = NextPowerOfTwo(viewportSize.x);
			const uint32_t hzbHeight = NextPowerOfTwo(viewportSize.y);
			const uint32_t maxDimension = glm::max(hzbWidth, hzbHeight);
			m_SSROptions.NumDepthMips = glm::max(1u, (uint32_t)glm::floor(glm::log2((float)maxDimension)) + 1u);
			m_SSROptions.HZBUvFactor = glm::vec2(viewportSize) / glm::vec2(hzbWidth, hzbHeight);

			m_HierarchicalDepthTexture.Texture->Resize(hzbWidth, hzbHeight);
			m_HZBPrimed = false;
			const uint32_t mipCount = m_HierarchicalDepthTexture.Texture->GetMipLevelCount();
			m_HierarchicalDepthTexture.ImageViews.resize(mipCount);

			ImageViewSpecification viewSpec;
			viewSpec.Image = m_HierarchicalDepthTexture.Texture->GetImage();
			viewSpec.MipCount = 1;
			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				viewSpec.Mip = mip;
				viewSpec.DebugName = "HierarchicalDepthTexture-" + std::to_string(mip);
				m_HierarchicalDepthTexture.ImageViews[mip] = ImageView::Create(viewSpec);
			}

			CreateHZBPassMaterials();
		}

		if (m_PreIntegrationVisibilityTexture.Texture)
		{
			m_PreIntegrationVisibilityTexture.Texture->Resize(viewportSize);
			const uint32_t mipCount = m_PreIntegrationVisibilityTexture.Texture->GetMipLevelCount();
			m_PreIntegrationVisibilityTexture.ImageViews.resize(mipCount > 0 ? mipCount - 1 : 0);

			ImageViewSpecification viewSpec;
			viewSpec.Image = m_PreIntegrationVisibilityTexture.Texture->GetImage();
			viewSpec.MipCount = 1;
			for (uint32_t mip = 1; mip < mipCount; mip++)
			{
				viewSpec.Mip = mip;
				viewSpec.DebugName = "PreIntegrationVisibilityTexture-" + std::to_string(mip);
				m_PreIntegrationVisibilityTexture.ImageViews[mip - 1] = ImageView::Create(viewSpec);
			}

			CreatePreIntegrationPassMaterials();
		}

		if (m_Options.EnableGTAO && m_GTAOOutputImage && m_GTAODenoiseImage && m_GTAOEdgesOutputImage)
		{
			m_GTAODataCB.ResolutionScale = GetEffectResolutionDivisor(m_Options.GTAOResolutionScale);
			const glm::uvec2 gtaoSize = GetScaledExtent(viewportSize, m_Options.GTAOResolutionScale);
			const glm::uvec2 denoiseSize = gtaoSize;
			m_GTAOOutputImage->GetSpecification().Format = ImageFormat::RED32UI;
			m_GTAODenoiseImage->GetSpecification().Format = ImageFormat::RED32UI;

			constexpr uint32_t GTAO_WORKGROUP_SIZE = 16u;
			m_GTAOOutputImage->Resize(gtaoSize.x, gtaoSize.y);
			m_GTAOEdgesOutputImage->Resize(gtaoSize.x, gtaoSize.y);
			for (Ref<Image2D>& historyImage : m_GTAOHistoryImages)
			{
				if (historyImage)
					historyImage->Resize(gtaoSize.x, gtaoSize.y);
			}

			m_GTAOWorkGroups = { DivideRoundUp(gtaoSize.x, GTAO_WORKGROUP_SIZE), DivideRoundUp(gtaoSize.y, GTAO_WORKGROUP_SIZE), 1 };
			m_GTAOTemporalWorkGroups = { DivideRoundUp(gtaoSize.x, 8u), DivideRoundUp(gtaoSize.y, 8u), 1 };

			constexpr uint32_t DENOISE_WORKGROUP_SIZE = 8u;
			m_GTAODenoiseImage->Resize(denoiseSize.x, denoiseSize.y);
			m_GTAODenoiseWorkGroups = {
				DivideRoundUp(denoiseSize.x, DENOISE_WORKGROUP_SIZE * 2u),
				DivideRoundUp(denoiseSize.y, DENOISE_WORKGROUP_SIZE),
				1
			};

			m_GTAOFinalImage = (m_Options.GTAODenoisePasses % 2 != 0) ? m_GTAODenoiseImage : m_GTAOOutputImage;
			if (m_AOCompositePass)
			{
				m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
				m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
				m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
			}
			if (m_AODebugPass)
			{
				m_AODebugPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
				m_AODebugPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
				m_AODebugPass->SetInput("u_Normal", GetGeometryNormalOutput());
			}
			if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
				m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			if (m_GTAOTemporalPass)
			{
				m_GTAOTemporalPass->SetInput("u_CurrentAO", m_GTAOFinalImage);
				m_GTAOTemporalPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			}
		}

		// TAA history runs at full scene-color (viewport) resolution.
		for (Ref<Image2D>& historyImage : m_TAAHistoryImages)
		{
			if (historyImage)
				historyImage->Resize(viewportSize.x, viewportSize.y);
		}

		if (m_SSRImage && m_PreConvolutedTexture.Texture)
		{
			constexpr uint32_t SSR_WORKGROUP_SIZE = 8u;
			m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
			m_SSROptions.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);
			m_SSROptions.HalfRes = m_SSROptions.ResolutionScale > 1u;
			glm::uvec2 ssrSize = GetScaledExtent(viewportSize, m_Options.SSRResolutionScale);

			m_SSRImage->Resize(ssrSize.x, ssrSize.y);
			for (Ref<Image2D>& historyImage : m_SSRHistoryImages)
			{
				if (historyImage)
					historyImage->Resize(ssrSize.x, ssrSize.y);
			}
			m_SSRFinalImage = m_SSRImage;
			m_SSRWorkGroups = { DivideRoundUp(ssrSize.x, SSR_WORKGROUP_SIZE), DivideRoundUp(ssrSize.y, SSR_WORKGROUP_SIZE), 1 };
			m_SSRTemporalWorkGroups = m_SSRWorkGroups;
			if (m_SSRTemporalPass)
			{
				m_SSRTemporalPass->SetInput("u_CurrentSSR", m_SSRImage);
				m_SSRTemporalPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			}
			if (m_SSRCompositePass)
			{
				m_SSRCompositePass->SetInput("u_SSR", m_SSRFinalImage);
				m_SSRCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
				m_SSRCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
			}

			m_PreConvolutedTexture.Texture->Resize(ssrSize.x, ssrSize.y);
			const uint32_t mipCount = m_PreConvolutedTexture.Texture->GetMipLevelCount();
			m_PreConvolutedTexture.ImageViews.resize(mipCount);

			ImageViewSpecification viewSpec;
			viewSpec.Image = m_PreConvolutedTexture.Texture->GetImage();
			viewSpec.MipCount = 1;
			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				viewSpec.Mip = mip;
				viewSpec.DebugName = "PreConvolutionCompute-" + std::to_string(mip);
				m_PreConvolutedTexture.ImageViews[mip] = ImageView::Create(viewSpec);
			}

			CreatePreConvolutionPassMaterials();
		}

		ResizeBloomResources();
		CreateBloomPassMaterials();
		m_TemporalHistoryValid = false;
		m_CloudHistoryValid = false;
	}

	void SceneRenderer::CreateHZBPassMaterials()
	{
		if (!m_HierarchicalDepthPass || !m_PreDepthPass || !m_HierarchicalDepthTexture.Texture)
			return;

		constexpr uint32_t maxMipBatchSize = 4;
		const uint32_t hzbMipCount = m_HierarchicalDepthTexture.Texture->GetMipLevelCount();
		m_HZBMaterials.clear();
		m_HZBMaterials.resize(DivideRoundUp(hzbMipCount, maxMipBatchSize));

		for (uint32_t startDestMip = 0; startDestMip < hzbMipCount; startDestMip += maxMipBatchSize)
		{
			Ref<Material> material = Material::Create(m_HierarchicalDepthPass->GetShader(), "HZB");
			if (startDestMip == 0)
			{
				material->Set("u_InputDepth", m_PreDepthPass->GetDepthOutput());
			}
			else
			{
				const uint32_t parentMip = startDestMip - 1u;
				material->Set("u_InputDepth", m_HierarchicalDepthTexture.ImageViews[parentMip]);
			}

			for (uint32_t outputIndex = 0; outputIndex < maxMipBatchSize; outputIndex++)
			{
				const uint32_t destMip = glm::min(startDestMip + outputIndex, hzbMipCount - 1u);
				material->Set("o_HZB", m_HierarchicalDepthTexture.ImageViews[destMip], outputIndex);
			}

			m_HZBMaterials[startDestMip / maxMipBatchSize] = material;
		}
	}

	void SceneRenderer::CreatePreIntegrationPassMaterials()
	{
		if (!m_PreIntegrationPass || !m_PreIntegrationVisibilityTexture.Texture || !m_HierarchicalDepthTexture.Texture)
			return;

		const uint32_t mipCount = m_PreIntegrationVisibilityTexture.Texture->GetMipLevelCount();
		if (mipCount < 2)
			return;

		m_PreIntegrationMaterials.clear();
		m_PreIntegrationMaterials.resize(mipCount - 1);

		for (uint32_t mip = 1; mip < mipCount; mip++)
		{
			Ref<Material> material = Material::Create(m_PreIntegrationPass->GetShader(), "Pre-Integration");
			material->Set("o_VisibilityImage", m_PreIntegrationVisibilityTexture.ImageViews[mip - 1]);
			ImageViewSpecification visibilityInputSpec;
			visibilityInputSpec.Image = m_PreIntegrationVisibilityTexture.Texture->GetImage();
			visibilityInputSpec.Mip = mip - 1u;
			visibilityInputSpec.MipCount = 1;
			visibilityInputSpec.DebugName = "PreIntegrationVisibilityTexture-Input-" + std::to_string(mip - 1u);
			material->Set("u_VisibilityTex", ImageView::Create(visibilityInputSpec));
			material->Set("u_HZB", m_HierarchicalDepthTexture.ImageViews[mip - 1u]);
			m_PreIntegrationMaterials[mip - 1] = material;
		}
	}

	void SceneRenderer::CreatePreConvolutionPassMaterials()
	{
		if (!m_PreConvolutionComputePass || !m_SkyboxPass || !m_PreConvolutedTexture.Texture)
			return;

		const uint32_t mipCount = m_PreConvolutedTexture.Texture->GetMipLevelCount();
		m_PreConvolutionMaterials.clear();
		m_PreConvolutionMaterials.resize(mipCount);

		for (uint32_t mip = 0; mip < mipCount; mip++)
		{
			Ref<Material> material = Material::Create(m_PreConvolutionComputePass->GetShader(), "Pre-Convolution");
			material->Set("o_Image", m_PreConvolutedTexture.ImageViews[mip]);
			material->Set("u_Input", mip == 0 ? GetSceneColorOutput() : m_PreConvolutedTexture.ImageViews[mip - 1]);
			m_PreConvolutionMaterials[mip] = material;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Per-frame scene data setters (call between BeginScene and EndScene)
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::SetLightEnvironment(const LightEnvironment& lightEnvironment)
	{
		m_SceneData.SceneLightEnvironment = lightEnvironment;

		const uint32_t shadowResolutionLimit = static_cast<uint32_t>(SanitizeShadowResolutionTier(static_cast<uint32_t>(m_Options.ShadowResolution)));
		for (DirectionalLight& light : m_SceneData.SceneLightEnvironment.DirectionalLights)
			light.ShadowResolutionTier = glm::min(light.ShadowResolutionTier, shadowResolutionLimit);

		for (SpotLight& light : m_SceneData.SceneLightEnvironment.SpotLights)
			light.ShadowResolutionTier = glm::min(light.ShadowResolutionTier, shadowResolutionLimit);

		RefreshFrameEnvironment();
	}

	void SceneRenderer::SetEnvironment(Ref<Environment> environment, float intensity, float skyboxLod)
	{
		m_SceneData.SceneEnvironment = environment;
		m_SceneData.SceneEnvironmentIntensity = intensity;
		m_SceneData.SkyboxLod = skyboxLod;
		RefreshFrameEnvironment();
	}

	void SceneRenderer::SetAtmosphereEnvironment(const AtmosphereEnvironment& atmosphereEnvironment)
	{
		m_SceneData.Atmosphere = atmosphereEnvironment;
		m_RenderVolumeEnvironment = {};
		m_RenderVolumeEnvironment.Atmosphere = atmosphereEnvironment;
		m_HasRenderVolumeEnvironment = false;
		RefreshFrameEnvironment();
	}

	void SceneRenderer::SetRenderVolumeEnvironment(const RenderVolumeEnvironment& renderVolumeEnvironment)
	{
		m_RenderVolumeEnvironment = renderVolumeEnvironment;
		m_SceneData.Atmosphere = renderVolumeEnvironment.Atmosphere;
		m_HasRenderVolumeEnvironment = true;
		RefreshFrameEnvironment();
	}

	RenderVolumeBaseSettings SceneRenderer::GetBaseRenderVolumeSettings(float cameraExposure) const
	{
		RenderVolumeBaseSettings settings;
		settings.Atmosphere = m_SceneData.Atmosphere;
		settings.PostProcess.Exposure = cameraExposure;
		settings.PostProcess.BloomEnabled = m_BloomSettings.Enabled;
		settings.PostProcess.BloomThreshold = m_BloomSettings.Threshold;
		settings.PostProcess.BloomKnee = m_BloomSettings.Knee;
		settings.PostProcess.BloomUpsampleScale = m_BloomSettings.UpsampleScale;
		settings.PostProcess.BloomIntensity = m_BloomSettings.Intensity;
		settings.PostProcess.BloomDirtIntensity = m_BloomSettings.DirtIntensity;
		settings.PostProcess.DOFEnabled = m_DOFSettings.Enabled;
		settings.PostProcess.DOFFocusDistance = m_DOFSettings.FocusDistance;
		settings.PostProcess.DOFBlurSize = m_DOFSettings.BlurSize;
		return settings;
	}

	RenderVolumePostProcessSettings SceneRenderer::GetResolvedPostProcessSettings() const
	{
		return ResolveFrameEnvironment().PostProcess;
	}

	SceneRenderer::ResolvedFrameEnvironment SceneRenderer::ResolveFrameEnvironment() const
	{
		ResolvedFrameEnvironment frame;
		frame.DeferredPath = true;
		frame.Environment = m_SceneData.SceneEnvironment;
		frame.EnvironmentIntensity = m_SceneData.SceneEnvironmentIntensity;
		frame.SkyboxLod = m_SceneData.SkyboxLod;
		frame.HasRenderVolumeEnvironment = m_HasRenderVolumeEnvironment;

		if (m_HasRenderVolumeEnvironment)
		{
			frame.Volumes = m_RenderVolumeEnvironment;
			frame.Atmosphere = m_RenderVolumeEnvironment.Atmosphere;
			frame.PostProcess = m_RenderVolumeEnvironment.PostProcess;
		}
		else
		{
			frame.Atmosphere = m_SceneData.Atmosphere;
			frame.Volumes = {};
			frame.Volumes.Atmosphere = frame.Atmosphere;
			frame.PostProcess = GetBaseRenderVolumeSettings(m_SceneData.SceneCamera.Camera.GetExposure()).PostProcess;
		}

		frame.SkyAtmosphereEnabled = frame.Atmosphere.SkyAtmosphere.Enabled;
		frame.VolumetricCloudsEnabled = frame.Atmosphere.VolumetricClouds.Enabled;
		frame.HeightFogEnabled = frame.Atmosphere.HeightFog.Enabled;
		frame.LocalFogEnabled = frame.Volumes.LocalFogVolumeCount > 0;
		frame.BloomEnabled = frame.PostProcess.BloomEnabled;
		frame.DOFEnabled = frame.PostProcess.DOFEnabled;
		return frame;
	}

	void SceneRenderer::RefreshFrameEnvironment()
	{
		m_FrameEnvironment = ResolveFrameEnvironment();
	}

	SceneRenderer::RendererFrameDebugSnapshot SceneRenderer::GetRendererFrameDebugSnapshot() const
	{
		const ResolvedFrameEnvironment frame = ResolveFrameEnvironment();

		RendererFrameDebugSnapshot snapshot;
		snapshot.DeferredPath = frame.DeferredPath;
		snapshot.HasRenderScene = m_SubmittedRenderScene != nullptr;
		snapshot.HasRenderVolumeEnvironment = frame.HasRenderVolumeEnvironment;
		snapshot.SkyAtmosphereEnabled = frame.SkyAtmosphereEnabled;
		snapshot.VolumetricCloudsEnabled = frame.VolumetricCloudsEnabled;
		snapshot.HeightFogEnabled = frame.HeightFogEnabled;
		snapshot.LocalFogEnabled = frame.LocalFogEnabled;
		snapshot.BloomEnabled = frame.BloomEnabled;
		snapshot.DOFEnabled = frame.DOFEnabled;
		snapshot.ActiveVolumeCount = frame.Volumes.ActiveVolumeCount;
		snapshot.ActivePostProcessVolumeCount = frame.Volumes.ActivePostProcessVolumeCount;
		snapshot.ActiveAtmosphereVolumeCount = frame.Volumes.ActiveAtmosphereVolumeCount;
		snapshot.LocalFogVolumeCount = frame.Volumes.LocalFogVolumeCount;
		snapshot.CulledLocalFogVolumeCount = frame.Volumes.CulledLocalFogVolumeCount;
		snapshot.DroppedLocalFogVolumeCount = frame.Volumes.DroppedLocalFogVolumeCount;
		return snapshot;
	}

	void SceneRenderer::CalculateCascades(CascadeData* cascades, const SceneRendererCamera& sceneCamera, const glm::vec3& lightDirection, float maxShadowDistance) const
	{
		const float nearClip = glm::max(sceneCamera.Near, 0.001f);
		const float cameraFar = glm::max(sceneCamera.Far, nearClip + 0.001f);
		const float shadowFar = glm::clamp(maxShadowDistance, nearClip + 0.001f, cameraFar);
		const float cameraClipRange = cameraFar - nearClip;
		const float shadowRange = shadowFar - nearClip;
		const float ratio = shadowFar / nearClip;

		float cascadeSplits[ShadowCascadeCount]{};
		for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
		{
			if (m_UseManualCascadeSplits)
			{
				cascadeSplits[cascade] = glm::clamp(m_ShadowCascadeSplits[cascade], 0.0f, 1.0f);
			}
			else
			{
				const float p = (cascade + 1.0f) / static_cast<float>(ShadowCascadeCount);
				const float logSplit = nearClip * std::pow(ratio, p);
				const float uniformSplit = nearClip + shadowRange * p;
				const float splitDistance = glm::mix(uniformSplit, logSplit, glm::clamp(m_Options.ShadowCascadeSplitLambda, 0.0f, 1.0f));
				cascadeSplits[cascade] = (splitDistance - nearClip) / cameraClipRange;
			}
		}
		if (!m_UseManualCascadeSplits)
			cascadeSplits[ShadowCascadeCount - 1] = (shadowFar - nearClip) / cameraClipRange;

		glm::mat4 viewMatrix = sceneCamera.ViewMatrix;
		if (m_ScaleShadowCascadesToOrigin > 0.0f)
		{
			constexpr glm::vec4 origin = glm::vec4(glm::vec3(0.0f), 1.0f);
			viewMatrix[3] = glm::mix(viewMatrix[3], origin, glm::clamp(m_ScaleShadowCascadesToOrigin, 0.0f, 1.0f));
		}

		const glm::mat4 viewProjection = sceneCamera.Camera.GetUnReversedProjectionMatrix() * viewMatrix;
		const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
		const float shadowMapResolution = m_ShadowMapPass ? static_cast<float>(m_ShadowMapPass->GetTargetFramebuffer()->GetWidth()) : 4096.0f;
		const glm::vec3 normalizedLightDirection = NormalizeOrFallback(lightDirection, { 0.0f, -1.0f, 0.0f });
		const glm::vec3 up = glm::abs(glm::dot(normalizedLightDirection, glm::vec3(0.0f, 1.0f, 0.0f))) < 0.99f
			? glm::vec3(0.0f, 1.0f, 0.0f)
			: glm::vec3(1.0f, 0.0f, 0.0f);

		float lastSplitDist = 0.0f;
		for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
		{
			const float splitDist = cascadeSplits[cascade];

			glm::vec3 frustumCorners[8] =
			{
				{ -1.0f,  1.0f, 0.0f },
				{  1.0f,  1.0f, 0.0f },
				{  1.0f, -1.0f, 0.0f },
				{ -1.0f, -1.0f, 0.0f },
				{ -1.0f,  1.0f, 1.0f },
				{  1.0f,  1.0f, 1.0f },
				{  1.0f, -1.0f, 1.0f },
				{ -1.0f, -1.0f, 1.0f },
			};

			for (glm::vec3& corner : frustumCorners)
			{
				const glm::vec4 worldCorner = inverseViewProjection * glm::vec4(corner, 1.0f);
				corner = glm::vec3(worldCorner) / worldCorner.w;
			}

			for (uint32_t i = 0; i < 4; i++)
			{
				const glm::vec3 cornerRay = frustumCorners[i + 4] - frustumCorners[i];
				frustumCorners[i + 4] = frustumCorners[i] + cornerRay * splitDist;
				frustumCorners[i] = frustumCorners[i] + cornerRay * lastSplitDist;
			}

			glm::vec3 frustumCenter(0.0f);
			for (const glm::vec3& corner : frustumCorners)
				frustumCenter += corner;
			frustumCenter /= 8.0f;

			float radius = 0.0f;
			for (const glm::vec3& corner : frustumCorners)
				radius = glm::max(radius, glm::length(corner - frustumCenter));
			radius = glm::max(std::ceil(radius * 16.0f) / 16.0f, 0.01f);

			const glm::vec3 maxExtents(radius);
			const glm::vec3 minExtents = -maxExtents;
			glm::mat4 lightView = glm::lookAt(frustumCenter - normalizedLightDirection * radius, frustumCenter, up);
			glm::mat4 lightProjection = glm::ortho(
				minExtents.x, maxExtents.x,
				minExtents.y, maxExtents.y,
				glm::max(0.0f, m_Options.ShadowCascadeNearPlaneOffset),
				maxExtents.z - minExtents.z + glm::max(0.0f, m_Options.ShadowCascadeFarPlaneOffset));

			glm::mat4 shadowMatrix = lightProjection * lightView;
			glm::vec4 shadowOrigin = (shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) * shadowMapResolution * 0.5f;
			glm::vec4 roundedOrigin = glm::round(shadowOrigin);
			glm::vec4 roundOffset = (roundedOrigin - shadowOrigin) * (2.0f / shadowMapResolution);
			roundOffset.z = 0.0f;
			roundOffset.w = 0.0f;
			lightProjection[3] += roundOffset;

			cascades[cascade].SplitDepth = -(nearClip + splitDist * cameraClipRange);
			cascades[cascade].ViewProj = lightProjection * lightView;

			lastSplitDist = splitDist;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// BeginScene
	// ─────────────────────────────────────────────────────────────────────────

	SceneRenderer::ScopedCPUProfile::ScopedCPUProfile(SceneRenderer& renderer, const char* name)
		: Renderer(renderer), Name(name)
	{
#if LUX_ENABLE_PROFILING
		// Open a Tracy zone whose name is the pass name. Because every SceneRenderer
		// pass already wraps itself in a ScopedCPUProfile, this single chokepoint makes
		// the entire render pipeline visible in a Tracy capture (no per-pass edits).
		const uint64_t srcloc = ___tracy_alloc_srcloc_name(
			(uint32_t)__LINE__, __FILE__, sizeof(__FILE__) - 1,
			"SceneRenderer::Pass", 19,
			name, std::strlen(name), 0);
		ProfileZone = ___tracy_emit_zone_begin_alloc(srcloc, 1);
#endif
	}

	SceneRenderer::ScopedCPUProfile::~ScopedCPUProfile()
	{
#if LUX_ENABLE_PROFILING
		___tracy_emit_zone_end(ProfileZone);
#endif
		Renderer.RecordCPUProfile(Name, ProfileTimer.ElapsedMillis());
	}

	SceneRenderer::PassProfile& SceneRenderer::GetOrCreatePassProfile(const char* name)
	{
		for (PassProfile& profile : m_Statistics.PassProfiles)
		{
			if (std::strcmp(profile.Name, name) == 0)
				return profile;
		}

		PassProfile& profile = m_Statistics.PassProfiles.emplace_back();
		profile.Name = name;
		return profile;
	}

	void SceneRenderer::ResetProfilingData()
	{
		if (m_Statistics.PassProfiles.size() != s_ProfiledSceneRendererPasses.size())
		{
			m_Statistics.PassProfiles.clear();
			m_Statistics.PassProfiles.reserve(s_ProfiledSceneRendererPasses.size());
			for (const char* passName : s_ProfiledSceneRendererPasses)
			{
				PassProfile& profile = m_Statistics.PassProfiles.emplace_back();
				profile.Name = passName;
			}
		}

		m_Statistics.TotalCPUTime = 0.0f;
		for (PassProfile& profile : m_Statistics.PassProfiles)
		{
			profile.CPUTime = 0.0f;
			profile.GPUTime = 0.0f;
			profile.Active = false;
			profile.GPUActive = false;
		}
	}

	void SceneRenderer::RecordCPUProfile(const char* name, float cpuTime)
	{
		PassProfile& profile = GetOrCreatePassProfile(name);
		profile.CPUTime += cpuTime;
		profile.Active = true;
		m_Statistics.TotalCPUTime += cpuTime;
	}

	void SceneRenderer::BeginProfiledGPU(const char* name)
	{
		PassProfile& profile = GetOrCreatePassProfile(name);
		profile.GPUActive = true;
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, name);
	}

	void SceneRenderer::EndProfiledGPU()
	{
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::UpdateGPUProfileTimes()
	{
		if (!m_CommandBuffer)
			return;

		for (PassProfile& profile : m_Statistics.PassProfiles)
		{
			if (!profile.GPUActive)
				continue;

			profile.GPUTime = m_CommandBuffer->GetTimerQueryTime(profile.Name);
		}
	}

	void SceneRenderer::UpdateMemoryStatistics()
	{
		// Memory statistics are display-only (Render Stats / Renderer Debugger panels) and
		// are gathered with a full VMA allocation walk (vmaCalculateStats) plus a render-graph
		// alias-plan pass — far too heavy to run every frame for numbers that change slowly.
		// Refresh a few times per second and keep the previous values in between.
		if (m_MemoryStatsCountdown > 0)
		{
			m_MemoryStatsCountdown--;
			return;
		}
		m_MemoryStatsCountdown = MemoryStatsRefreshFrameInterval;

		auto& memoryStats = m_Statistics.MemoryStats;
		memoryStats = {};

		const GPUMemoryStats gpuStats = Renderer::GetGPUMemoryStats();
		memoryStats.BudgetBytes = gpuStats.TotalAvailable;
		memoryStats.UsedBytes = gpuStats.Used;
		memoryStats.BufferBytes = gpuStats.BufferAllocationSize;
		memoryStats.BufferCount = static_cast<uint32_t>(gpuStats.BufferAllocationCount);

		uint64_t estimatedBufferBytes = 0;
		uint32_t estimatedBufferCount = 0;
		auto addUniformBufferSet = [&](const Ref<UniformBufferSet>& bufferSet)
			{
				if (!bufferSet)
					return;

				estimatedBufferBytes += bufferSet->GetAllocatedSize();
				estimatedBufferCount += bufferSet->GetBufferCount();
			};
		auto addStorageBufferSet = [&](const Ref<StorageBufferSet>& bufferSet)
			{
				if (!bufferSet)
					return;

				estimatedBufferBytes += bufferSet->GetAllocatedSize();
				estimatedBufferCount += bufferSet->GetBufferCount();
			};

		addUniformBufferSet(m_UBSCamera);
		addUniformBufferSet(m_UBSScene);
		addUniformBufferSet(m_UBSShadow);
		addUniformBufferSet(m_UBSSpotShadow);
		addUniformBufferSet(m_UBSRendererData);
		addUniformBufferSet(m_UBSScreenData);
		addUniformBufferSet(m_UBSAtmosphere);
		addUniformBufferSet(m_UBSPointLights);
		addUniformBufferSet(m_UBSSpotLights);
		addStorageBufferSet(m_SBSObjectIndexes);
		addStorageBufferSet(m_SBSVisibleObjectIndexes);
		addStorageBufferSet(m_SBSGPUSceneInstances);
		addStorageBufferSet(m_SBSGPUMaterials);
		addStorageBufferSet(m_SBSMeshCullDrawData);
		addStorageBufferSet(m_SBSIndirectDrawCommands);
		addStorageBufferSet(m_SBSClusterAABBs);
		addStorageBufferSet(m_SBSPointLightGrid);
		addStorageBufferSet(m_SBSSpotLightGrid);
		addStorageBufferSet(m_SBSPointLightIndexList);
		addStorageBufferSet(m_SBSSpotLightIndexList);
		addStorageBufferSet(m_SBSClusterLightCounter);

		memoryStats.BufferBytes = std::max(memoryStats.BufferBytes, estimatedBufferBytes);
		memoryStats.BufferCount = std::max(memoryStats.BufferCount, estimatedBufferCount);

		std::unordered_set<uint64_t> renderTargetImageHandles;
		std::unordered_set<const Framebuffer*> framebuffers;

		auto addRenderTargetImage = [&](const Ref<Image2D>& image)
			{
				if (!image || image->GetHandle() == nullptr)
					return;

				const uint64_t handle = reinterpret_cast<uint64_t>(image->GetHandle().Get());
				if (!renderTargetImageHandles.insert(handle).second)
					return;

				memoryStats.RenderTargetCount++;
				uint64_t imageSize = image->GetGPUMemoryUsage();
				if (imageSize == 0)
				{
					const ImageSpecification& spec = image->GetSpecification();
					imageSize = Utils::GetImageMemorySize(spec.Format, spec.Width, spec.Height, spec.Mips, spec.Layers);
				}
				memoryStats.RenderTargetBytes += imageSize;
			};

		auto addFramebuffer = [&](const Ref<Framebuffer>& framebuffer)
			{
				if (!framebuffer || !framebuffers.insert(framebuffer.Raw()).second)
					return;

				memoryStats.FramebufferCount++;
				for (uint32_t attachment = 0; attachment < framebuffer->GetColorAttachmentCount(); attachment++)
					addRenderTargetImage(framebuffer->GetImage(attachment));

				if (framebuffer->HasDepthAttachment())
					addRenderTargetImage(framebuffer->GetDepthImage());
			};

		auto addRenderPass = [&](const Ref<RenderPass>& pass)
			{
				if (!pass)
					return;

				addFramebuffer(pass->GetTargetFramebuffer());

				memoryStats.DescriptorSetCount += pass->GetBindingSetCount();
			};

		auto addComputePass = [&](const Ref<ComputePass>& pass)
			{
				if (!pass)
					return;

				memoryStats.DescriptorSetCount += pass->GetBindingSetCount();
			};

		for (const Ref<RenderPass>& pass : m_ShadowMapPasses)
			addRenderPass(pass);
		addRenderPass(m_SpotShadowMapPass);
		addRenderPass(m_PreDepthPass);
		addRenderPass(m_AOCompositePass);
		addRenderPass(m_AODebugPass);
		addRenderPass(m_SSRCompositePass);
		addRenderPass(m_DOFPass);
		addRenderPass(m_JumpFloodInitPass);
		addRenderPass(m_JumpFloodPasses[0]);
		addRenderPass(m_JumpFloodPasses[1]);
		addRenderPass(m_JumpFloodCompositePass);
		addRenderPass(m_GeometryPass);
		addRenderPass(m_GeometryPassTransparent);
		addRenderPass(m_DeferredLightingPass);
		addRenderPass(m_GBufferDebugPass);
		addRenderPass(m_SelectedGeometryPass);
		addRenderPass(m_GeometryWireframePass);
		addRenderPass(m_SkyboxPass);
		addRenderPass(m_SkyAtmospherePass);
		addRenderPass(m_VolumetricCloudPass);
		addRenderPass(m_VolumetricCloudCompositePass);
		addRenderPass(m_AtmosphericFogPass);
		addRenderPass(m_CompositePass);
		addRenderPass(m_GridRenderPass);

		addFramebuffer(m_GeometryPassFramebuffer);
		addFramebuffer(m_SceneColorFramebuffer);
		addFramebuffer(m_CompositingFramebuffer);

		addComputePass(m_MeshCullingPass);
		addComputePass(m_ClusterBuildPass);
		addComputePass(m_ClusterLightCullingPass);
		addComputePass(m_HierarchicalDepthPass);
		addComputePass(m_PreIntegrationPass);
		addComputePass(m_PreConvolutionComputePass);
		addComputePass(m_GTAOComputePass);
		addComputePass(m_GTAODenoisePass[0]);
		addComputePass(m_GTAODenoisePass[1]);
		addComputePass(m_GTAOTemporalPass);
		addComputePass(m_SSRPass);
		addComputePass(m_SSRTemporalPass);
		addComputePass(m_BloomComputePass);

		if (m_HierarchicalDepthTexture.Texture)
			addRenderTargetImage(m_HierarchicalDepthTexture.Texture->GetImage());
		if (m_PreIntegrationVisibilityTexture.Texture)
			addRenderTargetImage(m_PreIntegrationVisibilityTexture.Texture->GetImage());
		if (m_PreConvolutedTexture.Texture)
			addRenderTargetImage(m_PreConvolutedTexture.Texture->GetImage());
		for (const BloomComputeTextures& bloomTexture : m_BloomComputeTextures)
		{
			if (bloomTexture.Texture)
				addRenderTargetImage(bloomTexture.Texture->GetImage());
		}

		addRenderTargetImage(m_GTAOOutputImage);
		addRenderTargetImage(m_GTAODenoiseImage);
		addRenderTargetImage(m_GTAOFinalImage);
		for (const Ref<Image2D>& historyImage : m_GTAOHistoryImages)
			addRenderTargetImage(historyImage);
		addRenderTargetImage(m_GTAOEdgesOutputImage);
		addRenderTargetImage(m_SSRImage);
		addRenderTargetImage(m_SSRFinalImage);
		for (const Ref<Image2D>& historyImage : m_SSRHistoryImages)
			addRenderTargetImage(historyImage);

		uint64_t liveImageBytes = 0;
		uint32_t liveImageCount = 0;
		for (const auto& [handle, image] : Image2D::GetImageRefs())
		{
			if (!image)
				continue;

			uint64_t imageSize = image->GetGPUMemoryUsage();
			if (imageSize == 0)
			{
				const ImageSpecification& spec = image->GetSpecification();
				imageSize = Utils::GetImageMemorySize(spec.Format, spec.Width, spec.Height, spec.Mips, spec.Layers);
			}

			liveImageBytes += imageSize;
			liveImageCount++;
		}

		const uint64_t imageAllocationSize = std::max(gpuStats.ImageAllocationSize, liveImageBytes);
		const uint32_t imageAllocationCount = std::max<uint32_t>(static_cast<uint32_t>(gpuStats.ImageAllocationCount), liveImageCount);
		memoryStats.TextureBytes = imageAllocationSize > memoryStats.RenderTargetBytes
			? imageAllocationSize - memoryStats.RenderTargetBytes
			: 0;
		memoryStats.TextureCount = imageAllocationCount > memoryStats.RenderTargetCount
			? imageAllocationCount - memoryStats.RenderTargetCount
			: 0;

		UpdateRenderGraphStatistics();
	}

	void SceneRenderer::BuildRenderGraph(bool executable)
	{
		m_RenderGraph.Reset();
		const ResolvedFrameEnvironment frame = ResolveFrameEnvironment();
		std::unordered_map<const Image2D*, RenderGraph::ResourceHandle> resourceLookup;

		// Resource/pass names are only consumed by diagnostics and the Renderer Debugger
		// snapshot (which builds the graph with executable=false). The per-frame executable
		// build never feeds the debugger, so skip all name materialization there — names are
		// passed as string_view and only copied into a std::string when captured. This avoids
		// dozens of per-frame heap allocations (long literals + the std::format'd names).
		const bool captureNames = !executable;
		const auto graphName = [captureNames](std::string_view name) -> std::string
			{
				return captureNames ? std::string(name) : std::string{};
			};

		auto reportGraphDiagnostic = [&](RenderGraph::DiagnosticSeverity severity,
			RenderGraph::DiagnosticCode code,
			std::string passName,
			std::string resourceName,
			std::string message,
			RenderGraph::ResourceHandle resource = RenderGraph::InvalidResource)
			{
				RenderGraph::Diagnostic diagnostic;
				diagnostic.Severity = severity;
				diagnostic.Code = code;
				diagnostic.PassName = std::move(passName);
				diagnostic.Resource = resource;
				diagnostic.ResourceName = std::move(resourceName);
				diagnostic.Message = std::move(message);
				m_RenderGraph.AddDiagnostic(std::move(diagnostic));
			};

		auto makeExecute = [&](auto memberFunction) -> RenderGraph::ExecuteCallback
			{
				if (!executable)
					return {};

				return [this, memberFunction]()
					{
						(this->*memberFunction)();
					};
			};

		auto addTexture = [&](std::string_view name, const Ref<Image2D>& image) -> RenderGraph::ResourceHandle
			{
				if (!image)
				{
					reportGraphDiagnostic(RenderGraph::DiagnosticSeverity::Error,
						RenderGraph::DiagnosticCode::NullTexture,
						{},
						std::string(name),
						std::format("Render graph resource '{}' is missing an Image2D reference.", name));
					return RenderGraph::InvalidResource;
				}

				if (!image->IsValid())
				{
					reportGraphDiagnostic(RenderGraph::DiagnosticSeverity::Error,
						RenderGraph::DiagnosticCode::NullTexture,
						{},
						std::string(name),
						std::format("Render graph resource '{}' has no valid GPU image handle.", name));
					return RenderGraph::InvalidResource;
				}

				const Image2D* key = image.Raw();
				if (auto it = resourceLookup.find(key); it != resourceLookup.end())
					return it->second;

				const ImageSpecification& spec = image->GetSpecification();
				const bool allowAlias = IsRenderGraphAliasCandidate(image);
				RenderGraph::TextureDesc desc;
				desc.Name = graphName(name);
				desc.Image = image;
				desc.Format = spec.Format;
				desc.Usage = spec.Usage;
				desc.Dimension = spec.Dimension;
				desc.Width = spec.Width;
				desc.Height = spec.Height;
				desc.Mips = spec.Mips;
				desc.Layers = spec.Layers;
				desc.Transient = allowAlias;
				desc.AllowAlias = allowAlias;
				const RenderGraph::ResourceHandle resource = m_RenderGraph.AddTransientTexture(desc);
				resourceLookup[key] = resource;
				return resource;
			};

		auto addFramebufferResources = [&](std::string_view name, const Ref<Framebuffer>& framebuffer)
			{
				std::vector<RenderGraph::ResourceHandle> resources;
				if (!framebuffer)
				{
					reportGraphDiagnostic(RenderGraph::DiagnosticSeverity::Error,
						RenderGraph::DiagnosticCode::NullTexture,
						std::string(name),
						std::string(name),
						std::format("Render graph pass '{}' has no target framebuffer.", name));
					return resources;
				}

				for (uint32_t attachment = 0; attachment < framebuffer->GetColorAttachmentCount(); attachment++)
				{
					std::string childName = captureNames ? std::format("{} Color {}", name, attachment) : std::string{};
					resources.push_back(addTexture(childName, framebuffer->GetImage(attachment)));
				}
				if (framebuffer->HasDepthAttachment())
				{
					std::string depthName = captureNames ? std::format("{} Depth", name) : std::string{};
					resources.push_back(addTexture(depthName, framebuffer->GetDepthImage()));
				}

				return resources;
			};

		auto addRenderPassResources = [&](std::string_view name, const Ref<RenderPass>& pass)
			{
				if (!pass)
				{
					reportGraphDiagnostic(RenderGraph::DiagnosticSeverity::Error,
						RenderGraph::DiagnosticCode::NullTexture,
						std::string(name),
						std::string(name),
						std::format("Render graph pass '{}' is missing its RenderPass object.", name));
					return std::vector<RenderGraph::ResourceHandle>{};
				}

				return addFramebufferResources(name, pass->GetTargetFramebuffer());
			};

		auto appendResources = [](std::vector<RenderGraph::ResourceHandle>& dst, const std::vector<RenderGraph::ResourceHandle>& src)
			{
				dst.insert(dst.end(), src.begin(), src.end());
			};

		auto addPass = [&](std::string_view name,
			std::vector<RenderGraph::ResourceHandle> reads,
			std::vector<RenderGraph::ResourceHandle> writes,
			RenderGraph::PassFlags flags,
			RenderGraph::ExecuteCallback execute = {})
			{
				if (reads.empty() && writes.empty() && !execute)
					return;

				RenderGraph::PassDesc pass;
				pass.Name = graphName(name);
				pass.Reads = std::move(reads);
				pass.Writes = std::move(writes);
				pass.Flags = execute
					? RenderGraph::CombineFlags(flags, RenderGraph::PassFlags::SideEffect)
					: flags;
				pass.Execute = std::move(execute);
				m_RenderGraph.AddPass(std::move(pass));
			};

		std::vector<RenderGraph::ResourceHandle> directionalShadowOutputs = { addTexture("Directional Shadow Atlas", m_ShadowMapImage) };
		std::vector<RenderGraph::ResourceHandle> spotShadowOutputs = { addTexture("Spot Shadow Atlas", m_SpotShadowMapImage) };
		addPass("Directional Shadow Maps", {}, directionalShadowOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::ShadowMapPass));
		addPass("Spot Shadow Maps", {}, spotShadowOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::SpotShadowMapPass));

		std::vector<RenderGraph::ResourceHandle> shadowOutputs = directionalShadowOutputs;
		appendResources(shadowOutputs, spotShadowOutputs);

		std::vector<RenderGraph::ResourceHandle> preDepthOutputs = addRenderPassResources("PreDepth", m_PreDepthPass);
		addPass("PreDepth", {}, preDepthOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::PreDepthPass));

		std::vector<RenderGraph::ResourceHandle> hzbOutputs;
		hzbOutputs.push_back(m_HierarchicalDepthTexture.Texture ? addTexture("HZB", m_HierarchicalDepthTexture.Texture->GetImage()) : RenderGraph::InvalidResource);
		addPass("HZB", preDepthOutputs, hzbOutputs, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::HZBCompute));
		addPass("Mesh Culling", hzbOutputs, {}, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::MeshCullingPass));

		std::vector<RenderGraph::ResourceHandle> preIntegrationOutputs;
		preIntegrationOutputs.push_back(m_PreIntegrationVisibilityTexture.Texture ? addTexture("PreIntegration Visibility", m_PreIntegrationVisibilityTexture.Texture->GetImage()) : RenderGraph::InvalidResource);
		addPass("PreIntegration", hzbOutputs, preIntegrationOutputs, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::PreIntegration));

		// Cluster build runs before light culling; it only depends on the camera
		// projection (SSBO synchronized via a manual barrier inside the pass).
		addPass("Cluster Build", {}, {}, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::ClusterBuildPass));

		// Cluster light assignment depends on the cluster AABBs + the light UBOs;
		// it is depth-independent (SSBOs synchronized via manual barriers).
		addPass("Cluster Light Culling", {}, {}, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::ClusterLightCullingPass));

		std::vector<RenderGraph::ResourceHandle> gbufferOutputs = addFramebufferResources("GBuffer", m_GeometryPassFramebuffer);
		std::vector<RenderGraph::ResourceHandle> sceneColorOutputs = addFramebufferResources("SceneColor", m_SceneColorFramebuffer);
		std::vector<RenderGraph::ResourceHandle> skyboxOutputs = addRenderPassResources("Skybox", m_SkyboxPass);
		addPass("Skybox", {}, skyboxOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::SkyboxPass));

		const bool skyAtmosphereEnabled = frame.SkyAtmosphereEnabled && m_SkyAtmospherePass;
		std::vector<RenderGraph::ResourceHandle> sceneColorCurrent = skyboxOutputs.empty() ? sceneColorOutputs : skyboxOutputs;
		if (skyAtmosphereEnabled)
		{
			std::vector<RenderGraph::ResourceHandle> skyAtmosphereOutputs = addRenderPassResources("Sky Atmosphere", m_SkyAtmospherePass);
			addPass("Sky Atmosphere", sceneColorCurrent, skyAtmosphereOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::SkyAtmospherePass));
			sceneColorCurrent = skyAtmosphereOutputs;
		}

		std::vector<RenderGraph::ResourceHandle> selectedOutputs = addRenderPassResources("SelectedGeometry", m_SelectedGeometryPass);
		addPass("Selected Geometry", preDepthOutputs, selectedOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::SelectedGeometryPass));

		std::vector<RenderGraph::ResourceHandle> geometryOutputs = gbufferOutputs;
		appendResources(geometryOutputs, sceneColorCurrent);

		{
			addPass("GBuffer", preDepthOutputs, gbufferOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::GBufferPass));

			std::vector<RenderGraph::ResourceHandle> deferredReads = gbufferOutputs;
			appendResources(deferredReads, preDepthOutputs);
			appendResources(deferredReads, shadowOutputs);
			appendResources(deferredReads, sceneColorCurrent);
			std::vector<RenderGraph::ResourceHandle> deferredOutputs = addRenderPassResources("Deferred Lighting", m_DeferredLightingPass);
			addPass("Deferred Lighting", deferredReads, deferredOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::DeferredLightingPass));
			sceneColorCurrent = deferredOutputs;

			geometryOutputs = gbufferOutputs;
			appendResources(geometryOutputs, sceneColorCurrent);
		}

		if (UsesGBufferDebugPass(m_DebugViewMode))
		{
			std::vector<RenderGraph::ResourceHandle> debugReads = gbufferOutputs;
			appendResources(debugReads, sceneColorCurrent);
			addPass("GBuffer Debug", debugReads, addRenderPassResources("GBuffer Debug", m_GBufferDebugPass), RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::GBufferDebugPass));
		}

		std::vector<RenderGraph::ResourceHandle> aoFinalOutputs;
		if (m_Options.EnableGTAO)
		{
			const RenderGraph::ResourceHandle gtaoOutput = addTexture("GTAO Output", m_GTAOOutputImage);
			const RenderGraph::ResourceHandle gtaoDenoise = addTexture("GTAO Denoise", m_GTAODenoiseImage);
			const RenderGraph::ResourceHandle gtaoEdges = addTexture("GTAO Edges", m_GTAOEdgesOutputImage);
			const RenderGraph::ResourceHandle gtaoHistoryA = addTexture("GTAO History A", m_GTAOHistoryImages[0]);
			const RenderGraph::ResourceHandle gtaoHistoryB = addTexture("GTAO History B", m_GTAOHistoryImages[1]);

			std::vector<RenderGraph::ResourceHandle> gtaoReads = geometryOutputs;
			appendResources(gtaoReads, hzbOutputs);
			addPass("GTAO", gtaoReads, { gtaoOutput, gtaoEdges }, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::GTAOCompute));
			addPass("GTAO Denoise", { gtaoOutput, gtaoEdges }, { gtaoDenoise, gtaoOutput }, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::GTAODenoiseCompute));

			aoFinalOutputs = { gtaoOutput, gtaoDenoise };
			if (m_Options.EnableGTAOTemporalAccumulation)
			{
				addPass("GTAO Temporal", { gtaoOutput, gtaoDenoise, gtaoHistoryA, preDepthOutputs.empty() ? RenderGraph::InvalidResource : preDepthOutputs.front() }, { gtaoHistoryB }, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::GTAOTemporalAccumulationCompute));
				aoFinalOutputs.push_back(gtaoHistoryA);
				aoFinalOutputs.push_back(gtaoHistoryB);
			}

			std::vector<RenderGraph::ResourceHandle> aoCompositeReads = geometryOutputs;
			appendResources(aoCompositeReads, preDepthOutputs);
			appendResources(aoCompositeReads, aoFinalOutputs);
			appendResources(aoCompositeReads, sceneColorCurrent);
			std::vector<RenderGraph::ResourceHandle> aoCompositeOutputs = addRenderPassResources("AO Composite", m_AOCompositePass);
			addPass("AO Composite", aoCompositeReads, aoCompositeOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::AOComposite));
			sceneColorCurrent = aoCompositeOutputs;

			if (m_DebugViewMode == DebugViewMode::AO)
				addPass("AO Debug", aoCompositeReads, addRenderPassResources("AO Debug", m_AODebugPass), RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::AODebugPass));
		}

		std::vector<RenderGraph::ResourceHandle> ssrOutputs;
		if (m_Options.EnableSSR)
		{
			std::vector<RenderGraph::ResourceHandle> preConvolutionOutputs;
			preConvolutionOutputs.push_back(m_PreConvolutedTexture.Texture ? addTexture("Pre-Convoluted Scene", m_PreConvolutedTexture.Texture->GetImage()) : RenderGraph::InvalidResource);
			addPass("Pre-Convolution", sceneColorCurrent, preConvolutionOutputs, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::PreConvolutionCompute));

			const RenderGraph::ResourceHandle ssrImage = addTexture("SSR", m_SSRImage);
			const RenderGraph::ResourceHandle ssrHistoryA = addTexture("SSR History A", m_SSRHistoryImages[0]);
			const RenderGraph::ResourceHandle ssrHistoryB = addTexture("SSR History B", m_SSRHistoryImages[1]);
			ssrOutputs.push_back(ssrImage);

			std::vector<RenderGraph::ResourceHandle> ssrReads = geometryOutputs;
			appendResources(ssrReads, hzbOutputs);
			appendResources(ssrReads, preIntegrationOutputs);
			appendResources(ssrReads, preConvolutionOutputs);
			appendResources(ssrReads, aoFinalOutputs);
			addPass("SSR", ssrReads, ssrOutputs, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::SSRCompute));

			if (m_Options.EnableSSRTemporalAccumulation)
			{
				addPass("SSR Temporal", { ssrImage, ssrHistoryA, preDepthOutputs.empty() ? RenderGraph::InvalidResource : preDepthOutputs.front() }, { ssrHistoryB }, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::SSRTemporalAccumulationCompute));
				ssrOutputs.push_back(ssrHistoryA);
				ssrOutputs.push_back(ssrHistoryB);
			}

			std::vector<RenderGraph::ResourceHandle> ssrCompositeReads = geometryOutputs;
			appendResources(ssrCompositeReads, ssrOutputs);
			appendResources(ssrCompositeReads, sceneColorCurrent);
			std::vector<RenderGraph::ResourceHandle> ssrCompositeOutputs = addRenderPassResources("SSR Composite", m_SSRCompositePass);
			addPass("SSR Composite", ssrCompositeReads, ssrCompositeOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::SSRCompositePass));
			sceneColorCurrent = ssrCompositeOutputs;
		}

		if (frame.VolumetricCloudsEnabled && m_VolumetricCloudPass && m_VolumetricCloudCompositePass)
		{
			std::vector<RenderGraph::ResourceHandle> cloudReads;
			appendResources(cloudReads, preDepthOutputs);
			std::vector<RenderGraph::ResourceHandle> cloudOutputs = addRenderPassResources("Volumetric Clouds", m_VolumetricCloudPass);
			addPass("Volumetric Clouds", cloudReads, cloudOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::VolumetricCloudPass));

			std::vector<RenderGraph::ResourceHandle> cloudCompositeReads = sceneColorCurrent;
			appendResources(cloudCompositeReads, preDepthOutputs);
			appendResources(cloudCompositeReads, cloudOutputs);

			// Temporal scattering integration (compute) between raymarch and composite.
			if (m_VolumetricCloudTemporalPass && m_CloudHistoryImages[0] && m_CloudHistoryImages[1])
			{
				const uint32_t resolvedIndex = (m_CloudHistoryIndex & 1u) ^ 1u;
				const RenderGraph::ResourceHandle cloudResolved = addTexture("Volumetric Cloud Resolved", m_CloudHistoryImages[resolvedIndex]);
				addPass("Volumetric Cloud Temporal", cloudOutputs, { cloudResolved }, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::VolumetricCloudTemporalPass));
				appendResources(cloudCompositeReads, { cloudResolved });
			}

			std::vector<RenderGraph::ResourceHandle> cloudCompositeOutputs = addRenderPassResources("Volumetric Cloud Composite", m_VolumetricCloudCompositePass);
			addPass("Volumetric Cloud Composite", cloudCompositeReads, cloudCompositeOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::VolumetricCloudCompositePass));
			sceneColorCurrent = cloudCompositeOutputs;
		}

		if ((frame.HeightFogEnabled || frame.LocalFogEnabled) && m_AtmosphericFogPass)
		{
			std::vector<RenderGraph::ResourceHandle> fogReads = sceneColorCurrent;
			appendResources(fogReads, preDepthOutputs);
			std::vector<RenderGraph::ResourceHandle> fogOutputs = addRenderPassResources("Atmospheric Fog", m_AtmosphericFogPass);
			addPass("Atmospheric Fog", fogReads, fogOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::AtmosphericFogPass));
			sceneColorCurrent = fogOutputs;
		}

		std::vector<RenderGraph::ResourceHandle> transparentReads = shadowOutputs;
		appendResources(transparentReads, preDepthOutputs);
		appendResources(transparentReads, sceneColorCurrent);
		std::vector<RenderGraph::ResourceHandle> transparentOutputs = addRenderPassResources("Transparent Forward", m_GeometryPassTransparent);
		addPass("Transparent Forward", transparentReads, transparentOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::TransparentForwardPass));
		sceneColorCurrent = transparentOutputs;

		std::vector<RenderGraph::ResourceHandle> wireframeReads = sceneColorCurrent;
		std::vector<RenderGraph::ResourceHandle> wireframeOutputs = addRenderPassResources("Geometry Wireframe", m_GeometryWireframePass);
		addPass("Geometry Wireframe", wireframeReads, wireframeOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::GeometryWireframePass));
		sceneColorCurrent = wireframeOutputs;

		std::vector<RenderGraph::ResourceHandle> jumpFloodAOutputs;
		std::vector<RenderGraph::ResourceHandle> jumpFloodBOutputs;
		const bool jumpFloodActive = m_Options.EnableJumpFlood && (executable ? !GetMeshPass(MeshPassType::SelectedMask).DrawList.empty() : true);
		if (jumpFloodActive)
		{
			std::vector<RenderGraph::ResourceHandle> jumpFloodInitOutputs = addRenderPassResources("JumpFlood Init", m_JumpFloodInitPass);
			jumpFloodAOutputs = addRenderPassResources("JumpFlood A", m_JumpFloodPasses[0]);
			jumpFloodBOutputs = addRenderPassResources("JumpFlood B", m_JumpFloodPasses[1]);

			std::vector<RenderGraph::ResourceHandle> jumpFloodWrites = jumpFloodInitOutputs;
			appendResources(jumpFloodWrites, jumpFloodAOutputs);
			appendResources(jumpFloodWrites, jumpFloodBOutputs);
			addPass("JumpFlood", selectedOutputs, jumpFloodWrites, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::JumpFloodPass));
		}

		const RenderVolumePostProcessSettings& postProcessSettings = frame.PostProcess;

		// TAA resolves the final scene color in place (copies its result back over
		// scene color), so it runs before bloom / auto-exposure / composite. The
		// scene-color write is a side effect not tracked as a graph texture.
		if (m_Options.EnableTAA)
		{
			constexpr auto taaFlags = static_cast<RenderGraph::PassFlags>(
				static_cast<uint32_t>(RenderGraph::PassFlags::Compute) | static_cast<uint32_t>(RenderGraph::PassFlags::SideEffect));
			addPass("TAA", sceneColorCurrent, {}, taaFlags, makeExecute(&SceneRenderer::TAAResolvePass));
		}

		// Histogram auto-exposure reads the final scene color and writes the exposure
		// state buffer (a side effect not tracked as a graph texture, so it is pinned).
		if (postProcessSettings.ExposureControl == ExposureMode::Automatic)
		{
			constexpr auto autoExposureFlags = static_cast<RenderGraph::PassFlags>(
				static_cast<uint32_t>(RenderGraph::PassFlags::Compute) | static_cast<uint32_t>(RenderGraph::PassFlags::SideEffect));
			addPass("Auto Exposure", sceneColorCurrent, {}, autoExposureFlags, makeExecute(&SceneRenderer::AutoExposurePass));
		}

		std::vector<RenderGraph::ResourceHandle> bloomOutputs;
		if (postProcessSettings.BloomEnabled)
		{
			for (uint32_t index = 0; index < m_BloomComputeTextures.size(); index++)
			{
				if (m_BloomComputeTextures[index].Texture)
				{
					std::string bloomName = captureNames ? std::format("Bloom {}", index) : std::string{};
					bloomOutputs.push_back(addTexture(bloomName, m_BloomComputeTextures[index].Texture->GetImage()));
				}
			}
			addPass("Bloom", sceneColorCurrent, bloomOutputs, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::BloomCompute));
		}

		std::vector<RenderGraph::ResourceHandle> compositeReads = sceneColorCurrent;
		appendResources(compositeReads, geometryOutputs);
		appendResources(compositeReads, ssrOutputs);
		appendResources(compositeReads, bloomOutputs);
		appendResources(compositeReads, preDepthOutputs);
		std::vector<RenderGraph::ResourceHandle> compositeOutputs = addFramebufferResources("Composite", m_CompositingFramebuffer);
		addPass("Composite", compositeReads, compositeOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::CompositePass));

		const bool compositeDOFIntoFinalTarget = CanCompositeDOFIntoFinalTarget();
		std::vector<RenderGraph::ResourceHandle> dofOutputs = postProcessSettings.DOFEnabled
			? addRenderPassResources("DOF", m_DOFPass)
			: std::vector<RenderGraph::ResourceHandle>{};
		if (compositeDOFIntoFinalTarget)
		{
			std::vector<RenderGraph::ResourceHandle> dofWrites = dofOutputs;
			appendResources(dofWrites, compositeOutputs);
			addPass("DOF", compositeOutputs, dofWrites, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::DOFPass));
		}

		if (executable && m_WorldOverlayRenderCallback)
		{
			addPass("WorldOverlay2D", compositeOutputs, compositeOutputs, RenderGraph::PassFlags::Graphics,
				[this]()
				{
					m_CommandBuffer->End();
					m_CommandBuffer->Submit();

					ScopedCPUProfile cpuProfile(*this, "WorldOverlay2D");
					m_WorldOverlayRenderCallback();
					m_WorldOverlayRenderCallback = nullptr;

					m_CommandBuffer->Begin();
				});
		}

		if (jumpFloodActive)
		{
			std::vector<RenderGraph::ResourceHandle> jumpFloodCompositeReads = compositeOutputs;
			appendResources(jumpFloodCompositeReads, jumpFloodAOutputs);
			appendResources(jumpFloodCompositeReads, jumpFloodBOutputs);
			addPass("JumpFlood Composite", jumpFloodCompositeReads, addRenderPassResources("JumpFlood Composite", m_JumpFloodCompositePass), RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::JumpFloodCompositePass));
		}

		if (m_Options.ShowGrid)
			addPass("Grid", compositeOutputs, addRenderPassResources("Grid", m_GridRenderPass), RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::GridPass));

		if (executable && m_DebugRenderer && !m_DebugRenderer->GetRenderQueue().empty())
		{
			addPass("Renderer2D Overlay", compositeOutputs, compositeOutputs, RenderGraph::PassFlags::Graphics,
				[this]()
				{
					ScopedCPUProfile cpuProfile(*this, "Renderer2D");

					const auto& sceneCamera = m_SceneData.SceneCamera;
					const glm::mat4 viewProj = sceneCamera.Camera.GetProjectionMatrix() * sceneCamera.ViewMatrix;

					Ref<Renderer2D> overlayRenderer = m_Renderer2DScreenSpace ? m_Renderer2DScreenSpace : m_Renderer2D;
					overlayRenderer->SetTargetFramebuffer(m_CompositingFramebuffer);
					overlayRenderer->ResetStats();
					overlayRenderer->BeginScene(viewProj, sceneCamera.ViewMatrix);

					for (auto& fn : m_DebugRenderer->GetRenderQueue())
						fn(overlayRenderer);
					m_DebugRenderer->ClearRenderQueue();

					overlayRenderer->EndScene();
				});
		}

		if (postProcessSettings.DOFEnabled && !compositeDOFIntoFinalTarget)
			addPass("DOF", compositeOutputs, dofOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::DOFPass));
	}

	SceneRenderer::RenderGraphDebugSnapshot SceneRenderer::GetRenderGraphDebugSnapshot()
	{
		BuildRenderGraph();

		RenderGraphDebugSnapshot snapshot;
		const auto compileResult = m_RenderGraph.Compile();
		const auto& textures = m_RenderGraph.GetTextures();
		const auto& passes = m_RenderGraph.GetPasses();
		snapshot.ErrorCount = compileResult.ErrorCount;
		snapshot.WarningCount = compileResult.WarningCount;
		snapshot.InfoCount = compileResult.InfoCount;
		snapshot.ExecutedPassCount = static_cast<uint32_t>(compileResult.ExecutionOrder.size());
		snapshot.CulledPassCount = static_cast<uint32_t>(compileResult.CulledPasses.size());

		auto findProfile = [&](const std::string& passName) -> const PassProfile*
			{
				auto remapProfileName = [](const std::string& name) -> const char*
					{
						if (name == "Directional Shadow Maps") return "ShadowMapPass";
						if (name == "Spot Shadow Maps") return "SpotShadowMapPass";
						if (name == "PreDepth") return "PreDepthPass";
						if (name == "Mesh Culling") return "MeshCullingPass";
						if (name == "Skybox") return "SkyboxPass";
						if (name == "Sky Atmosphere") return "SkyAtmospherePass";
						if (name == "Volumetric Clouds") return "VolumetricCloudPass";
						if (name == "Volumetric Cloud Temporal") return "VolumetricCloudTemporalPass";
						if (name == "Volumetric Cloud Composite") return "VolumetricCloudCompositePass";
						if (name == "Atmospheric Fog") return "AtmosphericFogPass";
						if (name == "Selected Geometry") return "SelectedGeometryPass";
						if (name == "GBuffer") return "GBufferPass";
						if (name == "Deferred Lighting") return "DeferredLightingPass";
						if (name == "Transparent Forward") return "TransparentForwardPass";
						if (name == "Geometry Wireframe") return "GeometryWireframePass";
						if (name == "GBuffer Debug") return "GBufferDebugPass";
						if (name == "GTAO Denoise") return "GTAO-Denoise";
						if (name == "GTAO Temporal") return "GTAO-Temporal";
						if (name == "AO Composite") return "AOComposite";
						if (name == "AO Debug") return "AODebug";
						if (name == "Pre-Convolution") return "PreConvolution";
						if (name == "SSR Temporal") return "SSR-Temporal";
						if (name == "SSR Composite") return "SSRComposite";
						if (name == "JumpFlood Composite") return "JumpFloodComposite";
						if (name == "Bloom") return "BloomCompute";
						if (name == "Composite") return "CompositePass";
						if (name == "Grid") return "GridPass";
						if (name == "Cluster Build") return "ClusterBuildPass";
						if (name == "Cluster Light Culling") return "ClusterLightCullingPass";
						return name.c_str();
					};

				const char* profileName = remapProfileName(passName);
				for (const PassProfile& profile : m_Statistics.PassProfiles)
				{
					if (profile.Name && std::strcmp(profile.Name, profileName) == 0)
						return &profile;
				}
				return nullptr;
			};

		snapshot.Textures.reserve(textures.size());
		for (uint32_t resource = 0; resource < textures.size(); resource++)
		{
			const RenderGraph::TextureDesc& texture = textures[resource];
			RenderGraphTextureDebugInfo& textureInfo = snapshot.Textures.emplace_back();
			textureInfo.Resource = resource;
			textureInfo.Name = texture.Name;
			textureInfo.Format = texture.Format;
			textureInfo.Usage = texture.Usage;
			textureInfo.Dimension = texture.Dimension;
			textureInfo.Width = texture.Width;
			textureInfo.Height = texture.Height;
			textureInfo.Mips = texture.Mips;
			textureInfo.Layers = texture.Layers;
			textureInfo.EstimatedBytes = Utils::GetImageMemorySize(texture.Format, texture.Width, texture.Height, texture.Mips, texture.Layers);
			textureInfo.Transient = texture.Transient;
			textureInfo.AllowAlias = texture.AllowAlias;

			if (resource < compileResult.Lifetimes.size())
			{
				const RenderGraph::ResourceLifetime& lifetime = compileResult.Lifetimes[resource];
				textureInfo.FirstPass = lifetime.FirstPass;
				textureInfo.LastPass = lifetime.LastPass;
				textureInfo.AliasGroup = lifetime.AliasIndex;
			}

			if (resource < compileResult.ResourceFirstWriter.size())
				textureInfo.FirstWriter = compileResult.ResourceFirstWriter[resource];
			if (resource < compileResult.ResourceLastReader.size())
				textureInfo.LastReader = compileResult.ResourceLastReader[resource];
			if (resource < compileResult.ResourceConsumers.size())
				textureInfo.Consumers = compileResult.ResourceConsumers[resource];

			if (texture.Image)
			{
				textureInfo.AliasedNow = texture.Image->IsTransientAlias();
				textureInfo.CurrentState = texture.Image->GetImageInfo().State;
			}
		}

		for (const RenderGraph::ResourceLifetime& lifetime : compileResult.Lifetimes)
		{
			if (lifetime.FirstPass == UINT32_MAX || lifetime.Resource >= textures.size())
				continue;

			const RenderGraph::TextureDesc& texture = textures[lifetime.Resource];
			if (!texture.Transient || !texture.AllowAlias)
				continue;

			snapshot.TransientBytes += Utils::GetImageMemorySize(texture.Format, texture.Width, texture.Height, texture.Mips, texture.Layers);
		}

		snapshot.AliasGroups.reserve(compileResult.AliasGroups.size());
		for (const RenderGraph::AliasGroupSummary& aliasGroup : compileResult.AliasGroups)
		{
			RenderGraphAliasGroupDebugInfo& aliasInfo = snapshot.AliasGroups.emplace_back();
			aliasInfo.AliasGroup = aliasGroup.AliasIndex;
			aliasInfo.Compatible = aliasGroup.Compatible;

			for (RenderGraph::ResourceHandle resource : aliasGroup.Resources)
			{
				if (resource >= textures.size())
					continue;

				aliasInfo.Resources.push_back(resource);
				const RenderGraph::TextureDesc& texture = textures[resource];
				const uint64_t size = Utils::GetImageMemorySize(texture.Format, texture.Width, texture.Height, texture.Mips, texture.Layers);
				aliasInfo.EstimatedBytes += size;
				aliasInfo.BackingBytes = std::max(aliasInfo.BackingBytes, size);
			}

			aliasInfo.SavedBytes = aliasInfo.EstimatedBytes > aliasInfo.BackingBytes ? aliasInfo.EstimatedBytes - aliasInfo.BackingBytes : 0;
			snapshot.AliasedBytes += aliasInfo.BackingBytes;
			snapshot.SavedBytes += aliasInfo.SavedBytes;
		}

		auto containsResource = [](const std::vector<RenderGraph::ResourceHandle>& resources, RenderGraph::ResourceHandle resource)
			{
				return std::find(resources.begin(), resources.end(), resource) != resources.end();
			};

		auto accessState = [&](const RenderGraph::PassDesc& pass, RenderGraph::ResourceHandle resource, bool asInput)
			{
				const bool read = containsResource(pass.Reads, resource);
				const bool write = containsResource(pass.Writes, resource);
				if (read && write)
					return std::string("ReadWrite");
				return std::string(asInput ? "Read" : "Write");
			};

		std::vector<bool> culledPasses(passes.size(), false);
		for (uint32_t passIndex : compileResult.CulledPasses)
		{
			if (passIndex < culledPasses.size())
				culledPasses[passIndex] = true;
		}

		snapshot.Passes.reserve(passes.size());
		for (uint32_t passIndex = 0; passIndex < passes.size(); passIndex++)
		{
			const RenderGraph::PassDesc& pass = passes[passIndex];
			RenderGraphPassDebugInfo& passInfo = snapshot.Passes.emplace_back();
			passInfo.Index = passIndex;
			passInfo.Name = pass.Name;
			passInfo.Flags = static_cast<uint32_t>(pass.Flags);
			passInfo.Executable = static_cast<bool>(pass.Execute);
			passInfo.Culled = culledPasses[passIndex];
			if (const PassProfile* profile = findProfile(pass.Name))
			{
				passInfo.CPUTime = profile->CPUTime;
				passInfo.GPUTime = profile->GPUTime;
			}

			for (RenderGraph::ResourceHandle resource : pass.Reads)
			{
				passInfo.Inputs.push_back({ resource, accessState(pass, resource, true) });
			}

			for (RenderGraph::ResourceHandle resource : pass.Writes)
			{
				passInfo.Outputs.push_back({ resource, accessState(pass, resource, false) });
			}
		}

		snapshot.Diagnostics.reserve(compileResult.Diagnostics.size());
		for (uint32_t diagnosticIndex = 0; diagnosticIndex < compileResult.Diagnostics.size(); diagnosticIndex++)
		{
			const RenderGraph::Diagnostic& diagnostic = compileResult.Diagnostics[diagnosticIndex];
			RenderGraphDiagnosticDebugInfo& debugDiagnostic = snapshot.Diagnostics.emplace_back();
			debugDiagnostic.Severity = diagnostic.Severity;
			debugDiagnostic.Code = diagnostic.Code;
			debugDiagnostic.PassIndex = diagnostic.PassIndex;
			debugDiagnostic.PassName = diagnostic.PassName;
			debugDiagnostic.Resource = diagnostic.Resource;
			debugDiagnostic.ResourceName = diagnostic.ResourceName;
			debugDiagnostic.Message = diagnostic.Message;

			if (diagnostic.PassIndex < snapshot.Passes.size())
				snapshot.Passes[diagnostic.PassIndex].Diagnostics.push_back(diagnosticIndex);

			if (diagnostic.Resource < snapshot.Textures.size())
			{
				RenderGraphTextureDebugInfo& textureInfo = snapshot.Textures[diagnostic.Resource];
				textureInfo.DiagnosticCount++;
				if (diagnostic.Severity == RenderGraph::DiagnosticSeverity::Error)
					textureInfo.ErrorCount++;
				else if (diagnostic.Severity == RenderGraph::DiagnosticSeverity::Warning)
					textureInfo.WarningCount++;
			}
		}

		return snapshot;
	}

	void SceneRenderer::UpdateRenderGraphStatistics()
	{
		auto& memoryStats = m_Statistics.MemoryStats;
		memoryStats.RenderGraphTransientBytes = 0;
		memoryStats.RenderGraphAliasedBytes = 0;
		memoryStats.RenderGraphSavedBytes = 0;
		memoryStats.RenderGraphPassCount = 0;
		memoryStats.RenderGraphTransientCount = 0;
		memoryStats.RenderGraphAliasGroupCount = 0;

		// Reuse the executable graph already built and rendered this frame (this runs
		// from UpdateStatistics, after Build/Execute). Memory stats only read texture
		// descs and the alias plan — never names or execute callbacks — so a second
		// full BuildRenderGraph() here was pure per-frame waste.

		const auto lifetimes = m_RenderGraph.BuildAliasPlan();
		const auto& textures = m_RenderGraph.GetTextures();
		std::vector<uint64_t> aliasBytes;
		for (const RenderGraph::ResourceLifetime& lifetime : lifetimes)
		{
			if (lifetime.FirstPass == UINT32_MAX || lifetime.Resource >= textures.size())
				continue;

			const RenderGraph::TextureDesc& texture = textures[lifetime.Resource];
			if (!texture.Transient || !texture.AllowAlias)
				continue;

			const uint64_t size = Utils::GetImageMemorySize(texture.Format, texture.Width, texture.Height, texture.Mips, texture.Layers);
			memoryStats.RenderGraphTransientBytes += size;
			memoryStats.RenderGraphTransientCount++;

			if (lifetime.AliasIndex != UINT32_MAX)
			{
				if (aliasBytes.size() <= lifetime.AliasIndex)
					aliasBytes.resize(lifetime.AliasIndex + 1);
				aliasBytes[lifetime.AliasIndex] = std::max(aliasBytes[lifetime.AliasIndex], size);
			}
		}

		for (uint64_t size : aliasBytes)
			memoryStats.RenderGraphAliasedBytes += size;

		memoryStats.RenderGraphSavedBytes = memoryStats.RenderGraphTransientBytes > memoryStats.RenderGraphAliasedBytes
			? memoryStats.RenderGraphTransientBytes - memoryStats.RenderGraphAliasedBytes
			: 0;
		memoryStats.RenderGraphPassCount = static_cast<uint32_t>(m_RenderGraph.GetPasses().size());
		memoryStats.RenderGraphAliasGroupCount = static_cast<uint32_t>(aliasBytes.size());
	}

	void SceneRenderer::ApplyRenderTargetAliasing()
	{
		if (m_RenderTargetAliasingApplied)
			ClearRenderTargetAliasing(true);

		BuildRenderGraph();

		const auto lifetimes = m_RenderGraph.BuildAliasPlan();
		const auto& textures = m_RenderGraph.GetTextures();

		std::unordered_map<uint32_t, std::vector<RenderGraph::ResourceLifetime>> aliasGroups;
		for (const RenderGraph::ResourceLifetime& lifetime : lifetimes)
		{
			if (lifetime.FirstPass == UINT32_MAX || lifetime.AliasIndex == UINT32_MAX || lifetime.Resource >= textures.size())
				continue;

			const RenderGraph::TextureDesc& texture = textures[lifetime.Resource];
			if (!texture.Image || !texture.Transient || !texture.AllowAlias)
				continue;

			aliasGroups[lifetime.AliasIndex].push_back(lifetime);
		}

		m_RenderGraphAliasedImages.clear();
		for (auto& [aliasIndex, group] : aliasGroups)
		{
			if (group.size() < 2)
				continue;

			std::sort(group.begin(), group.end(), [](const RenderGraph::ResourceLifetime& lhs, const RenderGraph::ResourceLifetime& rhs)
				{
					if (lhs.FirstPass != rhs.FirstPass)
						return lhs.FirstPass < rhs.FirstPass;
					return lhs.LastPass < rhs.LastPass;
				});

			Ref<Image2D> aliasSource = textures[group.front().Resource].Image;
			if (!aliasSource || aliasSource->IsTransientAlias())
				continue;

			for (size_t i = 1; i < group.size(); i++)
			{
				Ref<Image2D> image = textures[group[i].Resource].Image;
				if (!image || image == aliasSource)
					continue;

				image->SetTransientAliasSource(aliasSource);
				m_RenderGraphAliasedImages.push_back(image);
			}
		}

		m_RenderTargetAliasingApplied = !m_RenderGraphAliasedImages.empty();
		if (m_RenderTargetAliasingApplied)
		{
			RecreateRenderTargetFramebuffers();
			RefreshRenderTargetImageViews();
		}
	}

	void SceneRenderer::ClearRenderTargetAliasing(bool recreateResources)
	{
		if (m_RenderGraphAliasedImages.empty())
		{
			m_RenderTargetAliasingApplied = false;
			return;
		}

		for (Ref<Image2D>& image : m_RenderGraphAliasedImages)
		{
			if (!image)
				continue;

			image->ClearTransientAliasSource();
			if (recreateResources)
				image->RT_Invalidate();
		}

		m_RenderGraphAliasedImages.clear();
		m_RenderTargetAliasingApplied = false;

		if (recreateResources)
		{
			RecreateRenderTargetFramebuffers();
			RefreshRenderTargetImageViews();
		}
	}

	void SceneRenderer::RecreateRenderTargetFramebuffers()
	{
		std::unordered_set<const Framebuffer*> recreated;

		auto recreateFramebuffer = [&](Ref<Framebuffer> framebuffer)
			{
				if (!framebuffer || framebuffer->GetSpecification().SwapChainTarget)
					return;

				if (!recreated.insert(framebuffer.Raw()).second)
					return;

				framebuffer->Resize(m_ViewportWidth, m_ViewportHeight, true);
			};

		auto recreatePassFramebuffer = [&](Ref<RenderPass> pass)
			{
				if (pass)
					recreateFramebuffer(pass->GetTargetFramebuffer());
			};

		recreateFramebuffer(m_GeometryPassFramebuffer);
		recreateFramebuffer(m_SceneColorFramebuffer);
		recreateFramebuffer(m_CompositingFramebuffer);

		recreatePassFramebuffer(m_PreDepthPass);
		recreatePassFramebuffer(m_GeometryPass);
		recreatePassFramebuffer(m_GeometryPassTransparent);
		recreatePassFramebuffer(m_DeferredLightingPass);
		recreatePassFramebuffer(m_GBufferDebugPass);
		recreatePassFramebuffer(m_SkyboxPass);
		recreatePassFramebuffer(m_SkyAtmospherePass);
		recreatePassFramebuffer(m_VolumetricCloudCompositePass);
		recreatePassFramebuffer(m_AtmosphericFogPass);
		recreatePassFramebuffer(m_SelectedGeometryPass);
		recreatePassFramebuffer(m_GeometryWireframePass);
		recreatePassFramebuffer(m_AOCompositePass);
		recreatePassFramebuffer(m_AODebugPass);
		recreatePassFramebuffer(m_SSRCompositePass);
		recreatePassFramebuffer(m_JumpFloodInitPass);
		recreatePassFramebuffer(m_JumpFloodPasses[0]);
		recreatePassFramebuffer(m_JumpFloodPasses[1]);
		recreatePassFramebuffer(m_JumpFloodCompositePass);
		recreatePassFramebuffer(m_CompositePass);
		recreatePassFramebuffer(m_GridRenderPass);
		recreatePassFramebuffer(m_DOFPass);
		ResizeVolumetricCloudResources(true);
	}

	void SceneRenderer::RefreshRenderTargetImageViews()
	{
		auto invalidateViews = [](std::vector<Ref<ImageView>>& imageViews)
			{
				for (Ref<ImageView>& imageView : imageViews)
				{
					if (imageView)
						imageView->Invalidate();
				}
			};

		invalidateViews(m_HierarchicalDepthTexture.ImageViews);
		invalidateViews(m_PreIntegrationVisibilityTexture.ImageViews);
		invalidateViews(m_PreConvolutedTexture.ImageViews);
		for (BloomComputeTextures& bloomTexture : m_BloomComputeTextures)
			invalidateViews(bloomTexture.ImageViews);
	}

	bool SceneRenderer::IsRenderGraphAliasCandidate(const Ref<Image2D>& image)
	{
		if (!image || !image->IsValid())
			return false;

		const ImageSpecification& spec = image->GetSpecification();
		if (spec.Usage != ImageUsage::Attachment && spec.Usage != ImageUsage::Storage)
			return false;
		if (spec.Dimension != nvrhi::TextureDimension::Texture2D || spec.Layers != 1)
			return false;
		if (spec.Width == 0 || spec.Height == 0 || spec.Format == ImageFormat::None)
			return false;
		if (Utils::IsDepthFormat(spec.Format) || Utils::IsBlockCompressed(spec.Format))
			return false;

		auto isSameImage = [&](const Ref<Image2D>& other)
			{
				return other && other.Raw() == image.Raw();
			};

		if (isSameImage(m_ShadowMapImage) || isSameImage(m_SpotShadowMapImage))
			return false;
		if (m_HierarchicalDepthTexture.Texture && isSameImage(m_HierarchicalDepthTexture.Texture->GetImage()))
			return false;
		if (m_PreDepthPass && isSameImage(m_PreDepthPass->GetDepthOutput()))
			return false;
		for (const Ref<Image2D>& historyImage : m_GTAOHistoryImages)
		{
			if (isSameImage(historyImage))
				return false;
		}
		for (const Ref<Image2D>& historyImage : m_SSRHistoryImages)
		{
			if (isSameImage(historyImage))
				return false;
		}
		for (const Ref<Image2D>& historyImage : m_CloudHistoryImages)
		{
			if (isSameImage(historyImage))
				return false;
		}
		if (m_DOFPass && isSameImage(m_DOFPass->GetOutput(0)))
			return false;

		auto isFramebufferImage = [&](const Ref<Framebuffer>& framebuffer)
			{
				if (!framebuffer)
					return false;

				for (uint32_t attachment = 0; attachment < framebuffer->GetColorAttachmentCount(); attachment++)
				{
					if (isSameImage(framebuffer->GetImage(attachment)))
						return true;
				}

				return framebuffer->HasDepthAttachment() && isSameImage(framebuffer->GetDepthImage());
			};

		if (isFramebufferImage(m_GeometryPassFramebuffer))
			return false;
		if (isFramebufferImage(m_SceneColorFramebuffer))
			return false;
		if (isFramebufferImage(m_CompositingFramebuffer))
			return false;

		return true;
	}

	void SceneRenderer::BeginScene(const SceneRendererCamera& camera)
	{
		LUX_PROFILE_FUNCTION("SceneRenderer::BeginScene");
		LUX_CORE_ASSERT(m_Scene, "No scene attached to SceneRenderer");
		LUX_CORE_ASSERT(!m_Active, "BeginScene called twice without EndScene");
		m_Active = true;
		ResetProfilingData();
		m_FrameCullingStats = {};
		m_MeshDrawCommandCacheFrame++;
		m_ShadowCascadeFrustumCount = 0;
		m_SpotShadowFrustumCount = 0;
		m_SubmittedRenderScene = nullptr;

		if (m_ResourcesCreatedGPU)
			m_ResourcesCreated = true;

		if (!m_ResourcesCreated)
			return; // GPU resources not yet available

		// Open the upload command buffer for uniform/storage buffer writes
		m_UploadCommandBuffer->Begin();

		m_SceneData.SceneCamera = camera;
		m_SceneData.CameraFrustum = Frustum::FromViewProjection(camera.Camera.GetProjectionMatrix() * camera.ViewMatrix);
		RefreshFrameEnvironment();

		// ── Handle viewport resize ────────────────────────────────────────────
		if (m_NeedsResize)
		{
			m_NeedsResize = false;
			m_ScreenSpaceProjectionMatrix = glm::ortho(0.0f, (float)m_ViewportWidth, 0.0f, (float)m_ViewportHeight);
			ClearRenderTargetAliasing(false);

			m_PreDepthPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPassFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SceneColorFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPassTransparent->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_DeferredLightingPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GBufferDebugPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SkyboxPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SkyAtmospherePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_VolumetricCloudCompositePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_AtmosphericFogPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SelectedGeometryPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryWireframePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_CompositingFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_CompositePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GridRenderPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			ResizeScreenSpaceEffectResources();
			ApplyRenderTargetAliasing();
			m_ShadowCascadeCacheValid = false;
			m_DirectionalShadowMapNeedsRender = true;
		}

		ResizeVolumetricCloudResources();

		// ── Camera uniform buffer ─────────────────────────────────────────────
		{
			const glm::mat4 unjitteredProj = camera.Camera.GetProjectionMatrix();
			const glm::mat4 unjitteredViewProj = unjitteredProj * camera.ViewMatrix;

			// TAA sub-pixel jitter (Halton 2,3) applied to the rasterized projection only.
			// The unjittered VP is kept for motion-vector reprojection / history sampling.
			glm::vec2 jitter(0.0f);
			if (m_Options.EnableTAA)
			{
				auto halton = [](uint32_t index, uint32_t base) -> float
				{
					float result = 0.0f, invBase = 1.0f / float(base), fraction = invBase;
					for (uint32_t i = index; i > 0u; i /= base)
					{
						result += float(i % base) * fraction;
						fraction *= invBase;
					}
					return result;
				};
				const uint32_t sampleIndex = (m_TAAJitterIndex % 8u) + 1u;
				const float jx = halton(sampleIndex, 2u) - 0.5f;
				const float jy = halton(sampleIndex, 3u) - 0.5f;
				jitter = glm::vec2(jx * 2.0f / float(glm::max(1u, m_ViewportWidth)),
				                   jy * 2.0f / float(glm::max(1u, m_ViewportHeight)));
			}

			glm::mat4 jitteredProj = unjitteredProj;
			jitteredProj[2][0] += jitter.x;
			jitteredProj[2][1] += jitter.y;
			const glm::mat4 viewProj = jitteredProj * camera.ViewMatrix;
			const glm::mat4 viewInverse = glm::inverse(camera.ViewMatrix);
			const glm::mat4 projInverse = glm::inverse(jitteredProj);

			m_CurrentViewProjection = unjitteredViewProj; // history/reprojection use unjittered
			m_CurrentJitter = jitter;
			if (!m_TemporalHistoryValid)
			{
				m_PreviousViewProjection = unjitteredViewProj;
				m_PreviousJitter = jitter;
			}

			m_CameraUB.ViewProjection = viewProj;
			m_CameraUB.InverseViewProjection = viewInverse * projInverse;
			m_CameraUB.Projection = jitteredProj;
			m_CameraUB.InverseProjection = projInverse;
			m_CameraUB.View = camera.ViewMatrix;
			m_CameraUB.InverseView = viewInverse;
			m_CameraUB.UnjitteredViewProjection = unjitteredViewProj;
			m_CameraUB.PreviousViewProjection = m_PreviousViewProjection;
			m_CameraUB.Jitter = jitter;
			m_CameraUB.PreviousJitter = m_PreviousJitter;

			// Depth linearization for GTAO-style effects (kept for forward compat)
			float depthLinearizeMul = -m_CameraUB.Projection[3][2];
			float depthLinearizeAdd = m_CameraUB.Projection[2][2];
			if (depthLinearizeMul * depthLinearizeAdd < 0) depthLinearizeAdd = -depthLinearizeAdd;
			m_CameraUB.DepthUnpackConsts = { depthLinearizeMul, depthLinearizeAdd };

			const float* P = glm::value_ptr(camera.Camera.GetProjectionMatrix());
			m_CameraUB.NDCToViewMul = { 2.0f / P[0],  2.0f / P[5] };
			m_CameraUB.NDCToViewAdd = { -1.0f,        -1.0f };
			m_CameraUB.CameraTanHalfFOV = { 1.0f / P[0],  1.0f / P[5] };

			auto cameraData = m_CameraUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, cameraData]() mutable {
				instance->m_UBSCamera->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &cameraData, sizeof(UBCamera));
				});
		}

		// ── Screen uniform buffer ─────────────────────────────────────────────
		{
			const glm::vec2 fullResolution = {
				glm::max(1.0f, static_cast<float>(m_ViewportWidth)),
				glm::max(1.0f, static_cast<float>(m_ViewportHeight))
			};
			const glm::vec2 halfResolution = {
				static_cast<float>((glm::max(1u, m_ViewportWidth) + 1u) / 2u),
				static_cast<float>((glm::max(1u, m_ViewportHeight) + 1u) / 2u)
			};
			const glm::vec2 quarterResolution = {
				static_cast<float>((glm::max(1u, m_ViewportWidth) + 3u) / 4u),
				static_cast<float>((glm::max(1u, m_ViewportHeight) + 3u) / 4u)
			};

			m_ScreenDataUB.FullResolution = fullResolution;
			m_ScreenDataUB.InvFullResolution = 1.0f / fullResolution;
			m_ScreenDataUB.HalfResolution = halfResolution;
			m_ScreenDataUB.InvHalfResolution = 1.0f / halfResolution;
			m_ScreenDataUB.QuarterResolution = quarterResolution;
			m_ScreenDataUB.InvQuarterResolution = 1.0f / quarterResolution;

			auto screenData = m_ScreenDataUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, screenData]() mutable {
				instance->m_UBSScreenData->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &screenData, sizeof(UBScreenData));
				});
		}

		// ── Screen-space effect constants ────────────────────────────────────
		{
			const uint32_t gtaoResolutionScale = GetEffectResolutionDivisor(m_Options.GTAOResolutionScale);
			const glm::uvec2 gtaoExtent = GetScaledExtent({ glm::max(1u, m_ViewportWidth), glm::max(1u, m_ViewportHeight) }, m_Options.GTAOResolutionScale);
			const glm::vec2 gtaoPixelSize = 1.0f / glm::vec2(gtaoExtent);
			m_GTAODataCB.ResolutionScale = gtaoResolutionScale;
			m_GTAODataCB.NDCToViewMul_x_PixelSize = m_CameraUB.NDCToViewMul * gtaoPixelSize;
			m_GTAODataCB.HZBUVFactor = m_SSROptions.HZBUvFactor;
			m_GTAODataCB.NoiseIndex = (int)(Renderer::GetCurrentFrameIndex() % 64);
			m_GTAODataCB.ShadowTolerance = m_Options.AOShadowTolerance;
			m_GTAODataCB.TemporalAccumulation = m_Options.EnableGTAOTemporalAccumulation ? 1u : 0u;
			m_GTAODataCB.TemporalBlend = m_Options.GTAOTemporalBlend;
			m_GTAODataCB.SliceCount = m_Options.EnableGTAOTemporalAccumulation ? 6u : 9u;
			m_GTAODataCB.StepsPerSlice = m_Options.EnableGTAOTemporalAccumulation ? 2u : 3u;

			m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
			m_SSROptions.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);
			m_SSROptions.HalfRes = m_SSROptions.ResolutionScale > 1u;
			m_SSROptions.TemporalAccumulation = m_Options.EnableSSRTemporalAccumulation ? 1u : 0u;
			m_SSROptions.TemporalBlend = m_Options.SSRTemporalBlend;
		}

		// ── Scene (light) uniform buffer ──────────────────────────────────────
		{
			const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
			m_SceneUB.Lights.Direction = dirLight.Direction;
			m_SceneUB.Lights.Radiance = dirLight.Radiance;
			m_SceneUB.Lights.Intensity = dirLight.Intensity;
			m_SceneUB.Lights.ShadowAmount = dirLight.ShadowAmount;
			m_SceneUB.CameraPosition = glm::vec3(glm::inverse(camera.ViewMatrix)[3]);
			m_SceneUB.EnvironmentMapIntensity = m_FrameEnvironment.EnvironmentIntensity;

			auto sceneData = m_SceneUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, sceneData]() mutable {
				instance->m_UBSScene->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &sceneData, sizeof(UBScene));
				});
		}

		// ── Atmosphere uniform buffer ────────────────────────────────────────
		{
			const SkyAtmosphereSettings& sky = m_FrameEnvironment.Atmosphere.SkyAtmosphere;
			const VolumetricCloudSettings& clouds = m_FrameEnvironment.Atmosphere.VolumetricClouds;
			const ExponentialHeightFogSettings& fog = m_FrameEnvironment.Atmosphere.HeightFog;

			glm::vec2 windDirection = clouds.WindDirection;
			if (glm::dot(windDirection, windDirection) > 0.0001f)
				windDirection = glm::normalize(windDirection);
			else
				windDirection = { 1.0f, 0.0f };

			m_AtmosphereUB.RayleighScattering = glm::vec4(glm::max(sky.RayleighScattering, glm::vec3(0.0f)), glm::max(sky.RayleighScatteringScale, 0.0f));
			m_AtmosphereUB.MieScattering = glm::vec4(glm::max(sky.MieScattering, glm::vec3(0.0f)), glm::max(sky.MieScatteringScale, 0.0f));
			m_AtmosphereUB.MieAbsorption = glm::vec4(glm::max(sky.MieAbsorption, glm::vec3(0.0f)), glm::clamp(sky.MieAnisotropy, -0.95f, 0.95f));
			m_AtmosphereUB.Absorption = glm::vec4(glm::max(sky.Absorption, glm::vec3(0.0f)), glm::max(sky.AbsorptionScale, 0.0f));
			m_AtmosphereUB.GroundAlbedo = glm::vec4(glm::clamp(sky.GroundAlbedo, glm::vec3(0.0f), glm::vec3(1.0f)), glm::clamp(sky.GroundContribution, 0.0f, 1.0f));
			m_AtmosphereUB.AtmosphereParams = {
				glm::max(sky.PlanetRadius, 1.0f),
				glm::max(sky.AtmosphereHeight, 1.0f),
				glm::max(sky.RayleighScaleHeight, 0.01f),
				glm::max(sky.MieScaleHeight, 0.01f)
			};
			m_AtmosphereUB.SunParams = {
				glm::max(sky.SunIntensity, 0.0f),
				glm::max(sky.SunAngularRadius, 0.0001f),
				glm::max(sky.MultiScattering, 0.0f),
				glm::max(sky.AerialPerspectiveViewDistanceScale, 0.0f)
			};
			m_AtmosphereUB.CloudGlobal0 = {
				glm::clamp(clouds.Coverage, 0.0f, 1.0f),
				glm::max(clouds.Density, 0.0f),
				windDirection.x,
				windDirection.y
			};
			m_AtmosphereUB.CloudGlobal1 = {
				clouds.WindSpeed,
				glm::max(0.0f, Application::Get().GetTime()),
				glm::max(clouds.WeatherScale, 1.0e-7f),
				glm::clamp(clouds.AerialPerspective, 0.0f, 1.0f)
			};
			m_AtmosphereUB.CloudLighting0 = glm::vec4(glm::max(clouds.Albedo, glm::vec3(0.0f)), glm::max(clouds.AmbientBoost, 0.0f));
			m_AtmosphereUB.CloudLighting1 = {
				glm::max(clouds.Extinction, 1.0e-4f),
				glm::max(clouds.ScatterMultiplier, 0.0f),
				glm::max(clouds.SilverIntensity, 0.0f),
				glm::clamp(clouds.PowderStrength, 0.0f, 2.0f)
			};
			m_AtmosphereUB.CloudLighting2 = {
				glm::clamp(clouds.PhaseG0, -0.95f, 0.95f),
				glm::clamp(clouds.PhaseG1, -0.95f, 0.95f),
				glm::clamp(clouds.PhaseBlend, 0.0f, 1.0f),
				glm::clamp(clouds.MultiScatter, 0.0f, 1.0f)
			};
			const float cloudMaxTraceDistance = glm::max(clouds.MaxTraceDistance, 1.0f);
			m_AtmosphereUB.CloudRender0 = {
				cloudMaxTraceDistance,
				glm::clamp(clouds.DistanceFade, 0.0f, cloudMaxTraceDistance),
				glm::clamp(clouds.LODStartDistance, 0.0f, cloudMaxTraceDistance),
				glm::max(clouds.ShadowTraceDistance, 0.0f)
			};
			m_AtmosphereUB.CloudRender1 = {
				(float)SanitizeCloudRenderScale(clouds.RenderScale),
				(float)glm::clamp(clouds.MarchSteps, 16u, 128u),
				(float)glm::clamp(clouds.ShadowSteps, 1u, 8u),
				(float)glm::clamp(clouds.ShadowSteps, 1u, 8u)
			};
			for (uint32_t layerIndex = 0; layerIndex < 3u; layerIndex++)
			{
				const CloudLayerSettings& layer = clouds.Layers[layerIndex];
				UBAtmosphere::CloudLayerData& gpu = m_AtmosphereUB.CloudLayers[layerIndex];
				gpu.Params0 = {
					layer.BottomAltitude,
					glm::max(layer.Thickness, 1.0f),
					glm::clamp(layer.Coverage, 0.0f, 1.0f),
					glm::clamp(layer.CloudShape, 0.0f, 1.0f)
				};
				gpu.Params1 = {
					glm::max(layer.DensityScale, 0.0f),
					glm::max(layer.ShapeScale, 1.0e-3f),
					glm::max(layer.DetailScale, 1.0e-3f),
					glm::clamp(layer.DetailStrength, 0.0f, 1.0f)
				};
				gpu.Params2 = {
					glm::max(layer.WindSpeedScale, 0.0f),
					layer.WindAngleOffset,
					glm::clamp(layer.AnvilBias, 0.0f, 1.0f),
					glm::clamp(layer.ErosionStrength, 0.0f, 1.0f)
				};
				gpu.Params3 = {
					(float)(uint32_t)layer.Tier,
					layer.Enabled ? 1.0f : 0.0f,
					glm::clamp(layer.CoverageBias, -1.0f, 1.0f),
					layer.HeightSkew
				};
			}
			m_AtmosphereUB.FogColorDensity = glm::vec4(glm::max(fog.FogColor, glm::vec3(0.0f)), glm::max(fog.FogDensity, 0.0f));
			m_AtmosphereUB.FogParams0 = {
				glm::max(fog.FogHeightFalloff, 0.0001f),
				glm::max(fog.StartDistance, 0.0f),
				glm::clamp(fog.MaxOpacity, 0.0f, 1.0f),
				glm::max(fog.CutoffDistance, fog.StartDistance + 1.0f)
			};
			m_AtmosphereUB.FogDirectionalInscattering = glm::vec4(
				glm::max(fog.DirectionalInscatteringColor, glm::vec3(0.0f)),
				glm::max(fog.DirectionalInscatteringExponent, 0.01f));
			m_AtmosphereUB.FogParams1 = {
				glm::max(fog.DirectionalInscatteringStartDistance, 0.0f),
				glm::max(fog.VolumetricScatteringIntensity, 0.0f),
				glm::clamp(fog.Anisotropy, -0.8f, 0.8f),
				0.0f
			};
			m_AtmosphereUB.Flags = {
				sky.Enabled ? 1u : 0u,
				clouds.Enabled ? 1u : 0u,
				fog.Enabled ? 1u : 0u,
				fog.VolumetricFog ? 1u : 0u
			};
			m_AtmosphereUB.Steps = {
				glm::clamp(clouds.MarchSteps, 8u, 128u),
				glm::clamp(clouds.ShadowSteps, 0u, 16u),
				(uint32_t)glm::max(0.0f, Application::Get().GetTime() * 60.0f),
				glm::clamp(fog.VolumetricFogSteps, 4u, 96u)
			};
			m_AtmosphereUB.LocalFogParams = {
				m_FrameEnvironment.Volumes.LocalFogVolumeCount,
				m_FrameEnvironment.Volumes.DroppedLocalFogVolumeCount,
				m_FrameEnvironment.Volumes.CulledLocalFogVolumeCount,
				m_FrameEnvironment.Volumes.ActiveLocalFogVolumeCount
			};
			for (uint32_t index = 0; index < m_FrameEnvironment.Volumes.LocalFogVolumeCount; index++)
			{
				const LocalFogVolumeGPUData& source = m_FrameEnvironment.Volumes.LocalFogVolumes[index];
				auto& destination = m_AtmosphereUB.LocalFogVolumes[index];
				destination.WorldToLocal = source.WorldToLocal;
				destination.ColorDensity = source.ColorDensity;
				destination.Params0 = source.Params0;
				destination.Params1 = source.Params1;
				destination.Metadata = source.Metadata;
			}

			auto atmosphereData = m_AtmosphereUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, atmosphereData]() mutable {
				instance->m_UBSAtmosphere->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &atmosphereData, sizeof(UBAtmosphere));
				});
		}

		// ── Point lights uniform buffer ───────────────────────────────────────
		{
			const auto& pointLights = m_SceneData.SceneLightEnvironment.PointLights;
			m_PointLightsUB.Count = (uint32_t)glm::min((size_t)256, pointLights.size());
			if (m_PointLightsUB.Count > 0)
				std::memcpy(m_PointLightsUB.PointLights, pointLights.data(),
					sizeof(PointLight) * m_PointLightsUB.Count);

			auto plData = m_PointLightsUB;
			uint32_t plSize = (uint32_t)(16ull + sizeof(PointLight) * plData.Count);
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, plData, plSize]() mutable {
				instance->m_UBSPointLights->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &plData, plSize);
				});
		}

		// ── Spot lights uniform buffer + shadow data ─────────────────────────-
		{
			const auto& spotLights = m_SceneData.SceneLightEnvironment.SpotLights;
			m_SpotLightsUB.Count = (uint32_t)glm::min((size_t)256, spotLights.size());
			m_SpotShadowCount = 0;

			if (m_SpotLightsUB.Count > 0)
				std::memcpy(m_SpotLightsUB.SpotLights, spotLights.data(),
					sizeof(SpotLight) * m_SpotLightsUB.Count);

			struct SpotShadowCandidate
			{
				uint32_t Index = 0;
				float Score = 0.0f;
				float ShadowDistance = 0.0f;
				uint32_t ResolutionTier = 0;
			};

			std::vector<SpotShadowCandidate> shadowCandidates;
			shadowCandidates.reserve(m_SpotLightsUB.Count);

			for (uint32_t i = 0; i < m_SpotLightsUB.Count; i++)
			{
				SpotLight& light = m_SpotLightsUB.SpotLights[i];
				light.ShadowIndex = 0;
				light.AtlasOffsetX = 0.0f;
				light.AtlasOffsetY = 0.0f;
				light.AtlasScale = 1.0f;

				if (!light.CastsShadows)
					continue;

				const float shadowDistance = ResolveShadowDistance(light.ShadowDistance, light.Range);
				const glm::vec3 toLight = light.Position - m_SceneUB.CameraPosition;
				const float distanceSquared = glm::dot(toLight, toLight);
				const float distance = glm::sqrt(glm::max(distanceSquared, 0.0f));
				if (distance > shadowDistance)
				{
					light.CastsShadows = 0;
					continue;
				}

				const float luminance = glm::max(CalculateLightLuminance(light.Radiance), 0.001f);
				const float angleWeight = glm::clamp(light.Angle / 60.0f, 0.25f, 2.5f);
				const float distanceWeight = 1.0f / glm::max(1.0f, distanceSquared);
				const float tierWeight = 1.0f + 0.25f * (float)glm::min(light.ShadowResolutionTier, 3u);
				const float score = light.Intensity * luminance * angleWeight * shadowDistance * distanceWeight * tierWeight;
				shadowCandidates.push_back({ i, score, shadowDistance, glm::min(light.ShadowResolutionTier, 3u) });
			}

			std::sort(shadowCandidates.begin(), shadowCandidates.end(), [](const SpotShadowCandidate& a, const SpotShadowCandidate& b)
			{
				if (a.Score == b.Score)
					return a.Index < b.Index;
				return a.Score > b.Score;
			});

			const uint32_t selectedShadowCount = glm::min((uint32_t)shadowCandidates.size(), (uint32_t)MaxSpotShadows);
			std::array<bool, 256> selectedShadowCasters{};
			uint32_t maxSelectedTier = 1;
			for (uint32_t i = 0; i < selectedShadowCount; i++)
			{
				selectedShadowCasters[shadowCandidates[i].Index] = true;
				maxSelectedTier = glm::max(maxSelectedTier, shadowCandidates[i].ResolutionTier);
			}

			m_SpotShadowMapSize = ResolveShadowResolutionTier(maxSelectedTier);
			m_SpotShadowAtlasGridSize = (uint32_t)glm::ceil(glm::sqrt((float)glm::max(1u, selectedShadowCount)));
			m_SpotShadowAtlasGridSize = glm::max(1u, glm::min(m_SpotShadowAtlasGridSize, 4u));
			m_SpotShadowTileSize = m_SpotShadowMapSize / m_SpotShadowAtlasGridSize;

			bool spotShadowFramebufferResized = false;
			if (m_SpotShadowMapImage && m_SpotShadowMapImage->GetSize().x != m_SpotShadowMapSize)
			{
				m_SpotShadowMapImage->Resize(m_SpotShadowMapSize, m_SpotShadowMapSize);
				spotShadowFramebufferResized = true;
			}
			if (m_SpotShadowMapPass && m_SpotShadowMapPass->GetTargetFramebuffer()
				&& (m_SpotShadowMapPass->GetTargetFramebuffer()->GetWidth() != m_SpotShadowMapSize
					|| m_SpotShadowMapPass->GetTargetFramebuffer()->GetHeight() != m_SpotShadowMapSize))
			{
				m_SpotShadowMapPass->GetTargetFramebuffer()->Resize(m_SpotShadowMapSize, m_SpotShadowMapSize);
				spotShadowFramebufferResized = true;
			}

			uint64_t spotShadowStateHash = 1469598103934665603ull;
			spotShadowStateHash = HashCombine(spotShadowStateHash, selectedShadowCount);
			spotShadowStateHash = HashCombine(spotShadowStateHash, maxSelectedTier);
			spotShadowStateHash = HashCombine(spotShadowStateHash, m_SpotShadowMapSize);
			spotShadowStateHash = HashCombine(spotShadowStateHash, m_SpotShadowAtlasGridSize);
			spotShadowStateHash = HashCombine(spotShadowStateHash, m_SpotShadowTileSize);

			m_SpotShadowUB.Count = 0;
			for (uint32_t i = 0; i < m_SpotLightsUB.Count; i++)
			{
				auto& light = m_SpotLightsUB.SpotLights[i];
				if (!light.CastsShadows || !selectedShadowCasters[i])
				{
					light.ShadowIndex = 0;
					light.AtlasOffsetX = 0.0f;
					light.AtlasOffsetY = 0.0f;
					light.AtlasScale = 1.0f;
					light.CastsShadows = 0;
					continue;
				}

				const float shadowDistance = ResolveShadowDistance(light.ShadowDistance, light.Range);
				spotShadowStateHash = HashVec3(spotShadowStateHash, light.Position);
				spotShadowStateHash = HashVec3(spotShadowStateHash, light.Direction);
				spotShadowStateHash = HashCombine(spotShadowStateHash, HashFloat(light.Range));
				spotShadowStateHash = HashCombine(spotShadowStateHash, HashFloat(shadowDistance));
				spotShadowStateHash = HashCombine(spotShadowStateHash, HashFloat(light.Angle));
				spotShadowStateHash = HashCombine(spotShadowStateHash, light.ShadowResolutionTier);

				const uint32_t atlasIndex = m_SpotShadowCount++;
				const uint32_t tileX = atlasIndex % m_SpotShadowAtlasGridSize;
				const uint32_t tileY = atlasIndex / m_SpotShadowAtlasGridSize;
				const float atlasScale = 1.0f / (float)m_SpotShadowAtlasGridSize;

				light.ShadowIndex = atlasIndex;
				light.AtlasOffsetX = tileX * atlasScale;
				light.AtlasOffsetY = tileY * atlasScale;
				light.AtlasScale = atlasScale;

				const glm::vec3 direction = glm::normalize(light.Direction);
				const glm::vec3 up = glm::abs(glm::dot(direction, glm::vec3(0, 1, 0))) < 0.99f
					? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
				const glm::mat4 view = glm::lookAt(light.Position, light.Position + direction, up);
				const float nearPlane = 0.1f;
				const float farPlane = shadowDistance;
				const float fov = glm::radians(glm::clamp(light.Angle, 1.0f, 179.0f));
				const glm::mat4 proj = glm::perspective(fov, 1.0f, nearPlane, farPlane);
				const glm::mat4 viewProjection = proj * view;
				m_SpotShadowUB.ViewProjection[atlasIndex] = viewProjection;
				m_SpotShadowFrustums[atlasIndex] = Frustum::FromViewProjection(viewProjection);
				m_SpotShadowFrustumCount = glm::max(m_SpotShadowFrustumCount, atlasIndex + 1u);
			}

			m_SpotShadowUB.Count = m_SpotShadowCount;
			spotShadowStateHash = HashCombine(spotShadowStateHash, m_SpotShadowCount);

			if (spotShadowFramebufferResized)
			{
				m_SpotShadowMapCacheValid = false;
				m_SpotShadowMapNeedsRender = true;
			}

			if (!m_SpotShadowMapCacheValid || spotShadowStateHash != m_LastSpotShadowStateHash)
				m_SpotShadowMapNeedsRender = true;
			m_LastSpotShadowStateHash = spotShadowStateHash;

			auto slData = m_SpotLightsUB;
			uint32_t slSize = (uint32_t)(16ull + sizeof(SpotLight) * slData.Count);
			auto spotShadowData = m_SpotShadowUB;
			uint32_t spotShadowSize = (uint32_t)(sizeof(glm::mat4) * MaxSpotShadows + 16ull);
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, slData, slSize]() mutable {
				instance->m_UBSSpotLights->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &slData, slSize);
				});

			Renderer::Submit([instance, spotShadowData, spotShadowSize]() mutable {
				instance->m_UBSSpotShadow->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &spotShadowData, spotShadowSize);
				});
		}

		// ── Directional shadow matrices ───────────────────────────────────────
		{
			const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
			const float directionalShadowDistance = ResolveShadowDistance(dirLight.ShadowDistance, m_Options.MaxShadowDistance);
			const uint32_t directionalShadowResolution = ResolveShadowResolutionTier(dirLight.ShadowResolutionTier);
			bool directionalShadowFramebufferResized = false;

			if (m_ShadowMapImage && m_ShadowMapImage->GetSize().x != directionalShadowResolution)
			{
				m_ShadowMapImage->Resize(directionalShadowResolution, directionalShadowResolution);
				directionalShadowFramebufferResized = true;
			}

			for (Ref<RenderPass>& shadowMapPass : m_ShadowMapPasses)
			{
				if (shadowMapPass && shadowMapPass->GetTargetFramebuffer()
					&& (shadowMapPass->GetTargetFramebuffer()->GetWidth() != directionalShadowResolution
						|| shadowMapPass->GetTargetFramebuffer()->GetHeight() != directionalShadowResolution))
				{
					shadowMapPass->GetTargetFramebuffer()->Resize(directionalShadowResolution, directionalShadowResolution);
					directionalShadowFramebufferResized = true;
				}
			}

			if (directionalShadowFramebufferResized)
			{
				m_ShadowCascadeCacheValid = false;
				m_DirectionalShadowMapCacheValid = false;
				m_DirectionalShadowMapNeedsRender = true;
			}

			if (dirLight.Intensity > 0.0f && dirLight.CastShadows)
			{
				constexpr float cameraMoveThreshold = 0.25f;
				constexpr float directionDotThreshold = 0.9995f;
				constexpr float floatThreshold = 0.0001f;

				const glm::mat4 viewInverse = glm::inverse(camera.ViewMatrix);
				const glm::vec3 cameraPosition = glm::vec3(viewInverse[3]);
				const glm::vec3 cameraForward = NormalizeOrFallback(-glm::vec3(viewInverse[2]), m_CachedShadowCameraForward);
				const glm::vec3 lightDirection = NormalizeOrFallback(dirLight.Direction, m_CachedShadowLightDirection);
				const glm::vec3 cachedCameraForward = NormalizeOrFallback(m_CachedShadowCameraForward, cameraForward);
				const glm::vec3 cachedLightDirection = NormalizeOrFallback(m_CachedShadowLightDirection, lightDirection);
				const uint32_t shadowMapResolution = m_ShadowMapPass && m_ShadowMapPass->GetTargetFramebuffer()
					? m_ShadowMapPass->GetTargetFramebuffer()->GetWidth()
					: 0u;

				bool cascadeSettingsChanged =
					std::abs(camera.FOV - m_CachedShadowFOV) > floatThreshold ||
					std::abs(camera.Near - m_CachedShadowNear) > floatThreshold ||
					std::abs(camera.Far - m_CachedShadowFar) > floatThreshold ||
					std::abs(directionalShadowDistance - m_CachedMaxShadowDistance) > floatThreshold ||
					std::abs(m_Options.ShadowCascadeSplitLambda - m_CachedShadowCascadeSplitLambda) > floatThreshold ||
					std::abs(m_Options.ShadowCascadeNearPlaneOffset - m_CachedShadowCascadeNearPlaneOffset) > floatThreshold ||
					std::abs(m_Options.ShadowCascadeFarPlaneOffset - m_CachedShadowCascadeFarPlaneOffset) > floatThreshold ||
					std::abs(m_ScaleShadowCascadesToOrigin - m_CachedScaleShadowCascadesToOrigin) > floatThreshold ||
					m_UseManualCascadeSplits != m_CachedUseManualCascadeSplits ||
					shadowMapResolution != m_CachedShadowMapResolution;

				for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
				{
					if (std::abs(m_ShadowCascadeSplits[cascade] - m_CachedShadowCascadeSplits[cascade]) > floatThreshold)
					{
						cascadeSettingsChanged = true;
						break;
					}
				}

				const bool cameraMoved =
					glm::dot(cameraPosition - m_CachedShadowCameraPosition, cameraPosition - m_CachedShadowCameraPosition)
					> cameraMoveThreshold * cameraMoveThreshold;
				const bool cameraRotated = glm::dot(cameraForward, cachedCameraForward) < directionDotThreshold;
				const bool lightMoved = glm::dot(lightDirection, cachedLightDirection) < directionDotThreshold;
				const bool shouldRecalculateCascades =
					!m_ShadowCascadeCacheValid || cascadeSettingsChanged || cameraMoved || cameraRotated || lightMoved;

				if (shouldRecalculateCascades)
				{
					CascadeData cascades[ShadowCascadeCount];
					CalculateCascades(cascades, camera, lightDirection, directionalShadowDistance);
					for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
					{
						m_ShadowUB.ViewProjection[cascade] = cascades[cascade].ViewProj;
						m_ShadowCascadeFrustums[cascade] = Frustum::FromViewProjection(cascades[cascade].ViewProj);
					}

					m_RendererDataUB.CascadeSplits = {
						cascades[0].SplitDepth,
						cascades[1].SplitDepth,
						cascades[2].SplitDepth,
						cascades[3].SplitDepth
					};

					m_CachedShadowCameraPosition = cameraPosition;
					m_CachedShadowCameraForward = cameraForward;
					m_CachedShadowLightDirection = lightDirection;
					m_CachedShadowFOV = camera.FOV;
					m_CachedShadowNear = camera.Near;
					m_CachedShadowFar = camera.Far;
					m_CachedMaxShadowDistance = directionalShadowDistance;
					m_CachedShadowCascadeSplitLambda = m_Options.ShadowCascadeSplitLambda;
					m_CachedShadowCascadeNearPlaneOffset = m_Options.ShadowCascadeNearPlaneOffset;
					m_CachedShadowCascadeFarPlaneOffset = m_Options.ShadowCascadeFarPlaneOffset;
					m_CachedScaleShadowCascadesToOrigin = m_ScaleShadowCascadesToOrigin;
					for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
						m_CachedShadowCascadeSplits[cascade] = m_ShadowCascadeSplits[cascade];
					m_CachedUseManualCascadeSplits = m_UseManualCascadeSplits;
					m_CachedShadowMapResolution = shadowMapResolution;
					m_ShadowCascadeCacheValid = true;
					m_DirectionalShadowMapNeedsRender = true;
				}

				m_ShadowCascadeFrustumCount = ShadowCascadeCount;
			}
			else
			{
				if (m_ShadowCascadeCacheValid || !m_DirectionalShadowMapCacheValid)
					m_DirectionalShadowMapNeedsRender = true;
				m_ShadowCascadeCacheValid = false;
				for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
					m_ShadowUB.ViewProjection[cascade] = glm::mat4(1.0f);
				m_RendererDataUB.CascadeSplits = glm::vec4(-1000000.0f);
			}

			auto shadowData = m_ShadowUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, shadowData]() mutable {
				instance->m_UBSShadow->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &shadowData, sizeof(UBShadow));
				});
		}

		// ── Renderer data uniform buffer ──────────────────────────────────────
		{
			const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
			const float directionalShadowDistance = ResolveShadowDistance(dirLight.ShadowDistance, m_Options.MaxShadowDistance);
			m_RendererDataUB.SoftShadows = m_Options.SoftShadows && dirLight.SoftShadows;
			m_RendererDataUB.LightSize = dirLight.LightSize;
			m_RendererDataUB.MaxShadowDistance = directionalShadowDistance;
			m_RendererDataUB.ShadowFade = m_Options.ShadowFade;
			m_RendererDataUB.CascadeFading = true;
			m_RendererDataUB.CascadeTransitionFade = m_Options.ShadowCascadeTransitionFade;
			m_RendererDataUB.ShowCascades = m_Options.ShowShadowCascades;
			m_RendererDataUB.ShowLightComplexity = m_Options.ShowLightComplexity;
			m_RendererDataUB.ShowMaterialComplexity = m_Options.ShowMaterialComplexity;
			// Clustered light culling: near/far drive the exponential Z-slice mapping
			// (must match ClusterBuildPass). Grid dims are compile-time in Cluster.glslh.
			m_RendererDataUB.ClusterZParams = {
				m_SceneData.SceneCamera.Near,
				m_SceneData.SceneCamera.Far,
				0.0f,
				0.0f
			};
			// TAA jitter supersamples sub-pixel detail, so bias texture LOD down while it
			// is active to recover the texture sharpness the resolve is meant to resolve.
			m_RendererDataUB.TextureMipBias = m_Options.TextureMipBias + (m_Options.EnableTAA ? -1.0f : 0.0f);
			m_RendererDataUB.EnableDistanceMipBias = m_Options.EnableDistanceMipBias;
			m_RendererDataUB.DistanceMipBiasStart = m_Options.DistanceMipBiasStart;
			m_RendererDataUB.DistanceMipBiasEnd = glm::max(m_Options.DistanceMipBiasEnd, m_Options.DistanceMipBiasStart + 1.0f);
			m_RendererDataUB.DistanceMipBiasMax = m_Options.DistanceMipBiasMax;
			m_RendererDataUB.GPUSceneDebugMode = ResolveGPUSceneDebugMode(m_DebugViewMode);

			auto rdData = m_RendererDataUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, rdData]() mutable {
				instance->m_UBSRendererData->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &rdData, sizeof(UBRendererData));
				});
		}

		// ── Update environment texture bindings in geometry passes ────────────
		Ref<TextureCube> radianceMap = GetEnvironmentRadianceMap(m_FrameEnvironment.Environment);
		Ref<TextureCube> irradianceMap = GetEnvironmentIrradianceMap(m_FrameEnvironment.Environment);
		if (m_DeferredLightingPass)
		{
			m_DeferredLightingPass->SetInput("u_EnvRadianceTex", radianceMap);
			m_DeferredLightingPass->SetInput("u_EnvIrradianceTex", irradianceMap);
		}
		if (m_GeometryPassTransparent)
		{
			m_GeometryPassTransparent->SetInput("u_EnvRadianceTex", radianceMap);
			m_GeometryPassTransparent->SetInput("u_EnvIrradianceTex", irradianceMap);
		}

		m_UploadCommandBuffer->End();
		m_UploadCommandBuffer->Submit();
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Mesh submission
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::SubmitRenderScene(const Ref<RenderScene>& renderScene)
	{
		LUX_CORE_ASSERT(m_Active, "SubmitRenderScene called outside BeginScene/EndScene");

		m_SubmittedRenderScene = renderScene;
		if (!renderScene)
			return;

		for (const StaticMeshRenderProxy* proxy : renderScene->GetStaticMeshProxies())
		{
			if (!proxy || !proxy->Visible || !proxy->StaticMesh || !proxy->MeshSource)
				continue;

			SubmitStaticMeshProxy(*proxy);
		}
	}

	void SceneRenderer::SubmitStaticMeshProxy(const StaticMeshRenderProxy& proxy)
	{
		SubmitStaticMeshInternal(proxy.StaticMesh, proxy.MeshSource, proxy.MaterialTable, proxy.WorldTransform, nullptr, proxy.Selected, &proxy);
	}

	void SceneRenderer::SubmitStaticMesh(
		Ref<StaticMesh>    staticMesh,
		Ref<MeshSource>    meshSource,
		Ref<MaterialTable> materialTable,
		const glm::mat4& transform,
		Ref<Material>      overrideMaterial)
	{
		SubmitStaticMeshInternal(staticMesh, meshSource, materialTable, transform, overrideMaterial, false);
	}

	void SceneRenderer::SubmitSelectedStaticMesh(
		Ref<StaticMesh>    staticMesh,
		Ref<MeshSource>    meshSource,
		Ref<MaterialTable> materialTable,
		const glm::mat4& transform,
		Ref<Material>      overrideMaterial)
	{
		SubmitStaticMeshInternal(staticMesh, meshSource, materialTable, transform, overrideMaterial, true);
	}

	bool SceneRenderer::IsMainViewVisible(const BoundingSphere& bounds) const
	{
		if (!m_Options.EnableFrustumCulling || !m_Options.EnableMainViewCulling)
			return true;

		return m_SceneData.CameraFrustum.IsSphereVisible(bounds);
	}

	bool SceneRenderer::IsShadowCasterVisible(const BoundingSphere& bounds) const
	{
		if (!m_Options.EnableShadowCulling)
			return true;

		if (m_ShadowCascadeFrustumCount == 0 && m_SpotShadowFrustumCount == 0)
			return false;

		for (uint32_t cascade = 0; cascade < m_ShadowCascadeFrustumCount; cascade++)
		{
			if (m_ShadowCascadeFrustums[cascade].IsSphereVisible(bounds))
				return true;
		}

		for (uint32_t shadowIndex = 0; shadowIndex < m_SpotShadowFrustumCount; shadowIndex++)
		{
			if (m_SpotShadowFrustums[shadowIndex].IsSphereVisible(bounds))
				return true;
		}

		return false;
	}

	void SceneRenderer::BuildSortedDrawCommandOrder(const DrawCommandList& drawList, DrawCommandOrder& drawOrder) const
	{
		drawOrder.clear();
		drawOrder.reserve(drawList.size());

		for (const auto& [key, dc] : drawList)
			drawOrder.push_back(key);

		std::sort(drawOrder.begin(), drawOrder.end(), [&](const MeshKey& lhsKey, const MeshKey& rhsKey)
			{
				const StaticDrawCommand& lhs = drawList.at(lhsKey);
				const StaticDrawCommand& rhs = drawList.at(rhsKey);

				if (lhs.PipelineSortKey != rhs.PipelineSortKey) return lhs.PipelineSortKey < rhs.PipelineSortKey;
				if (lhs.ShaderSortKey != rhs.ShaderSortKey) return lhs.ShaderSortKey < rhs.ShaderSortKey;
				if (lhs.MaterialSortKey != rhs.MaterialSortKey) return lhs.MaterialSortKey < rhs.MaterialSortKey;
				if (lhs.MeshSortKey != rhs.MeshSortKey) return lhs.MeshSortKey < rhs.MeshSortKey;
				return lhsKey < rhsKey;
			});
	}

	SceneRenderer::MeshPassState& SceneRenderer::GetMeshPass(MeshPassType passType)
	{
		const size_t passIndex = static_cast<size_t>(passType);
		LUX_CORE_ASSERT(passIndex < m_MeshPasses.size(), "Invalid mesh pass type");
		return m_MeshPasses[passIndex];
	}

	const SceneRenderer::MeshPassState& SceneRenderer::GetMeshPass(MeshPassType passType) const
	{
		const size_t passIndex = static_cast<size_t>(passType);
		LUX_CORE_ASSERT(passIndex < m_MeshPasses.size(), "Invalid mesh pass type");
		return m_MeshPasses[passIndex];
	}

	GPUTextureIndex SceneRenderer::ResolveTransientGPUTextureIndex(AssetHandle textureHandle)
	{
		if (!textureHandle)
			return InvalidGPUTextureIndex;

		auto [it, inserted] = m_TransientGPUTextureIndexByHandle.try_emplace(textureHandle, InvalidGPUTextureIndex);
		if (inserted || it->second == InvalidGPUTextureIndex)
		{
			it->second = (GPUTextureIndex)m_TransientGPUTextureHandles.size();
			m_TransientGPUTextureHandles.push_back(textureHandle);
			m_NextTransientGPUTextureIndex = (GPUTextureIndex)m_TransientGPUTextureHandles.size();
		}

		return EncodeTransientGPUTextureIndex(it->second);
	}

	RenderMaterialID SceneRenderer::GetOrCreateTransientRenderMaterialID(
		AssetHandle materialHandle,
		const Ref<MaterialAsset>& materialAsset,
		const Ref<Material>& overrideMaterial,
		bool transparent)
	{
		if (!materialHandle && !materialAsset && !overrideMaterial)
			return InvalidRenderMaterialID;

		constexpr uint64_t assetMaterialKeySeed = 0x4c55584d41544153ull;
		constexpr uint64_t overrideMaterialKeySeed = 0x4c55584d41544f56ull;
		uint64_t materialKey = 0;

		if (overrideMaterial)
		{
			materialKey = HashCombine(overrideMaterialKeySeed, SortKeyFromRef(overrideMaterial.Raw()));
		}
		else
		{
			AssetHandle resolvedHandle = materialHandle;
			if (!resolvedHandle && materialAsset)
				resolvedHandle = materialAsset->Handle;

			materialKey = resolvedHandle
				? HashCombine(assetMaterialKeySeed, (uint64_t)resolvedHandle)
				: HashCombine(assetMaterialKeySeed, SortKeyFromRef(materialAsset.Raw()));
		}

		auto materialIt = m_TransientGPUMaterialIndexByKey.find(materialKey);
		if (materialIt != m_TransientGPUMaterialIndexByKey.end())
			return EncodeTransientRenderMaterialIndex(materialIt->second);

		GPUMaterialBuildInput input;
		input.MaterialHandle = materialHandle ? materialHandle : (materialAsset ? materialAsset->Handle : AssetHandle(0));
		input.MaterialAsset = materialAsset;
		input.OverrideMaterial = overrideMaterial;
		input.Transparent = transparent;

		GPUMaterialData materialData = MaterialScene::BuildGPUMaterialData(
			input,
			[this](AssetHandle textureHandle) { return ResolveTransientGPUTextureIndex(textureHandle); });

		const uint32_t transientMaterialIndex = (uint32_t)m_TransientGPUMaterials.size();
		materialData.Metadata.z = EncodeTransientRenderMaterialIndex(transientMaterialIndex);
		m_TransientGPUMaterialIndexByKey[materialKey] = transientMaterialIndex;
		m_TransientGPUMaterials.push_back(materialData);
		return EncodeTransientRenderMaterialIndex(transientMaterialIndex);
	}

	SceneRenderer::StaticDrawCommand& SceneRenderer::SubmitMeshPassDraw(MeshPassType passType,
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
		uint64_t meshSortKey)
	{
		MeshDrawCommandCacheKey cacheKey;
		cacheKey.PassType = passType;
		cacheKey.Key = key;
		cacheKey.StaticMeshPtr = SortKeyFromRef(staticMesh.Raw());
		cacheKey.MeshSourcePtr = SortKeyFromRef(meshSource.Raw());
		cacheKey.MaterialTablePtr = SortKeyFromRef(materialTable.Raw());
		cacheKey.OverrideMaterialPtr = SortKeyFromRef(overrideMaterial.Raw());
		cacheKey.PipelineSortKey = pipelineSortKey;
		cacheKey.ShaderSortKey = shaderSortKey;
		cacheKey.MaterialSortKey = materialSortKey;
		cacheKey.MeshSortKey = meshSortKey;

		auto cacheIt = m_MeshDrawCommandCache.find(cacheKey);
		if (cacheIt == m_MeshDrawCommandCache.end())
		{
			CachedStaticDrawCommand cachedCommand;
			cachedCommand.Command.StaticMesh = staticMesh;
			cachedCommand.Command.MeshSource = meshSource;
			cachedCommand.Command.SubmeshIndex = submeshIndex;
			cachedCommand.Command.MaterialHandle = materialHandle;
			cachedCommand.Command.MaterialTable = materialTable;
			cachedCommand.Command.OverrideMaterial = overrideMaterial;
			cachedCommand.Command.PipelineSortKey = pipelineSortKey;
			cachedCommand.Command.ShaderSortKey = shaderSortKey;
			cachedCommand.Command.MaterialSortKey = materialSortKey;
			cachedCommand.Command.MeshSortKey = meshSortKey;
			cachedCommand.LastUsedFrame = m_MeshDrawCommandCacheFrame;
			cacheIt = m_MeshDrawCommandCache.emplace(cacheKey, std::move(cachedCommand)).first;
		}
		else
		{
			cacheIt->second.LastUsedFrame = m_MeshDrawCommandCacheFrame;
		}

		MeshPassState& pass = GetMeshPass(passType);
		auto [drawIt, inserted] = pass.DrawList.try_emplace(key);
		if (inserted)
		{
			drawIt->second = cacheIt->second.Command;
			drawIt->second.InstanceCount = 0;
		}

		drawIt->second.InstanceCount++;
		return drawIt->second;
	}

	void SceneRenderer::ClearFrameMeshPasses()
	{
		m_TransientGPUSceneInstances.clear();
		m_TransientGPUMaterials.clear();
		m_TransientGPUMaterialIndexByKey.clear();
		m_TransientGPUTextureHandles.clear();
		m_TransientGPUTextureIndexByHandle.clear();
		m_NextTransientGPUTextureIndex = 0;
		m_MeshTransformMap.clear();
		m_ShadowMeshTransformMap.clear();

		for (MeshPassState& pass : m_MeshPasses)
		{
			pass.DrawList.clear();
			pass.DrawOrder.clear();
		}

		m_MeshCullDrawCount = 0;
		PruneMeshDrawCommandCache();
	}

	void SceneRenderer::PruneMeshDrawCommandCache()
	{
		for (auto it = m_MeshDrawCommandCache.begin(); it != m_MeshDrawCommandCache.end();)
		{
			if (m_MeshDrawCommandCacheFrame - it->second.LastUsedFrame > MeshDrawCommandCacheRetireAge)
				it = m_MeshDrawCommandCache.erase(it);
			else
				++it;
		}
	}

	uint64_t SceneRenderer::CalculateShadowCasterHash() const
	{
		uint64_t hash = 1469598103934665603ull;
		auto resolveInstanceData = [this](uint32_t sceneInstanceIndex) -> const GPUSceneInstanceData*
			{
				if (IsTransientGPUSceneInstanceIndex(sceneInstanceIndex))
				{
					const uint32_t transientIndex = DecodeTransientGPUSceneInstanceIndex(sceneInstanceIndex);
					return transientIndex < m_TransientGPUSceneInstances.size()
						? &m_TransientGPUSceneInstances[transientIndex]
						: nullptr;
				}

				if (!m_SubmittedRenderScene)
					return nullptr;

				const auto& instances = m_SubmittedRenderScene->GetGPUScene().GetInstances();
				return sceneInstanceIndex < instances.size() ? &instances[sceneInstanceIndex] : nullptr;
			};

		const MeshPassState& shadowPass = GetMeshPass(MeshPassType::ShadowDepth);
		for (const MeshKey& key : shadowPass.DrawOrder)
		{
			const auto drawIt = shadowPass.DrawList.find(key);
			const auto transformIt = m_ShadowMeshTransformMap.find(key);
			if (drawIt == shadowPass.DrawList.end() || transformIt == m_ShadowMeshTransformMap.end())
				continue;

			const StaticDrawCommand& dc = drawIt->second;
			const TransformMapData& tmd = transformIt->second.Cascade;
			hash = HashCombine(hash, (uint64_t)key.MeshHandle);
			hash = HashCombine(hash, (uint64_t)key.MaterialHandle);
			hash = HashCombine(hash, key.SubmeshIndex);
			hash = HashCombine(hash, dc.MeshSortKey);
			hash = HashCombine(hash, dc.MaterialSortKey);
			hash = HashCombine(hash, tmd.ObjectIndices.size());

			for (uint32_t sceneInstanceIndex : tmd.ObjectIndices)
			{
				const GPUSceneInstanceData* instanceData = resolveInstanceData(sceneInstanceIndex);
				if (!instanceData)
					continue;

				hash = HashVec4(hash, instanceData->TransformRows[0]);
				hash = HashVec4(hash, instanceData->TransformRows[1]);
				hash = HashVec4(hash, instanceData->TransformRows[2]);
			}
		}

		return hash;
	}

	void SceneRenderer::SubmitStaticMeshInternal(
		Ref<StaticMesh>    staticMesh,
		Ref<MeshSource>    meshSource,
		Ref<MaterialTable> materialTable,
		const glm::mat4& transform,
		Ref<Material>      overrideMaterial,
		bool               isSelected,
		const StaticMeshRenderProxy* renderProxy)
	{
		LUX_CORE_ASSERT(m_Active, "SubmitStaticMesh called outside BeginScene/EndScene");

		const auto& submeshData = meshSource->GetSubmeshes();

		auto submitSubmesh = [&](uint32_t submeshIndex, const GPUSceneInstanceRef* gpuSceneInstanceRef)
		{
			if (submeshIndex >= submeshData.size())
				return;

			const auto& submesh = submeshData[submeshIndex];
			const GPUSceneInstanceData* gpuSceneInstance = gpuSceneInstanceRef ? &gpuSceneInstanceRef->Data : nullptr;

			const glm::mat4 submeshTransform = transform * submesh.Transform;

			// Resolve material handle
			AssetHandle materialHandle = 0;
			Ref<MaterialAsset> materialAsset;
			Ref<Material> resolvedOverrideMaterial = overrideMaterial;
			const bool hasExplicitOverrideMaterial = resolvedOverrideMaterial != nullptr;

			if (!resolvedOverrideMaterial)
			{
				materialHandle = ResolveStaticMeshMaterialHandle(materialTable, staticMesh, meshSource, submesh.MaterialIndex);

				if (materialHandle)
					materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);
				if (materialAsset)
					materialAsset->UpdateMaterialComplexityMetadata();

				if (!materialAsset)
					resolvedOverrideMaterial = Renderer::GetDefaultWhiteMaterial();
			}

			LUX_CORE_ASSERT(resolvedOverrideMaterial || materialAsset, "No material found for submesh {}", submeshIndex);

			const bool missingMaterialAsset = !hasExplicitOverrideMaterial && materialHandle && !materialAsset;
			const bool isTransparent = materialAsset ? materialAsset->IsTransparent() : (resolvedOverrideMaterial && resolvedOverrideMaterial->GetFlag(MaterialFlag::Blend));
			Ref<Material> materialSceneOverride = resolvedOverrideMaterial;
			if (missingMaterialAsset)
				materialSceneOverride = nullptr;

			const AssetHandle keyMaterialHandle = resolvedOverrideMaterial
				? AssetHandle((uint64_t)resolvedOverrideMaterial.Raw())
				: materialHandle;
			const MeshKey key{ GetStaticMeshKeyHandle(staticMesh), keyMaterialHandle, submeshIndex, isSelected };
			Ref<Material> sortMaterial = resolvedOverrideMaterial
				? resolvedOverrideMaterial
				: (materialAsset ? materialAsset->GetMaterial() : Renderer::GetDefaultWhiteMaterial());
			const uint64_t meshSortKey = (uint64_t)GetStaticMeshKeyHandle(staticMesh);
			const uint64_t materialSortKey = sortMaterial ? SortKeyFromRef(sortMaterial.Raw()) : (uint64_t)keyMaterialHandle;
			Ref<Shader> sortShader = sortMaterial ? sortMaterial->GetShader() : nullptr;
			const uint64_t shaderSortKey = sortShader ? SortKeyFromRef(sortShader.Raw()) : 0;

			// ── Shadow pass list ──────────────────────────────────────────────
			const bool castsShadows = resolvedOverrideMaterial
				? true // override materials always cast shadows
				: (materialAsset && materialAsset->IsShadowCasting());

			const glm::vec4 boundsSphereData = gpuSceneInstance
				? gpuSceneInstance->BoundsSphere
				: CalculateWorldBoundsSphere(submesh.BoundingBox, submeshTransform);
			const BoundingSphere boundsSphere{ glm::vec3(boundsSphereData), boundsSphereData.w };
			const bool mainViewVisible = IsMainViewVisible(boundsSphere);
			const bool shadowVisible = castsShadows && IsShadowCasterVisible(boundsSphere);

			m_FrameCullingStats.SubmittedInstances++;
			if (!mainViewVisible)
				m_FrameCullingStats.MainViewCulledInstances++;
			if (castsShadows && !shadowVisible)
				m_FrameCullingStats.ShadowCulledInstances++;
			if (!mainViewVisible && !shadowVisible)
			{
				m_FrameCullingStats.FullyCulledInstances++;
				return;
			}

			uint32_t sceneInstanceIndex = InvalidGPUSceneInstanceID;
			if (gpuSceneInstanceRef && gpuSceneInstanceRef->InstanceID != InvalidGPUSceneInstanceID)
			{
				sceneInstanceIndex = gpuSceneInstanceRef->InstanceID;
			}
			else
			{
				const RenderMaterialID transientMaterialID = GetOrCreateTransientRenderMaterialID(
					materialHandle,
					materialAsset,
					materialSceneOverride,
					isTransparent);
				const uint32_t transientIndex = (uint32_t)m_TransientGPUSceneInstances.size();
				GPUSceneInstanceData transientInstance = BuildTransientGPUSceneInstanceData(submeshTransform, boundsSphereData, submeshIndex, transientMaterialID);
				transientInstance.ObjectData.z = transientIndex;
				m_TransientGPUSceneInstances.push_back(transientInstance);
				sceneInstanceIndex = EncodeTransientGPUSceneInstanceIndex(transientIndex);
			}

			if (mainViewVisible)
			{
				// ── Main draw list ────────────────────────────────────────────────
				m_MeshTransformMap[key].ObjectIndices.push_back(sceneInstanceIndex);

				Ref<Pipeline> geometryPipeline = isTransparent ? m_TransparentGeometryPipeline : m_GeometryPipeline;
				SubmitMeshPassDraw(isTransparent ? MeshPassType::Transparent : MeshPassType::Opaque,
					key,
					staticMesh,
					meshSource,
					materialTable,
					submeshIndex,
					materialHandle,
					resolvedOverrideMaterial,
					SortKeyFromRef(geometryPipeline.Raw()),
					shaderSortKey,
					materialSortKey,
					meshSortKey);

				if (!isTransparent)
				{
					const uint64_t preDepthShaderSortKey = m_PreDepthMaterial && m_PreDepthMaterial->GetShader()
						? SortKeyFromRef(m_PreDepthMaterial->GetShader().Raw()) : 0;
					SubmitMeshPassDraw(MeshPassType::DepthPrepass,
						key,
						staticMesh,
						meshSource,
						materialTable,
						submeshIndex,
						materialHandle,
						m_PreDepthMaterial,
						SortKeyFromRef(m_PreDepthPipeline.Raw()),
						preDepthShaderSortKey,
						SortKeyFromRef(m_PreDepthMaterial.Raw()),
						meshSortKey);
				}

				// ── Selected list ─────────────────────────────────────────────────
				if (isSelected)
				{
					const uint64_t selectedShaderSortKey = m_SelectedGeometryMaterial && m_SelectedGeometryMaterial->GetShader()
						? SortKeyFromRef(m_SelectedGeometryMaterial->GetShader().Raw()) : 0;
					SubmitMeshPassDraw(MeshPassType::SelectedMask,
						key,
						staticMesh,
						meshSource,
						materialTable,
						submeshIndex,
						materialHandle,
						m_SelectedGeometryMaterial,
						m_SelectedGeometryPass ? SortKeyFromRef(m_SelectedGeometryPass->GetPipeline().Raw()) : 0,
						selectedShaderSortKey,
						SortKeyFromRef(m_SelectedGeometryMaterial.Raw()),
						meshSortKey);

					const uint64_t wireframeShaderSortKey = m_WireframeMaterial && m_WireframeMaterial->GetShader()
						? SortKeyFromRef(m_WireframeMaterial->GetShader().Raw()) : 0;
					SubmitMeshPassDraw(MeshPassType::Wireframe,
						key,
						staticMesh,
						meshSource,
						materialTable,
						submeshIndex,
						materialHandle,
						m_WireframeMaterial,
						m_GeometryWireframePass ? SortKeyFromRef(m_GeometryWireframePass->GetPipeline().Raw()) : 0,
						wireframeShaderSortKey,
						SortKeyFromRef(m_WireframeMaterial.Raw()),
						meshSortKey);
				}
			}

			if (shadowVisible)
			{
				// ── Shadow pass list ──────────────────────────────────────────────
				m_ShadowMeshTransformMap[key].Cascade.ObjectIndices.push_back(sceneInstanceIndex);

				const uint64_t shadowShaderSortKey = m_ShadowPassMaterial && m_ShadowPassMaterial->GetShader()
					? SortKeyFromRef(m_ShadowPassMaterial->GetShader().Raw()) : 0;
				SubmitMeshPassDraw(MeshPassType::ShadowDepth,
					key,
					staticMesh,
					meshSource,
					materialTable,
					submeshIndex,
					materialHandle,
					m_ShadowPassMaterial,
					m_ShadowMapPass ? SortKeyFromRef(m_ShadowMapPass->GetPipeline().Raw()) : 0,
					shadowShaderSortKey,
					SortKeyFromRef(m_ShadowPassMaterial.Raw()),
					meshSortKey);
			}
		};

		if (renderProxy && !renderProxy->SubmeshInstances.empty())
		{
			for (const GPUSceneInstanceRef& instanceRef : renderProxy->SubmeshInstances)
				submitSubmesh(GetGPUSceneSubmeshIndex(instanceRef.Data), &instanceRef);

			return;
		}

		for (uint32_t submeshIndex : staticMesh->GetSubmeshes())
			submitSubmesh(submeshIndex, nullptr);
	}

	void SceneRenderer::SubmitPhysicsStaticDebugMesh(Ref<StaticMesh> staticMesh,
		Ref<MeshSource> meshSource,
		const glm::mat4& transform,
		bool isSimpleCollider)
	{
		SubmitStaticDebugMesh(MeshPassType::PhysicsCollider, staticMesh, meshSource, transform,
			isSimpleCollider ? m_SimpleColliderMaterial : m_ComplexColliderMaterial);
	}

	void SceneRenderer::SubmitStaticDebugMesh(MeshPassType passType,
		Ref<StaticMesh>  staticMesh,
		Ref<MeshSource>  meshSource,
		const glm::mat4& transform,
		Ref<Material>    material)
	{
		const auto& submeshData = meshSource->GetSubmeshes();

		for (uint32_t submeshIndex : staticMesh->GetSubmeshes())
		{
			const auto& submesh = submeshData[submeshIndex];
			const glm::mat4 submeshTransform = transform * submesh.Transform;

			// Use the material pointer as a fake asset handle so each material gets its own MeshKey bucket
			const AssetHandle fakeHandle = (AssetHandle)(uint64_t)material.Raw();
			const MeshKey key{ GetStaticMeshKeyHandle(staticMesh), fakeHandle, submeshIndex, false };
			const bool isTransparent = material && material->GetFlag(MaterialFlag::Blend);
			const RenderMaterialID transientMaterialID = GetOrCreateTransientRenderMaterialID(
				fakeHandle,
				nullptr,
				material,
				isTransparent);

			const uint32_t transientIndex = (uint32_t)m_TransientGPUSceneInstances.size();
			GPUSceneInstanceData transientInstance = BuildTransientGPUSceneInstanceData(
				submeshTransform,
				CalculateWorldBoundsSphere(submesh.BoundingBox, submeshTransform),
				submeshIndex,
				transientMaterialID);
			transientInstance.ObjectData.z = transientIndex;
			m_TransientGPUSceneInstances.push_back(transientInstance);
			m_MeshTransformMap[key].ObjectIndices.push_back(EncodeTransientGPUSceneInstanceIndex(transientIndex));

			SubmitMeshPassDraw(passType,
				key,
				staticMesh,
				meshSource,
				nullptr,
				submeshIndex,
				fakeHandle,
				material,
				m_GeometryWireframePass ? SortKeyFromRef(m_GeometryWireframePass->GetPipeline().Raw()) : SortKeyFromRef(m_GeometryPipeline.Raw()),
				material && material->GetShader() ? SortKeyFromRef(material->GetShader().Raw()) : 0,
				SortKeyFromRef(material.Raw()),
				(uint64_t)GetStaticMeshKeyHandle(staticMesh));
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// EndScene → FlushDrawList
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::EndScene()
	{
		LUX_PROFILE_FUNCTION("SceneRenderer::EndScene");
		LUX_CORE_ASSERT(m_Active);
		FlushDrawList();
		m_Active = false;
	}

	void SceneRenderer::WaitForThreads()
	{
		// The editor can run without an active project (welcome screen, project closed,
		// or startup-project loading disabled), in which case there is no asset worker.
		if (Ref<AssetManagerBase> assetManager = Project::GetAssetManager())
			assetManager->SyncWithAssetThread();
	}

	void SceneRenderer::FlushDrawList()
	{
		LUX_PROFILE_FUNCTION("SceneRenderer::FlushDrawList");
		// Clear lists and bail if GPU resources not ready
		auto clearAll = [this]()
			{
				ClearFrameMeshPasses();
			};

		if (!m_ResourcesCreated)
		{
			m_GPUSceneDebugSnapshot = {};
			clearAll();
			return;
		}

		// ── 1. Build the flat ObjectIndex array and assign base offsets ────────
		// Each MeshKey in m_MeshTransformMap gets a contiguous block of GPUScene instance indices.
		// The shader uses objectIndex = ObjectIndexBase + gl_InstanceIndex to fetch GPUSceneInstanceData.
		uint32_t cursor = 0;
		uint32_t visibleCursor = 0;
		// Reuse persistent scratch storage (capacity retained) instead of allocating
		// fresh vectors every frame.
		std::vector<uint32_t>& objectIndexData = m_ScratchObjectIndexData;
		std::vector<uint32_t>& visibleObjectIndexData = m_ScratchVisibleObjectIndexData;
		std::vector<MeshCullDrawData>& meshCullDrawData = m_ScratchMeshCullDrawData;
		std::vector<nvrhi::DrawIndexedIndirectArguments>& indirectDrawData = m_ScratchIndirectDrawData;
		objectIndexData.clear();
		visibleObjectIndexData.clear();
		meshCullDrawData.clear();
		indirectDrawData.clear();

		for (MeshPassState& pass : m_MeshPasses)
			BuildSortedDrawCommandOrder(pass.DrawList, pass.DrawOrder);

		const GPUScene* submittedGPUScene = m_SubmittedRenderScene ? &m_SubmittedRenderScene->GetGPUScene() : nullptr;
		const std::vector<GPUSceneInstanceData>* persistentGPUSceneInstances = submittedGPUScene ? &submittedGPUScene->GetInstances() : nullptr;
		const uint32_t persistentGPUSceneInstanceCount = persistentGPUSceneInstances ? (uint32_t)persistentGPUSceneInstances->size() : 0;
		const MaterialScene* submittedMaterialScene = m_SubmittedRenderScene ? &m_SubmittedRenderScene->GetMaterialScene() : nullptr;
		const TextureScene* submittedTextureScene = m_SubmittedRenderScene ? &m_SubmittedRenderScene->GetTextureScene() : nullptr;

		std::vector<AssetHandle>& gpuTextureHandles = m_ScratchTextureHandles;
		if (submittedTextureScene)
		{
			const std::vector<AssetHandle>& src = submittedTextureScene->GetTextureHandles();
			gpuTextureHandles.assign(src.begin(), src.end());
		}
		else
		{
			gpuTextureHandles.clear();
		}
		if (gpuTextureHandles.empty())
			gpuTextureHandles.push_back(AssetHandle(0));
		const uint32_t persistentTextureCount = (uint32_t)gpuTextureHandles.size();
		for (AssetHandle transientTextureHandle : m_TransientGPUTextureHandles)
			gpuTextureHandles.push_back(transientTextureHandle);

		uint32_t textureTableOverflowCount = 0;
		if (gpuTextureHandles.size() > MaxGPUTextureSceneTextures)
		{
			textureTableOverflowCount = (uint32_t)(gpuTextureHandles.size() - MaxGPUTextureSceneTextures);
			gpuTextureHandles.resize(MaxGPUTextureSceneTextures);
		}

		auto resolveMaterialTexture = [](AssetHandle textureHandle) -> Ref<Texture2D>
			{
				if (!textureHandle || !Project::GetAssetManager() || !AssetManager::IsAssetHandleValid(textureHandle))
					return Renderer::GetWhiteTexture();

				if (AssetManager::GetAssetType(textureHandle) != AssetType::Texture)
					return Renderer::GetWhiteTexture();

				Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(textureHandle);
				if (!texture || !texture->Loaded())
					return Renderer::GetWhiteTexture();

				return texture;
			};

		uint32_t missingTextureDescriptorCount = 0;
		if (m_GPUMaterialTextures.empty())
			m_GPUMaterialTextures.assign(MaxGPUTextureSceneTextures, Renderer::GetWhiteTexture());

		for (uint32_t textureIndex = 0; textureIndex < MaxGPUTextureSceneTextures; textureIndex++)
		{
			const AssetHandle textureHandle = textureIndex < gpuTextureHandles.size() ? gpuTextureHandles[textureIndex] : AssetHandle(0);
			Ref<Texture2D> texture = resolveMaterialTexture(textureHandle);
			if (textureHandle && texture.Raw() == Renderer::GetWhiteTexture().Raw())
				missingTextureDescriptorCount++;

			if (m_GPUMaterialTextures[textureIndex].Raw() == texture.Raw())
				continue;

			m_GPUMaterialTextures[textureIndex] = texture;
			if (m_GeometryPass && m_GeometryPass->IsInputValid("u_GPUMaterialTextures"))
				m_GeometryPass->SetInput("u_GPUMaterialTextures", texture, textureIndex);
			if (m_GeometryPassTransparent && m_GeometryPassTransparent->IsInputValid("u_GPUMaterialTextures"))
				m_GeometryPassTransparent->SetInput("u_GPUMaterialTextures", texture, textureIndex);
			if (m_DeferredLightingPass && m_DeferredLightingPass->IsInputValid("u_GPUMaterialTextures"))
				m_DeferredLightingPass->SetInput("u_GPUMaterialTextures", texture, textureIndex);
			if (m_GBufferDebugPass && m_GBufferDebugPass->IsInputValid("u_GPUMaterialTextures"))
				m_GBufferDebugPass->SetInput("u_GPUMaterialTextures", texture, textureIndex);
		}

		std::vector<GPUMaterialData>& gpuMaterialData = m_ScratchMaterialData;
		if (submittedMaterialScene)
		{
			const std::vector<GPUMaterialData>& src = submittedMaterialScene->GetMaterials();
			gpuMaterialData.assign(src.begin(), src.end());
		}
		else
		{
			gpuMaterialData.clear();
		}
		if (gpuMaterialData.empty())
			gpuMaterialData.push_back(MaterialScene::GetFallbackMaterialData());
		const uint32_t persistentMaterialCount = (uint32_t)gpuMaterialData.size();

		std::vector<GPUMaterialData>& transientGPUMaterialData = m_ScratchTransientMaterialData;
		transientGPUMaterialData.assign(m_TransientGPUMaterials.begin(), m_TransientGPUMaterials.end());
		auto resolveUploadedTextureIndex = [persistentTextureCount](GPUTextureIndex textureIndex) -> GPUTextureIndex
			{
				if (textureIndex == InvalidGPUTextureIndex)
					return InvalidGPUTextureIndex;
				if (!IsTransientGPUTextureIndex(textureIndex))
					return textureIndex;

				const uint32_t transientTextureIndex = DecodeTransientGPUTextureIndex(textureIndex);
				const uint64_t uploadedTextureIndex = (uint64_t)persistentTextureCount + transientTextureIndex;
				return uploadedTextureIndex < MaxGPUTextureSceneTextures
					? (GPUTextureIndex)uploadedTextureIndex
					: InvalidGPUTextureIndex;
			};
		for (uint32_t transientMaterialIndex = 0; transientMaterialIndex < transientGPUMaterialData.size(); transientMaterialIndex++)
		{
			GPUMaterialData& materialData = transientGPUMaterialData[transientMaterialIndex];
			materialData.Metadata.z = persistentMaterialCount + transientMaterialIndex;
			for (uint32_t textureSlot = 0; textureSlot < 4; textureSlot++)
				materialData.TextureIndices[textureSlot] = resolveUploadedTextureIndex(materialData.TextureIndices[textureSlot]);
		}
		const uint32_t uploadedMaterialCount = persistentMaterialCount + (uint32_t)transientGPUMaterialData.size();
		const uint32_t uploadedTextureCount = (uint32_t)gpuTextureHandles.size();

		auto resolveInstanceData = [this, persistentGPUSceneInstances](uint32_t sceneInstanceIndex) -> const GPUSceneInstanceData*
			{
				if (IsTransientGPUSceneInstanceIndex(sceneInstanceIndex))
				{
					const uint32_t transientIndex = DecodeTransientGPUSceneInstanceIndex(sceneInstanceIndex);
					return transientIndex < m_TransientGPUSceneInstances.size()
						? &m_TransientGPUSceneInstances[transientIndex]
						: nullptr;
				}

				return persistentGPUSceneInstances && sceneInstanceIndex < persistentGPUSceneInstances->size()
					? &(*persistentGPUSceneInstances)[sceneInstanceIndex]
					: nullptr;
			};

		auto resolveGPUSceneBufferIndex = [persistentGPUSceneInstanceCount](uint32_t sceneInstanceIndex)
			{
				if (IsTransientGPUSceneInstanceIndex(sceneInstanceIndex))
					return persistentGPUSceneInstanceCount + DecodeTransientGPUSceneInstanceIndex(sceneInstanceIndex);

				return sceneInstanceIndex;
			};

		auto isInstanceVisible = [this, &resolveInstanceData](uint32_t sceneInstanceIndex)
			{
				if (!m_Options.EnableFrustumCulling)
					return true;

				const GPUSceneInstanceData* instanceData = resolveInstanceData(sceneInstanceIndex);
				if (!instanceData)
					return true;

				const glm::vec4& sphereData = instanceData->BoundsSphere;
				return m_SceneData.CameraFrustum.IsSphereVisible({ glm::vec3(sphereData), sphereData.w });
			};

		for (auto& [key, tmd] : m_MeshTransformMap)
		{
			tmd.ObjectIndexBase = cursor;
			tmd.VisibleObjectIndexBase = visibleCursor;
			tmd.VisibleInstanceCount = 0;
			tmd.IndirectDrawOffsetBytes = std::numeric_limits<uint32_t>::max();

			for (uint32_t idx : tmd.ObjectIndices)
			{
				const uint32_t objectIndex = resolveGPUSceneBufferIndex(idx);
				objectIndexData.push_back(objectIndex);

				if (isInstanceVisible(idx))
				{
					visibleObjectIndexData.push_back(objectIndex);
					tmd.VisibleInstanceCount++;
					visibleCursor++;
				}
			}

			cursor += (uint32_t)tmd.ObjectIndices.size();
		}

		// Do the same for the shadow-specific transform map
		for (auto& [key, shadowTmd] : m_ShadowMeshTransformMap)
		{
			shadowTmd.Cascade.ObjectIndexBase = cursor;
			for (uint32_t idx : shadowTmd.Cascade.ObjectIndices)
				objectIndexData.push_back(resolveGPUSceneBufferIndex(idx));
			cursor += (uint32_t)shadowTmd.Cascade.ObjectIndices.size();
		}

		auto registerIndirectDraws = [this, &meshCullDrawData, &indirectDrawData](const DrawCommandList& drawList, const DrawCommandOrder& drawOrder)
			{
				for (const MeshKey& key : drawOrder)
				{
					const auto drawIt = drawList.find(key);
					if (drawIt == drawList.end())
						continue;

					const StaticDrawCommand& dc = drawIt->second;
					auto transformIt = m_MeshTransformMap.find(key);
					if (transformIt == m_MeshTransformMap.end())
						continue;

					auto& tmd = transformIt->second;
					if (tmd.IndirectDrawOffsetBytes != std::numeric_limits<uint32_t>::max())
						continue;

					tmd.IndirectDrawOffsetBytes = (uint32_t)(indirectDrawData.size() * sizeof(nvrhi::DrawIndexedIndirectArguments));
					meshCullDrawData.push_back({
						tmd.ObjectIndexBase,
						(uint32_t)tmd.ObjectIndices.size(),
						tmd.VisibleObjectIndexBase,
						0
					});
					BuildIndirectDrawCommand(dc, tmd, indirectDrawData);
				}
			};

		registerIndirectDraws(GetMeshPass(MeshPassType::SelectedMask).DrawList, GetMeshPass(MeshPassType::SelectedMask).DrawOrder);
		registerIndirectDraws(GetMeshPass(MeshPassType::Opaque).DrawList, GetMeshPass(MeshPassType::Opaque).DrawOrder);
		registerIndirectDraws(GetMeshPass(MeshPassType::Transparent).DrawList, GetMeshPass(MeshPassType::Transparent).DrawOrder);
		registerIndirectDraws(GetMeshPass(MeshPassType::PhysicsCollider).DrawList, GetMeshPass(MeshPassType::PhysicsCollider).DrawOrder);
		m_MeshCullDrawCount = (uint32_t)meshCullDrawData.size();

		const uint64_t shadowCasterHash = CalculateShadowCasterHash();
		const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
		const bool directionalShadowsEnabled = dirLight.Intensity > 0.0f && dirLight.CastShadows;
		if (directionalShadowsEnabled && (!m_DirectionalShadowMapCacheValid || shadowCasterHash != m_LastShadowCasterHash))
			m_DirectionalShadowMapNeedsRender = true;
		if (m_SpotShadowCount > 0 && (!m_SpotShadowMapCacheValid || shadowCasterHash != m_LastShadowCasterHash))
			m_SpotShadowMapNeedsRender = true;
		m_LastShadowCasterHash = shadowCasterHash;

		// ── 2. Upload GPUScene tails, ObjectIndexes, culling data, and indirect args
		m_UploadCommandBuffer->Begin();

		std::vector<GPUSceneInstanceData> gpuSceneInstanceData;
		if (submittedGPUScene)
			gpuSceneInstanceData = submittedGPUScene->GetInstances();

		std::vector<GPUSceneInstanceData> transientGPUSceneData = m_TransientGPUSceneInstances;
		for (uint32_t transientIndex = 0; transientIndex < transientGPUSceneData.size(); transientIndex++)
		{
			transientGPUSceneData[transientIndex].ObjectData.z = persistentGPUSceneInstanceCount + transientIndex;

			const RenderMaterialID materialID = transientGPUSceneData[transientIndex].Metadata.z;
			if (IsTransientRenderMaterialID(materialID))
			{
				const uint32_t transientMaterialIndex = DecodeTransientRenderMaterialIndex(materialID);
				transientGPUSceneData[transientIndex].Metadata.z = transientMaterialIndex < transientGPUMaterialData.size()
					? persistentMaterialCount + transientMaterialIndex
					: InvalidRenderMaterialID;
			}
		}

		{
			GPUSceneDebugSnapshot snapshot;
			snapshot.PersistentInstanceCount = persistentGPUSceneInstanceCount;
			snapshot.TransientInstanceCount = (uint32_t)transientGPUSceneData.size();
			snapshot.TotalUploadedInstanceCount = snapshot.PersistentInstanceCount + snapshot.TransientInstanceCount;
			snapshot.ObjectIndexCount = (uint32_t)objectIndexData.size();
			snapshot.VisibleObjectIndexCount = (uint32_t)visibleObjectIndexData.size();
			snapshot.MeshCullDrawCount = (uint32_t)meshCullDrawData.size();
			snapshot.IndirectDrawCount = (uint32_t)indirectDrawData.size();
			snapshot.PersistentMaterialCount = persistentMaterialCount;
			snapshot.TransientMaterialCount = (uint32_t)transientGPUMaterialData.size();
			snapshot.UploadedMaterialCount = uploadedMaterialCount;
			snapshot.PersistentTextureCount = persistentTextureCount;
			snapshot.TransientTextureCount = (uint32_t)m_TransientGPUTextureHandles.size();
			snapshot.UploadedTextureCount = uploadedTextureCount;
			snapshot.MissingTextureDescriptorCount = missingTextureDescriptorCount;
			snapshot.TextureTableOverflowCount = textureTableOverflowCount;

			if (m_SubmittedRenderScene)
			{
				const RenderSceneSyncStats& syncStats = m_SubmittedRenderScene->GetLastSyncStats();
				snapshot.ActivePrimitiveCount = syncStats.StaticMeshProxyCount;
				snapshot.VisiblePrimitiveCount = syncStats.VisibleStaticMeshProxyCount;
			}

			if (submittedGPUScene)
			{
				snapshot.DirtyInstanceCount = submittedGPUScene->GetDirtyInstanceCount();
				snapshot.DirtyRangeCount = (uint32_t)submittedGPUScene->GetDirtyRanges().size();
			}

			if (submittedMaterialScene)
			{
				snapshot.DirtyMaterialCount = submittedMaterialScene->GetDirtyMaterialCount();
				snapshot.DirtyMaterialRangeCount = (uint32_t)submittedMaterialScene->GetDirtyRanges().size();
			}

			if (submittedTextureScene)
			{
				snapshot.DirtyTextureCount = submittedTextureScene->GetDirtyTextureCount();
				snapshot.DirtyTextureRangeCount = (uint32_t)submittedTextureScene->GetDirtyRanges().size();
			}

			for (uint32_t objectIndex : objectIndexData)
			{
				if (objectIndex >= snapshot.TotalUploadedInstanceCount)
					snapshot.InvalidObjectIndexCount++;
			}

			for (uint32_t objectIndex : visibleObjectIndexData)
			{
				if (objectIndex >= snapshot.TotalUploadedInstanceCount)
					snapshot.InvalidVisibleObjectIndexCount++;
			}

			std::vector<uint32_t> materialReferenceCounts(uploadedMaterialCount, 0);
			auto validateInstance = [&snapshot, uploadedMaterialCount, &materialReferenceCounts](const GPUSceneInstanceData& data, uint32_t expectedInstanceID, bool persistent)
				{
					snapshot.MaxMaterialIndex = glm::max(snapshot.MaxMaterialIndex, data.Metadata.z);

					if (data.Metadata.z >= uploadedMaterialCount)
					{
						snapshot.InvalidMaterialIDCount++;
					}
					else
					{
						materialReferenceCounts[data.Metadata.z]++;
					}

					if (!IsFiniteTransformRows(data.PreviousTransformRows))
						snapshot.InvalidPreviousTransformCount++;

					if (!IsFiniteVec4(data.BoundsSphere) || data.BoundsSphere.w <= 0.0f)
						snapshot.InvalidBoundsCount++;

					if (data.ObjectData.z != expectedInstanceID)
						snapshot.InvalidStoredInstanceIDCount++;

					if (!persistent)
						return;

					if (data.Metadata.x == InvalidRenderPrimitiveID)
						snapshot.PersistentInvalidPrimitiveIDCount++;

					if (data.ObjectData.x == 0 && data.ObjectData.y == 0)
						snapshot.MissingPersistentObjectIDCount++;
				};

			for (uint32_t instanceIndex = 0; instanceIndex < gpuSceneInstanceData.size(); instanceIndex++)
				validateInstance(gpuSceneInstanceData[instanceIndex], instanceIndex, true);

			for (uint32_t transientIndex = 0; transientIndex < transientGPUSceneData.size(); transientIndex++)
				validateInstance(transientGPUSceneData[transientIndex], persistentGPUSceneInstanceCount + transientIndex, false);

			auto validateMaterial = [&snapshot, &materialReferenceCounts, uploadedTextureCount](const GPUMaterialData& data, uint32_t materialID)
				{
					if (materialID == InvalidRenderMaterialID && materialReferenceCounts[materialID] == 0)
						return;

					if (HasGPUMaterialFlag(data.Metadata.x, GPUMaterialFlags::Missing))
						snapshot.MissingMaterialCount++;
					if (HasGPUMaterialFlag(data.Metadata.x, GPUMaterialFlags::MissingTexture))
						snapshot.MissingTextureCount++;

					for (uint32_t textureSlot = 0; textureSlot < 4; textureSlot++)
					{
						const GPUTextureIndex textureIndex = data.TextureIndices[textureSlot];
						if (textureIndex != InvalidGPUTextureIndex && textureIndex >= uploadedTextureCount)
							snapshot.InvalidTextureIndexCount++;
					}
				};

			for (uint32_t materialIndex = 0; materialIndex < gpuMaterialData.size(); materialIndex++)
				validateMaterial(gpuMaterialData[materialIndex], materialIndex);
			for (uint32_t materialIndex = 0; materialIndex < transientGPUMaterialData.size(); materialIndex++)
				validateMaterial(transientGPUMaterialData[materialIndex], persistentMaterialCount + materialIndex);

			if (snapshot.InvalidObjectIndexCount > 0 || snapshot.InvalidVisibleObjectIndexCount > 0)
			{
				snapshot.Diagnostics.push_back(std::format(
					"Object index buffer contains out-of-range GPUScene rows: {} total, {} visible.",
					snapshot.InvalidObjectIndexCount,
					snapshot.InvalidVisibleObjectIndexCount));
			}

			if (snapshot.InvalidBoundsCount > 0)
				snapshot.Diagnostics.push_back(std::format("{} GPUScene instance(s) have invalid or empty bounds.", snapshot.InvalidBoundsCount));
			if (snapshot.InvalidPreviousTransformCount > 0)
				snapshot.Diagnostics.push_back(std::format("{} GPUScene instance(s) have non-finite previous transforms.", snapshot.InvalidPreviousTransformCount));
			if (snapshot.InvalidStoredInstanceIDCount > 0)
				snapshot.Diagnostics.push_back(std::format("{} GPUScene instance(s) have ObjectData.z that does not match the uploaded row.", snapshot.InvalidStoredInstanceIDCount));
			if (snapshot.InvalidMaterialIDCount > 0)
				snapshot.Diagnostics.push_back(std::format("{} GPUScene instance(s) reference material rows outside the uploaded material table.", snapshot.InvalidMaterialIDCount));
			if (snapshot.PersistentInvalidPrimitiveIDCount > 0)
				snapshot.Diagnostics.push_back(std::format("{} persistent GPUScene instance(s) are missing stable primitive IDs.", snapshot.PersistentInvalidPrimitiveIDCount));
			if (snapshot.MissingPersistentObjectIDCount > 0)
				snapshot.Diagnostics.push_back(std::format("{} persistent GPUScene instance(s) are missing entity object IDs.", snapshot.MissingPersistentObjectIDCount));
			if (snapshot.MissingMaterialCount > 0)
				snapshot.Diagnostics.push_back(std::format("{} GPU material row(s) are using the missing-material fallback.", snapshot.MissingMaterialCount));
			if (snapshot.MissingTextureCount > 0)
				snapshot.Diagnostics.push_back(std::format("{} GPU material row(s) reference at least one missing texture.", snapshot.MissingTextureCount));
			if (snapshot.InvalidTextureIndexCount > 0)
				snapshot.Diagnostics.push_back(std::format("{} GPU material texture index reference(s) are outside the uploaded texture table.", snapshot.InvalidTextureIndexCount));
			if (snapshot.MissingTextureDescriptorCount > 0)
				snapshot.Diagnostics.push_back(std::format("{} GPU texture table slot(s) fell back to the default white texture.", snapshot.MissingTextureDescriptorCount));
			if (snapshot.TextureTableOverflowCount > 0)
				snapshot.Diagnostics.push_back(std::format("GPU texture table overflowed by {} texture row(s). Increase MaxGPUTextureSceneTextures before relying on these rows.", snapshot.TextureTableOverflowCount));

			m_GPUSceneDebugSnapshot = std::move(snapshot);
		}

		if (!objectIndexData.empty()
			|| !visibleObjectIndexData.empty()
			|| !meshCullDrawData.empty()
			|| !indirectDrawData.empty()
			|| !gpuSceneInstanceData.empty()
			|| !transientGPUSceneData.empty()
			|| !gpuMaterialData.empty()
			|| !transientGPUMaterialData.empty())
		{
			const uint32_t persistentSceneCount = persistentGPUSceneInstanceCount;
			const uint32_t persistentGPUMaterialCount = persistentMaterialCount;
			Ref<SceneRenderer> instance = this;

			// Init-captures: one copy per vector instead of the previous
			// local-copy-then-capture-copy. Scratch-backed vectors (reused next
			// frame) are copied; the frame-local GPUScene vectors are moved —
			// nothing reads them after this block.
			Renderer::Submit([instance,
				indexData = objectIndexData,
				visibleIndexData = visibleObjectIndexData,
				cullDrawData = meshCullDrawData,
				indirectCommands = indirectDrawData,
				gpuSceneData = std::move(gpuSceneInstanceData),
				transientSceneData = std::move(transientGPUSceneData),
				materialData = gpuMaterialData,
				transientMaterialData = transientGPUMaterialData,
				persistentSceneCount, persistentGPUMaterialCount]() mutable {

				Ref<RenderCommandBuffer> cmd = instance->m_UploadCommandBuffer;

				// Grow ObjectIndexes SSBO if needed
				if (!indexData.empty())
				{
					const uint32_t indexBytes = (uint32_t)(sizeof(uint32_t) * indexData.size());
					if (instance->m_SBSObjectIndexes->RT_Get()->GetHandle()->getDesc().byteSize < indexBytes)
						instance->m_SBSObjectIndexes->Resize(indexBytes * 2u);
					instance->m_SBSObjectIndexes->RT_Get()->RT_SetData(cmd, indexData.data(), indexBytes);

					if (instance->m_SBSVisibleObjectIndexes->RT_Get()->GetHandle()->getDesc().byteSize < indexBytes)
						instance->m_SBSVisibleObjectIndexes->Resize(indexBytes * 2u);
				}

				if (!visibleIndexData.empty())
				{
					const uint32_t visibleIndexBytes = (uint32_t)(sizeof(uint32_t) * visibleIndexData.size());
					instance->m_SBSVisibleObjectIndexes->RT_Get()->RT_SetData(cmd, visibleIndexData.data(), visibleIndexBytes);
				}

				if (!cullDrawData.empty())
				{
					const uint32_t cullDrawBytes = (uint32_t)(sizeof(MeshCullDrawData) * cullDrawData.size());
					if (instance->m_SBSMeshCullDrawData->RT_Get()->GetHandle()->getDesc().byteSize < cullDrawBytes)
						instance->m_SBSMeshCullDrawData->Resize(cullDrawBytes * 2u);
					instance->m_SBSMeshCullDrawData->RT_Get()->RT_SetData(cmd, cullDrawData.data(), cullDrawBytes);
				}

				if (!indirectCommands.empty())
				{
					const uint32_t indirectBytes = (uint32_t)(sizeof(nvrhi::DrawIndexedIndirectArguments) * indirectCommands.size());
					if (instance->m_SBSIndirectDrawCommands->RT_Get()->GetHandle()->getDesc().byteSize < indirectBytes)
						instance->m_SBSIndirectDrawCommands->Resize(indirectBytes * 2u);
					instance->m_SBSIndirectDrawCommands->RT_Get()->RT_SetData(cmd, indirectCommands.data(), indirectBytes);
				}

				const uint32_t totalGPUSceneInstances = persistentSceneCount + (uint32_t)transientSceneData.size();
				if (totalGPUSceneInstances > 0)
				{
					const uint32_t gpuSceneBytes = totalGPUSceneInstances * (uint32_t)sizeof(GPUSceneInstanceData);
					if (instance->m_SBSGPUSceneInstances->RT_Get()->GetHandle()->getDesc().byteSize < gpuSceneBytes)
						instance->m_SBSGPUSceneInstances->Resize(gpuSceneBytes * 2u);
				}

				// StorageBufferSet owns one buffer per frame-in-flight. Until GPUScene tracks
				// dirty ranges per frame buffer, upload the persistent scene rows for the
				// current frame to avoid alternating stale GPUScene data.
				if (!gpuSceneData.empty())
				{
					const uint32_t uploadBytes = (uint32_t)(gpuSceneData.size() * sizeof(GPUSceneInstanceData));
					instance->m_SBSGPUSceneInstances->RT_Get()->RT_SetData(cmd, gpuSceneData.data(), uploadBytes);
				}

				if (!transientSceneData.empty())
				{
					const uint32_t uploadBytes = (uint32_t)(transientSceneData.size() * sizeof(GPUSceneInstanceData));
					const uint32_t uploadOffset = persistentSceneCount * (uint32_t)sizeof(GPUSceneInstanceData);
					instance->m_SBSGPUSceneInstances->RT_Get()->RT_SetData(cmd, transientSceneData.data(), uploadBytes, uploadOffset);
				}

				const uint32_t totalGPUMaterials = persistentGPUMaterialCount + (uint32_t)transientMaterialData.size();
				if (totalGPUMaterials > 0)
				{
					const uint32_t gpuMaterialBytes = totalGPUMaterials * (uint32_t)sizeof(GPUMaterialData);
					if (instance->m_SBSGPUMaterials->RT_Get()->GetHandle()->getDesc().byteSize < gpuMaterialBytes)
						instance->m_SBSGPUMaterials->Resize(gpuMaterialBytes * 2u);
				}

				if (!materialData.empty())
				{
					const uint32_t uploadBytes = (uint32_t)(materialData.size() * sizeof(GPUMaterialData));
					instance->m_SBSGPUMaterials->RT_Get()->RT_SetData(cmd, materialData.data(), uploadBytes);
				}

				if (!transientMaterialData.empty())
				{
					const uint32_t uploadBytes = (uint32_t)(transientMaterialData.size() * sizeof(GPUMaterialData));
					const uint32_t uploadOffset = persistentGPUMaterialCount * (uint32_t)sizeof(GPUMaterialData);
					instance->m_SBSGPUMaterials->RT_Get()->RT_SetData(cmd, transientMaterialData.data(), uploadBytes, uploadOffset);
				}
			});
		}

		m_UploadCommandBuffer->End();
		m_UploadCommandBuffer->Submit();

		// ── 3. Execute render passes ──────────────────────────────────────────
		m_CommandBuffer->Begin();

		// The graph is rebuilt every frame (cheap — keeps execute callbacks and image
		// refs current), but Compile() — the lifetime/alias analysis, by far the heavier
		// half — is skipped while the graph's structure is unchanged. Execute() consumes
		// only ExecutionOrder, which is a pure function of that structure, so reusing the
		// cached result is correct even if the underlying GPU images were reallocated.
		{
			LUX_PROFILE_SCOPE("RenderGraph::Build");
			BuildRenderGraph(true);
		}

		const uint64_t structureHash = m_RenderGraph.ComputeStructureHash();
		if (!m_RenderGraphResultValid || structureHash != m_RenderGraphStructureHash)
		{
			LUX_PROFILE_SCOPE("RenderGraph::Compile");
			m_CachedRenderGraphResult = m_RenderGraph.Compile();
			m_RenderGraphStructureHash = structureHash;
			m_RenderGraphResultValid = true;
		}
		const RenderGraph::CompileResult& renderGraphResult = m_CachedRenderGraphResult;
		if (renderGraphResult.ErrorCount > 0 || renderGraphResult.WarningCount > 0)
		{
			size_t diagnosticHash = renderGraphResult.ErrorCount;
			diagnosticHash = HashCombine(diagnosticHash, renderGraphResult.WarningCount);
			for (const RenderGraph::Diagnostic& diagnostic : renderGraphResult.Diagnostics)
			{
				if (diagnostic.Severity == RenderGraph::DiagnosticSeverity::Info)
					continue;

				diagnosticHash = HashCombine(diagnosticHash, static_cast<uint64_t>(diagnostic.Code));
				diagnosticHash = HashCombine(diagnosticHash, diagnostic.PassIndex);
				diagnosticHash = HashCombine(diagnosticHash, diagnostic.Resource);
				diagnosticHash = HashCombine(diagnosticHash, std::hash<std::string>{}(diagnostic.Message));
			}

			if (diagnosticHash != m_LastRenderGraphDiagnosticHash)
			{
				m_LastRenderGraphDiagnosticHash = diagnosticHash;
				if (renderGraphResult.ErrorCount > 0)
					LUX_CORE_ERROR_TAG("RenderGraph", "Validation found {} error(s) and {} warning(s). Rendering will continue.", renderGraphResult.ErrorCount, renderGraphResult.WarningCount);
				else
					LUX_CORE_WARN_TAG("RenderGraph", "Validation found {} warning(s). Rendering will continue.", renderGraphResult.WarningCount);

				uint32_t loggedDiagnostics = 0;
				for (const RenderGraph::Diagnostic& diagnostic : renderGraphResult.Diagnostics)
				{
					if (diagnostic.Severity == RenderGraph::DiagnosticSeverity::Info)
						continue;

					if (loggedDiagnostics++ >= 8)
					{
						LUX_CORE_WARN_TAG("RenderGraph", "Additional diagnostics hidden. Open the Renderer Debugger Render Graph inspector for the full list.");
						break;
					}

					const char* severity = diagnostic.Severity == RenderGraph::DiagnosticSeverity::Error ? "Error" : "Warning";
					LUX_CORE_WARN_TAG("RenderGraph", "{}: {}", severity, diagnostic.Message);
				}
			}
		}
		else
		{
			m_LastRenderGraphDiagnosticHash = 0;
		}
		{
			LUX_PROFILE_SCOPE("RenderGraph::Execute");
			m_RenderGraph.Execute(renderGraphResult);
		}

		m_CommandBuffer->End();
		m_CommandBuffer->Submit();

		m_PreviousViewProjection = m_CurrentViewProjection;
		m_PreviousJitter = m_CurrentJitter;
		m_TAAJitterIndex++;
		m_TemporalHistoryValid = true;

		// ── 5. Update statistics ──────────────────────────────────────────────
		{
			LUX_PROFILE_SCOPE("UpdateStatistics");
			UpdateStatistics();
		}

		// ── 6. Clear draw lists for next frame ────────────────────────────────
		clearAll();
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Individual pass implementations
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::ShadowMapPass()
	{
		ScopedCPUProfile cpuProfile(*this, "ShadowMapPass");
		const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
		if (dirLight.Intensity <= 0.0f || !dirLight.CastShadows)
		{
			if (m_DirectionalShadowMapCacheValid && !m_DirectionalShadowMapNeedsRender)
				return;

			// Clear every cascade so geometry doesn't sample stale data.
			for (auto& shadowMapPass : m_ShadowMapPasses)
			{
				Renderer::BeginRenderPass(m_CommandBuffer, shadowMapPass, /*explicitClear=*/true);
				Renderer::EndRenderPass(m_CommandBuffer);
			}
			m_DirectionalShadowMapCacheValid = true;
			m_DirectionalShadowMapNeedsRender = false;
			return;
		}

		if (m_DirectionalShadowMapCacheValid && !m_DirectionalShadowMapNeedsRender)
			return;

		BeginProfiledGPU("ShadowMapPass");
		const MeshPassState& shadowPass = GetMeshPass(MeshPassType::ShadowDepth);
		for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_ShadowMapPasses[cascade], /*explicitClear=*/true);

			for (const MeshKey& key : shadowPass.DrawOrder)
			{
				const auto drawIt = shadowPass.DrawList.find(key);
				if (drawIt == shadowPass.DrawList.end()) continue;
				auto it = m_ShadowMeshTransformMap.find(key);
				if (it == m_ShadowMeshTransformMap.end()) continue;

				const auto& cascadeTmd = it->second.Cascade;
				const uint32_t instCount = (uint32_t)cascadeTmd.ObjectIndices.size();
				if (instCount == 0) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				drawCmd.InstanceCount = instCount;

				const MeshDrawParams params(cascadeTmd);
				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, params, cascade]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/false, cascade);
					});
			}

			Renderer::EndRenderPass(m_CommandBuffer);
		}
		EndProfiledGPU();
		m_DirectionalShadowMapCacheValid = true;
		m_DirectionalShadowMapNeedsRender = false;
	}

	void SceneRenderer::SpotShadowMapPass()
	{
		ScopedCPUProfile cpuProfile(*this, "SpotShadowMapPass");
		if (m_SpotShadowCount == 0)
		{
			if (m_SpotShadowMapCacheValid && !m_SpotShadowMapNeedsRender)
				return;

			Renderer::BeginRenderPass(m_CommandBuffer, m_SpotShadowMapPass, /*explicitClear=*/true);
			Renderer::EndRenderPass(m_CommandBuffer);
			m_SpotShadowMapCacheValid = true;
			m_SpotShadowMapNeedsRender = false;
			return;
		}

		if (m_SpotShadowMapCacheValid && !m_SpotShadowMapNeedsRender)
			return;

		BeginProfiledGPU("SpotShadowMapPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_SpotShadowMapPass, /*explicitClear=*/true);

		const uint32_t tilesPerRow = m_SpotShadowAtlasGridSize;
		const uint32_t tileSize = m_SpotShadowTileSize;
		const uint32_t atlasSize = m_SpotShadowMapSize;
		const MeshPassState& shadowPass = GetMeshPass(MeshPassType::ShadowDepth);

		for (uint32_t shadowIndex = 0; shadowIndex < m_SpotShadowCount; shadowIndex++)
		{
			const uint32_t tileX = shadowIndex % tilesPerRow;
			const uint32_t tileY = shadowIndex / tilesPerRow;
			Renderer::SetViewport(m_CommandBuffer, tileX * tileSize, tileY * tileSize, tileSize, tileSize);

			for (const MeshKey& key : shadowPass.DrawOrder)
			{
				const auto drawIt = shadowPass.DrawList.find(key);
				if (drawIt == shadowPass.DrawList.end()) continue;
				auto it = m_ShadowMeshTransformMap.find(key);
				if (it == m_ShadowMeshTransformMap.end()) continue;
				const auto& cascadeTmd = it->second.Cascade;
				const uint32_t instCount = (uint32_t)cascadeTmd.ObjectIndices.size();
				if (instCount == 0) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				drawCmd.InstanceCount = instCount;

				const MeshDrawParams params(cascadeTmd);
				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, params, shadowIndex]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/false, shadowIndex);
					});
			}
		}

		Renderer::SetViewport(m_CommandBuffer, 0, 0, atlasSize, atlasSize);

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
		m_SpotShadowMapCacheValid = true;
		m_SpotShadowMapNeedsRender = false;
	}

	void SceneRenderer::PreDepthPass()
	{
		ScopedCPUProfile cpuProfile(*this, "PreDepthPass");
		BeginProfiledGPU("PreDepthPass");

		Renderer::BeginRenderPass(m_CommandBuffer, m_PreDepthPass, /*explicitClear=*/true);

		const MeshPassState& depthPass = GetMeshPass(MeshPassType::DepthPrepass);
		for (const MeshKey& key : depthPass.DrawOrder)
		{
			const auto drawIt = depthPass.DrawList.find(key);
			if (drawIt == depthPass.DrawList.end()) continue;
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			const MeshDrawParams params(it->second);
			StaticDrawCommand drawCmd = drawIt->second;

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, params]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/false, 0, /*useVisibleObjectIndexes=*/false, /*useIndirect=*/false);
				});
		}

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::HZBCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "HZB");
		if (!m_HierarchicalDepthPass || !m_HierarchicalDepthTexture.Texture || !m_PreDepthPass || m_HZBMaterials.empty())
			return;

		constexpr uint32_t maxMipBatchSize = 4;
		const uint32_t hzbMipCount = m_HierarchicalDepthTexture.Texture->GetMipLevelCount();
		if (hzbMipCount == 0)
			return;

		struct HierarchicalZComputePushConstants
		{
			glm::vec2 DispatchThreadIdToBufferUV;
			glm::vec2 InputViewportMaxBound;
			glm::vec2 InvSize;
			int FirstLod = 0;
			int IsFirstPass = 0;
		};

		BeginProfiledGPU("HZB");
		Renderer::BeginComputePass(m_CommandBuffer, m_HierarchicalDepthPass);

		auto reduceHZB = [&](uint32_t startDestMip, uint32_t parentMip, const glm::vec2& dispatchThreadIdToBufferUV, const glm::vec2& inputViewportMaxBound, bool isFirstPass)
		{
			const uint32_t materialIndex = startDestMip / maxMipBatchSize;
			if (materialIndex >= m_HZBMaterials.size() || !m_HZBMaterials[materialIndex])
				return;

			const glm::uvec2 srcSize = DivideRoundUp(m_HierarchicalDepthTexture.Texture->GetSize(), 1u << glm::min(parentMip, 31u));
			const glm::uvec2 dstSize = DivideRoundUp(m_HierarchicalDepthTexture.Texture->GetSize(), 1u << glm::min(startDestMip, 31u));

			HierarchicalZComputePushConstants pushConstants;
			pushConstants.DispatchThreadIdToBufferUV = dispatchThreadIdToBufferUV;
			pushConstants.InputViewportMaxBound = inputViewportMaxBound;
			pushConstants.InvSize = {
				srcSize.x > 0 ? 1.0f / (float)srcSize.x : 1.0f,
				srcSize.y > 0 ? 1.0f / (float)srcSize.y : 1.0f
			};
			pushConstants.FirstLod = 1;
			pushConstants.IsFirstPass = isFirstPass ? 1 : 0;

			const glm::uvec3 workGroups = {
				DivideRoundUp(glm::max(1u, dstSize.x), 8u),
				DivideRoundUp(glm::max(1u, dstSize.y), 8u),
				1
			};

			Renderer::DispatchCompute(m_CommandBuffer, m_HierarchicalDepthPass, m_HZBMaterials[materialIndex], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			m_HierarchicalDepthPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_HierarchicalDepthTexture.Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		};

		const glm::uvec2 depthSize = m_PreDepthPass->GetDepthOutput()->GetSize();
		if (depthSize.x > 0 && depthSize.y > 0)
		{
			reduceHZB(
				0,
				0,
				1.0f / glm::vec2(depthSize),
				(glm::vec2(depthSize) - 0.5f) / glm::vec2(depthSize),
				true);
		}

		for (uint32_t startDestMip = maxMipBatchSize; startDestMip < hzbMipCount; startDestMip += maxMipBatchSize)
		{
			const glm::uvec2 parentSize = DivideRoundUp(m_HierarchicalDepthTexture.Texture->GetSize(), 1u << glm::min(startDestMip - 1u, 31u));
			if (parentSize.x == 0 || parentSize.y == 0)
				continue;

			reduceHZB(
				startDestMip,
				startDestMip - 1u,
				2.0f / glm::vec2(parentSize),
				glm::vec2(1.0f),
				false);
		}

		Renderer::EndComputePass(m_CommandBuffer, m_HierarchicalDepthPass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
		m_HZBPrimed = true;
	}

	void SceneRenderer::PreIntegration()
	{
		ScopedCPUProfile cpuProfile(*this, "PreIntegration");
		if (!m_PreIntegrationPass || !m_PreIntegrationVisibilityTexture.Texture || m_PreIntegrationMaterials.empty())
			return;

		Ref<Texture2D> visibilityTexture = m_PreIntegrationVisibilityTexture.Texture;
		const uint32_t mipCount = visibilityTexture->GetMipLevelCount();
		if (mipCount < 2)
			return;

		Ref<Image2D> visibilityImage = visibilityTexture->GetImage();
		Renderer::ClearImage(m_CommandBuffer, visibilityImage, nvrhi::Color(1.0f, 1.0f, 1.0f, 1.0f), visibilityImage->GetMipImageView(0));

		struct PreIntegrationComputePushConstants
		{
			glm::vec2 HZBResFactor;
			glm::vec2 ResFactor;
			glm::vec2 ProjectionParams;
			int PrevLod = 0;
		} pushConstants;

		pushConstants.ProjectionParams = { m_SceneData.SceneCamera.Far, m_SceneData.SceneCamera.Near };

		BeginProfiledGPU("PreIntegration");
		Renderer::BeginComputePass(m_CommandBuffer, m_PreIntegrationPass);

		for (uint32_t mip = 1; mip < mipCount && mip - 1 < m_PreIntegrationMaterials.size(); mip++)
		{
			auto [mipWidth, mipHeight] = visibilityTexture->GetMipSize(mip);
			if (mipWidth == 0 || mipHeight == 0 || !m_PreIntegrationMaterials[mip - 1])
				continue;

			const glm::vec2 resFactor = 1.0f / glm::vec2(mipWidth, mipHeight);
			pushConstants.HZBResFactor = resFactor * m_SSROptions.HZBUvFactor;
			pushConstants.ResFactor = resFactor;
			pushConstants.PrevLod = 0;

			const glm::uvec3 workGroups = { DivideRoundUp(mipWidth, 8u), DivideRoundUp(mipHeight, 8u), 1 };
			Renderer::DispatchCompute(m_CommandBuffer, m_PreIntegrationPass, m_PreIntegrationMaterials[mip - 1], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			m_PreIntegrationPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, visibilityImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		Renderer::EndComputePass(m_CommandBuffer, m_PreIntegrationPass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::ClusterBuildPass()
	{
		ScopedCPUProfile cpuProfile(*this, "ClusterBuildPass");
		if (!m_ClusterBuildPass || m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		struct ClusterBuildPushConstants
		{
			glm::vec4  ScreenSizeNearFar; // xy = render resolution (px), z = zNear, w = zFar
			glm::uvec4 GridSize;          // xyz = cluster grid dims
		} push;

		push.ScreenSizeNearFar = {
			static_cast<float>(glm::max(1u, m_ViewportWidth)),
			static_cast<float>(glm::max(1u, m_ViewportHeight)),
			m_SceneData.SceneCamera.Near,
			m_SceneData.SceneCamera.Far
		};
		push.GridSize = { ClusterGridX, ClusterGridY, ClusterGridZ, 0u };

		// TODO(clustered): cache — only rebuild on resize / projection change.
		// Rebuilt every frame for now (4608 froxels, negligible cost).
		constexpr uint32_t kThreadsPerGroup = 64;
		const glm::uvec3 groups = { (ClusterCount + kThreadsPerGroup - 1u) / kThreadsPerGroup, 1u, 1u };

		BeginProfiledGPU("ClusterBuildPass");
		Renderer::BeginComputePass(m_CommandBuffer, m_ClusterBuildPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_ClusterBuildPass, nullptr, groups, Buffer(&push, sizeof(push)));
		m_ClusterBuildPass->GetPipeline()->BufferMemoryBarrier(m_CommandBuffer, m_SBSClusterAABBs->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndComputePass(m_CommandBuffer, m_ClusterBuildPass);
		EndProfiledGPU();
	}

	void SceneRenderer::ClusterLightCullingPass()
	{
		ScopedCPUProfile cpuProfile(*this, "ClusterLightCullingPass");
		if (!m_ClusterLightCullingPass || m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		// Reset the dynamic-allocation cursors ([0]=point, [1]=spot) before the
		// assignment dispatch atomically appends into the packed index lists.
		Ref<RenderCommandBuffer> commandBuffer = m_CommandBuffer;
		Ref<StorageBufferSet> counter = m_SBSClusterLightCounter;
		Renderer::Submit([commandBuffer, counter]() mutable
		{
			commandBuffer->GetActive()->clearBufferUInt(counter->RT_Get()->GetHandle(), 0u);
		});

		constexpr uint32_t kThreadsPerGroup = 64;
		const glm::uvec3 groups = { (ClusterCount + kThreadsPerGroup - 1u) / kThreadsPerGroup, 1u, 1u };

		BeginProfiledGPU("ClusterLightCullingPass");
		Renderer::BeginComputePass(m_CommandBuffer, m_ClusterLightCullingPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_ClusterLightCullingPass, nullptr, groups, Buffer());

		Ref<PipelineCompute> pipeline = m_ClusterLightCullingPass->GetPipeline();
		pipeline->BufferMemoryBarrier(m_CommandBuffer, m_SBSPointLightGrid->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		pipeline->BufferMemoryBarrier(m_CommandBuffer, m_SBSSpotLightGrid->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		pipeline->BufferMemoryBarrier(m_CommandBuffer, m_SBSPointLightIndexList->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		pipeline->BufferMemoryBarrier(m_CommandBuffer, m_SBSSpotLightIndexList->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndComputePass(m_CommandBuffer, m_ClusterLightCullingPass);
		EndProfiledGPU();
	}

	void SceneRenderer::MeshCullingPass()
	{
		ScopedCPUProfile cpuProfile(*this, "MeshCullingPass");
		if (!m_Options.EnableGPUDrivenRendering || !m_MeshCullingPass || m_MeshCullDrawCount == 0)
			return;

		struct MeshCullingPushConstants
		{
			glm::mat4 ViewProjection;
			glm::vec4 HZBUVFactorAndViewportSize;
			uint32_t DrawCount = 0;
			uint32_t FrustumCullingEnabled = 1;
			uint32_t OcclusionCullingEnabled = 0;
			uint32_t NumDepthMips = 1;
			float DepthBias = 0.001f;
			float BoundsScale = 1.05f;
			uint32_t Padding0 = 0;
			uint32_t Padding1 = 0;
		} pushConstants;

		pushConstants.ViewProjection = m_SceneData.SceneCamera.Camera.GetProjectionMatrix() * m_SceneData.SceneCamera.ViewMatrix;
		pushConstants.HZBUVFactorAndViewportSize = {
			m_SSROptions.HZBUvFactor.x,
			m_SSROptions.HZBUvFactor.y,
			(float)glm::max(1u, m_ViewportWidth),
			(float)glm::max(1u, m_ViewportHeight)
		};
		pushConstants.DrawCount = m_MeshCullDrawCount;
		pushConstants.FrustumCullingEnabled = m_Options.EnableFrustumCulling ? 1u : 0u;
		constexpr bool enableConservativeHZBOcclusion = true;
		pushConstants.OcclusionCullingEnabled = enableConservativeHZBOcclusion && m_Options.EnableOcclusionCulling && m_HZBPrimed && m_HierarchicalDepthTexture.Texture ? 1u : 0u;
		pushConstants.NumDepthMips = m_HierarchicalDepthTexture.Texture ? glm::max(1u, m_HierarchicalDepthTexture.Texture->GetMipLevelCount()) : 1u;
		pushConstants.DepthBias = m_Options.OcclusionDepthBias;
		pushConstants.BoundsScale = m_Options.OcclusionBoundsScale;

		BeginProfiledGPU("MeshCullingPass");
		Renderer::BeginComputePass(m_CommandBuffer, m_MeshCullingPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_MeshCullingPass, nullptr, { m_MeshCullDrawCount, 1, 1 }, Buffer(&pushConstants, sizeof(pushConstants)));
		m_MeshCullingPass->GetPipeline()->BufferMemoryBarrier(m_CommandBuffer, m_SBSVisibleObjectIndexes->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		m_MeshCullingPass->GetPipeline()->BufferMemoryBarrier(m_CommandBuffer, m_SBSIndirectDrawCommands->Get(), PipelineStage::ComputeShader, ResourceAccessFlags::ShaderWrite, PipelineStage::DrawIndirect, ResourceAccessFlags::IndirectCommandRead);
		Renderer::EndComputePass(m_CommandBuffer, m_MeshCullingPass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SkyboxPass()
	{
		ScopedCPUProfile cpuProfile(*this, "SkyboxPass");
		Ref<TextureCube> radianceMap = GetEnvironmentRadianceMap(m_FrameEnvironment.Environment);
		if (!radianceMap)
			return;

		BeginProfiledGPU("SkyboxPass");

		m_SkyboxMaterial->Set("u_Uniforms.TextureLod", m_FrameEnvironment.SkyboxLod);
		m_SkyboxMaterial->Set("u_Uniforms.Intensity", m_FrameEnvironment.EnvironmentIntensity);
		m_SkyboxMaterial->Set("u_Texture", radianceMap);

		Renderer::BeginRenderPass(m_CommandBuffer, m_SkyboxPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_SkyboxPass->GetPipeline(), m_SkyboxMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SkyAtmospherePass()
	{
		ScopedCPUProfile cpuProfile(*this, "SkyAtmospherePass");
		if (!m_SkyAtmospherePass || !m_SkyAtmosphereMaterial || !m_FrameEnvironment.SkyAtmosphereEnabled)
			return;

		BeginProfiledGPU("SkyAtmospherePass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_SkyAtmospherePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_SkyAtmospherePass->GetPipeline(), m_SkyAtmosphereMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::BakeCloudNoise()
	{
		if (m_CloudNoiseBaked || !m_CloudBaseShapeBakePass || !m_CloudDetailBakePass || !m_CloudCurlBakePass)
			return;

		BeginProfiledGPU("CloudNoiseBake");
		Renderer::BeginComputePass(m_CommandBuffer, m_CloudBaseShapeBakePass);
		Renderer::DispatchCompute(m_CommandBuffer, m_CloudBaseShapeBakePass, nullptr, { 16u, 16u, 16u }, Buffer());
		Renderer::EndComputePass(m_CommandBuffer, m_CloudBaseShapeBakePass);
		m_CloudBaseShapeBakePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_CloudBaseShapeVolume, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

		Renderer::BeginComputePass(m_CommandBuffer, m_CloudDetailBakePass);
		Renderer::DispatchCompute(m_CommandBuffer, m_CloudDetailBakePass, nullptr, { 4u, 4u, 4u }, Buffer());
		Renderer::EndComputePass(m_CommandBuffer, m_CloudDetailBakePass);
		m_CloudDetailBakePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_CloudDetailVolume, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

		Renderer::BeginComputePass(m_CommandBuffer, m_CloudCurlBakePass);
		Renderer::DispatchCompute(m_CommandBuffer, m_CloudCurlBakePass, nullptr, { 4u, 4u, 4u }, Buffer());
		Renderer::EndComputePass(m_CommandBuffer, m_CloudCurlBakePass);
		m_CloudCurlBakePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_CloudCurlVolume, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);

		m_CloudNoiseBaked = true;
	}

	void SceneRenderer::VolumetricCloudPass()
	{
		ScopedCPUProfile cpuProfile(*this, "VolumetricCloudPass");
		if (!m_VolumetricCloudPass || !m_VolumetricCloudMaterial || !m_FrameEnvironment.VolumetricCloudsEnabled)
			return;

		BakeCloudNoise();

		BeginProfiledGPU("VolumetricCloudPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_VolumetricCloudPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_VolumetricCloudPass->GetPipeline(), m_VolumetricCloudMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::VolumetricCloudTemporalPass()
	{
		ScopedCPUProfile cpuProfile(*this, "VolumetricCloudTemporalPass");
		if (!m_VolumetricCloudTemporalPass || !m_VolumetricCloudPass || !m_FrameEnvironment.VolumetricCloudsEnabled)
			return;

		const uint32_t readIndex = m_CloudHistoryIndex & 1u;
		const uint32_t writeIndex = readIndex ^ 1u;
		Ref<Image2D> historyInput = m_CloudHistoryImages[readIndex];
		Ref<Image2D> historyOutput = m_CloudHistoryImages[writeIndex];
		if (!historyInput || !historyOutput)
			return;

		m_VolumetricCloudTemporalPass->SetInput("u_CurrentCloud", m_VolumetricCloudPass->GetOutput(0));
		m_VolumetricCloudTemporalPass->SetInput("u_CurrentCloudDepth", m_VolumetricCloudPass->GetOutput(1));
		m_VolumetricCloudTemporalPass->SetInput("u_HistoryCloud", historyInput);
		m_VolumetricCloudTemporalPass->SetInput("o_ResolvedCloud", historyOutput);

		struct CloudTemporalPushConstants
		{
			float Blend;
			uint32_t HasHistory;
			glm::vec2 Padding;
		} push;
		push.Blend = 0.92f;
		push.HasHistory = m_CloudHistoryValid ? 1u : 0u;
		push.Padding = { 0.0f, 0.0f };

		const glm::uvec3 groups = {
			(glm::max(m_CloudRenderSize.x, 1u) + 7u) / 8u,
			(glm::max(m_CloudRenderSize.y, 1u) + 7u) / 8u,
			1u
		};

		BeginProfiledGPU("VolumetricCloudTemporalPass");
		Renderer::BeginComputePass(m_CommandBuffer, m_VolumetricCloudTemporalPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_VolumetricCloudTemporalPass, nullptr, groups, Buffer(&push, sizeof(push)));
		Renderer::EndComputePass(m_CommandBuffer, m_VolumetricCloudTemporalPass);
		m_VolumetricCloudTemporalPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, historyOutput, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);

		m_CloudHistoryIndex = writeIndex;
		m_CloudHistoryValid = true;
	}

	void SceneRenderer::VolumetricCloudCompositePass()
	{
		ScopedCPUProfile cpuProfile(*this, "VolumetricCloudCompositePass");
		if (!m_VolumetricCloudCompositePass || !m_VolumetricCloudCompositeMaterial || !m_VolumetricCloudPass || !m_FrameEnvironment.VolumetricCloudsEnabled)
			return;

		// Prefer the temporally-resolved half-res buffer; fall back to the raw raymarch.
		Ref<Image2D> cloudColor = (m_VolumetricCloudTemporalPass && m_CloudHistoryImages[m_CloudHistoryIndex & 1u])
			? m_CloudHistoryImages[m_CloudHistoryIndex & 1u]
			: m_VolumetricCloudPass->GetOutput(0);
		SetRenderPassInputIfValid(m_VolumetricCloudCompositePass, "u_CloudTexture", cloudColor);
		SetRenderPassInputIfValid(m_VolumetricCloudCompositePass, "u_CloudDepthTexture", m_VolumetricCloudPass->GetOutput(1));

		BeginProfiledGPU("VolumetricCloudCompositePass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_VolumetricCloudCompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_VolumetricCloudCompositePass->GetPipeline(), m_VolumetricCloudCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::AtmosphericFogPass()
	{
		ScopedCPUProfile cpuProfile(*this, "AtmosphericFogPass");
		if (!m_AtmosphericFogPass || !m_AtmosphericFogMaterial || (!m_FrameEnvironment.HeightFogEnabled && !m_FrameEnvironment.LocalFogEnabled))
			return;

		m_AtmosphericFogMaterial->Set("u_Uniforms.DebugMode", m_DebugViewMode == DebugViewMode::LocalFogDensity ? 1u : 0u);

		BeginProfiledGPU("AtmosphericFogPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_AtmosphericFogPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_AtmosphericFogPass->GetPipeline(), m_AtmosphericFogMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SelectedGeometryPass()
	{
		ScopedCPUProfile cpuProfile(*this, "SelectedGeometryPass");
		const MeshPassState& selectedPass = GetMeshPass(MeshPassType::SelectedMask);
		if (selectedPass.DrawList.empty())
			return;

		BeginProfiledGPU("SelectedGeometryPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_SelectedGeometryPass);

		for (const MeshKey& key : selectedPass.DrawOrder)
		{
			const auto drawIt = selectedPass.DrawList.find(key);
			if (drawIt == selectedPass.DrawList.end()) continue;
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			StaticDrawCommand drawCmd = drawIt->second;
			const MeshDrawParams params(it->second);

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, params]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/true, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering,
					instance->m_SelectedGeometryPass->GetPipeline()->GetShader());
				});
		}

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GBufferPass()
	{
		ScopedCPUProfile cpuProfile(*this, "GBufferPass");
		BeginProfiledGPU("GBufferPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryPass);

		const MeshPassState& opaquePass = GetMeshPass(MeshPassType::Opaque);
		for (const MeshKey& key : opaquePass.DrawOrder)
		{
			const auto drawIt = opaquePass.DrawList.find(key);
			if (drawIt == opaquePass.DrawList.end()) continue;
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			StaticDrawCommand drawCmd = drawIt->second;
			const MeshDrawParams params(it->second);

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, params]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/true, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering,
					instance->m_GeometryPass->GetPipeline()->GetShader());
				});
		}

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::DeferredLightingPass()
	{
		ScopedCPUProfile cpuProfile(*this, "DeferredLightingPass");
		if (!m_DeferredLightingPass || !m_DeferredLightingMaterial)
			return;

		BeginProfiledGPU("DeferredLightingPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_DeferredLightingPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_DeferredLightingPass->GetPipeline(), m_DeferredLightingMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::TransparentForwardPass()
	{
		ScopedCPUProfile cpuProfile(*this, "TransparentForwardPass");
		const MeshPassState& transparentPass = GetMeshPass(MeshPassType::Transparent);
		if (transparentPass.DrawList.empty())
			return;

		BeginProfiledGPU("TransparentForwardPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryPassTransparent);

		for (const MeshKey& key : transparentPass.DrawOrder)
		{
			const auto drawIt = transparentPass.DrawList.find(key);
			if (drawIt == transparentPass.DrawList.end()) continue;
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			StaticDrawCommand drawCmd = drawIt->second;
			const MeshDrawParams params(it->second);

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, params]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/true, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering,
					instance->m_GeometryPassTransparent->GetPipeline()->GetShader());
				});
		}

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GeometryWireframePass()
	{
		ScopedCPUProfile cpuProfile(*this, "GeometryWireframePass");
		const MeshPassState& wireframePass = GetMeshPass(MeshPassType::Wireframe);
		const MeshPassState& colliderPass = GetMeshPass(MeshPassType::PhysicsCollider);
		if ((!m_Options.ShowSelectedInWireframe || wireframePass.DrawList.empty())
			&& (!m_Options.ShowPhysicsColliders || colliderPass.DrawList.empty()))
			return;

		BeginProfiledGPU("GeometryWireframePass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryWireframePass);

		if (m_Options.ShowSelectedInWireframe)
		{
			for (const MeshKey& key : wireframePass.DrawOrder)
			{
				const auto drawIt = wireframePass.DrawList.find(key);
				if (drawIt == wireframePass.DrawList.end()) continue;
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				const MeshDrawParams params(it->second);

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, params]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/true, 0, /*useVisibleObjectIndexes=*/false, false,
						instance->m_GeometryWireframePass->GetPipeline()->GetShader());
					});
			}
		}

		if (m_Options.ShowPhysicsColliders)
		{
			for (const MeshKey& key : colliderPass.DrawOrder)
			{
				const auto drawIt = colliderPass.DrawList.find(key);
				if (drawIt == colliderPass.DrawList.end()) continue;
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				const MeshDrawParams params(it->second);

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, params]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/true, 0, /*useVisibleObjectIndexes=*/false, false,
						instance->m_GeometryWireframePass->GetPipeline()->GetShader());
					});
			}
		}

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GBufferDebugPass()
	{
		ScopedCPUProfile cpuProfile(*this, "GBufferDebugPass");
		if (!m_GBufferDebugPass || !m_GBufferDebugMaterial)
			return;

		const uint32_t debugMode = ResolveGBufferDebugMode(m_DebugViewMode);
		if (debugMode == 0)
			return;

		m_GBufferDebugMaterial->Set("u_Uniforms.Mode", debugMode);

		BeginProfiledGPU("GBufferDebugPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_GBufferDebugPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_GBufferDebugPass->GetPipeline(), m_GBufferDebugMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GTAOCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "GTAO");
		if (!m_Options.EnableGTAO || !m_GTAOComputePass || !m_GTAOOutputImage)
			return;

		BeginProfiledGPU("GTAO");
		Renderer::BeginComputePass(m_CommandBuffer, m_GTAOComputePass);
		Renderer::DispatchCompute(m_CommandBuffer, m_GTAOComputePass, nullptr, m_GTAOWorkGroups, Buffer(&m_GTAODataCB, sizeof(m_GTAODataCB)));
		Renderer::EndComputePass(m_CommandBuffer, m_GTAOComputePass);
		m_GTAOComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_GTAOOutputImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		m_GTAOComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_GTAOEdgesOutputImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GTAODenoiseCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "GTAO-Denoise");
		if (!m_Options.EnableGTAO || !m_GTAODenoisePass[0] || !m_GTAODenoisePass[1] || !m_GTAOOutputImage)
			return;

		const uint32_t denoisePasses = (uint32_t)glm::max(m_Options.GTAODenoisePasses, 0);
		if (denoisePasses == 0)
		{
			m_GTAOFinalImage = m_GTAOOutputImage;
			if (m_AOCompositePass)
			{
				m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
				m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
				m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
			}
			if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
				m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			return;
		}

		m_GTAODenoiseConstants.DenoiseBlurBeta = m_GTAODataCB.DenoiseBlurBeta;
		m_GTAODenoiseConstants.ResolutionScale = m_GTAODataCB.ResolutionScale;

		BeginProfiledGPU("GTAO-Denoise");
		for (uint32_t pass = 0; pass < denoisePasses; pass++)
		{
			const uint32_t passIndex = (pass % 2u) != 0u ? 1u : 0u;
			Ref<ComputePass> denoisePass = m_GTAODenoisePass[passIndex];
			Ref<Image2D> outputImage = passIndex == 0 ? m_GTAODenoiseImage : m_GTAOOutputImage;

			Renderer::BeginComputePass(m_CommandBuffer, denoisePass);
			Renderer::DispatchCompute(m_CommandBuffer, denoisePass, nullptr, m_GTAODenoiseWorkGroups, Buffer(&m_GTAODenoiseConstants, sizeof(m_GTAODenoiseConstants)));
			Renderer::EndComputePass(m_CommandBuffer, denoisePass);
			denoisePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, outputImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		m_GTAOFinalImage = (denoisePasses % 2u) != 0u ? m_GTAODenoiseImage : m_GTAOOutputImage;
		if (m_AOCompositePass)
		{
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
		}
		if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
			m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GTAOTemporalAccumulationCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "GTAO-Temporal");
		if (!m_Options.EnableGTAO || !m_Options.EnableGTAOTemporalAccumulation)
			return;
		if (!m_GTAOTemporalPass || !m_GTAOFinalImage || !m_GTAOHistoryImages[0] || !m_GTAOHistoryImages[1])
			return;

		const uint32_t readIndex = m_GTAOHistoryIndex & 1u;
		const uint32_t writeIndex = readIndex ^ 1u;
		Ref<Image2D> historyInput = m_GTAOHistoryImages[readIndex];
		Ref<Image2D> historyOutput = m_GTAOHistoryImages[writeIndex];

		m_GTAOTemporalPass->SetInput("u_CurrentAO", m_GTAOFinalImage);
		m_GTAOTemporalPass->SetInput("u_HistoryAO", historyInput);
		m_GTAOTemporalPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
		m_GTAOTemporalPass->SetInput("o_HistoryAO", historyOutput);

		TemporalAccumulationConstants constants;
		constants.PreviousViewProjection = m_PreviousViewProjection;
		constants.Blend = m_Options.GTAOTemporalBlend;
		constants.HasHistory = m_TemporalHistoryValid ? 1u : 0u;
		constants.BentNormals = m_Options.GTAOBentNormals ? 1u : 0u;
		constants.ResolutionScale = GetEffectResolutionDivisor(m_Options.GTAOResolutionScale);

		BeginProfiledGPU("GTAO-Temporal");
		Renderer::BeginComputePass(m_CommandBuffer, m_GTAOTemporalPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_GTAOTemporalPass, nullptr, m_GTAOTemporalWorkGroups, Buffer(&constants, sizeof(constants)));
		Renderer::EndComputePass(m_CommandBuffer, m_GTAOTemporalPass);
		m_GTAOTemporalPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, historyOutput, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);

		m_GTAOHistoryIndex = writeIndex;
		m_GTAOFinalImage = historyOutput;
		if (m_AOCompositePass)
		{
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
		}
		if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
			m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
	}

	void SceneRenderer::AOComposite()
	{
		ScopedCPUProfile cpuProfile(*this, "AOComposite");
		if (!m_AOCompositePass || !m_AOCompositeMaterial || !m_GTAOFinalImage)
			return;

		BeginProfiledGPU("AOComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_AOCompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_AOCompositePass->GetPipeline(), m_AOCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::AODebugPass()
	{
		ScopedCPUProfile cpuProfile(*this, "AODebug");
		if (!m_AODebugPass || !m_AODebugMaterial || !m_GTAOFinalImage)
			return;

		m_AODebugPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
		m_AODebugPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
		m_AODebugPass->SetInput("u_Normal", GetGeometryNormalOutput());

		BeginProfiledGPU("AODebug");
		Renderer::BeginRenderPass(m_CommandBuffer, m_AODebugPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_AODebugPass->GetPipeline(), m_AODebugMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::PreConvolutionCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "PreConvolution");
		if (!m_Options.EnableSSR || !m_PreConvolutionComputePass || !m_PreConvolutedTexture.Texture || m_PreConvolutionMaterials.empty())
			return;

		struct PreConvolutionComputePushConstants
		{
			int PrevLod = 0;
			int Mode = 0;
		} pushConstants;

		Ref<Image2D> preConvolutedImage = m_PreConvolutedTexture.Texture->GetImage();
		auto transitionMip = [commandBuffer = m_CommandBuffer, preConvolutedImage](uint32_t mip, nvrhi::ResourceStates state, const char* label)
		{
			std::string markerName = std::format("Barrier PreConvolution mip {} {}", mip, label);
			Renderer::Submit([commandBuffer, preConvolutedImage, mip, state, markerName]() mutable
			{
				nvrhi::CommandListHandle commandList = commandBuffer->GetActive();
				commandBuffer->RT_BeginMarker(markerName);
				commandList->setTextureState(preConvolutedImage->GetHandle(), nvrhi::TextureSubresourceSet(mip, 1, 0, 1), state);
				commandList->commitBarriers();
				commandBuffer->RT_EndMarker();
			});
		};

		BeginProfiledGPU("PreConvolution");
		Renderer::BeginComputePass(m_CommandBuffer, m_PreConvolutionComputePass);

		if (m_PreConvolutionMaterials[0])
		{
			auto [width, height] = m_PreConvolutedTexture.Texture->GetMipSize(0);
			const glm::uvec3 workGroups = { DivideRoundUp(glm::max(1u, width), 16u), DivideRoundUp(glm::max(1u, height), 16u), 1 };
			pushConstants.PrevLod = 0;
			pushConstants.Mode = 0;
			transitionMip(0, nvrhi::ResourceStates::UnorderedAccess, "write");
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[0], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			transitionMip(0, nvrhi::ResourceStates::ShaderResource, "read");
		}

		const uint32_t mipCount = m_PreConvolutedTexture.Texture->GetMipLevelCount();
		for (uint32_t mip = 1; mip < mipCount && mip < m_PreConvolutionMaterials.size(); mip++)
		{
			if (!m_PreConvolutionMaterials[mip])
				continue;

			auto [mipWidth, mipHeight] = m_PreConvolutedTexture.Texture->GetMipSize(mip);
			const glm::uvec3 workGroups = { DivideRoundUp(glm::max(1u, mipWidth), 16u), DivideRoundUp(glm::max(1u, mipHeight), 16u), 1 };
			pushConstants.PrevLod = (int)mip - 1;

			pushConstants.Mode = 1;
			transitionMip(mip, nvrhi::ResourceStates::UnorderedAccess, "write");
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[mip], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			transitionMip(mip, nvrhi::ResourceStates::ShaderResource, "read");

			pushConstants.Mode = 2;
			transitionMip(mip, nvrhi::ResourceStates::UnorderedAccess, "write");
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[mip], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			transitionMip(mip, nvrhi::ResourceStates::ShaderResource, "read");
		}

		Renderer::EndComputePass(m_CommandBuffer, m_PreConvolutionComputePass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SSRCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "SSR");
		if (!m_Options.EnableSSR || !m_SSRPass || !m_SSRImage)
			return;

		SSROptionsUB ssrOptions = m_SSROptions;
		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
		ssrOptions.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);
		ssrOptions.HalfRes = ssrOptions.ResolutionScale > 1u;
		ssrOptions.TemporalAccumulation = m_Options.EnableSSRTemporalAccumulation ? 1u : 0u;
		ssrOptions.TemporalBlend = m_Options.SSRTemporalBlend;
		if (m_Options.EnableSSRTemporalAccumulation)
			ssrOptions.MaxSteps = glm::max(8, ssrOptions.MaxSteps / 2);

		if (m_SSRPass->IsInputValid("u_GTAOTex") && m_GTAOFinalImage)
			m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);

		BeginProfiledGPU("SSR");
		Renderer::BeginComputePass(m_CommandBuffer, m_SSRPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_SSRPass, nullptr, m_SSRWorkGroups, Buffer(&ssrOptions, sizeof(ssrOptions)));
		Renderer::EndComputePass(m_CommandBuffer, m_SSRPass);
		m_SSRPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_SSRImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		m_SSRFinalImage = m_SSRImage;
		if (m_SSRCompositePass)
		{
			m_SSRCompositePass->SetInput("u_SSR", m_SSRFinalImage);
			m_SSRCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_SSRCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
		}
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SSRTemporalAccumulationCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "SSR-Temporal");
		if (!m_Options.EnableSSR || !m_Options.EnableSSRTemporalAccumulation)
			return;
		if (!m_SSRTemporalPass || !m_SSRImage || !m_SSRHistoryImages[0] || !m_SSRHistoryImages[1])
			return;

		const uint32_t readIndex = m_SSRHistoryIndex & 1u;
		const uint32_t writeIndex = readIndex ^ 1u;
		Ref<Image2D> historyInput = m_SSRHistoryImages[readIndex];
		Ref<Image2D> historyOutput = m_SSRHistoryImages[writeIndex];

		m_SSRTemporalPass->SetInput("u_CurrentSSR", m_SSRImage);
		m_SSRTemporalPass->SetInput("u_HistorySSR", historyInput);
		m_SSRTemporalPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
		m_SSRTemporalPass->SetInput("o_HistorySSR", historyOutput);

		TemporalAccumulationConstants constants;
		constants.PreviousViewProjection = m_PreviousViewProjection;
		constants.Blend = m_Options.SSRTemporalBlend;
		constants.HasHistory = m_TemporalHistoryValid ? 1u : 0u;
		constants.BentNormals = 0u;
		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
		constants.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);

		BeginProfiledGPU("SSR-Temporal");
		Renderer::BeginComputePass(m_CommandBuffer, m_SSRTemporalPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_SSRTemporalPass, nullptr, m_SSRTemporalWorkGroups, Buffer(&constants, sizeof(constants)));
		Renderer::EndComputePass(m_CommandBuffer, m_SSRTemporalPass);
		m_SSRTemporalPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, historyOutput, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);

		m_SSRHistoryIndex = writeIndex;
		m_SSRFinalImage = historyOutput;
		if (m_SSRCompositePass)
		{
			m_SSRCompositePass->SetInput("u_SSR", m_SSRFinalImage);
			m_SSRCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_SSRCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
		}
	}

	void SceneRenderer::SSRCompositePass()
	{
		ScopedCPUProfile cpuProfile(*this, "SSRComposite");
		if (!m_Options.EnableSSR || !m_SSRCompositePass || !m_SSRCompositeMaterial)
			return;

		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
		m_SSRCompositeMaterial->Set("u_Uniforms.ResolutionScale", GetEffectResolutionDivisor(m_Options.SSRResolutionScale));
		m_SSRCompositeMaterial->Set("u_Uniforms.BilateralUpscale", UsesSSRBilateralUpscale(m_Options.SSRQuality) ? 1u : 0u);
		m_SSRCompositeMaterial->Set("u_Uniforms.QuarterDebug", m_Options.SSRQuality == SceneRendererOptions::SSRQualityPreset::QuarterDebug ? 1u : 0u);
		m_SSRCompositeMaterial->Set("u_Uniforms.DepthSigma", 0.035f);
		m_SSRCompositeMaterial->Set("u_Uniforms.NormalSigma", 32.0f);

		BeginProfiledGPU("SSRComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_SSRCompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_SSRCompositePass->GetPipeline(), m_SSRCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::DOFPass()
	{
		ScopedCPUProfile cpuProfile(*this, "DOF");
		const RenderVolumePostProcessSettings postProcessSettings = GetResolvedPostProcessSettings();
		if (!postProcessSettings.DOFEnabled || !m_DOFPass || !m_DOFMaterial)
			return;

		const float focusDistance = glm::max(0.001f, postProcessSettings.DOFFocusDistance);
		m_DOFMaterial->Set("u_Uniforms.DOFParams", glm::vec2(focusDistance, postProcessSettings.DOFBlurSize));

		BeginProfiledGPU("DOF");
		Renderer::BeginRenderPass(m_CommandBuffer, m_DOFPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_DOFPass->GetPipeline(), m_DOFMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		if (CanCompositeDOFIntoFinalTarget())
			Renderer::CopyImage(m_CommandBuffer, m_DOFPass->GetOutput(0), m_CompositePass->GetOutput(0));

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	bool SceneRenderer::CanCompositeDOFIntoFinalTarget()
	{
		if (!GetResolvedPostProcessSettings().DOFEnabled || !m_DOFPass || !m_CompositePass)
			return false;

		Ref<Image2D> dofImage = m_DOFPass->GetOutput(0);
		Ref<Image2D> compositeImage = m_CompositePass->GetOutput(0);
		if (!dofImage || !compositeImage)
			return false;

		return dofImage->GetSize() == compositeImage->GetSize();
	}

	void SceneRenderer::JumpFloodPass()
	{
		ScopedCPUProfile cpuProfile(*this, "JumpFlood");
		if (!m_Options.EnableJumpFlood || !m_JumpFloodInitPass || !m_JumpFloodInitMaterial || !m_JumpFloodPasses[0] || !m_JumpFloodPasses[1])
			return;

		BeginProfiledGPU("JumpFlood");
		Renderer::BeginRenderPass(m_CommandBuffer, m_JumpFloodInitPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_JumpFloodInitPass->GetPipeline(), m_JumpFloodInitMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		int step = 4;
		uint32_t passIndex = 0;
		Ref<Image2D> input = m_JumpFloodInitPass->GetOutput(0);
		Ref<Image2D> output = input;

		Ref<Framebuffer> passFramebuffer = m_JumpFloodPasses[0]->GetTargetFramebuffer();
		const glm::vec2 texelSize = {
			passFramebuffer && passFramebuffer->GetWidth() > 0 ? 1.0f / (float)passFramebuffer->GetWidth() : 1.0f,
			passFramebuffer && passFramebuffer->GetHeight() > 0 ? 1.0f / (float)passFramebuffer->GetHeight() : 1.0f
		};

		Buffer vertexOverrides;
		vertexOverrides.Allocate(sizeof(glm::vec2) + sizeof(int));
		vertexOverrides.Write(glm::value_ptr(texelSize), sizeof(glm::vec2));

		while (step > 0)
		{
			Ref<RenderPass> jumpFloodPass = m_JumpFloodPasses[passIndex];
			if (!jumpFloodPass || !m_JumpFloodPassMaterials[passIndex])
				break;

			jumpFloodPass->SetInput("u_Texture", input);
			vertexOverrides.Write(&step, sizeof(int), sizeof(glm::vec2));

			Renderer::BeginRenderPass(m_CommandBuffer, jumpFloodPass);
			Renderer::SubmitFullscreenQuadWithOverrides(m_CommandBuffer, jumpFloodPass->GetPipeline(), m_JumpFloodPassMaterials[passIndex], vertexOverrides, Buffer());
			Renderer::EndRenderPass(m_CommandBuffer);

			output = jumpFloodPass->GetOutput(0);
			input = output;
			passIndex = (passIndex + 1u) % 2u;
			step /= 2;
		}

		vertexOverrides.Release();

		if (m_JumpFloodCompositePass && output)
			m_JumpFloodCompositePass->SetInput("u_Texture", output);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::JumpFloodCompositePass()
	{
		ScopedCPUProfile cpuProfile(*this, "JumpFloodComposite");
		if (!m_Options.EnableJumpFlood || !m_JumpFloodCompositePass || !m_JumpFloodCompositeMaterial)
			return;

		BeginProfiledGPU("JumpFloodComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_JumpFloodCompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_JumpFloodCompositePass->GetPipeline(), m_JumpFloodCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::BloomCompute()
	{
		ScopedCPUProfile cpuProfile(*this, "BloomCompute");
		const RenderVolumePostProcessSettings postProcessSettings = GetResolvedPostProcessSettings();
		if (!postProcessSettings.BloomEnabled || !m_BloomComputePass || !m_BloomComputePipeline || !m_BloomComputeMaterials.PrefilterMaterial)
			return;

		const uint32_t mipCount = m_BloomComputeTextures[0].Texture->GetMipLevelCount();
		if (mipCount < 4)
			return;

		const uint32_t mips = mipCount - 2;
		if (mips < 3 || !m_BloomComputeMaterials.FirstUpsampleMaterial)
			return;

		struct BloomComputePushConstants
		{
			glm::vec4 Params;
			glm::vec4 TexSize;
			float LOD = 0.0f;
			int Mode = 0;
		} pushConstants;

		const float knee = glm::max(postProcessSettings.BloomKnee, 0.0001f);
		pushConstants.Params = {
			postProcessSettings.BloomThreshold,
			postProcessSettings.BloomThreshold - knee,
			knee * 2.0f,
			0.25f / knee
		};

		auto setTexSize = [&](uint32_t mip)
		{
			auto [mipWidth, mipHeight] = m_BloomComputeTextures[0].Texture->GetMipSize(mip);
			pushConstants.TexSize = {
				(float)mipWidth,
				(float)mipHeight,
				mipWidth > 0 ? 1.0f / (float)mipWidth : 0.0f,
				mipHeight > 0 ? 1.0f / (float)mipHeight : 0.0f
			};
		};

		auto dispatchForMip = [&](Ref<Material> material, uint32_t mip)
		{
			if (!material)
				return;

			auto [mipWidth, mipHeight] = m_BloomComputeTextures[0].Texture->GetMipSize(mip);
			const glm::uvec3 workGroups = {
				AlignUp(glm::max(1u, mipWidth), m_BloomComputeWorkgroupSize) / m_BloomComputeWorkgroupSize,
				AlignUp(glm::max(1u, mipHeight), m_BloomComputeWorkgroupSize) / m_BloomComputeWorkgroupSize,
				1
			};
			Renderer::DispatchCompute(m_CommandBuffer, m_BloomComputePass, material, workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
		};

		auto transitionBloomMip = [commandBuffer = m_CommandBuffer, this](uint32_t textureIndex, uint32_t mip, nvrhi::ResourceStates state, const char* label)
		{
			if (textureIndex >= m_BloomComputeTextures.size() || !m_BloomComputeTextures[textureIndex].Texture)
				return;

			Ref<Image2D> image = m_BloomComputeTextures[textureIndex].Texture->GetImage();
			std::string markerName = std::format("Barrier Bloom {} mip {} {}", textureIndex, mip, label);
			Renderer::Submit([commandBuffer, image, mip, state, markerName]() mutable
			{
				nvrhi::CommandListHandle commandList = commandBuffer->GetActive();
				commandBuffer->RT_BeginMarker(markerName);
				commandList->setTextureState(image->GetHandle(), nvrhi::TextureSubresourceSet(mip, 1, 0, 1), state);
				commandList->commitBarriers();
				commandBuffer->RT_EndMarker();
			});
		};

		BeginProfiledGPU("BloomCompute");
		Renderer::BeginComputePass(m_CommandBuffer, m_BloomComputePass);

		// Prefilter
		pushConstants.Mode = 0;
		pushConstants.LOD = 0.0f;
		setTexSize(0);
		transitionBloomMip(0, 0, nvrhi::ResourceStates::UnorderedAccess, "write");
		dispatchForMip(m_BloomComputeMaterials.PrefilterMaterial, 0);
		transitionBloomMip(0, 0, nvrhi::ResourceStates::ShaderResource, "read");

		// Downsample, ping-ponging between texture 0 and texture 1.
		pushConstants.Mode = 1;
		for (uint32_t i = 1; i < mips; i++)
		{
			setTexSize(i);
			pushConstants.LOD = (float)i - 1.0f;
			transitionBloomMip(1, i, nvrhi::ResourceStates::UnorderedAccess, "write");
			dispatchForMip(m_BloomComputeMaterials.DownsampleAMaterials[i], i);
			transitionBloomMip(1, i, nvrhi::ResourceStates::ShaderResource, "read");

			pushConstants.LOD = (float)i;
			transitionBloomMip(0, i, nvrhi::ResourceStates::UnorderedAccess, "write");
			dispatchForMip(m_BloomComputeMaterials.DownsampleBMaterials[i], i);
			transitionBloomMip(0, i, nvrhi::ResourceStates::ShaderResource, "read");
		}

		// First upsample from the smallest downsampled mip.
		pushConstants.Mode = 2;
		pushConstants.LOD = (float)mips - 2.0f;
		setTexSize(mips - 1);
		transitionBloomMip(2, mips - 2, nvrhi::ResourceStates::UnorderedAccess, "write");
		dispatchForMip(m_BloomComputeMaterials.FirstUpsampleMaterial, mips - 2);
		transitionBloomMip(2, mips - 2, nvrhi::ResourceStates::ShaderResource, "read");

		// Upsample back to mip 0.
		pushConstants.Mode = 3;
		for (int32_t mip = (int32_t)mips - 3; mip >= 0; mip--)
		{
			pushConstants.LOD = (float)mip;
			setTexSize((uint32_t)mip + 1u);
			transitionBloomMip(2, (uint32_t)mip, nvrhi::ResourceStates::UnorderedAccess, "write");
			dispatchForMip(m_BloomComputeMaterials.UpsampleMaterials[mip], (uint32_t)mip);
			transitionBloomMip(2, (uint32_t)mip, nvrhi::ResourceStates::ShaderResource, "read");
		}

		Renderer::EndComputePass(m_CommandBuffer, m_BloomComputePass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::AutoExposurePass()
	{
		ScopedCPUProfile cpuProfile(*this, "AutoExposurePass");
		if (!m_LuminanceHistogramPass || !m_LuminanceAveragePass)
			return;

		Ref<Image2D> sceneColor = GetSceneColorOutput();
		if (!sceneColor)
			return;

		const RenderVolumePostProcessSettings settings = GetResolvedPostProcessSettings();
		if (settings.ExposureControl != ExposureMode::Automatic)
			return;

		const uint32_t width = glm::max(1u, sceneColor->GetWidth());
		const uint32_t height = glm::max(1u, sceneColor->GetHeight());
		const float minLog = s_AutoExposureMinLogLuminance;
		const float maxLog = s_AutoExposureMaxLogLuminance;
		const float logRange = maxLog - minLog;

		BeginProfiledGPU("AutoExposurePass");

		// 1) Build the log-luminance histogram of the HDR scene color.
		struct HistogramPushConstants
		{
			float MinLogLuminance;
			float InverseLogLuminanceRange;
			uint32_t InputWidth;
			uint32_t InputHeight;
		} histogramPush;
		histogramPush.MinLogLuminance = minLog;
		histogramPush.InverseLogLuminanceRange = logRange > 0.0f ? 1.0f / logRange : 0.0f;
		histogramPush.InputWidth = width;
		histogramPush.InputHeight = height;

		const glm::uvec3 histogramGroups = { AlignUp(width, 16u) / 16u, AlignUp(height, 16u) / 16u, 1u };
		Renderer::BeginComputePass(m_CommandBuffer, m_LuminanceHistogramPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_LuminanceHistogramPass, nullptr, histogramGroups, Buffer(&histogramPush, sizeof(histogramPush)));
		m_LuminanceHistogramPass->GetPipeline()->BufferMemoryBarrier(m_CommandBuffer, m_SBSLuminanceHistogram->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndComputePass(m_CommandBuffer, m_LuminanceHistogramPass);

		// 2) Reduce to an average luminance, temporally adapt, write the exposure
		//    multiplier, and clear the histogram for the next frame.
		struct AveragePushConstants
		{
			float MinLogLuminance;
			float LogLuminanceRange;
			float TimeDelta;
			float SpeedUp;
			float SpeedDown;
			float MinEV100;
			float MaxEV100;
			uint32_t PixelCount;
		} averagePush;
		averagePush.MinLogLuminance = minLog;
		averagePush.LogLuminanceRange = logRange;
		averagePush.TimeDelta = Application::Get().GetFrametime().GetSeconds();
		averagePush.SpeedUp = glm::max(settings.AutoAdaptationSpeedUp, 0.0f);
		averagePush.SpeedDown = glm::max(settings.AutoAdaptationSpeedDown, 0.0f);
		averagePush.MinEV100 = settings.AutoMinEV100;
		averagePush.MaxEV100 = glm::max(settings.AutoMaxEV100, settings.AutoMinEV100);
		averagePush.PixelCount = width * height;

		Renderer::BeginComputePass(m_CommandBuffer, m_LuminanceAveragePass);
		Renderer::DispatchCompute(m_CommandBuffer, m_LuminanceAveragePass, nullptr, { 1u, 1u, 1u }, Buffer(&averagePush, sizeof(averagePush)));
		m_LuminanceAveragePass->GetPipeline()->BufferMemoryBarrier(m_CommandBuffer, m_SBSExposureState->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		m_LuminanceAveragePass->GetPipeline()->BufferMemoryBarrier(m_CommandBuffer, m_SBSLuminanceHistogram->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndComputePass(m_CommandBuffer, m_LuminanceAveragePass);

		m_AutoExposureValid = true;
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::TAAResolvePass()
	{
		ScopedCPUProfile cpuProfile(*this, "TAA");
		if (!m_Options.EnableTAA || !m_TAAResolvePass || !m_TAAHistoryImages[0] || !m_TAAHistoryImages[1])
			return;

		Ref<Image2D> sceneColor = GetSceneColorOutput();
		if (!sceneColor)
			return;

		const uint32_t readIndex = m_TAAHistoryIndex & 1u;
		const uint32_t writeIndex = readIndex ^ 1u;
		Ref<Image2D> historyInput = m_TAAHistoryImages[readIndex];
		Ref<Image2D> historyOutput = m_TAAHistoryImages[writeIndex];

		m_TAAResolvePass->SetInput("u_SceneColor", sceneColor);
		m_TAAResolvePass->SetInput("u_History", historyInput);
		m_TAAResolvePass->SetInput("u_Velocity", GetGeometryVelocityOutput());
		m_TAAResolvePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
		m_TAAResolvePass->SetInput("o_Resolved", historyOutput);

		struct TAAPushConstants
		{
			float Blend;
			uint32_t HasHistory;
			float Sharpness;
			uint32_t Padding1;
		} push;
		push.Blend = glm::clamp(m_Options.TAAHistoryBlend, 0.0f, 0.98f);
		push.HasHistory = m_TemporalHistoryValid ? 1u : 0u;
		push.Sharpness = glm::max(m_Options.TAASharpness, 0.0f);
		push.Padding1 = 0u;

		const glm::uvec3 groups = {
			(glm::max(1u, sceneColor->GetWidth()) + 7u) / 8u,
			(glm::max(1u, sceneColor->GetHeight()) + 7u) / 8u,
			1u
		};

		BeginProfiledGPU("TAA");
		Renderer::BeginComputePass(m_CommandBuffer, m_TAAResolvePass);
		Renderer::DispatchCompute(m_CommandBuffer, m_TAAResolvePass, nullptr, groups, Buffer(&push, sizeof(push)));
		Renderer::EndComputePass(m_CommandBuffer, m_TAAResolvePass);
		m_TAAResolvePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, historyOutput, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

		// Copy the resolved frame back into scene color so downstream passes
		// (bloom / auto-exposure / composite) consume the anti-aliased result.
		Renderer::CopyImage(m_CommandBuffer, historyOutput, sceneColor);

		m_TAAHistoryIndex = writeIndex;
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	float SceneRenderer::ComputeFinalExposure(const RenderVolumePostProcessSettings& settings) const
	{
		switch (settings.ExposureControl)
		{
			case ExposureMode::ManualEV:
				return Exposure::ExposureFromEV100(settings.ExposureEV100 - settings.ExposureCompensation);
			case ExposureMode::Camera:
				return Exposure::ExposureFromCamera(settings.Aperture, settings.ShutterSpeed, settings.ISO, settings.ExposureCompensation);
			case ExposureMode::Automatic:
				// Driven by the histogram auto-exposure passes; falls back to the manual
				// multiplier until the first auto-exposure result is available.
				return m_AutoExposureValid ? m_AutoExposure : settings.Exposure;
			case ExposureMode::Manual:
			default:
				return settings.Exposure;
		}
	}

	void SceneRenderer::CompositePass()
	{
		ScopedCPUProfile cpuProfile(*this, "CompositePass");
		BeginProfiledGPU("CompositePass");

		const RenderVolumePostProcessSettings postProcessSettings = GetResolvedPostProcessSettings();
		const bool useAutoExposure = postProcessSettings.ExposureControl == ExposureMode::Automatic && m_AutoExposureValid;
		m_CompositeMaterial->Set("u_Uniforms.Exposure", ComputeFinalExposure(postProcessSettings));
		m_CompositeMaterial->Set("u_Uniforms.UseAutoExposure", useAutoExposure ? 1 : 0);
		m_CompositeMaterial->Set("u_Uniforms.BloomIntensity", postProcessSettings.BloomEnabled ? postProcessSettings.BloomIntensity : 0.0f);
		m_CompositeMaterial->Set("u_Uniforms.BloomDirtIntensity", postProcessSettings.BloomEnabled ? postProcessSettings.BloomDirtIntensity : 0.0f);
		m_CompositeMaterial->Set("u_Uniforms.Opacity", m_Opacity);
		m_CompositeMaterial->Set("u_Uniforms.Time", Application::Get().GetTime());

		// White balance is a tint multiplier, folded into the colour filter.
		auto whiteBalanceToRGB = [](float temperature, float tint) -> glm::vec3
		{
			const float t = glm::clamp(temperature, -1.0f, 1.0f);
			const float g = glm::clamp(tint, -1.0f, 1.0f);
			return glm::max(glm::vec3(1.0f + 0.30f * t, 1.0f + 0.15f * g, 1.0f - 0.30f * t), glm::vec3(0.0f));
		};
		const glm::vec3 whiteBalance = whiteBalanceToRGB(postProcessSettings.WhiteTemperature, postProcessSettings.WhiteTint);
		const glm::vec3 colorFilter = glm::max(postProcessSettings.ColorFilter, glm::vec3(0.0f)) * whiteBalance;
		m_CompositeMaterial->Set("u_Uniforms.ColorFilterSaturation", glm::vec4(colorFilter, glm::max(postProcessSettings.Saturation, 0.0f)));
		m_CompositeMaterial->Set("u_Uniforms.ContrastGamma", glm::vec2(glm::max(postProcessSettings.Contrast, 0.0f), glm::max(postProcessSettings.Gamma, 0.01f)));
		m_CompositeMaterial->Set("u_Uniforms.TonemapOperator", static_cast<int>(postProcessSettings.Tonemap));
		m_CompositeMaterial->Set("u_Uniforms.Lift", glm::vec4(postProcessSettings.Lift, 0.0f));
		m_CompositeMaterial->Set("u_Uniforms.GradeGamma", glm::vec4(glm::max(postProcessSettings.GradeGamma, glm::vec3(1e-3f)), 0.0f));
		m_CompositeMaterial->Set("u_Uniforms.Gain", glm::vec4(postProcessSettings.Gain, 0.0f));

		Renderer::BeginRenderPass(m_CommandBuffer, m_CompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_CompositePass->GetPipeline(), m_CompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GridPass()
	{
		ScopedCPUProfile cpuProfile(*this, "GridPass");
		BeginProfiledGPU("GridPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_GridRenderPass);
		Renderer::RenderQuad(m_CommandBuffer, m_GridRenderPass->GetPipeline(), m_GridMaterial, glm::mat4(1.0f));
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::BuildIndirectDrawCommand(const StaticDrawCommand& dc,
		const TransformMapData& tmd,
		std::vector<nvrhi::DrawIndexedIndirectArguments>& drawCommands)
	{
		const auto& submesh = dc.MeshSource->GetSubmeshes()[dc.SubmeshIndex];

		nvrhi::DrawIndexedIndirectArguments args{};
		args.indexCount = submesh.IndexCount;
		args.instanceCount = tmd.VisibleInstanceCount;
		args.startIndexLocation = submesh.BaseIndex;
		args.baseVertexLocation = (int32_t)submesh.BaseVertex;
		args.startInstanceLocation = 0;
		drawCommands.push_back(args);
	}

	void SceneRenderer::UpdateStatistics()
	{
		m_Statistics.DrawCalls = 0;
		m_Statistics.Meshes = 0;
		m_Statistics.SubmittedInstances = m_FrameCullingStats.SubmittedInstances;
		m_Statistics.Instances = 0;
		m_Statistics.VisibleInstances = 0;
		m_Statistics.GPUVisibleInstances = 0;
		m_Statistics.CulledInstances = 0;
		m_Statistics.FrustumCulledInstances = m_FrameCullingStats.MainViewCulledInstances;
		m_Statistics.MainViewCulledInstances = m_Statistics.FrustumCulledInstances;
		m_Statistics.ShadowCulledInstances = m_FrameCullingStats.ShadowCulledInstances;
		m_Statistics.OcclusionCulledInstances = 0;
		m_Statistics.FullyCulledInstances = m_FrameCullingStats.FullyCulledInstances;
		m_Statistics.IndirectDraws = 0;
		m_Statistics.SavedDraws = 0;
		m_Statistics.SpotlightShadowcasters = 0;
		m_Statistics.SpotlightShadowsCulled = m_FrameCullingStats.ShadowCulledInstances;

		auto accumulate = [this](const DrawCommandList& drawList, const DrawCommandOrder& drawOrder)
			{
				for (const MeshKey& key : drawOrder)
				{
					const auto drawIt = drawList.find(key);
					if (drawIt == drawList.end())
						continue;

					const StaticDrawCommand& dc = drawIt->second;
					m_Statistics.DrawCalls++;
					m_Statistics.Meshes++;
					m_Statistics.Instances += dc.InstanceCount;

					auto transformIt = m_MeshTransformMap.find(key);
					if (transformIt != m_MeshTransformMap.end())
					{
						m_Statistics.VisibleInstances += transformIt->second.VisibleInstanceCount;
						if (m_Options.EnableGPUDrivenRendering && transformIt->second.IndirectDrawOffsetBytes != std::numeric_limits<uint32_t>::max())
							m_Statistics.IndirectDraws++;
					}
					else
					{
						m_Statistics.VisibleInstances += dc.InstanceCount;
					}
				}
			};

		const MeshPassState& selectedPass = GetMeshPass(MeshPassType::SelectedMask);
		const MeshPassState& opaquePass = GetMeshPass(MeshPassType::Opaque);
		const MeshPassState& transparentPass = GetMeshPass(MeshPassType::Transparent);
		const MeshPassState& colliderPass = GetMeshPass(MeshPassType::PhysicsCollider);

		accumulate(selectedPass.DrawList, selectedPass.DrawOrder);
		accumulate(opaquePass.DrawList, opaquePass.DrawOrder);
		accumulate(transparentPass.DrawList, transparentPass.DrawOrder);

		if (m_Options.ShowPhysicsColliders)
			accumulate(colliderPass.DrawList, colliderPass.DrawOrder);

		m_Statistics.SavedDraws = m_Statistics.Instances > m_Statistics.DrawCalls
			? m_Statistics.Instances - m_Statistics.DrawCalls
			: 0;
		const uint32_t lateCulledInstances = m_Statistics.Instances > m_Statistics.VisibleInstances
			? m_Statistics.Instances - m_Statistics.VisibleInstances
			: 0;
		m_Statistics.GPUVisibleInstances = m_Statistics.VisibleInstances;
		// Real HZB occlusion rejection counts require reading the post-compute counters back from the GPU.
		m_Statistics.CulledInstances = lateCulledInstances + m_Statistics.FrustumCulledInstances + m_Statistics.OcclusionCulledInstances;

		m_Statistics.SpotlightShadowcasters = m_SpotShadowCount;

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		m_Statistics.TotalGPUTime = m_CommandBuffer->GetExecutionGPUTime(frameIndex);
		m_Statistics.PipelineStats = m_CommandBuffer->GetPipelineStatistics(frameIndex);
		UpdateGPUProfileTimes();
		UpdateMemoryStatistics();
		UpdateDynamicRenderResolution();
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Render-thread draw helper
	// Must be called inside a Renderer::Submit() lambda so it executes
	// on the render thread while the command buffer is recording.
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::RT_DrawStaticMesh(
		Ref<RenderCommandBuffer>  cmd,
		const StaticDrawCommand& dc,
		MeshDrawParams            params,
		bool                      bindMaterial,
		uint32_t                  lightIndex,
		bool                      useVisibleObjectIndexes,
		bool                      useIndirect,
		Ref<Shader>               pipelineShader)
	{
		// Non-const copy of the MeshSource Ref: MeshSource::GetVertexBuffer /
		// GetIndexBuffer / GetMaterials are not marked const, so we cannot call
		// them through a const Ref (which is what we get from a const StaticDrawCommand&).
		Ref<MeshSource> meshSource = dc.MeshSource;
		if (!meshSource || dc.SubmeshIndex >= meshSource->GetSubmeshes().size())
			return;

		// The GPU vertex/index buffers may not be uploaded yet (assets stream in
		// asynchronously). Skip the draw until both are ready rather than binding a
		// null buffer — doing so trips Vulkan validation (vkCmdBindVertexBuffers:
		// pBuffers[0] is VK_NULL_HANDLE) and then crashes on the null dereference.
		Ref<VertexBuffer> vertexBuffer = meshSource->GetVertexBuffer();
		Ref<IndexBuffer>  indexBuffer = meshSource->GetIndexBuffer();
		if (!vertexBuffer || !indexBuffer || !vertexBuffer->GetHandle() || !indexBuffer->GetHandle())
			return;

		const auto& submesh = meshSource->GetSubmeshes()[dc.SubmeshIndex];
		nvrhi::GraphicsState& gs = cmd->GetGraphicsState();

		// ── Vertex buffer ─────────────────────────────────────────────────────
		nvrhi::VertexBufferBinding vbb;
		vbb.buffer = vertexBuffer->GetHandle();
		vbb.slot = 0;
		vbb.offset = 0;
		gs.vertexBuffers = { vbb };

		// ── Index buffer ──────────────────────────────────────────────────────
		nvrhi::IndexBufferBinding ibb;
		ibb.buffer = indexBuffer->GetHandle();
		ibb.format = nvrhi::Format::R32_UINT;
		ibb.offset = 0;
		gs.indexBuffer = ibb;
		gs.indirectParams = nullptr;

		// ── Material (descriptor set 0) ───────────────────────────────────────
		Ref<Material> material;
		if (bindMaterial)
		{
			material = dc.OverrideMaterial;

			if (!material)
			{
				AssetHandle matHandle = dc.MaterialHandle
					? dc.MaterialHandle
					: ResolveStaticMeshMaterialHandle(dc.MaterialTable, dc.StaticMesh, meshSource, submesh.MaterialIndex);

				if (matHandle)
					if (auto matAsset = AssetManager::GetAsset<MaterialAsset>(matHandle))
					{
						matAsset->UpdateMaterialComplexityMetadata();
						material = matAsset->GetMaterial();
					}
			}

			if (!material)
				material = Renderer::GetDefaultWhiteMaterial();

			if (material)
			{
				Renderer::RT_BindMaterialDescriptorSet(gs.bindings, pipelineShader, material);
			}
		}

		cmd->RT_CommitGraphicsState();

		// ── Push constants ────────────────────────────────────────────────────
		Buffer materialUniforms = material ? material->GetUniformStorageBuffer() : Buffer();
		const uint64_t pushConstantSize = std::max<uint64_t>(sizeof(MeshDrawPushConstants), materialUniforms.Size);
		// Reuse the render-thread scratch instead of heap-allocating per draw.
		// assign() zero-fills while retaining capacity; the zero-fill matters for
		// the tail bytes when materialUniforms.Size and sizeof(MeshDrawPushConstants)
		// differ.
		std::vector<uint8_t>& pushConstants = m_RTPushConstantScratch;
		pushConstants.assign(pushConstantSize, 0);

		if (materialUniforms)
			std::memcpy(pushConstants.data(), materialUniforms.Data, materialUniforms.Size);

		auto& pc = *reinterpret_cast<MeshDrawPushConstants*>(pushConstants.data());
		pc.ObjectIndexBase = useVisibleObjectIndexes ? params.VisibleObjectIndexBase : params.ObjectIndexBase;
		pc.LightIndex = lightIndex;
		pc.BoneTransformBase = 0;
		pc.BoneTransformStride = 0;
		cmd->GetActive()->setPushConstants(pushConstants.data(), pushConstants.size());

		if (useIndirect && params.IndirectDrawOffsetBytes != std::numeric_limits<uint32_t>::max())
		{
			gs.indirectParams = m_SBSIndirectDrawCommands->RT_Get()->GetHandle();
			cmd->RT_CommitGraphicsState();
			cmd->GetActive()->drawIndexedIndirect(params.IndirectDrawOffsetBytes, 1);
			return;
		}

		const uint32_t instanceCount = useVisibleObjectIndexes ? params.VisibleInstanceCount : dc.InstanceCount;
		if (instanceCount == 0)
			return;

		nvrhi::DrawArguments drawArgs{};
		drawArgs.vertexCount = submesh.IndexCount;
		drawArgs.startIndexLocation = submesh.BaseIndex;
		drawArgs.startVertexLocation = submesh.BaseVertex;
		drawArgs.instanceCount = instanceCount;
		cmd->GetActive()->drawIndexed(drawArgs);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Output accessors
	// ─────────────────────────────────────────────────────────────────────────

	bool SceneRenderer::UsesDeferredPath() const
	{
		// Deferred is now the only path (forward renderer removed).
		return true;
	}

	Ref<Image2D> SceneRenderer::GetSceneColorOutput() const
	{
		return m_SceneColorFramebuffer ? m_SceneColorFramebuffer->GetImage(0) : nullptr;
	}

	Ref<Image2D> SceneRenderer::GetGeometryBaseColorOutput() const
	{
		return m_GeometryPassFramebuffer ? m_GeometryPassFramebuffer->GetImage(0) : nullptr;
	}

	Ref<Image2D> SceneRenderer::GetGeometryNormalOutput() const
	{
		return m_GeometryPassFramebuffer ? m_GeometryPassFramebuffer->GetImage(1) : nullptr;
	}

	Ref<Image2D> SceneRenderer::GetGeometryMetalRoughOutput() const
	{
		return m_GeometryPassFramebuffer ? m_GeometryPassFramebuffer->GetImage(2) : nullptr;
	}

	Ref<Image2D> SceneRenderer::GetGeometryMaterialIDOutput() const
	{
		return m_GeometryPassFramebuffer ? m_GeometryPassFramebuffer->GetImage(3) : nullptr;
	}

	Ref<Image2D> SceneRenderer::GetGeometryObjectIDOutput() const
	{
		return m_GeometryPassFramebuffer ? m_GeometryPassFramebuffer->GetImage(4) : nullptr;
	}

	Ref<Image2D> SceneRenderer::GetGeometryVelocityOutput() const
	{
		return m_GeometryPassFramebuffer ? m_GeometryPassFramebuffer->GetImage(5) : nullptr;
	}

	Ref<Image2D> SceneRenderer::GetFinalPassImage()
	{
		if (Ref<Image2D> debugImage = GetDebugViewImage(m_DebugViewMode))
			return debugImage;

		return GetDebugViewImage(DebugViewMode::Final);
	}

	Ref<Image2D> SceneRenderer::GetDebugViewImage(DebugViewMode mode)
	{
		switch (mode)
		{
			case DebugViewMode::Final:
				if (GetResolvedPostProcessSettings().DOFEnabled && m_DOFPass && !CanCompositeDOFIntoFinalTarget())
					return m_DOFPass->GetOutput(0);
				if (m_CompositePass)
					return m_CompositePass->GetOutput(0);
				return nullptr;

			case DebugViewMode::Geometry:
				return GetSceneColorOutput();

			case DebugViewMode::Depth:
				return m_PreDepthPass ? m_PreDepthPass->GetDepthOutput() : nullptr;

			case DebugViewMode::Normals:
				return GetGeometryNormalOutput();

			case DebugViewMode::SSR:
				if (!m_Options.EnableSSR)
					return nullptr;
				return m_SSRFinalImage ? m_SSRFinalImage : m_SSRImage;

			case DebugViewMode::AO:
				if (!m_Options.EnableGTAO)
					return nullptr;
				return m_AODebugPass ? m_AODebugPass->GetOutput(0) : nullptr;

			case DebugViewMode::Bloom:
				if (!GetResolvedPostProcessSettings().BloomEnabled)
					return nullptr;
				if (m_BloomComputeTextures.size() > 2 && m_BloomComputeTextures[2].Texture)
					return m_BloomComputeTextures[2].Texture->GetImage();
				return nullptr;

			case DebugViewMode::Composite:
				return m_CompositePass ? m_CompositePass->GetOutput(0) : nullptr;

			case DebugViewMode::LocalFogDensity:
				if (!ResolveFrameEnvironment().LocalFogEnabled)
					return nullptr;
				return m_AtmosphericFogPass ? m_AtmosphericFogPass->GetOutput(0) : nullptr;

			case DebugViewMode::GBufferBaseColor:
				return UsesDeferredPath() ? GetGeometryBaseColorOutput() : nullptr;

			case DebugViewMode::GBufferNormal:
				return UsesDeferredPath() ? GetGeometryNormalOutput() : nullptr;

			case DebugViewMode::GBufferMetalRough:
				return UsesDeferredPath() ? GetGeometryMetalRoughOutput() : nullptr;

			case DebugViewMode::GBufferMaterialID:
			case DebugViewMode::GBufferObjectID:
				return UsesDeferredPath() && m_GBufferDebugPass ? m_GBufferDebugPass->GetOutput(0) : nullptr;

			case DebugViewMode::DeferredLighting:
				return UsesDeferredPath() && m_GBufferDebugPass ? m_GBufferDebugPass->GetOutput(0) : nullptr;

			case DebugViewMode::GPUScenePrimitiveID:
			case DebugViewMode::GPUSceneMaterialIndex:
			case DebugViewMode::GPUSceneObjectID:
			case DebugViewMode::GPUSceneBounds:
			case DebugViewMode::GPUSceneMotion:
			case DebugViewMode::GPUMaterialTextureValidity:
			case DebugViewMode::GPUMaterialAlphaMode:
			case DebugViewMode::GPUMaterialRoughness:
			case DebugViewMode::GPUMaterialMetalness:
			case DebugViewMode::GPUMaterialMissing:
				if (UsesDeferredPath() && UsesGBufferDebugPass(mode))
					return m_GBufferDebugPass ? m_GBufferDebugPass->GetOutput(0) : nullptr;
				return GetSceneColorOutput();
		}

		return nullptr;
	}

	Ref<Pipeline> SceneRenderer::GetFinalPipeline()
	{
		Ref<RenderPass> finalPass = GetFinalRenderPass();
		return finalPass ? finalPass->GetPipeline() : nullptr;
	}

	Ref<RenderPass> SceneRenderer::GetFinalRenderPass()
	{
		if (m_DOFSettings.Enabled && m_DOFPass && !CanCompositeDOFIntoFinalTarget())
			return m_DOFPass;
		return m_CompositePass;
	}

	Ref<Framebuffer> SceneRenderer::GetExternalCompositeFramebuffer()
	{
		Ref<RenderPass> finalPass = GetFinalRenderPass();
		if (finalPass)
			return finalPass->GetTargetFramebuffer();

		return m_CompositingFramebuffer;
	}

	Ref<Framebuffer> SceneRenderer::GetDepthCompositeFramebuffer()
	{
		return m_CompositingFramebuffer;
	}

	void SceneRenderer::SetLineWidth(float width)
	{
		m_LineWidth = width;
	}

	void SceneRenderer::SetQualityPreset(QualityPreset preset)
	{
		ApplyQualityPreset(preset);
		RefreshRenderResolutionScale();
		RefreshScreenSpaceEffectResources();
		m_TemporalHistoryValid = false;
		m_CloudHistoryValid = false;
	}

} // namespace Lux
