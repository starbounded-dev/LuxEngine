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
#include "Lux/Platform/Vulkan/VulkanShader.h"

#include <nvrhi/utils.h>

// SMAA's precomputed lookup tables ship as headers in the reference implementation
// (github.com/iryoku/smaa, MIT) rather than as loadable assets. They are optional: without
// them SMAA reports itself unavailable and the renderer behaves exactly as it did before.
// Path is relative to this file so dropping the headers in needs no premake change.
#if __has_include("../../../vendor/smaa/AreaTex.h") && __has_include("../../../vendor/smaa/SearchTex.h")
	#include "../../../vendor/smaa/AreaTex.h"
	#include "../../../vendor/smaa/SearchTex.h"
	#define LUX_HAS_SMAA_TEXTURES 1
#endif

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
	// Must match the first four fields of the mesh draw push_constant blocks.
	// Material data for scene geometry lives in GPUScene/GPUMaterials.
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
			if (mode > static_cast<uint32_t>(SceneRendererOptions::RenderResolutionScaleMode::FixedResolution))
				return SceneRendererOptions::RenderResolutionScaleMode::Native;

			return static_cast<SceneRendererOptions::RenderResolutionScaleMode>(mode);
		}

		QualityPreset SanitizeQualityPreset(uint32_t preset)
		{
			// Custom is a legal stored value (a hand-edited category), so the valid range
			// runs to Custom, not Cinematic.
			if (preset > static_cast<uint32_t>(QualityPreset::Custom))
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

		SceneRendererOptions::ShadowFilterMode SanitizeShadowFilterMode(uint32_t mode)
		{
			switch (mode)
			{
				case static_cast<uint32_t>(SceneRendererOptions::ShadowFilterMode::TunedPCF):
				case static_cast<uint32_t>(SceneRendererOptions::ShadowFilterMode::PCSS):
				case static_cast<uint32_t>(SceneRendererOptions::ShadowFilterMode::Hybrid):
					return static_cast<SceneRendererOptions::ShadowFilterMode>(mode);
				default:
					return SceneRendererOptions::ShadowFilterMode::Hybrid;
			}
		}

		uint32_t SanitizeActiveShadowCascadeCount(uint32_t count)
		{
			return glm::clamp(count, 1u, SceneRenderer::ShadowCascadeCount);
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

		// Size is deduced, never written out: a std::array<T, N> initialised with fewer than
		// N elements is legal and silently value-initialises the tail to nullptr. Those nulls
		// reach ResetProfilingData, which stores them as PassProfile::Name, and the next
		// GetOrCreatePassProfile strcmps against a null pointer. Removing a pass from this
		// list without also editing the count is exactly how that happened once already.
		constexpr auto s_ProfiledSceneRendererPasses = std::to_array<const char*>({
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
			"AOComposite",
			"PreConvolution",
			"SSR",
			"SSRComposite",
			"JumpFlood",
			"BloomCompute",
			"CompositePass",
			"JumpFloodComposite",
			"GridPass",
			"Renderer2D",
			"DOF",
			"SMAA"
		});

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

	QualityPreset& RendererQualityCategories::Get(QualityCategory category)
	{
		switch (category)
		{
			case QualityCategory::Shadows:          return Shadows;
			case QualityCategory::AmbientOcclusion: return AmbientOcclusion;
			case QualityCategory::Reflections:      return Reflections;
			case QualityCategory::AntiAliasing:     return AntiAliasing;
			case QualityCategory::Textures:         return Textures;
			case QualityCategory::ResolutionScale:  return ResolutionScale;
			case QualityCategory::PostProcessing:   return PostProcessing;
			default: break;
		}

		LUX_CORE_VERIFY(false, "Unhandled QualityCategory");
		return Shadows;
	}

	QualityPreset RendererQualityCategories::Get(QualityCategory category) const
	{
		return const_cast<RendererQualityCategories*>(this)->Get(category);
	}

	void RendererQualityCategories::SetAll(QualityPreset level)
	{
		for (uint32_t i = 0; i < static_cast<uint32_t>(QualityCategory::COUNT); ++i)
			Get(static_cast<QualityCategory>(i)) = level;
	}

	QualityPreset RendererQualityCategories::Unified() const
	{
		const QualityPreset first = Get(static_cast<QualityCategory>(0));
		for (uint32_t i = 1; i < static_cast<uint32_t>(QualityCategory::COUNT); ++i)
		{
			if (Get(static_cast<QualityCategory>(i)) != first)
				return QualityPreset::Custom;
		}

		return first;
	}

	const char* QualityPresetToDisplayString(QualityPreset preset)
	{
		switch (preset)
		{
			case QualityPreset::Low:       return "Low";
			case QualityPreset::Medium:    return "Medium";
			case QualityPreset::High:      return "High";
			case QualityPreset::Ultra:     return "Ultra";
			case QualityPreset::Cinematic: return "Cinematic";
			case QualityPreset::Custom:    return "Custom";
		}

		return "Unknown";
	}

	const char* QualityCategoryToDisplayString(QualityCategory category)
	{
		switch (category)
		{
			case QualityCategory::Shadows:          return "Shadows";
			case QualityCategory::AmbientOcclusion: return "Ambient Occlusion";
			case QualityCategory::Reflections:      return "Reflections";
			case QualityCategory::AntiAliasing:     return "Anti-Aliasing";
			case QualityCategory::Textures:         return "Textures";
			case QualityCategory::ResolutionScale:  return "Resolution Scale";
			case QualityCategory::PostProcessing:   return "Post-Processing";
			default: break;
		}

		return "Unknown";
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Per-category quality
	//
	// Each applier writes every field it owns, for every tier, so a tier is fully
	// described by reading its own branch - no field silently carries over from
	// whichever tier happened to be applied before. Categories own disjoint fields,
	// which is what lets them be mixed freely.
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::ApplyShadowQuality(QualityPreset level)
	{
		if (level == QualityPreset::Custom)
			return;

		// Shared across tiers; only the fields listed per-tier below differ.
		m_Options.SoftShadows = true;
		m_Options.ShadowCascadeSplitLambda = 0.82f;
		m_Options.ShadowFilter = SceneRendererOptions::ShadowFilterMode::Hybrid;
		m_Options.ShadowPCFRadiusTexels = 1.25f;
		m_Options.SpotShadowPCFRadiusTexels = 1.5f;
		m_Options.ShadowFade = 25.0f;

		switch (level)
		{
			case QualityPreset::Low:
				m_Options.SoftShadows = false;
				m_Options.ActiveShadowCascadeCount = 2;
				m_Options.ShadowFilter = SceneRendererOptions::ShadowFilterMode::TunedPCF;
				m_Options.DirectionalPCSSCascadeCount = 0;
				m_Options.ShadowPCFRadiusTexels = 1.0f;
				m_Options.SpotShadowPCFRadiusTexels = 1.0f;
				m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_1K;
				m_Options.MaxShadowDistance = 100.0f;
				m_Options.ShadowFade = 15.0f;
				break;
			case QualityPreset::Medium:
				m_Options.ActiveShadowCascadeCount = 3;
				m_Options.DirectionalPCSSCascadeCount = 1;
				m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_2K;
				m_Options.MaxShadowDistance = 150.0f;
				break;
			case QualityPreset::High:
				m_Options.ActiveShadowCascadeCount = 3;
				m_Options.DirectionalPCSSCascadeCount = 1;
				// 2K + soft shadows as the realtime default; a 4-layer 4K array costs
				// ~268 MB and a lot of shadow-render bandwidth. Ultra raises to 4K.
				m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_2K;
				m_Options.MaxShadowDistance = 200.0f;
				break;
			case QualityPreset::Ultra:
				m_Options.ActiveShadowCascadeCount = 4;
				m_Options.DirectionalPCSSCascadeCount = 2;
				m_Options.ShadowPCFRadiusTexels = 1.5f;
				m_Options.SpotShadowPCFRadiusTexels = 1.75f;
				m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_4K;
				m_Options.MaxShadowDistance = 300.0f;
				break;
			case QualityPreset::Cinematic:
				m_Options.ActiveShadowCascadeCount = 4;
				m_Options.DirectionalPCSSCascadeCount = 2;
				m_Options.ShadowPCFRadiusTexels = 1.75f;
				m_Options.SpotShadowPCFRadiusTexels = 2.0f;
				// Deliberately 2K, not 8K: an 8K directional atlas is a large per-frame
				// shadow-pass cost for little visible gain at typical scene scale. Users
				// who want more can set the resolution by hand, which moves this category
				// to Custom and stops the tier overwriting it - which is exactly what the
				// old single-preset path got wrong.
				m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_2K;
				m_Options.MaxShadowDistance = 450.0f;
				m_Options.ShadowFade = 50.0f;
				break;
			default:
				break;
		}
	}

	void SceneRenderer::ApplyAmbientOcclusionQuality(QualityPreset level)
	{
		if (level == QualityPreset::Custom)
			return;

		m_Options.EnableGTAO = true;
		m_Options.GTAOBentNormals = false;
		// NEVER enabled by a tier. Temporal accumulation reprojects last frame's AO, which
		// smears ghosting behind anything that moves. Cost is paid with spatial denoise
		// passes and full-resolution AO instead - see the denoise counts below.
		m_Options.AOShadowTolerance = 1.0f;

		switch (level)
		{
			case QualityPreset::Low:
				m_Options.EnableGTAO = false;
				m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
				m_Options.GTAODenoisePasses = 0;
				break;
			case QualityPreset::Medium:
				m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
				m_Options.GTAODenoisePasses = 2;
				break;
			case QualityPreset::High:
				m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
				// 4 rather than 2: with temporal accumulation off, spatial denoising is the
				// only thing cleaning up GTAO's noise.
				m_Options.GTAODenoisePasses = 4;
				break;
			case QualityPreset::Ultra:
				m_Options.GTAOBentNormals = true;
				m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
				m_Options.GTAODenoisePasses = 6;
				break;
			case QualityPreset::Cinematic:
				m_Options.GTAOBentNormals = true;
				m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
				m_Options.GTAODenoisePasses = 8;
				break;
			default:
				break;
		}
	}

	void SceneRenderer::ApplyReflectionQuality(QualityPreset level)
	{
		if (level == QualityPreset::Custom)
			return;

		m_Options.EnableSSR = true;
		// NEVER enabled by a tier. Reprojected reflections ghost badly on moving geometry
		// and on the camera cut. Ray count carries the quality instead.
		m_SSROptions.Brightness = 0.7f;
		m_SSROptions.DepthTolerance = 0.8f;

		switch (level)
		{
			case QualityPreset::Low:
				m_Options.EnableSSR = false;
				m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::HalfBilateral;
				m_SSROptions.MaxSteps = 32;
				break;
			case QualityPreset::Medium:
				m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::HalfBilateral;
				m_SSROptions.MaxSteps = 48;
				break;
			case QualityPreset::High:
				m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::Full;
				m_SSROptions.MaxSteps = 70;
				break;
			case QualityPreset::Ultra:
				m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::Full;
				m_SSROptions.MaxSteps = 96;
				break;
			case QualityPreset::Cinematic:
				m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::Full;
				m_SSROptions.MaxSteps = 128;
				break;
			default:
				break;
		}
	}

	void SceneRenderer::ApplyAntiAliasingQuality(QualityPreset level)
	{
		if (level == QualityPreset::Custom)
			return;

		// NOTE: the previous single-preset path did not touch anti-aliasing at all, so
		// SMAA stayed at whatever the project last saved regardless of tier. Tiers now
		// drive it, which is a deliberate behaviour change.
		//
		// SMAA 1x only - never T2x, and never TAA. Both jitter the projection and resolve
		// against reprojected history, which is precisely the ghosting/smearing this engine
		// refuses to ship. SMAA 1x is purely spatial: it analyses edges within the current
		// frame and cannot smear.
		//
		// MSAASamples is intentionally NOT owned here: it is an expensive opt-in that
		// interacts with the deferred path, so it stays a manual choice.
		m_Options.SMAALocalContrastAdaptationFactor = 2.0f;

		switch (level)
		{
			case QualityPreset::Low:
				m_Options.EnableSMAA = false;
				m_Options.SMAAThreshold = 0.1f;
				break;
			case QualityPreset::Medium:
				m_Options.EnableSMAA = true;
				m_Options.SMAAThreshold = 0.1f;
				break;
			case QualityPreset::High:
			case QualityPreset::Ultra:
			case QualityPreset::Cinematic:
				m_Options.EnableSMAA = true;
				// Lower threshold catches more edges - the spatial way to buy quality,
				// since the temporal route is off the table.
				m_Options.SMAAThreshold = 0.05f;
				break;
			default:
				break;
		}
	}

	void SceneRenderer::ApplyTextureQuality(QualityPreset level)
	{
		if (level == QualityPreset::Custom)
			return;

		m_Options.EnableDistanceMipBias = true;

		switch (level)
		{
			case QualityPreset::Low:
				m_Options.TextureMipBias = 1.0f;
				m_Options.DistanceMipBiasStart = 25.0f;
				m_Options.DistanceMipBiasEnd = 120.0f;
				m_Options.DistanceMipBiasMax = 3.0f;
				break;
			case QualityPreset::Medium:
				m_Options.TextureMipBias = 0.25f;
				m_Options.DistanceMipBiasStart = 50.0f;
				m_Options.DistanceMipBiasEnd = 250.0f;
				m_Options.DistanceMipBiasMax = 2.0f;
				break;
			case QualityPreset::High:
				m_Options.TextureMipBias = -0.5f;
				m_Options.DistanceMipBiasStart = 50.0f;
				m_Options.DistanceMipBiasEnd = 250.0f;
				m_Options.DistanceMipBiasMax = 1.5f;
				break;
			case QualityPreset::Ultra:
				m_Options.TextureMipBias = -1.0f;
				m_Options.DistanceMipBiasStart = 25.0f;
				m_Options.DistanceMipBiasEnd = 150.0f;
				m_Options.DistanceMipBiasMax = 1.0f;
				break;
			case QualityPreset::Cinematic:
				m_Options.TextureMipBias = -1.5f;
				m_Options.DistanceMipBiasStart = 10.0f;
				m_Options.DistanceMipBiasEnd = 100.0f;
				m_Options.DistanceMipBiasMax = 0.5f;
				break;
			default:
				break;
		}
	}

	void SceneRenderer::ApplyResolutionScaleQuality(QualityPreset level)
	{
		if (level == QualityPreset::Custom)
			return;

		m_Options.DynamicResolutionMinScale = 0.5f;
		m_Options.DynamicResolutionMaxScale = 1.0f;
		m_Options.DynamicResolutionTargetGPUTime = 16.67f;

		switch (level)
		{
			case QualityPreset::Low:
				m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Scale50;
				break;
			case QualityPreset::Medium:
				m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Scale75;
				break;
			case QualityPreset::High:
			case QualityPreset::Ultra:
			case QualityPreset::Cinematic:
				m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Native;
				break;
			default:
				break;
		}
	}

	void SceneRenderer::ApplyPostProcessQuality(QualityPreset level)
	{
		if (level == QualityPreset::Custom)
			return;

		m_BloomSettings.Enabled = true;
		m_BloomSettings.Threshold = 1.0f;
		m_BloomSettings.Knee = 0.1f;
		m_BloomSettings.UpsampleScale = 1.0f;
		m_BloomSettings.Intensity = 1.0f;
		m_BloomSettings.DirtIntensity = 1.0f;
		m_DOFSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;

		switch (level)
		{
			case QualityPreset::Low:
				m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Quarter;
				break;
			case QualityPreset::Medium:
			case QualityPreset::High:
				m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
				break;
			case QualityPreset::Ultra:
			case QualityPreset::Cinematic:
				m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
				break;
			default:
				break;
		}
	}

	void SceneRenderer::FinalizeQualityChange()
	{
		// Derived scratch values the appliers feed. Recomputed once after any tier change
		// rather than inside each applier, since SSR spans two of them.
		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
		m_GTAODataCB.ResolutionScale = GetEffectResolutionDivisor(m_Options.GTAOResolutionScale);
		m_SSROptions.HalfRes = GetEffectResolutionDivisor(m_Options.SSRResolutionScale) > 1u;
		m_SSROptions.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);

		// The overall tier is a view onto the categories, never stored independently.
		m_Options.Quality = m_Options.QualityCategories.Unified();

		UpdateGTAOData();
	}

	void SceneRenderer::ApplyAllCategoryQuality()
	{
		ApplyShadowQuality(m_Options.QualityCategories.Shadows);
		ApplyAmbientOcclusionQuality(m_Options.QualityCategories.AmbientOcclusion);
		ApplyReflectionQuality(m_Options.QualityCategories.Reflections);
		ApplyAntiAliasingQuality(m_Options.QualityCategories.AntiAliasing);
		ApplyTextureQuality(m_Options.QualityCategories.Textures);
		ApplyResolutionScaleQuality(m_Options.QualityCategories.ResolutionScale);
		ApplyPostProcessQuality(m_Options.QualityCategories.PostProcessing);

		FinalizeQualityChange();
	}

	void SceneRenderer::SetCategoryQuality(QualityCategory category, QualityPreset level)
	{
		level = SanitizeQualityPreset(static_cast<uint32_t>(level));
		m_Options.QualityCategories.Get(category) = level;

		switch (category)
		{
			case QualityCategory::Shadows:          ApplyShadowQuality(level); break;
			case QualityCategory::AmbientOcclusion: ApplyAmbientOcclusionQuality(level); break;
			case QualityCategory::Reflections:      ApplyReflectionQuality(level); break;
			case QualityCategory::AntiAliasing:     ApplyAntiAliasingQuality(level); break;
			case QualityCategory::Textures:         ApplyTextureQuality(level); break;
			case QualityCategory::ResolutionScale:  ApplyResolutionScaleQuality(level); break;
			case QualityCategory::PostProcessing:   ApplyPostProcessQuality(level); break;
			default: break;
		}

		FinalizeQualityChange();
	}

	void SceneRenderer::MarkCategoryCustom(QualityCategory category)
	{
		m_Options.QualityCategories.Get(category) = QualityPreset::Custom;
		m_Options.Quality = m_Options.QualityCategories.Unified();
	}

	void SceneRenderer::ApplyQualityPreset(QualityPreset preset)
	{
		preset = SanitizeQualityPreset(static_cast<uint32_t>(preset));

		// "Everything Custom" is meaningless - Custom means "leave these values alone",
		// so there is nothing to apply. Keep the categories as they are.
		if (preset == QualityPreset::Custom)
			return;

		m_Options.QualityCategories.SetAll(preset);
		ApplyAllCategoryQuality();
	}

	void SceneRenderer::UpdateGTAOData()
	{
		const bool gtaoEnabled = m_Options.EnableGTAO;
		const int aoMethod = (int)ShaderDef::GetAOMethod(gtaoEnabled);
		const int bentNormals = m_Options.GTAOBentNormals ? 1 : 0;
		// __HZ_AO_METHOD (GTAO on/off) recompiles the AO-Composite/AO-Debug shaders
		// into a new variant. The reload can't restore inputs (Camera/samplers/GTAO
		// textures) that weren't declared in the prior variant, so flag a full rebind
		// + rebake. The rebake self-gates on the new variant being live, so flagging is
		// always safe. (Bent-normals no longer recompiles these passes — they decode
		// the visibility byte at runtime from u_AOSettings.BentNormals — but it's kept
		// in the check as a harmless belt-and-suspenders rebake.)
		if (aoMethod != m_AppliedAOMethod || bentNormals != m_AppliedGTAOBentNormals)
		{
			m_AppliedAOMethod = aoMethod;
			m_AppliedGTAOBentNormals = bentNormals;
			m_AOPassInputsDirty = true;
		}
		Renderer::SetGlobalMacroInShaders("__HZ_AO_METHOD", std::to_string(aoMethod));
		Renderer::SetGlobalMacroInShaders("__HZ_GTAO_COMPUTE_BENT_NORMALS", bentNormals ? "1" : "0");

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
		const bool previousGTAOBentNormals = m_Options.GTAOBentNormals;
		const auto previousBloomScale = m_BloomSettings.ResolutionScale;
		const auto previousDOFScale = m_DOFSettings.ResolutionScale;

		// Legacy path: the project file still stores every resolved value, so the tier is
		// only a label here and the raw assignments below are what actually take effect.
		// Seed the categories from it so Quality == QualityCategories.Unified() holds; once
		// the per-category tiers are serialized this is replaced by applying them.
		// TODO: read per-category tiers and only take raw values for Custom categories.
		m_Options.QualityCategories.SetAll(SanitizeQualityPreset(settings.QualityPreset));
		m_Options.Quality = m_Options.QualityCategories.Unified();
		m_Options.EnableFrustumCulling = settings.EnableFrustumCulling;
		m_Options.EnableOcclusionCulling = settings.EnableOcclusionCulling;
		m_Options.OcclusionDepthBias = std::clamp(settings.OcclusionDepthBias, 0.0f, 0.1f);
		m_Options.OcclusionBoundsScale = std::clamp(settings.OcclusionBoundsScale, 1.0f, 2.0f);
		m_Options.EnableGPUDrivenRendering = settings.EnableGPUDrivenRendering;
		m_Options.EnableMeshLODs = settings.EnableMeshLODs;
		m_Options.MeshLODDistanceScale = std::clamp(settings.MeshLODDistanceScale, 0.25f, 4.0f);
		m_Options.EnableVariableRateShading = settings.EnableVariableRateShading;
		m_Options.EnableMeshShaders = settings.EnableMeshShaders;
		m_Options.EnableGTAO = settings.EnableGTAO;
		m_Options.GTAOBentNormals = settings.GTAOBentNormals;
		m_Options.GTAODenoisePasses = settings.GTAODenoisePasses;
		m_Options.AOShadowTolerance = settings.AOShadowTolerance;
		m_Options.EnableSSR = settings.EnableSSR;
		m_Options.GTAOResolutionScale = SanitizeEffectResolutionScale(settings.GTAOResolutionScale);
		m_Options.SSRQuality = SanitizeSSRQualityPreset(settings.SSRQuality);
		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
		m_Options.EnableSMAA = settings.EnableSMAA;
		m_Options.SMAAThreshold = std::clamp(settings.SMAAThreshold, 0.01f, 0.5f);
		m_Options.SMAALocalContrastAdaptationFactor = std::clamp(settings.SMAALocalContrastAdaptationFactor, 1.0f, 8.0f);
		m_Options.EnableJumpFlood = settings.EnableJumpFlood;
		m_Options.ResolutionScaleMode = SanitizeRenderResolutionScaleMode(settings.RenderScaleMode);
		m_Options.FixedRenderWidth = std::clamp(settings.FixedRenderWidth, 64u, 16384u);
		m_Options.FixedRenderHeight = std::clamp(settings.FixedRenderHeight, 64u, 16384u);
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
		m_Options.ActiveShadowCascadeCount = SanitizeActiveShadowCascadeCount(settings.ActiveShadowCascadeCount);
		m_Options.ShadowCascadeSplitLambda = std::clamp(settings.ShadowCascadeSplitLambda, 0.0f, 1.0f);
		m_Options.ShadowCascadeNearPlaneOffset = std::max(0.0f, settings.ShadowCascadeNearPlaneOffset);
		m_Options.ShadowCascadeFarPlaneOffset = std::max(0.0f, settings.ShadowCascadeFarPlaneOffset);
		m_Options.ShadowCascadeTransitionFade = std::max(0.0f, settings.ShadowCascadeTransitionFade);
		m_Options.ShadowFilter = SanitizeShadowFilterMode(settings.ShadowFilterMode);
		m_Options.DirectionalPCSSCascadeCount = glm::min(settings.DirectionalPCSSCascadeCount, m_Options.ActiveShadowCascadeCount);
		m_Options.ShadowPCFRadiusTexels = std::clamp(settings.ShadowPCFRadiusTexels, 0.25f, 8.0f);
		m_Options.SpotShadowPCFRadiusTexels = std::clamp(settings.SpotShadowPCFRadiusTexels, 0.25f, 8.0f);
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

	}

	void SceneRenderer::WriteProjectSettings(ProjectSceneRendererSettings& settings) const
	{
		settings.QualityPreset = static_cast<uint32_t>(SanitizeQualityPreset(static_cast<uint32_t>(m_Options.Quality)));
		settings.EnableFrustumCulling = m_Options.EnableFrustumCulling;
		settings.EnableOcclusionCulling = m_Options.EnableOcclusionCulling;
		settings.OcclusionDepthBias = m_Options.OcclusionDepthBias;
		settings.OcclusionBoundsScale = m_Options.OcclusionBoundsScale;
		settings.EnableGPUDrivenRendering = m_Options.EnableGPUDrivenRendering;
		settings.EnableMeshLODs = m_Options.EnableMeshLODs;
		settings.MeshLODDistanceScale = m_Options.MeshLODDistanceScale;
		settings.EnableVariableRateShading = m_Options.EnableVariableRateShading;
		settings.EnableMeshShaders = m_Options.EnableMeshShaders;
		settings.EnableGTAO = m_Options.EnableGTAO;
		settings.GTAOBentNormals = m_Options.GTAOBentNormals;
		settings.GTAODenoisePasses = m_Options.GTAODenoisePasses;
		settings.AOShadowTolerance = m_Options.AOShadowTolerance;
		settings.EnableSSR = m_Options.EnableSSR;
		settings.GTAOResolutionScale = GetEffectResolutionDivisor(m_Options.GTAOResolutionScale);
		const SceneRendererOptions::SSRQualityPreset ssrQuality = SanitizeSSRQualityPreset(static_cast<uint32_t>(m_Options.SSRQuality));
		const SceneRendererOptions::EffectResolutionScale ssrResolutionScale = GetSSRQualityResolutionScale(ssrQuality);
		settings.SSRQuality = static_cast<uint32_t>(ssrQuality);
		settings.SSRResolutionScale = GetEffectResolutionDivisor(ssrResolutionScale);
		settings.EnableSMAA = m_Options.EnableSMAA;
		settings.SMAAThreshold = m_Options.SMAAThreshold;
		settings.SMAALocalContrastAdaptationFactor = m_Options.SMAALocalContrastAdaptationFactor;
		settings.EnableJumpFlood = m_Options.EnableJumpFlood;
		settings.RenderScaleMode = static_cast<uint32_t>(m_Options.ResolutionScaleMode);
		settings.FixedRenderWidth = m_Options.FixedRenderWidth;
		settings.FixedRenderHeight = m_Options.FixedRenderHeight;
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
		settings.ActiveShadowCascadeCount = SanitizeActiveShadowCascadeCount(m_Options.ActiveShadowCascadeCount);
		settings.ShadowCascadeSplitLambda = m_Options.ShadowCascadeSplitLambda;
		settings.ShadowCascadeNearPlaneOffset = m_Options.ShadowCascadeNearPlaneOffset;
		settings.ShadowCascadeFarPlaneOffset = m_Options.ShadowCascadeFarPlaneOffset;
		settings.ShadowCascadeTransitionFade = m_Options.ShadowCascadeTransitionFade;
		settings.ShadowFilterMode = static_cast<uint32_t>(SanitizeShadowFilterMode(static_cast<uint32_t>(m_Options.ShadowFilter)));
		settings.DirectionalPCSSCascadeCount = glm::min(m_Options.DirectionalPCSSCascadeCount, settings.ActiveShadowCascadeCount);
		settings.ShadowPCFRadiusTexels = m_Options.ShadowPCFRadiusTexels;
		settings.SpotShadowPCFRadiusTexels = m_Options.SpotShadowPCFRadiusTexels;
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
		// Async-compute queue command buffer. Created unconditionally (the compute
		// queue is enabled at device creation); only used when EnableAsyncCompute
		// routes independent compute passes onto it.
		m_ComputeCommandBuffer = RenderCommandBuffer::Create(0, "SceneRenderer-AsyncCompute", /*queries=*/false, nvrhi::CommandQueue::Compute);

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

			ImageSpecification staticShadowMapSpec = shadowMapSpec;
			staticShadowMapSpec.DebugName = "StaticShadowMapCacheArray";
			m_ShadowMapStaticCacheImage = Image2D::Create(staticShadowMapSpec);
			m_ShadowMapStaticCacheImage->RT_Invalidate();

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

				FramebufferSpecification staticFBSpec = fbSpec;
				staticFBSpec.DebugName = "StaticShadowMapCache-Cascade" + std::to_string(cascade);
				staticFBSpec.ExistingImage = m_ShadowMapStaticCacheImage;

				PipelineSpecification staticPipelineSpec = pipelineSpec;
				staticPipelineSpec.DebugName = "StaticDirShadowMap-Cascade" + std::to_string(cascade);
				staticPipelineSpec.TargetFramebuffer = Framebuffer::Create(staticFBSpec);

				RenderPassSpecification staticRPSpec;
				staticRPSpec.DebugName = "StaticShadowMapCachePass-Cascade" + std::to_string(cascade);
				staticRPSpec.Pipeline = Pipeline::Create(staticPipelineSpec);

				m_ShadowMapStaticCachePasses[cascade] = RenderPass::Create(staticRPSpec);
				m_ShadowMapStaticCachePasses[cascade]->SetInput("ShadowData", m_UBSShadow);
				m_ShadowMapStaticCachePasses[cascade]->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
				m_ShadowMapStaticCachePasses[cascade]->SetInput("ObjectIndexes", m_SBSObjectIndexes);
				LUX_CORE_VERIFY(m_ShadowMapStaticCachePasses[cascade]->Validate());
				m_ShadowMapStaticCachePasses[cascade]->Bake();

				FramebufferSpecification dynamicFBSpec = fbSpec;
				dynamicFBSpec.DebugName = "DynamicShadowMap-Cascade" + std::to_string(cascade);
				dynamicFBSpec.ClearDepthOnLoad = false;

				PipelineSpecification dynamicPipelineSpec = pipelineSpec;
				dynamicPipelineSpec.DebugName = "DynamicDirShadowMap-Cascade" + std::to_string(cascade);
				dynamicPipelineSpec.TargetFramebuffer = Framebuffer::Create(dynamicFBSpec);

				RenderPassSpecification dynamicRPSpec;
				dynamicRPSpec.DebugName = "DynamicShadowMapPass-Cascade" + std::to_string(cascade);
				dynamicRPSpec.Pipeline = Pipeline::Create(dynamicPipelineSpec);

				m_ShadowMapDynamicPasses[cascade] = RenderPass::Create(dynamicRPSpec);
				m_ShadowMapDynamicPasses[cascade]->SetInput("ShadowData", m_UBSShadow);
				m_ShadowMapDynamicPasses[cascade]->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
				m_ShadowMapDynamicPasses[cascade]->SetInput("ObjectIndexes", m_SBSObjectIndexes);
				LUX_CORE_VERIFY(m_ShadowMapDynamicPasses[cascade]->Validate());
				m_ShadowMapDynamicPasses[cascade]->Bake();
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

			ImageSpecification staticSpotShadowSpec = spotShadowSpec;
			staticSpotShadowSpec.DebugName = "StaticSpotShadowAtlas";
			m_SpotShadowStaticCacheImage = Image2D::Create(staticSpotShadowSpec);
			m_SpotShadowStaticCacheImage->RT_Invalidate();

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

			FramebufferSpecification staticFBSpec = fbSpec;
			staticFBSpec.DebugName = "StaticSpotShadowAtlas";
			staticFBSpec.ExistingImage = m_SpotShadowStaticCacheImage;

			PipelineSpecification staticPipelineSpec = pipelineSpec;
			staticPipelineSpec.DebugName = "StaticSpotShadowMap";
			staticPipelineSpec.TargetFramebuffer = Framebuffer::Create(staticFBSpec);

			RenderPassSpecification staticRPSpec;
			staticRPSpec.DebugName = "StaticSpotShadowMapPass";
			staticRPSpec.Pipeline = Pipeline::Create(staticPipelineSpec);

			m_SpotShadowStaticCachePass = RenderPass::Create(staticRPSpec);
			m_SpotShadowStaticCachePass->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_SpotShadowStaticCachePass->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
			m_SpotShadowStaticCachePass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_SpotShadowStaticCachePass->Validate());
			m_SpotShadowStaticCachePass->Bake();

			FramebufferSpecification dynamicFBSpec = fbSpec;
			dynamicFBSpec.DebugName = "DynamicSpotShadowAtlas";
			dynamicFBSpec.ClearDepthOnLoad = false;

			PipelineSpecification dynamicPipelineSpec = pipelineSpec;
			dynamicPipelineSpec.DebugName = "DynamicSpotShadowMap";
			dynamicPipelineSpec.TargetFramebuffer = Framebuffer::Create(dynamicFBSpec);

			RenderPassSpecification dynamicRPSpec;
			dynamicRPSpec.DebugName = "DynamicSpotShadowMapPass";
			dynamicRPSpec.Pipeline = Pipeline::Create(dynamicPipelineSpec);

			m_SpotShadowDynamicPass = RenderPass::Create(dynamicRPSpec);
			m_SpotShadowDynamicPass->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_SpotShadowDynamicPass->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
			m_SpotShadowDynamicPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_SpotShadowDynamicPass->Validate());
			m_SpotShadowDynamicPass->Bake();

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

			// Mesh-shader variant on the same framebuffer (used when the
			// EnableMeshShaders option is on and the GPU supports it).
			if (Renderer::SupportsMeshShaders())
			{
				PipelineSpecification meshletPipelineSpec;
				meshletPipelineSpec.DebugName = "PreDepth-Meshlet";
				meshletPipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("PreDepth_Meshlet");
				meshletPipelineSpec.TargetFramebuffer = pipelineSpec.TargetFramebuffer;
				meshletPipelineSpec.DepthOperator = DepthCompareOperator::GreaterOrEqual;
				m_PreDepthMeshletPipeline = Pipeline::Create(meshletPipelineSpec);

				RenderPassSpecification meshletRPSpec;
				meshletRPSpec.DebugName = "PreDepthMeshletPass";
				meshletRPSpec.Pipeline = m_PreDepthMeshletPipeline;

				m_PreDepthMeshletPass = RenderPass::Create(meshletRPSpec);
				m_PreDepthMeshletPass->SetInput("Camera", m_UBSCamera);
				m_PreDepthMeshletPass->SetInput("GPUSceneInstances", m_SBSGPUSceneInstances);
				m_PreDepthMeshletPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
				LUX_CORE_VERIFY(m_PreDepthMeshletPass->Validate());
				m_PreDepthMeshletPass->Bake();
			}
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
			// 16F: a blurred scene-color mip chain for SSR needs HDR range, not
			// fp32 precision — half the bandwidth/memory. Matches Pre-Convolution.glsl.
			preConvolutionSpec.Format = ImageFormat::RGBA16F;
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
			// Material + object IDs packed into one RG32UI target (one fewer
			// full-res attachment write/clear per frame).
			FramebufferTextureSpecification gbufferMaterialObjectID = ImageFormat::RG32UI;
			FramebufferTextureSpecification gbufferVelocity = ImageFormat::RG16F;
			gbufferBaseColor.Blend = false;
			gbufferNormal.Blend = false;
			gbufferMetalRough.Blend = false;
			gbufferMaterialObjectID.Blend = false;
			gbufferVelocity.Blend = false;

			FramebufferSpecification gbufferSpec;
			gbufferSpec.Width = m_ViewportWidth;
			gbufferSpec.Height = m_ViewportHeight;
			gbufferSpec.Attachments = {
				gbufferBaseColor,
				gbufferNormal,
				gbufferMetalRough,
				gbufferMaterialObjectID,
				gbufferVelocity,
				ImageFormat::DEPTH32FSTENCIL8UINT
			};
			gbufferSpec.ExistingImages[5] = m_PreDepthPass->GetDepthOutput();
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
			forwardSpec.ExistingImages[3] = m_GeometryPassFramebuffer->GetImage(4); // shared velocity buffer
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

			// GTAO images are shared by the compute chain, AO composite, and debug
			// views. Keep them alive independently of the optional AO composite pass.
			{
				ImageSpecification gtaoImageSpec;
				gtaoImageSpec.Format = ImageFormat::RED32UI;
				gtaoImageSpec.Usage = ImageUsage::Storage;
				gtaoImageSpec.DebugName = "GTAO";
				m_GTAOOutputImage = Image2D::Create(gtaoImageSpec);

				gtaoImageSpec.DebugName = "GTAO-Denoise";
				m_GTAODenoiseImage = Image2D::Create(gtaoImageSpec);

				gtaoImageSpec.Format = ImageFormat::RED8UN;
				gtaoImageSpec.DebugName = "GTAO-Edges";
				m_GTAOEdgesOutputImage = Image2D::Create(gtaoImageSpec);

				gtaoImageSpec.Format = ImageFormat::RED32UI;
				gtaoImageSpec.DebugName = "GTAO-History-A";
				gtaoImageSpec.DebugName = "GTAO-History-B";

				m_GTAOFinalImage = (m_Options.GTAODenoisePasses % 2 != 0) ? m_GTAODenoiseImage : m_GTAOOutputImage;
			}

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

			// Editor-only debug view target — not created in the standalone runtime.
			if (m_Specification.EnableEditorRenderTargets)
			{
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
		}

		// ── GTAO + AO composite ───────────────────────────────────────────────
		{
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
			if (m_AOCompositePass->IsInputValid("u_GTAOTex"))
			{
				m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
				m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
				m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
			}
			m_AOCompositePass->SetInput("Camera", m_UBSCamera);
			m_AOCompositePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_AOCompositePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_AOCompositePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_AOCompositePass->Validate());
			m_AOCompositePass->Bake();
			m_AOCompositeMaterial = Material::Create(aoPipelineSpec.Shader, "GTAO-Composite");

			// Editor-only AO debug view target — not created in the standalone runtime.
			if (m_Specification.EnableEditorRenderTargets)
			{
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
			if (m_AODebugPass->IsInputValid("u_GTAOTex"))
			{
				m_AODebugPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
				m_AODebugPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
				m_AODebugPass->SetInput("u_Normal", GetGeometryNormalOutput());
			}
			m_AODebugPass->SetInput("Camera", m_UBSCamera);
			m_AODebugPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_AODebugPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_AODebugPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_AODebugPass->Validate());
			m_AODebugPass->Bake();
			m_AODebugMaterial = Material::Create(aoPipelineSpec.Shader, "AO-Debug");
			}
		}

		// ── SSR ────────────────────────────────────────────────────────────────
		{
			ImageSpecification ssrImageSpec;
			ssrImageSpec.Format = ImageFormat::RGBA16F;
			ssrImageSpec.Usage = ImageUsage::Storage;
			ssrImageSpec.DebugName = "SSR";
			m_SSRImage = Image2D::Create(ssrImageSpec);
			ssrImageSpec.DebugName = "SSR-History-A";
			ssrImageSpec.DebugName = "SSR-History-B";
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
			// Hazel's composite only samples the SSR buffer - no depth/normal/Camera, since the
			// bilateral upscale (the one thing that needed them) is gone. Guarded so a shader
			// that no longer declares an input is skipped instead of erroring.
			m_SSRCompositePass->SetInput("u_SSR", m_SSRImage);
			if (m_SSRCompositePass->IsInputValid("r_DefaultSampler"))
				m_SSRCompositePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			if (m_SSRCompositePass->IsInputValid("r_PointSampler"))
				m_SSRCompositePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			if (m_SSRCompositePass->IsInputValid("r_LinearSampler"))
				m_SSRCompositePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_SSRCompositePass->Validate());
			m_SSRCompositePass->Bake();
			m_SSRCompositeMaterial = Material::Create(ssrCompositePipelineSpec.Shader, "SSR-Composite");
		}

		// ── Selected geometry (isolation for outline) ─────────────────────────
		// Editor-only (selection outline) — not created in the standalone runtime.
		if (m_Specification.EnableEditorRenderTargets)
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
		// Editor-only (selection outline; 3 full-viewport RGBA32F targets) — not
		// created in the standalone runtime.
		if (m_Specification.EnableEditorRenderTargets)
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
		// Editor-only (selection wireframe / collider view) — not created in the
		// standalone runtime.
		if (m_Specification.EnableEditorRenderTargets)
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
		// REMOVED: Sky Atmosphere, Volumetric Clouds, and Atmospheric/Height Fog
		// are cut. Their passes, pipelines, materials, 3D cloud-noise volumes,
		// cloud history buffers and framebuffers are no longer created; the
		// members stay null and every path goes inert -- the render-graph nodes
		// (gated on the pass existing), the execute functions (early-return on
		// null) and the resize/repair sweeps (all null-guarded). This reclaims
		// their VRAM and per-frame GPU cost. The shaders and pass functions are
		// kept on disk; recreate this block to restore.

		// ── Bloom compute (feeds the scene composite) ─────────────────────────
		{
			Ref<Shader> shader = Renderer::GetShaderLibrary()->Get("Bloom");
			m_BloomComputePipeline = PipelineCompute::Create(shader);

			TextureSpecification spec;
			// 16F: bloom is a blurred HDR pyramid — fp32 is 2x the bandwidth for
			// no visual gain. Matches Bloom.glsl's image layout.
			spec.Format = ImageFormat::RGBA16F;
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
			CreateSMAAPasses();

			// Editor-only (selection outline composite) — not created in the
			// standalone runtime. References m_JumpFloodPasses, gated by the same flag.
			if (m_Specification.EnableEditorRenderTargets)
			{
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

		if (m_Options.ResolutionScaleMode == SceneRendererOptions::RenderResolutionScaleMode::FixedResolution)
		{
			// An absolute target, so it cannot be expressed as a fraction of the output the way the
			// other modes are - the render aspect is whatever the fixed size says, independent of
			// the viewport. Presenters must letterbox this rather than stretch it to fill.
			width = glm::max(1u, m_Options.FixedRenderWidth);
			height = glm::max(1u, m_Options.FixedRenderHeight);
		}
		else
		{
			const float renderScale = ResolveRenderResolutionScale();
			width = glm::max(1u, (uint32_t)std::round((float)width * renderScale));
			height = glm::max(1u, (uint32_t)std::round((float)height * renderScale));
		}

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
		// Defer to the BeginScene m_NeedsResize block instead of resizing here.
		// Resizing immediately is unsafe: render-target aliasing is still applied
		// and the main framebuffers (SceneColor/GBuffer/PreDepth) have not been
		// resized yet, so effect framebuffers that wrap them via ExistingImages
		// bake the soon-to-be-orphaned texture handles into their FramebufferDesc.
		// The resize block's Framebuffer::Resize then early-outs (size already
		// matches) and never repairs them — meshes disappear permanently until
		// the renderer is recreated.
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		m_NeedsResize = true;
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
			SetRenderPassInputIfValid(renderPass, "u_GBufferMaterialObjectID", m_GeometryPass->GetOutput(3));
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
			case SceneRendererOptions::RenderResolutionScaleMode::FixedResolution:
			{
				// SetViewportSize does not route through this for the fixed mode - it sets the
				// absolute size directly. Report the equivalent linear ratio so the stats HUD and
				// any other reader still show a meaningful "scale" figure.
				if (m_OutputViewportHeight == 0)
					return 1.0f;

				return (float)glm::max(1u, m_Options.FixedRenderHeight) / (float)m_OutputViewportHeight;
			}
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
		// SMAA storage images track the viewport; all are null when SMAA is unavailable.
		if (m_SMAAEdgesImage)
		{
			m_SMAAEdgesImage->Resize(viewportSize.x, viewportSize.y);
			m_SMAAOutputImage->Resize(viewportSize.x, viewportSize.y);

			m_SMAAWorkGroups = {
				(uint32_t)glm::ceil(viewportSize.x / 8.0f),
				(uint32_t)glm::ceil(viewportSize.y / 8.0f),
				1
			};
		}
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

			m_GTAOWorkGroups = { DivideRoundUp(gtaoSize.x, GTAO_WORKGROUP_SIZE), DivideRoundUp(gtaoSize.y, GTAO_WORKGROUP_SIZE), 1 };

			constexpr uint32_t DENOISE_WORKGROUP_SIZE = 8u;
			m_GTAODenoiseImage->Resize(denoiseSize.x, denoiseSize.y);
			m_GTAODenoiseWorkGroups = {
				DivideRoundUp(denoiseSize.x, DENOISE_WORKGROUP_SIZE * 2u),
				DivideRoundUp(denoiseSize.y, DENOISE_WORKGROUP_SIZE),
				1
			};

			m_GTAOFinalImage = (m_Options.GTAODenoisePasses % 2 != 0) ? m_GTAODenoiseImage : m_GTAOOutputImage;
			if (m_AOCompositePass && m_AOCompositePass->IsInputValid("u_GTAOTex"))
			{
				m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
				m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
				m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
			}
			if (m_AODebugPass && m_AODebugPass->IsInputValid("u_GTAOTex"))
			{
				m_AODebugPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
				m_AODebugPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
				m_AODebugPass->SetInput("u_Normal", GetGeometryNormalOutput());
			}
			if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
				m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
		}


		if (m_SSRImage && m_PreConvolutedTexture.Texture)
		{
			constexpr uint32_t SSR_WORKGROUP_SIZE = 8u;
			m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
			m_SSROptions.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);
			m_SSROptions.HalfRes = m_SSROptions.ResolutionScale > 1u;
			glm::uvec2 ssrSize = GetScaledExtent(viewportSize, m_Options.SSRResolutionScale);

			m_SSRImage->Resize(ssrSize.x, ssrSize.y);
			m_SSRFinalImage = m_SSRImage;
			m_SSRWorkGroups = { DivideRoundUp(ssrSize.x, SSR_WORKGROUP_SIZE), DivideRoundUp(ssrSize.y, SSR_WORKGROUP_SIZE), 1 };
			if (m_SSRCompositePass)
				m_SSRCompositePass->SetInput("u_SSR", m_SSRFinalImage);

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

	void SceneRenderer::CreateSMAAPasses()
	{
		if (!m_CompositePass)
			return;

		// SMAA's blending-weight pass is driven by two precomputed lookup tables from the
		// reference implementation (AreaTex encodes analytic coverage per edge pattern,
		// SearchTex accelerates the edge-distance walk). They are vendored headers rather
		// than assets, so build without them and SMAA simply stays unavailable - the passes
		// below are still created where they can be, and IsSMAAReady() gates every use.
#if defined(LUX_HAS_SMAA_TEXTURES)
		{
			TextureSpecification areaSpec;
			areaSpec.Format = ImageFormat::RG8;
			areaSpec.Width = AREATEX_WIDTH;
			areaSpec.Height = AREATEX_HEIGHT;
			areaSpec.SamplerWrap = TextureWrap::Clamp;
			areaSpec.SamplerFilter = TextureFilter::Linear;
			areaSpec.GenerateMips = false;
			areaSpec.FlipVertically = false;
			m_SMAAAreaTexture = Texture2D::Create(areaSpec, Buffer((void*)areaTexBytes, AREATEX_SIZE));

			TextureSpecification searchSpec;
			searchSpec.Format = ImageFormat::RED8UN;
			searchSpec.Width = SEARCHTEX_WIDTH;
			searchSpec.Height = SEARCHTEX_HEIGHT;
			searchSpec.SamplerWrap = TextureWrap::Clamp;
			// The search texture must not be filtered - it is a lookup table, and
			// interpolating between entries returns distances that do not exist.
			searchSpec.SamplerFilter = TextureFilter::Nearest;
			searchSpec.GenerateMips = false;
			searchSpec.FlipVertically = false;
			m_SMAASearchTexture = Texture2D::Create(searchSpec, Buffer((void*)searchTexBytes, SEARCHTEX_SIZE));
		}
#else
		LUX_CORE_WARN_TAG("Renderer", "SMAA unavailable: vendored AreaTex.h/SearchTex.h not found under Core/vendor/smaa. Antialiasing will stay off.");
#endif

		// Without the lookup tables SMAA can never run, so create nothing at all rather than
		// paying for a pipeline and a full-viewport target that would sit idle - the same cost
		// that got TAA removed.
		if (!m_SMAAAreaTexture || !m_SMAASearchTexture)
			return;

		// ShaderLibrary::Get is a map::at, so asking for a shader that was never registered
		// throws std::out_of_range - and in Release the preceding assert is compiled out, so it
		// surfaces as an unhandled exception during init with no indication of which name was
		// missing. Check every shader up front and disable SMAA if any is absent.
		{
			const auto& shaders = Renderer::GetShaderLibrary()->GetShaders();
			for (const char* required : { "SMAA-EdgeDetection", "SMAA-WeightAndBlend" })
			{
				if (shaders.find(required) == shaders.end())
				{
					LUX_CORE_WARN_TAG("Renderer", "SMAA disabled: shader '{}' is not registered.", required);
					m_SMAAAreaTexture = nullptr;
					m_SMAASearchTexture = nullptr;
					return;
				}
			}
		}

		const uint32_t width = glm::max(1u, m_ViewportWidth);
		const uint32_t height = glm::max(1u, m_ViewportHeight);

		// Storage images for the intermediate results, mirroring how GTAO/SSR are built.
		auto createStorageImage = [&](ImageFormat format, const char* debugName) -> Ref<Image2D>
		{
			ImageSpecification imageSpec;
			imageSpec.Format = format;
			imageSpec.Usage = ImageUsage::Storage;
			imageSpec.Width = width;
			imageSpec.Height = height;
			imageSpec.DebugName = debugName;
			Ref<Image2D> image = Image2D::Create(imageSpec);
			image->Invalidate();
			return image;
		};

		m_SMAAEdgesImage = createStorageImage(ImageFormat::RG8, "SMAA-Edges");
		m_SMAAOutputImage = createStorageImage(ImageFormat::RGBA, "SMAA-Output");

		auto createComputePass = [&](const char* name) -> Ref<ComputePass>
		{
			ComputePassSpecification passSpec;
			passSpec.DebugName = name;
			passSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get(name));
			return ComputePass::Create(passSpec);
		};

		// Pass 1: edge detection on the tone-mapped image.
		m_SMAAEdgeComputePass = createComputePass("SMAA-EdgeDetection");
		m_SMAAEdgeComputePass->SetInput("o_Edges", m_SMAAEdgesImage);
		m_SMAAEdgeComputePass->SetInput("u_InputTex", m_CompositePass->GetOutput(0));
		LUX_CORE_VERIFY(m_SMAAEdgeComputePass->Validate());
		m_SMAAEdgeComputePass->Bake();

		// Pass 2: blending weights (driven by the AreaTex/SearchTex lookups) followed by
		// neighbourhood blending, in one dispatch. The weights never leave shared memory,
		// so there is no intermediate blend-weights target to allocate or round-trip.
		m_SMAAWeightAndBlendComputePass = createComputePass("SMAA-WeightAndBlend");
		m_SMAAWeightAndBlendComputePass->SetInput("o_Output", m_SMAAOutputImage);
		m_SMAAWeightAndBlendComputePass->SetInput("u_InputTex", m_CompositePass->GetOutput(0));
		m_SMAAWeightAndBlendComputePass->SetInput("u_EdgeTex", m_SMAAEdgesImage);
		m_SMAAWeightAndBlendComputePass->SetInput("u_AreaTex", m_SMAAAreaTexture);
		m_SMAAWeightAndBlendComputePass->SetInput("u_SearchTex", m_SMAASearchTexture);
		LUX_CORE_VERIFY(m_SMAAWeightAndBlendComputePass->Validate());
		m_SMAAWeightAndBlendComputePass->Bake();

		m_SMAAWorkGroups = {
			(uint32_t)glm::ceil(width / 8.0f),
			(uint32_t)glm::ceil(height / 8.0f),
			1
		};
	}

	bool SceneRenderer::IsSMAAReady() const
	{
		return m_Options.EnableSMAA
			&& m_SMAAEdgeComputePass && m_SMAAWeightAndBlendComputePass
			&& m_SMAAAreaTexture && m_SMAASearchTexture;
	}

	// Not const: RenderPass::GetOutput and CanCompositeDOFIntoFinalTarget are both non-const,
	// matching GetDebugViewImage which resolves the same image the same way.
	Ref<Image2D> SceneRenderer::GetPostProcessInputImage()
	{
		if (GetResolvedPostProcessSettings().DOFEnabled && m_DOFPass && !CanCompositeDOFIntoFinalTarget())
			return m_DOFPass->GetOutput(0);

		return m_CompositePass ? m_CompositePass->GetOutput(0) : nullptr;
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

	void SceneRenderer::CalculateCascades(CascadeData* cascades, const SceneRendererCamera& sceneCamera, const glm::vec3& lightDirection, float maxShadowDistance, uint32_t activeCascadeCount) const
	{
		activeCascadeCount = SanitizeActiveShadowCascadeCount(activeCascadeCount);
		const float nearClip = glm::max(sceneCamera.Near, 0.001f);
		const float cameraFar = glm::max(sceneCamera.Far, nearClip + 0.001f);
		const float shadowFar = glm::clamp(maxShadowDistance, nearClip + 0.001f, cameraFar);
		const float cameraClipRange = cameraFar - nearClip;
		const float shadowRange = shadowFar - nearClip;
		const float ratio = shadowFar / nearClip;

		float cascadeSplits[ShadowCascadeCount]{};
		for (uint32_t cascade = 0; cascade < activeCascadeCount; cascade++)
		{
			if (m_UseManualCascadeSplits)
			{
				cascadeSplits[cascade] = glm::clamp(m_ShadowCascadeSplits[cascade], 0.0f, 1.0f);
			}
			else
			{
				const float p = (cascade + 1.0f) / static_cast<float>(activeCascadeCount);
				const float logSplit = nearClip * std::pow(ratio, p);
				const float uniformSplit = nearClip + shadowRange * p;
				const float splitDistance = glm::mix(uniformSplit, logSplit, glm::clamp(m_Options.ShadowCascadeSplitLambda, 0.0f, 1.0f));
				cascadeSplits[cascade] = (splitDistance - nearClip) / cameraClipRange;
			}
		}
		if (!m_UseManualCascadeSplits)
			cascadeSplits[activeCascadeCount - 1] = (shadowFar - nearClip) / cameraClipRange;
		for (uint32_t cascade = activeCascadeCount; cascade < ShadowCascadeCount; cascade++)
			cascadeSplits[cascade] = cascadeSplits[activeCascadeCount - 1];

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
		for (uint32_t cascade = 0; cascade < activeCascadeCount; cascade++)
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

		for (uint32_t cascade = activeCascadeCount; cascade < ShadowCascadeCount; cascade++)
			cascades[cascade] = cascades[activeCascadeCount - 1];
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
		for (const Ref<RenderPass>& pass : m_ShadowMapStaticCachePasses)
			addRenderPass(pass);
		for (const Ref<RenderPass>& pass : m_ShadowMapDynamicPasses)
			addRenderPass(pass);
		addRenderPass(m_SpotShadowMapPass);
		addRenderPass(m_SpotShadowStaticCachePass);
		addRenderPass(m_SpotShadowDynamicPass);
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
		addComputePass(m_SSRPass);
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
		addRenderTargetImage(m_GTAOEdgesOutputImage);
		addRenderTargetImage(m_SSRImage);
		addRenderTargetImage(m_SSRFinalImage);

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
				pass.DebugName = name.data(); // string-literal backed; valid for the process lifetime
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

		// PreIntegration's visibility pyramid is consumed only by SSR — skip the
		// whole pass (and its per-mip dispatches) when SSR is off. The vector stays
		// in scope because the SSR node below appends it as a read dependency.
		std::vector<RenderGraph::ResourceHandle> preIntegrationOutputs;
		if (m_Options.EnableSSR)
		{
			preIntegrationOutputs.push_back(m_PreIntegrationVisibilityTexture.Texture ? addTexture("PreIntegration Visibility", m_PreIntegrationVisibilityTexture.Texture->GetImage()) : RenderGraph::InvalidResource);
			addPass("PreIntegration", hzbOutputs, preIntegrationOutputs, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::PreIntegration));
		}

		// Cluster build + light culling are depth-independent (they only need the
		// camera + light UBOs), so with EnableAsyncCompute they run on the compute
		// queue in FlushDrawList *before* this graphics graph and are omitted here.
		// Their SSBO outputs (light grids/index lists) feed deferred lighting; the
		// cross-queue ordering is a queueWaitForCommandList, not a graph edge (the
		// graph never modeled these SSBOs — the nodes had no declared inputs/outputs).
		if (!m_Options.EnableAsyncCompute)
		{
			// Cluster build runs before light culling; it only depends on the camera
			// projection (SSBO synchronized via a manual barrier inside the pass).
			addPass("Cluster Build", {}, {}, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::ClusterBuildPass));

			// Cluster light assignment depends on the cluster AABBs + the light UBOs;
			// it is depth-independent (SSBOs synchronized via manual barriers).
			addPass("Cluster Light Culling", {}, {}, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::ClusterLightCullingPass));
		}

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

		std::vector<RenderGraph::ResourceHandle> selectedOutputs;
		if (m_SelectedGeometryPass && (executable ? !GetMeshPass(MeshPassType::SelectedMask).DrawList.empty() : true))
		{
			selectedOutputs = addRenderPassResources("SelectedGeometry", m_SelectedGeometryPass);
			addPass("Selected Geometry", preDepthOutputs, selectedOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::SelectedGeometryPass));
		}

		std::vector<RenderGraph::ResourceHandle> geometryOutputs = gbufferOutputs;
		appendResources(geometryOutputs, sceneColorCurrent);

		addPass("GBuffer", preDepthOutputs, gbufferOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::GBufferPass));

		{
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

		// GTAO runs on GBuffer depth/normals. The fullscreen AO composite then
		// multiplies the deferred scene color using the resolved AO image.
		std::vector<RenderGraph::ResourceHandle> aoFinalOutputs;
		if (m_Options.EnableGTAO)
		{
			const RenderGraph::ResourceHandle gtaoOutput = addTexture("GTAO Output", m_GTAOOutputImage);
			const RenderGraph::ResourceHandle gtaoDenoise = addTexture("GTAO Denoise", m_GTAODenoiseImage);
			const RenderGraph::ResourceHandle gtaoEdges = addTexture("GTAO Edges", m_GTAOEdgesOutputImage);

			std::vector<RenderGraph::ResourceHandle> gtaoReads = geometryOutputs;
			appendResources(gtaoReads, hzbOutputs);
			addPass("GTAO", gtaoReads, { gtaoOutput, gtaoEdges }, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::GTAOCompute));
			addPass("GTAO Denoise", { gtaoOutput, gtaoEdges }, { gtaoDenoise, gtaoOutput }, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::GTAODenoiseCompute));

			aoFinalOutputs = { gtaoOutput, gtaoDenoise };

			if (m_AOCompositePass)
			{
				std::vector<RenderGraph::ResourceHandle> aoCompositeReads = geometryOutputs;
				appendResources(aoCompositeReads, preDepthOutputs);
				appendResources(aoCompositeReads, aoFinalOutputs);
				appendResources(aoCompositeReads, sceneColorCurrent);
				std::vector<RenderGraph::ResourceHandle> aoCompositeOutputs = addRenderPassResources("AO Composite", m_AOCompositePass);
				addPass("AO Composite", aoCompositeReads, aoCompositeOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::AOComposite));
				sceneColorCurrent = aoCompositeOutputs;
			}

			geometryOutputs = gbufferOutputs;
			appendResources(geometryOutputs, sceneColorCurrent);
		}

		if (UsesGBufferDebugPass(m_DebugViewMode) && m_GBufferDebugPass)
		{
			std::vector<RenderGraph::ResourceHandle> debugReads = gbufferOutputs;
			appendResources(debugReads, sceneColorCurrent);
			addPass("GBuffer Debug", debugReads, addRenderPassResources("GBuffer Debug", m_GBufferDebugPass), RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::GBufferDebugPass));
		}

		if (m_Options.EnableGTAO && m_DebugViewMode == DebugViewMode::AO && m_AODebugPass)
		{
			std::vector<RenderGraph::ResourceHandle> aoDebugReads = geometryOutputs;
			appendResources(aoDebugReads, preDepthOutputs);
			appendResources(aoDebugReads, aoFinalOutputs);
			addPass("AO Debug", aoDebugReads, addRenderPassResources("AO Debug", m_AODebugPass), RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::AODebugPass));
		}

		std::vector<RenderGraph::ResourceHandle> ssrOutputs;
		if (m_Options.EnableSSR)
		{
			std::vector<RenderGraph::ResourceHandle> preConvolutionOutputs;
			preConvolutionOutputs.push_back(m_PreConvolutedTexture.Texture ? addTexture("Pre-Convoluted Scene", m_PreConvolutedTexture.Texture->GetImage()) : RenderGraph::InvalidResource);
			addPass("Pre-Convolution", sceneColorCurrent, preConvolutionOutputs, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::PreConvolutionCompute));

			const RenderGraph::ResourceHandle ssrImage = addTexture("SSR", m_SSRImage);
			ssrOutputs.push_back(ssrImage);

			std::vector<RenderGraph::ResourceHandle> ssrReads = geometryOutputs;
			appendResources(ssrReads, hzbOutputs);
			appendResources(ssrReads, preIntegrationOutputs);
			appendResources(ssrReads, preConvolutionOutputs);
			appendResources(ssrReads, aoFinalOutputs);
			addPass("SSR", ssrReads, ssrOutputs, RenderGraph::PassFlags::Compute, makeExecute(&SceneRenderer::SSRCompute));


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

		// Executable graphs skip the node entirely when nothing transparent was
		// submitted (avoids the render-pass open/clear); non-executable (debug
		// snapshot) graphs keep the full topology.
		if (executable ? !GetMeshPass(MeshPassType::Transparent).DrawList.empty() : true)
		{
			std::vector<RenderGraph::ResourceHandle> transparentReads = shadowOutputs;
			appendResources(transparentReads, preDepthOutputs);
			appendResources(transparentReads, sceneColorCurrent);
			std::vector<RenderGraph::ResourceHandle> transparentOutputs = addRenderPassResources("Transparent Forward", m_GeometryPassTransparent);
			addPass("Transparent Forward", transparentReads, transparentOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::TransparentForwardPass));
			sceneColorCurrent = transparentOutputs;
		}

		const bool wireframeActive = executable
			? ((m_Options.ShowSelectedInWireframe && !GetMeshPass(MeshPassType::Wireframe).DrawList.empty())
				|| (m_Options.ShowPhysicsColliders && !GetMeshPass(MeshPassType::PhysicsCollider).DrawList.empty()))
			: true;
		if (m_GeometryWireframePass && wireframeActive)
		{
			std::vector<RenderGraph::ResourceHandle> wireframeReads = sceneColorCurrent;
			std::vector<RenderGraph::ResourceHandle> wireframeOutputs = addRenderPassResources("Geometry Wireframe", m_GeometryWireframePass);
			addPass("Geometry Wireframe", wireframeReads, wireframeOutputs, RenderGraph::PassFlags::Graphics, makeExecute(&SceneRenderer::GeometryWireframePass));
			sceneColorCurrent = wireframeOutputs;
		}

		std::vector<RenderGraph::ResourceHandle> jumpFloodAOutputs;
		std::vector<RenderGraph::ResourceHandle> jumpFloodBOutputs;
		const bool jumpFloodActive = m_Options.EnableJumpFlood && m_JumpFloodInitPass && (executable ? !GetMeshPass(MeshPassType::SelectedMask).DrawList.empty() : true);
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

		// SMAA sits directly after the composite and before DOF: morphological AA keys off
		// perceived edges so it needs post-tonemap colour, and depth of field should blur an
		// already-antialiased image rather than SMAA chewing on a blurred one. The three
		// compute dispatches copy their result back over the composite output, which the
		// graph sees as a side effect on compositeOutputs.
		if (IsSMAAReady())
		{
			constexpr auto smaaFlags = static_cast<RenderGraph::PassFlags>(
				static_cast<uint32_t>(RenderGraph::PassFlags::Compute) | static_cast<uint32_t>(RenderGraph::PassFlags::SideEffect));
			addPass("SMAA", compositeOutputs, compositeOutputs, smaaFlags, makeExecute(&SceneRenderer::SMAAPass));
		}

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
							if (name == "Volumetric Cloud Composite") return "VolumetricCloudCompositePass";
						if (name == "Atmospheric Fog") return "AtmosphericFogPass";
						if (name == "Selected Geometry") return "SelectedGeometryPass";
						if (name == "GBuffer") return "GBufferPass";
						if (name == "Deferred Lighting") return "DeferredLightingPass";
						if (name == "Transparent Forward") return "TransparentForwardPass";
						if (name == "Geometry Wireframe") return "GeometryWireframePass";
						if (name == "GBuffer Debug") return "GBufferDebugPass";
						if (name == "GTAO Denoise") return "GTAO-Denoise";
						if (name == "AO Composite") return "AOComposite";
						if (name == "AO Debug") return "AODebug";
						if (name == "Pre-Convolution") return "PreConvolution";
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
			// Always restore real storage: ApplyRenderTargetAliasing() excludes
			// invalid images from the rebuilt alias graph, so a dead image is never
			// re-aliased and never recovers — and it feeds a null texture handle
			// into DescriptorSetManager::Bake, silently no-oping its passes.
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
		m_ShadowMotionFrameIndex++;
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
		m_SceneData.CameraPosition = glm::vec3(glm::inverse(camera.ViewMatrix)[3]);
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
			// Editor-only passes may not exist (EnableEditorRenderTargets=false).
			if (m_GBufferDebugPass)
				m_GBufferDebugPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SkyboxPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			// Sky Atmosphere / Volumetric Clouds / Atmospheric Fog removed — may be
			// null now, so guard the resize (the other sweeps already null-check).
			if (m_SkyAtmospherePass)
				m_SkyAtmospherePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			if (m_VolumetricCloudCompositePass)
				m_VolumetricCloudCompositePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			if (m_AtmosphericFogPass)
				m_AtmosphericFogPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			if (m_SelectedGeometryPass)
				m_SelectedGeometryPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			if (m_GeometryWireframePass)
				m_GeometryWireframePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_CompositingFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_CompositePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GridRenderPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			ResizeScreenSpaceEffectResources();
			ApplyRenderTargetAliasing();
			m_ShadowCascadeCacheValid = false;
			m_StaticShadowMapCacheValid = false;
			m_DirectionalShadowMapNeedsRender = true;
		}

		// ── Self-heal framebuffers with stale attachment handles ─────────────
		// Shared images (FramebufferSpecification::ExistingImages) are recreated
		// in place on resize/aliasing changes; a framebuffer wrapping one keeps
		// its old baked handle and silently renders into the orphaned texture
		// (e.g. runtime-fullscreen startup: deferred lighting writes into a dead
		// SceneColor → black geometry under a bright sky). Cheap pointer compares
		// per frame; re-invalidation only fires when actually stale. Owners come
		// first: repairing one recreates its images, and the wrappers checked
		// afterwards pick the new handles up in the same sweep.
		{
			// By value: Ref<> propagates constness to the pointee, and both
			// Invalidate() and GetTargetFramebuffer() are non-const.
			auto repairIfStale = [](Ref<Framebuffer> framebuffer, const char* name)
			{
				if (framebuffer && framebuffer->HasStaleAttachments())
				{
					LUX_CORE_WARN_TAG("Renderer", "Framebuffer '{}' had stale attachment handles - re-invalidating", name);
					framebuffer->Invalidate();
				}
			};
			auto repairPassIfStale = [&repairIfStale](auto pass, const char* name)
			{
				if (pass)
					repairIfStale(pass->GetTargetFramebuffer(), name);
			};

			repairPassIfStale(m_PreDepthPass, "PreDepth");
			repairIfStale(m_GeometryPassFramebuffer, "GBuffer (owner)");
			repairIfStale(m_SceneColorFramebuffer, "SceneColor");
			repairIfStale(m_CompositingFramebuffer, "Compositing");
			repairPassIfStale(m_GeometryPass, "GBuffer");
			repairPassIfStale(m_GeometryPassTransparent, "TransparentForward");
			repairPassIfStale(m_DeferredLightingPass, "DeferredLighting");
			repairPassIfStale(m_AOCompositePass, "AO-Composite");
			repairPassIfStale(m_SSRCompositePass, "SSR-Composite");
			repairPassIfStale(m_SkyboxPass, "Skybox");
			repairPassIfStale(m_SkyAtmospherePass, "SkyAtmosphere");
			repairPassIfStale(m_VolumetricCloudCompositePass, "VolumetricCloudComposite");
			repairPassIfStale(m_AtmosphericFogPass, "AtmosphericFog");
			repairPassIfStale(m_GBufferDebugPass, "GBufferDebug");
			repairPassIfStale(m_SelectedGeometryPass, "SelectedGeometry");
			repairPassIfStale(m_GeometryWireframePass, "GeometryWireframe");
			repairPassIfStale(m_CompositePass, "Composite");
			repairPassIfStale(m_GridRenderPass, "Grid");
			repairPassIfStale(m_JumpFloodCompositePass, "JumpFloodComposite");
		}

		ResizeVolumetricCloudResources();

		// ── Camera uniform buffer ─────────────────────────────────────────────
		{
			const glm::mat4 unjitteredProj = camera.Camera.GetProjectionMatrix();
			const glm::mat4 unjitteredViewProj = unjitteredProj * camera.ViewMatrix;

			// No sub-pixel jitter: the only consumers were TAA and SMAA T2x, both removed.
			// The projection is rasterized exactly as authored, so jittered == unjittered.
			// UnjitteredViewProjectionMatrix and the zeroed Jitter fields stay in UBCamera
			// because PreviousViewProjectionMatrix still feeds the G-buffer velocity target,
			// and that block is shared by nearly every shader.
			const glm::mat4 viewProj = unjitteredViewProj;
			const glm::mat4 viewInverse = glm::inverse(camera.ViewMatrix);
			const glm::mat4 projInverse = glm::inverse(unjitteredProj);

			m_CurrentViewProjection = unjitteredViewProj;
			m_CurrentJitter = glm::vec2(0.0f);

			m_CameraUB.ViewProjection = viewProj;
			m_CameraUB.InverseViewProjection = viewInverse * projInverse;
			m_CameraUB.Projection = unjitteredProj;
			m_CameraUB.InverseProjection = projInverse;
			m_CameraUB.View = camera.ViewMatrix;
			m_CameraUB.InverseView = viewInverse;
			m_CameraUB.UnjitteredViewProjection = unjitteredViewProj;
			// Still written: the G-buffer velocity target reprojects against it.
			m_CameraUB.PreviousViewProjection = m_PreviousViewProjection;
			// Permanently zero now that nothing jitters the projection.
			m_CameraUB.Jitter = glm::vec2(0.0f);
			m_CameraUB.PreviousJitter = glm::vec2(0.0f);

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

			m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
			m_SSROptions.ResolutionScale = GetEffectResolutionDivisor(m_Options.SSRResolutionScale);
			m_SSROptions.HalfRes = m_SSROptions.ResolutionScale > 1u;
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
		// The full rebuild + upload only runs while any atmosphere feature is
		// active (the struct has per-frame time fields, so it legitimately changes
		// every frame then). When everything is off, a disabled-flags UB is
		// written once per frame-in-flight buffer and the block goes idle —
		// shaders gate all reads of this UB on Flags/LocalFogParams.
		const bool atmosphereActive = m_FrameEnvironment.SkyAtmosphereEnabled
			|| m_FrameEnvironment.VolumetricCloudsEnabled
			|| m_FrameEnvironment.HeightFogEnabled
			|| m_FrameEnvironment.LocalFogEnabled;
		if (!atmosphereActive && m_AtmosphereIdleUploadsRemaining > 0)
		{
			m_AtmosphereUB.Flags = { 0u, 0u, 0u, 0u };
			m_AtmosphereUB.LocalFogParams = { 0u, 0u, 0u, 0u };
			m_AtmosphereIdleUploadsRemaining--;

			auto atmosphereData = m_AtmosphereUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, atmosphereData]() mutable {
				instance->m_UBSAtmosphere->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &atmosphereData, sizeof(UBAtmosphere));
				});
		}
		else if (atmosphereActive)
		{
			// Re-arm so the next transition to inactive rewrites every
			// frame-in-flight buffer before going idle.
			m_AtmosphereIdleUploadsRemaining = Renderer::GetConfig().FramesInFlight;

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

			// Candidate scoring/sorting and atlas sizing only matter when spot
			// lights exist; with none, skip straight to the (16-byte) UBO upload.
			if (m_SpotLightsUB.Count > 0)
			{
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
			if (m_SpotShadowStaticCacheImage && m_SpotShadowStaticCacheImage->GetSize().x != m_SpotShadowMapSize)
			{
				m_SpotShadowStaticCacheImage->Resize(m_SpotShadowMapSize, m_SpotShadowMapSize);
				spotShadowFramebufferResized = true;
			}
			if (m_SpotShadowMapPass && m_SpotShadowMapPass->GetTargetFramebuffer()
				&& (m_SpotShadowMapPass->GetTargetFramebuffer()->GetWidth() != m_SpotShadowMapSize
					|| m_SpotShadowMapPass->GetTargetFramebuffer()->GetHeight() != m_SpotShadowMapSize))
			{
				m_SpotShadowMapPass->GetTargetFramebuffer()->Resize(m_SpotShadowMapSize, m_SpotShadowMapSize);
				spotShadowFramebufferResized = true;
			}
			if (m_SpotShadowStaticCachePass && m_SpotShadowStaticCachePass->GetTargetFramebuffer()
				&& (m_SpotShadowStaticCachePass->GetTargetFramebuffer()->GetWidth() != m_SpotShadowMapSize
					|| m_SpotShadowStaticCachePass->GetTargetFramebuffer()->GetHeight() != m_SpotShadowMapSize))
			{
				m_SpotShadowStaticCachePass->GetTargetFramebuffer()->Resize(m_SpotShadowMapSize, m_SpotShadowMapSize);
				spotShadowFramebufferResized = true;
			}
			if (m_SpotShadowDynamicPass && m_SpotShadowDynamicPass->GetTargetFramebuffer()
				&& (m_SpotShadowDynamicPass->GetTargetFramebuffer()->GetWidth() != m_SpotShadowMapSize
					|| m_SpotShadowDynamicPass->GetTargetFramebuffer()->GetHeight() != m_SpotShadowMapSize))
			{
				m_SpotShadowDynamicPass->GetTargetFramebuffer()->Resize(m_SpotShadowMapSize, m_SpotShadowMapSize);
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
				m_StaticSpotShadowMapCacheValid = false;
				m_SpotShadowMapNeedsRender = true;
			}

			if (!m_SpotShadowMapCacheValid || spotShadowStateHash != m_LastSpotShadowStateHash)
			{
				if (spotShadowStateHash != m_LastSpotShadowStateHash)
					m_StaticSpotShadowMapCacheValid = false;
				m_SpotShadowMapNeedsRender = true;
			}
			m_LastSpotShadowStateHash = spotShadowStateHash;
			}
			else
			{
				// No spot lights: shaders read Count=0; reset the state hash so
				// lights reappearing always retrigger a shadow render.
				m_SpotShadowUB.Count = 0;
				m_LastSpotShadowStateHash = 0;
				m_StaticSpotShadowMapCacheValid = false;
			}

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
			if (m_ShadowMapStaticCacheImage && m_ShadowMapStaticCacheImage->GetSize().x != directionalShadowResolution)
			{
				m_ShadowMapStaticCacheImage->Resize(directionalShadowResolution, directionalShadowResolution);
				directionalShadowFramebufferResized = true;
			}

			auto resizeShadowPasses = [&](std::array<Ref<RenderPass>, ShadowCascadeCount>& passes)
			{
				for (Ref<RenderPass>& shadowMapPass : passes)
				{
					if (shadowMapPass && shadowMapPass->GetTargetFramebuffer()
						&& (shadowMapPass->GetTargetFramebuffer()->GetWidth() != directionalShadowResolution
							|| shadowMapPass->GetTargetFramebuffer()->GetHeight() != directionalShadowResolution))
					{
						shadowMapPass->GetTargetFramebuffer()->Resize(directionalShadowResolution, directionalShadowResolution);
						directionalShadowFramebufferResized = true;
					}
				}
			};
			resizeShadowPasses(m_ShadowMapPasses);
			resizeShadowPasses(m_ShadowMapStaticCachePasses);
			resizeShadowPasses(m_ShadowMapDynamicPasses);

			if (directionalShadowFramebufferResized)
			{
				m_ShadowCascadeCacheValid = false;
				m_DirectionalShadowMapCacheValid = false;
				m_StaticShadowMapCacheValid = false;
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
				const uint32_t activeShadowCascadeCount = SanitizeActiveShadowCascadeCount(m_Options.ActiveShadowCascadeCount);

				bool cascadeSettingsChanged =
					std::abs(camera.FOV - m_CachedShadowFOV) > floatThreshold ||
					std::abs(camera.Near - m_CachedShadowNear) > floatThreshold ||
					std::abs(camera.Far - m_CachedShadowFar) > floatThreshold ||
					std::abs(directionalShadowDistance - m_CachedMaxShadowDistance) > floatThreshold ||
					activeShadowCascadeCount != m_CachedActiveShadowCascadeCount ||
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
					CalculateCascades(cascades, camera, lightDirection, directionalShadowDistance, activeShadowCascadeCount);
					for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
					{
						m_ShadowUB.ViewProjection[cascade] = cascades[cascade].ViewProj;
						m_ShadowCascadeFrustums[cascade] = cascade < activeShadowCascadeCount
							? Frustum::FromViewProjection(cascades[cascade].ViewProj)
							: Frustum{};
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
					m_CachedActiveShadowCascadeCount = activeShadowCascadeCount;
					m_CachedShadowMapResolution = shadowMapResolution;
					m_ShadowCascadeCacheValid = true;
					m_StaticShadowMapCacheValid = false;
					m_DirectionalShadowMapNeedsRender = true;
				}

				m_ShadowCascadeFrustumCount = activeShadowCascadeCount;
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

			// Upload only when the cascade matrices actually changed, then once per
			// frame-in-flight buffer so every copy converges before going idle.
			if (std::memcmp(&m_ShadowUB, &m_LastUploadedShadowUB, sizeof(UBShadow)) != 0)
			{
				m_LastUploadedShadowUB = m_ShadowUB;
				m_ShadowUBUploadsRemaining = Renderer::GetConfig().FramesInFlight;
			}

			if (m_ShadowUBUploadsRemaining > 0)
			{
				m_ShadowUBUploadsRemaining--;

				auto shadowData = m_ShadowUB;
				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, shadowData]() mutable {
					instance->m_UBSShadow->RT_Get()->RT_SetData(
						instance->m_UploadCommandBuffer, &shadowData, sizeof(UBShadow));
					});
			}
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
			m_RendererDataUB.ActiveShadowCascadeCount = SanitizeActiveShadowCascadeCount(m_Options.ActiveShadowCascadeCount);
			m_RendererDataUB.ShadowFilterMode = static_cast<uint32_t>(SanitizeShadowFilterMode(static_cast<uint32_t>(m_Options.ShadowFilter)));
			m_RendererDataUB.DirectionalPCSSCascadeCount = glm::min(m_Options.DirectionalPCSSCascadeCount, m_RendererDataUB.ActiveShadowCascadeCount);
			m_RendererDataUB.ShadowFilterParams = {
				std::clamp(m_Options.ShadowPCFRadiusTexels, 0.25f, 8.0f),
				std::clamp(m_Options.SpotShadowPCFRadiusTexels, 0.25f, 8.0f),
				0.0f,
				0.0f
			};
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

	uint32_t SceneRenderer::SelectStaticMeshLOD(const MeshSource& meshSource, uint32_t submeshIndex, const BoundingSphere& bounds) const
	{
		if (!m_Options.EnableMeshLODs)
			return 0;

		const uint32_t lodCount = meshSource.GetSubmeshLODCount(submeshIndex);
		if (lodCount <= 1)
			return 0;

		// Distance from the camera to the bounding-sphere surface, measured in
		// radii: a large object keeps its detail further out than a small one at
		// the same world distance. MeshLODDistanceScale > 1 pushes every switch
		// further away (more detail), < 1 pulls it closer (more aggressive).
		const float radius = glm::max(bounds.Radius, 0.001f);
		const float distance = glm::max(glm::distance(m_SceneData.CameraPosition, bounds.Center) - radius, 0.0f);
		const float lodMetric = distance / (radius * m_Options.MeshLODDistanceScale);

		uint32_t selectedLOD = 0;
		for (uint32_t lodIndex = 1; lodIndex < lodCount; lodIndex++)
		{
			if (lodMetric < meshSource.GetSubmeshLOD(submeshIndex, lodIndex).DistanceMultiplier)
				break;
			selectedLOD = lodIndex;
		}
		return selectedLOD;
	}

	void SceneRenderer::ApplyCoarseFragmentShadingRate()
	{
		if (!m_Options.EnableVariableRateShading || !Renderer::SupportsVariableRateShading())
			return;

		Renderer::SetFragmentShadingRate(m_CommandBuffer, FragmentShadingRate::Rate2x2);
	}

	bool SceneRenderer::ShouldCullTinyDirectionalShadowCaster(const BoundingSphere& bounds, uint32_t cascade) const
	{
		if (cascade < 2)
			return false;

		const float radius = glm::max(bounds.Radius, 0.001f);
		const float distance = glm::max(glm::distance(m_SceneData.CameraPosition, bounds.Center) - radius, 0.001f);
		const float fovRadians = glm::radians(glm::clamp(m_SceneData.SceneCamera.FOV, 1.0f, 179.0f));
		const float viewportHeight = (float)glm::max(m_OutputViewportHeight, 1u);
		const float pixelsPerRadian = viewportHeight * 0.5f / glm::max(std::tan(fovRadians * 0.5f), 0.001f);
		const float projectedRadiusPixels = (radius / distance) * pixelsPerRadian;
		const float minShadowRadiusPixels = cascade >= 3 ? 1.25f : 0.75f;
		return projectedRadiusPixels < minShadowRadiusPixels;
	}

	uint32_t SceneRenderer::GetDirectionalShadowCascadeMask(const BoundingSphere& bounds) const
	{
		if (m_ShadowCascadeFrustumCount == 0)
			return 0;

		if (!m_Options.EnableShadowCulling)
			return (1u << m_ShadowCascadeFrustumCount) - 1u;

		uint32_t cascadeMask = 0;
		for (uint32_t cascade = 0; cascade < m_ShadowCascadeFrustumCount; cascade++)
		{
			if (!m_ShadowCascadeFrustums[cascade].IsSphereVisible(bounds))
				continue;
			if (ShouldCullTinyDirectionalShadowCaster(bounds, cascade))
				continue;
			cascadeMask |= 1u << cascade;
		}

		return cascadeMask;
	}

	bool SceneRenderer::IsSpotShadowCasterVisible(const BoundingSphere& bounds) const
	{
		if (m_SpotShadowFrustumCount == 0)
			return false;

		if (!m_Options.EnableShadowCulling)
			return true;

		for (uint32_t shadowIndex = 0; shadowIndex < m_SpotShadowFrustumCount; shadowIndex++)
		{
			if (m_SpotShadowFrustums[shadowIndex].IsSphereVisible(bounds))
				return true;
		}

		return false;
	}

	void SceneRenderer::BuildSortedDrawCommandOrder(const DrawCommandList& drawList, DrawCommandOrder& drawOrder, uint64_t& orderCacheHash) const
	{
		// Order-independent fingerprint of the draw-list key set. MeshKey's hash
		// covers every sort input (pipeline/shader/material/mesh sort keys), so
		// an unchanged fingerprint means an unchanged sorted order — reuse last
		// frame's DrawOrder instead of re-sorting each pass every frame.
		uint64_t hashSum = 0;
		uint64_t hashXor = 0;
		for (const auto& [key, dc] : drawList)
		{
			const uint64_t keyHash = (uint64_t)MeshKeyHasher{}(key);
			hashSum += keyHash;
			hashXor ^= keyHash;
		}
		const uint64_t fingerprint = hashSum ^ (hashXor * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)drawList.size() << 48);

		if (fingerprint == orderCacheHash && drawOrder.size() == drawList.size())
			return;
		orderCacheHash = fingerprint;

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
		uint32_t lodIndex,
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
			cachedCommand.Command.LODIndex = lodIndex;
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
			// DrawOrder is intentionally retained: BuildSortedDrawCommandOrder
			// reuses it when the rebuilt DrawList has the same key set (stale
			// keys are harmless — every consumer looks keys up with find()).
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

	bool SceneRenderer::UpdateShadowCasterMotion(uint32_t sceneInstanceIndex, const GPUSceneInstanceData* instanceData)
	{
		if (!instanceData || IsTransientGPUSceneInstanceIndex(sceneInstanceIndex))
			return true;

		uint64_t transformHash = 1469598103934665603ull;
		transformHash = HashVec4(transformHash, instanceData->TransformRows[0]);
		transformHash = HashVec4(transformHash, instanceData->TransformRows[1]);
		transformHash = HashVec4(transformHash, instanceData->TransformRows[2]);

		ShadowCasterMotionState& state = m_ShadowCasterMotion[sceneInstanceIndex];
		if (state.TransformHash != transformHash)
		{
			state.TransformHash = transformHash;
			state.StableFrames = 0;
		}
		else
		{
			state.StableFrames = glm::min(state.StableFrames + 1u, StaticShadowCasterStableFrames);
		}

		state.LastTouchedFrame = m_ShadowMotionFrameIndex;
		return state.StableFrames < StaticShadowCasterStableFrames;
	}

	bool SceneRenderer::IsShadowCasterStatic(uint32_t sceneInstanceIndex) const
	{
		if (IsTransientGPUSceneInstanceIndex(sceneInstanceIndex))
			return false;

		const auto it = m_ShadowCasterMotion.find(sceneInstanceIndex);
		return it != m_ShadowCasterMotion.end() && it->second.StableFrames >= StaticShadowCasterStableFrames;
	}

	void SceneRenderer::CalculateShadowCasterHashes(uint64_t& outStaticDirectionalHash, uint64_t& outDynamicDirectionalHash, uint64_t& outStaticSpotHash, uint64_t& outDynamicSpotHash) const
	{
		outStaticDirectionalHash = 1469598103934665603ull;
		outDynamicDirectionalHash = 1469598103934665603ull;
		outStaticSpotHash = 1469598103934665603ull;
		outDynamicSpotHash = 1469598103934665603ull;
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
			auto hashShadowBucket = [&](uint64_t& hash, const TransformMapData& tmd, uint32_t bucketIndex)
			{
				if (tmd.ObjectIndices.empty())
					return;

				hash = HashCombine(hash, (uint64_t)key.MeshHandle);
				hash = HashCombine(hash, (uint64_t)key.MaterialHandle);
				hash = HashCombine(hash, key.SubmeshIndex);
				hash = HashCombine(hash, key.LODIndex);
				hash = HashCombine(hash, dc.MeshSortKey);
				hash = HashCombine(hash, dc.MaterialSortKey);
				hash = HashCombine(hash, bucketIndex);
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
			};

			uint64_t& directionalHash = key.StaticShadowCaster ? outStaticDirectionalHash : outDynamicDirectionalHash;
			for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
				hashShadowBucket(directionalHash, transformIt->second.Cascades[cascade], cascade);

			uint64_t& spotHash = key.StaticShadowCaster ? outStaticSpotHash : outDynamicSpotHash;
			hashShadowBucket(spotHash, transformIt->second.Spot, 0);
		}
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

			const AssetHandle meshKeyHandle = GetStaticMeshKeyHandle(staticMesh);
			const AssetHandle keyMaterialHandle = resolvedOverrideMaterial
				? AssetHandle((uint64_t)resolvedOverrideMaterial.Raw())
				: materialHandle;
			Ref<Material> sortMaterial = resolvedOverrideMaterial
				? resolvedOverrideMaterial
				: (materialAsset ? materialAsset->GetMaterial() : Renderer::GetDefaultWhiteMaterial());
			const uint64_t meshSortKey = (uint64_t)meshKeyHandle;
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
			const uint32_t directionalShadowCascadeMask = castsShadows ? GetDirectionalShadowCascadeMask(boundsSphere) : 0;
			const bool spotShadowVisible = castsShadows && IsSpotShadowCasterVisible(boundsSphere);
			const bool shadowVisible = directionalShadowCascadeMask != 0 || spotShadowVisible;
			const uint32_t lodIndex = SelectStaticMeshLOD(*meshSource, submeshIndex, boundsSphere);
			const MeshKey materialKey{ meshKeyHandle, keyMaterialHandle, submeshIndex, lodIndex, false, false };
			const MeshKey bindlessMeshKey{ meshKeyHandle, 0, submeshIndex, lodIndex, false, false };
			const MeshKey selectedMeshKey{ meshKeyHandle, 0, submeshIndex, lodIndex, true, false };

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
				const MeshKey mainPassKey = isTransparent ? materialKey : bindlessMeshKey;
				m_MeshTransformMap[mainPassKey].ObjectIndices.push_back(sceneInstanceIndex);

				Ref<Pipeline> geometryPipeline = isTransparent ? m_TransparentGeometryPipeline : m_GeometryPipeline;
				SubmitMeshPassDraw(isTransparent ? MeshPassType::Transparent : MeshPassType::Opaque,
					mainPassKey,
					staticMesh,
					meshSource,
					isTransparent ? materialTable : nullptr,
					submeshIndex,
					lodIndex,
					isTransparent ? materialHandle : AssetHandle(0),
					isTransparent ? resolvedOverrideMaterial : nullptr,
					SortKeyFromRef(geometryPipeline.Raw()),
					isTransparent ? shaderSortKey : 0,
					isTransparent ? materialSortKey : 0,
					meshSortKey);

				if (!isTransparent)
				{
					const uint64_t preDepthShaderSortKey = m_PreDepthMaterial && m_PreDepthMaterial->GetShader()
						? SortKeyFromRef(m_PreDepthMaterial->GetShader().Raw()) : 0;
					SubmitMeshPassDraw(MeshPassType::DepthPrepass,
						bindlessMeshKey,
						staticMesh,
						meshSource,
						nullptr,
						submeshIndex,
						lodIndex,
						AssetHandle(0),
						m_PreDepthMaterial,
						SortKeyFromRef(m_PreDepthPipeline.Raw()),
						preDepthShaderSortKey,
						SortKeyFromRef(m_PreDepthMaterial.Raw()),
						meshSortKey);
				}

				// ── Selected list ─────────────────────────────────────────────────
				if (isSelected)
				{
					m_MeshTransformMap[selectedMeshKey].ObjectIndices.push_back(sceneInstanceIndex);

					const uint64_t selectedShaderSortKey = m_SelectedGeometryMaterial && m_SelectedGeometryMaterial->GetShader()
						? SortKeyFromRef(m_SelectedGeometryMaterial->GetShader().Raw()) : 0;
					SubmitMeshPassDraw(MeshPassType::SelectedMask,
						selectedMeshKey,
						staticMesh,
						meshSource,
						nullptr,
						submeshIndex,
						lodIndex,
						AssetHandle(0),
						m_SelectedGeometryMaterial,
						m_SelectedGeometryPass ? SortKeyFromRef(m_SelectedGeometryPass->GetPipeline().Raw()) : 0,
						selectedShaderSortKey,
						SortKeyFromRef(m_SelectedGeometryMaterial.Raw()),
						meshSortKey);

					const uint64_t wireframeShaderSortKey = m_WireframeMaterial && m_WireframeMaterial->GetShader()
						? SortKeyFromRef(m_WireframeMaterial->GetShader().Raw()) : 0;
					SubmitMeshPassDraw(MeshPassType::Wireframe,
						selectedMeshKey,
						staticMesh,
						meshSource,
						nullptr,
						submeshIndex,
						lodIndex,
						AssetHandle(0),
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
				const bool dynamicShadowCaster = gpuSceneInstance && !IsTransientGPUSceneInstanceIndex(sceneInstanceIndex)
					? UpdateShadowCasterMotion(sceneInstanceIndex, gpuSceneInstance)
					: true;
				const MeshKey shadowKey{ meshKeyHandle, 0, submeshIndex, lodIndex, false, !dynamicShadowCaster };
				ShadowTransformMapData& shadowTransformData = m_ShadowMeshTransformMap[shadowKey];
				for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
				{
					if ((directionalShadowCascadeMask & (1u << cascade)) != 0)
						shadowTransformData.Cascades[cascade].ObjectIndices.push_back(sceneInstanceIndex);
				}
				if (spotShadowVisible)
					shadowTransformData.Spot.ObjectIndices.push_back(sceneInstanceIndex);

				const uint64_t shadowShaderSortKey = m_ShadowPassMaterial && m_ShadowPassMaterial->GetShader()
					? SortKeyFromRef(m_ShadowPassMaterial->GetShader().Raw()) : 0;
				SubmitMeshPassDraw(MeshPassType::ShadowDepth,
					shadowKey,
					staticMesh,
					meshSource,
					nullptr,
					submeshIndex,
					lodIndex,
					AssetHandle(0),
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
			const MeshKey key{ GetStaticMeshKeyHandle(staticMesh), fakeHandle, submeshIndex, 0, false, false };
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
				0,
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
			BuildSortedDrawCommandOrder(pass.DrawList, pass.DrawOrder, pass.OrderCacheHash);

		const GPUScene* submittedGPUScene = m_SubmittedRenderScene ? &m_SubmittedRenderScene->GetGPUScene() : nullptr;
		const std::vector<GPUSceneInstanceData>* persistentGPUSceneInstances = submittedGPUScene ? &submittedGPUScene->GetInstances() : nullptr;
		const uint32_t persistentGPUSceneInstanceCount = persistentGPUSceneInstances ? (uint32_t)persistentGPUSceneInstances->size() : 0;
		const MaterialScene* submittedMaterialScene = m_SubmittedRenderScene ? &m_SubmittedRenderScene->GetMaterialScene() : nullptr;
		const TextureScene* submittedTextureScene = m_SubmittedRenderScene ? &m_SubmittedRenderScene->GetTextureScene() : nullptr;

		std::vector<AssetHandle>& gpuTextureHandles = m_ScratchTextureHandles;
		// Skip re-copying the whole texture table when the same TextureScene is
		// submitted with an unchanged version — the scratch already holds the
		// persistent rows (plus last frame's transients, truncated below).
		const bool textureTableChanged = !submittedTextureScene
			|| (const void*)submittedTextureScene != m_ScratchTextureSceneKey
			|| submittedTextureScene->GetVersion() != m_ScratchTextureSceneVersion;
		if (textureTableChanged)
		{
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

			m_ScratchTextureSceneKey = (const void*)submittedTextureScene;
			m_ScratchTextureSceneVersion = submittedTextureScene
				? submittedTextureScene->GetVersion()
				: std::numeric_limits<uint64_t>::max();
			m_ScratchPersistentTextureCount = (uint32_t)gpuTextureHandles.size();
		}
		else
		{
			// Unchanged table: just drop the transient handles appended last frame.
			gpuTextureHandles.resize(m_ScratchPersistentTextureCount);
		}
		const uint32_t persistentTextureCount = m_ScratchPersistentTextureCount;
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

		// Resolving all bindless slots costs an asset-manager lookup per slot per
		// frame. Steady state re-resolves only: slots still waiting on a
		// streaming texture (pending list), the per-frame transient region, and
		// a periodic full sweep as a safety net for asset hot-reloads that swap
		// a texture's contents without touching the table version.
		constexpr uint32_t TextureResolveSweepInterval = 32;
		const bool fullTextureResolve = textureTableChanged || m_TextureResolveSweepCountdown == 0;
		if (fullTextureResolve)
			m_TextureResolveSweepCountdown = TextureResolveSweepInterval;
		else
			m_TextureResolveSweepCountdown--;

		uint32_t missingTextureDescriptorCount = 0;
		if (m_GPUMaterialTextures.empty())
			m_GPUMaterialTextures.assign(MaxGPUTextureSceneTextures, Renderer::GetWhiteTexture());

		m_PendingTextureResolveScratch.swap(m_PendingTextureResolveSlots);
		m_PendingTextureResolveSlots.clear();

		auto resolveSlot = [&](uint32_t textureIndex)
		{
			const AssetHandle textureHandle = textureIndex < gpuTextureHandles.size() ? gpuTextureHandles[textureIndex] : AssetHandle(0);
			Ref<Texture2D> texture = resolveMaterialTexture(textureHandle);
			if (textureHandle && texture.Raw() == Renderer::GetWhiteTexture().Raw())
			{
				missingTextureDescriptorCount++;
				m_PendingTextureResolveSlots.push_back(textureIndex); // still streaming — retry next frame
			}

			if (m_GPUMaterialTextures[textureIndex].Raw() == texture.Raw())
				return;

			m_GPUMaterialTextures[textureIndex] = texture;
			if (m_GeometryPass && m_GeometryPass->IsInputValid("u_GPUMaterialTextures"))
				m_GeometryPass->SetInput("u_GPUMaterialTextures", texture, textureIndex);
			if (m_GeometryPassTransparent && m_GeometryPassTransparent->IsInputValid("u_GPUMaterialTextures"))
				m_GeometryPassTransparent->SetInput("u_GPUMaterialTextures", texture, textureIndex);
			if (m_DeferredLightingPass && m_DeferredLightingPass->IsInputValid("u_GPUMaterialTextures"))
				m_DeferredLightingPass->SetInput("u_GPUMaterialTextures", texture, textureIndex);
			if (m_GBufferDebugPass && m_GBufferDebugPass->IsInputValid("u_GPUMaterialTextures"))
				m_GBufferDebugPass->SetInput("u_GPUMaterialTextures", texture, textureIndex);
		};

		if (fullTextureResolve)
		{
			for (uint32_t textureIndex = 0; textureIndex < MaxGPUTextureSceneTextures; textureIndex++)
				resolveSlot(textureIndex);
			m_MissingTextureDescriptorCount = missingTextureDescriptorCount;
		}
		else
		{
			// Persistent pending slots (transient-region entries are re-added by
			// the transient loop below, avoiding duplicates in the pending list).
			for (uint32_t textureIndex : m_PendingTextureResolveScratch)
			{
				if (textureIndex < persistentTextureCount)
					resolveSlot(textureIndex);
			}

			// Transient slots change every frame; always resolve their region.
			const uint32_t transientEnd = glm::min((uint32_t)gpuTextureHandles.size(), MaxGPUTextureSceneTextures);
			for (uint32_t textureIndex = persistentTextureCount; textureIndex < transientEnd; textureIndex++)
				resolveSlot(textureIndex);

			// The missing-slot statistic refreshes on full sweeps.
			missingTextureDescriptorCount = m_MissingTextureDescriptorCount;
		}

		std::vector<GPUMaterialData>& gpuMaterialData = m_ScratchMaterialData;
		// Same version gate as the texture table. Unlike the texture scratch,
		// nothing is appended to this one, so an unchanged frame skips the copy
		// entirely and the scratch contents carry over bit-identical.
		const bool materialTableChanged = !submittedMaterialScene
			|| (const void*)submittedMaterialScene != m_ScratchMaterialSceneKey
			|| submittedMaterialScene->GetVersion() != m_ScratchMaterialSceneVersion;
		if (materialTableChanged)
		{
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

			m_ScratchMaterialSceneKey = (const void*)submittedMaterialScene;
			m_ScratchMaterialSceneVersion = submittedMaterialScene
				? submittedMaterialScene->GetVersion()
				: std::numeric_limits<uint64_t>::max();
		}
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
			auto uploadShadowBucket = [&](TransformMapData& tmd)
			{
				tmd.ObjectIndexBase = cursor;
				for (uint32_t idx : tmd.ObjectIndices)
					objectIndexData.push_back(resolveGPUSceneBufferIndex(idx));
				cursor += (uint32_t)tmd.ObjectIndices.size();
			};

			for (TransformMapData& cascadeTmd : shadowTmd.Cascades)
				uploadShadowBucket(cascadeTmd);
			uploadShadowBucket(shadowTmd.Spot);
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

		uint64_t staticDirectionalShadowCasterHash = 0;
		uint64_t dynamicDirectionalShadowCasterHash = 0;
		uint64_t staticSpotShadowCasterHash = 0;
		uint64_t dynamicSpotShadowCasterHash = 0;
		CalculateShadowCasterHashes(staticDirectionalShadowCasterHash, dynamicDirectionalShadowCasterHash, staticSpotShadowCasterHash, dynamicSpotShadowCasterHash);

		for (auto it = m_ShadowCasterMotion.begin(); it != m_ShadowCasterMotion.end();)
		{
			if (m_ShadowMotionFrameIndex - it->second.LastTouchedFrame > Renderer::GetConfig().FramesInFlight + StaticShadowCasterStableFrames)
				it = m_ShadowCasterMotion.erase(it);
			else
				++it;
		}

		const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
		const bool directionalShadowsEnabled = dirLight.Intensity > 0.0f && dirLight.CastShadows;
		const bool staticDirectionalShadowCastersChanged = staticDirectionalShadowCasterHash != m_LastStaticShadowCasterHash;
		const bool dynamicDirectionalShadowCastersChanged = dynamicDirectionalShadowCasterHash != m_LastDynamicShadowCasterHash;
		const bool staticSpotShadowCastersChanged = staticSpotShadowCasterHash != m_LastStaticSpotShadowCasterHash;
		const bool dynamicSpotShadowCastersChanged = dynamicSpotShadowCasterHash != m_LastDynamicSpotShadowCasterHash;
		if (staticDirectionalShadowCastersChanged)
			m_StaticShadowMapCacheValid = false;
		if (staticSpotShadowCastersChanged)
			m_StaticSpotShadowMapCacheValid = false;
		if (directionalShadowsEnabled
			&& (!m_DirectionalShadowMapCacheValid
				|| !m_StaticShadowMapCacheValid
				|| dynamicDirectionalShadowCastersChanged))
		{
			m_DirectionalShadowMapNeedsRender = true;
		}
		if (m_SpotShadowCount > 0
			&& (!m_SpotShadowMapCacheValid
				|| !m_StaticSpotShadowMapCacheValid
				|| staticSpotShadowCastersChanged
				|| dynamicSpotShadowCastersChanged))
		{
			m_SpotShadowMapNeedsRender = true;
		}
		m_LastStaticShadowCasterHash = staticDirectionalShadowCasterHash;
		m_LastDynamicShadowCasterHash = dynamicDirectionalShadowCasterHash;
		m_LastStaticSpotShadowCasterHash = staticSpotShadowCasterHash;
		m_LastDynamicSpotShadowCasterHash = dynamicSpotShadowCasterHash;

		// ── 2. Upload GPUScene tails, ObjectIndexes, culling data, and indirect args
		m_UploadCommandBuffer->Begin();

		// Persistent GPUScene rows: upload only the rows dirtied by recent syncs
		// instead of the full instance array every frame. A dirty range must be
		// written once per frame-in-flight buffer, so each sync's ranges are
		// queued as an "epoch" replayed FramesInFlight times with fresh row data
		// (newer content in an older slot is still correct — everything converges
		// to latest). Full uploads run on scene switch / instance-count change /
		// buffer growth (high-water tracked, so a mid-steady-state Resize is
		// impossible), and adaptively when the dirty volume exceeds a full array.
		std::vector<GPUSceneInstanceData> gpuSceneInstanceData;
		std::vector<GPUSceneDirtyRange> gpuSceneRangeList;
		std::vector<GPUSceneInstanceData> gpuSceneRangeRows;
		if (submittedGPUScene)
		{
			const std::vector<GPUSceneInstanceData>& instances = submittedGPUScene->GetInstances();

			const uint32_t totalInstancesThisFrame = persistentGPUSceneInstanceCount + (uint32_t)m_TransientGPUSceneInstances.size();
			const bool sceneChanged = (const void*)submittedGPUScene != m_LastGPUSceneKey
				|| persistentGPUSceneInstanceCount != m_LastGPUSceneInstanceCount
				|| totalInstancesThisFrame > m_GPUSceneMaxTotalInstancesSeen;
			if (sceneChanged)
			{
				m_GPUSceneFullUploadsRemaining = glm::max(m_GPUSceneFullUploadsRemaining, Renderer::GetConfig().FramesInFlight);
				m_LastGPUSceneKey = (const void*)submittedGPUScene;
				m_LastGPUSceneInstanceCount = persistentGPUSceneInstanceCount;
				m_GPUSceneMaxTotalInstancesSeen = glm::max(m_GPUSceneMaxTotalInstancesSeen, totalInstancesThisFrame);
				m_PendingGPUSceneRangeUploads.clear();
			}

			if (m_GPUSceneFullUploadsRemaining == 0)
			{
				const std::vector<GPUSceneDirtyRange>& dirtyRanges = submittedGPUScene->GetDirtyRanges();
				if (!dirtyRanges.empty())
				{
					GPUSceneRangeUploadEpoch& epoch = m_PendingGPUSceneRangeUploads.emplace_back();
					epoch.RemainingUploads = Renderer::GetConfig().FramesInFlight;
					epoch.Ranges.assign(dirtyRanges.begin(), dirtyRanges.end());
				}

				// Flatten all pending epochs into one range list + row payload
				// (rows re-copied from the current arrays: freshest data wins).
				size_t pendingRows = 0;
				for (const GPUSceneRangeUploadEpoch& epoch : m_PendingGPUSceneRangeUploads)
					for (const GPUSceneDirtyRange& range : epoch.Ranges)
						pendingRows += range.InstanceCount;

				if (pendingRows >= instances.size() && !instances.empty())
				{
					// Cheaper to re-upload everything.
					m_GPUSceneFullUploadsRemaining = Renderer::GetConfig().FramesInFlight;
					m_PendingGPUSceneRangeUploads.clear();
				}
				else if (pendingRows > 0)
				{
					gpuSceneRangeRows.reserve(pendingRows);
					for (GPUSceneRangeUploadEpoch& epoch : m_PendingGPUSceneRangeUploads)
					{
						for (const GPUSceneDirtyRange& range : epoch.Ranges)
						{
							const uint32_t first = glm::min(range.FirstInstance, (uint32_t)instances.size());
							const uint32_t count = glm::min(range.InstanceCount, (uint32_t)instances.size() - first);
							if (count == 0)
								continue;

							gpuSceneRangeList.push_back({ first, count });
							gpuSceneRangeRows.insert(gpuSceneRangeRows.end(), instances.begin() + first, instances.begin() + first + count);
						}
						epoch.RemainingUploads--;
					}
					std::erase_if(m_PendingGPUSceneRangeUploads, [](const GPUSceneRangeUploadEpoch& epoch) { return epoch.RemainingUploads == 0; });
				}
			}

			if (m_GPUSceneFullUploadsRemaining > 0)
			{
				m_GPUSceneFullUploadsRemaining--;
				m_PendingGPUSceneRangeUploads.clear();
				gpuSceneInstanceData = instances;
			}
		}

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

		// Built only when the Renderer Debugger panel asked for it this frame —
		// the validation loops below are O(instances + materials) CPU work that
		// exists purely to populate that panel.
		if (m_GPUSceneDebugSnapshotRequested)
		{
			m_GPUSceneDebugSnapshotRequested = false;

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

			// Validate against the GPUScene source array directly — the local
			// full-copy vector is empty on dirty-range-only frames.
			if (persistentGPUSceneInstances)
			{
				for (uint32_t instanceIndex = 0; instanceIndex < (uint32_t)persistentGPUSceneInstances->size(); instanceIndex++)
					validateInstance((*persistentGPUSceneInstances)[instanceIndex], instanceIndex, true);
			}

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
			|| !gpuSceneRangeRows.empty()
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
				sceneRangeList = std::move(gpuSceneRangeList),
				sceneRangeRows = std::move(gpuSceneRangeRows),
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

				// Full persistent upload (startup / scene switch / count change /
				// dirty volume exceeding a full array), repeated once per
				// frame-in-flight buffer by the main-thread counter.
				if (!gpuSceneData.empty())
				{
					const uint32_t uploadBytes = (uint32_t)(gpuSceneData.size() * sizeof(GPUSceneInstanceData));
					instance->m_SBSGPUSceneInstances->RT_Get()->RT_SetData(cmd, gpuSceneData.data(), uploadBytes);
				}

				// Steady state: write only the dirty ranges (flattened epoch
				// payload; each sync's ranges replay once per frame in flight).
				if (!sceneRangeRows.empty())
				{
					size_t sourceRow = 0;
					for (const GPUSceneDirtyRange& range : sceneRangeList)
					{
						const uint32_t uploadBytes = range.InstanceCount * (uint32_t)sizeof(GPUSceneInstanceData);
						const uint32_t uploadOffset = range.FirstInstance * (uint32_t)sizeof(GPUSceneInstanceData);
						instance->m_SBSGPUSceneInstances->RT_Get()->RT_SetData(cmd, sceneRangeRows.data() + sourceRow, uploadBytes, uploadOffset);
						sourceRow += range.InstanceCount;
					}
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

		// ── 2b. Async compute (cluster build + light culling) ─────────────────
		// Depth-independent and dependent only on the camera/light UBOs uploaded
		// above, so they run on the compute queue. Their SSBO outputs feed deferred
		// lighting on the graphics queue; the graphics submit below waits on this
		// compute submission (QueueWaitForCommandList) so the reads are safe.
		// Correctness-first: the graphics queue waits up front, so this does not yet
		// overlap graphics work — that split comes later. Gated + off by default.
		const bool asyncCompute = m_Options.EnableAsyncCompute && m_ComputeCommandBuffer;
		if (asyncCompute)
		{
			m_ComputeCommandBuffer->Begin();
			ClusterBuildPass();
			ClusterLightCullingPass();
			m_ComputeCommandBuffer->End();
			m_ComputeCommandBuffer->Submit();
		}

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

		// Make the graphics submit wait for the async compute cluster work so
		// deferred lighting reads valid light grids/index lists. Enqueued on the
		// render thread before the graphics submit; reads the compute execution
		// instance at that point (it was set when the compute buffer submitted above).
		if (asyncCompute)
		{
			Ref<RenderCommandBuffer> computeCB = m_ComputeCommandBuffer;
			Renderer::Submit([computeCB]()
			{
				Renderer::QueueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Compute, computeCB->GetLastExecutionInstance());
			});
		}

		m_CommandBuffer->Submit();

		m_PreviousViewProjection = m_CurrentViewProjection;
		m_PreviousJitter = m_CurrentJitter;

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
			for (auto& shadowMapPass : m_ShadowMapStaticCachePasses)
			{
				Renderer::BeginRenderPass(m_CommandBuffer, shadowMapPass, /*explicitClear=*/true);
				Renderer::EndRenderPass(m_CommandBuffer);
			}
			m_DirectionalShadowMapCacheValid = true;
			m_StaticShadowMapCacheValid = true;
			m_DirectionalShadowMapNeedsRender = false;
			return;
		}

		if (m_DirectionalShadowMapCacheValid && !m_DirectionalShadowMapNeedsRender)
			return;

		BeginProfiledGPU("ShadowMapPass");
		const MeshPassState& shadowPass = GetMeshPass(MeshPassType::ShadowDepth);
		const uint32_t activeShadowCascadeCount = SanitizeActiveShadowCascadeCount(m_Options.ActiveShadowCascadeCount);

		auto hasShadowCasters = [&](bool staticCasters)
		{
			for (const MeshKey& key : shadowPass.DrawOrder)
			{
				if (key.StaticShadowCaster != staticCasters)
					continue;

				const auto transformIt = m_ShadowMeshTransformMap.find(key);
				if (transformIt == m_ShadowMeshTransformMap.end())
					continue;

				for (uint32_t cascade = 0; cascade < activeShadowCascadeCount; cascade++)
				{
					if (!transformIt->second.Cascades[cascade].ObjectIndices.empty())
						return true;
				}
			}
			return false;
		};

		auto hasShadowCastersForCascade = [&](uint32_t cascade, bool staticCasters)
		{
			for (const MeshKey& key : shadowPass.DrawOrder)
			{
				if (key.StaticShadowCaster != staticCasters)
					continue;

				const auto transformIt = m_ShadowMeshTransformMap.find(key);
				if (transformIt != m_ShadowMeshTransformMap.end() && !transformIt->second.Cascades[cascade].ObjectIndices.empty())
					return true;
			}
			return false;
		};

		auto drawShadowCasters = [&](uint32_t cascade, bool staticCasters)
		{
			bool drewCaster = false;

			for (const MeshKey& key : shadowPass.DrawOrder)
			{
				if (key.StaticShadowCaster != staticCasters)
					continue;

				const auto drawIt = shadowPass.DrawList.find(key);
				if (drawIt == shadowPass.DrawList.end()) continue;
				auto it = m_ShadowMeshTransformMap.find(key);
				if (it == m_ShadowMeshTransformMap.end()) continue;

				const auto& cascadeTmd = it->second.Cascades[cascade];
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
				drewCaster = true;
			}

			return drewCaster;
		};

		if (!m_StaticShadowMapCacheValid)
		{
			for (uint32_t cascade = 0; cascade < activeShadowCascadeCount; cascade++)
			{
				Renderer::BeginRenderPass(m_CommandBuffer, m_ShadowMapStaticCachePasses[cascade], /*explicitClear=*/true);
				drawShadowCasters(cascade, true);
				Renderer::EndRenderPass(m_CommandBuffer);
			}
			m_StaticShadowMapCacheValid = true;
		}

		const bool hasDynamicCasters = hasShadowCasters(false);
		Renderer::CopyImage(m_CommandBuffer, m_ShadowMapStaticCacheImage, m_ShadowMapImage);

		if (hasDynamicCasters)
		{
			for (uint32_t cascade = 0; cascade < activeShadowCascadeCount; cascade++)
			{
				if (!hasShadowCastersForCascade(cascade, false))
					continue;
				Renderer::BeginRenderPass(m_CommandBuffer, m_ShadowMapDynamicPasses[cascade], /*explicitClear=*/false);
				drawShadowCasters(cascade, false);
				Renderer::EndRenderPass(m_CommandBuffer);
			}
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
			Renderer::BeginRenderPass(m_CommandBuffer, m_SpotShadowStaticCachePass, /*explicitClear=*/true);
			Renderer::EndRenderPass(m_CommandBuffer);
			m_SpotShadowMapCacheValid = true;
			m_StaticSpotShadowMapCacheValid = true;
			m_SpotShadowMapNeedsRender = false;
			return;
		}

		if (m_SpotShadowMapCacheValid && !m_SpotShadowMapNeedsRender)
			return;

		BeginProfiledGPU("SpotShadowMapPass");

		const uint32_t tilesPerRow = m_SpotShadowAtlasGridSize;
		const uint32_t tileSize = m_SpotShadowTileSize;
		const uint32_t atlasSize = m_SpotShadowMapSize;
		const MeshPassState& shadowPass = GetMeshPass(MeshPassType::ShadowDepth);

		auto hasShadowCasters = [&](bool staticCasters)
		{
			for (const MeshKey& key : shadowPass.DrawOrder)
			{
				if (key.StaticShadowCaster != staticCasters)
					continue;

				const auto transformIt = m_ShadowMeshTransformMap.find(key);
				if (transformIt != m_ShadowMeshTransformMap.end() && !transformIt->second.Spot.ObjectIndices.empty())
					return true;
			}
			return false;
		};

		auto drawSpotShadowCasters = [&](uint32_t shadowIndex, bool staticCasters)
		{
			for (const MeshKey& key : shadowPass.DrawOrder)
			{
				if (key.StaticShadowCaster != staticCasters)
					continue;

				const auto drawIt = shadowPass.DrawList.find(key);
				if (drawIt == shadowPass.DrawList.end()) continue;
				auto it = m_ShadowMeshTransformMap.find(key);
				if (it == m_ShadowMeshTransformMap.end()) continue;
				const auto& spotTmd = it->second.Spot;
				const uint32_t instCount = (uint32_t)spotTmd.ObjectIndices.size();
				if (instCount == 0) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				drawCmd.InstanceCount = instCount;

				const MeshDrawParams params(spotTmd);
				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, params, shadowIndex]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/false, shadowIndex);
					});
			}
		};

		auto drawSpotAtlas = [&](bool staticCasters)
		{
			for (uint32_t shadowIndex = 0; shadowIndex < m_SpotShadowCount; shadowIndex++)
			{
				const uint32_t tileX = shadowIndex % tilesPerRow;
				const uint32_t tileY = shadowIndex / tilesPerRow;
				Renderer::SetViewport(m_CommandBuffer, tileX * tileSize, tileY * tileSize, tileSize, tileSize);
				drawSpotShadowCasters(shadowIndex, staticCasters);
			}

			Renderer::SetViewport(m_CommandBuffer, 0, 0, atlasSize, atlasSize);
		};

		if (!m_StaticSpotShadowMapCacheValid)
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_SpotShadowStaticCachePass, /*explicitClear=*/true);
			drawSpotAtlas(true);
			Renderer::EndRenderPass(m_CommandBuffer);
			m_StaticSpotShadowMapCacheValid = true;
		}

		const bool hasDynamicCasters = hasShadowCasters(false);
		Renderer::CopyImage(m_CommandBuffer, m_SpotShadowStaticCacheImage, m_SpotShadowMapImage);

		if (hasDynamicCasters)
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_SpotShadowDynamicPass, /*explicitClear=*/false);
			drawSpotAtlas(false);
			Renderer::EndRenderPass(m_CommandBuffer);
		}

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
		m_SpotShadowMapCacheValid = true;
		m_SpotShadowMapNeedsRender = false;
	}

	void SceneRenderer::PreDepthPass()
	{
		if (m_Options.EnableMeshShaders && m_PreDepthMeshletPass && m_PreDepthMeshletPipeline && m_PreDepthMeshletPipeline->IsMeshletPipeline())
		{
			PreDepthMeshletPass();
			return;
		}

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

	void SceneRenderer::PreDepthMeshletPass()
	{
		ScopedCPUProfile cpuProfile(*this, "PreDepthPass");
		BeginProfiledGPU("PreDepthPass");

		Ref<SceneRenderer> instance = this;

		// Pass begin: marker, explicit depth clear, descriptor prepare. The
		// per-draw meshlet state carries the framebuffer/viewport, so there is no
		// graphics-state commit here (this is not a vertex-pipeline pass).
		Renderer::Submit([instance]() mutable {
			Ref<RenderCommandBuffer> cmd = instance->m_CommandBuffer;
			cmd->RT_BeginMarker("PreDepthMeshletPass");

			Ref<Framebuffer> framebuffer = instance->m_PreDepthMeshletPipeline->GetSpecification().TargetFramebuffer;
			if (framebuffer->HasDepthAttachment())
			{
				const auto& clearValues = framebuffer->GetClearValues();
				const auto& depthStencil = clearValues[clearValues.size() - 1].DepthStencil;
				nvrhi::utils::ClearDepthStencilAttachment(cmd->GetActive(), framebuffer->GetHandle(), depthStencil.Depth, depthStencil.Stencil);
			}

			instance->m_PreDepthMeshletPass->Prepare();
		});

		const MeshPassState& depthPass = GetMeshPass(MeshPassType::DepthPrepass);
		for (const MeshKey& key : depthPass.DrawOrder)
		{
			const auto drawIt = depthPass.DrawList.find(key);
			if (drawIt == depthPass.DrawList.end()) continue;
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			const MeshDrawParams params(it->second);
			StaticDrawCommand drawCmd = drawIt->second;

			Renderer::Submit([instance, drawCmd, params]() mutable {
				instance->RT_DrawStaticMeshMeshlets(instance->m_CommandBuffer, drawCmd, params);
				});
		}

		Renderer::Submit([instance]() mutable {
			instance->m_CommandBuffer->RT_EndMarker();
		});
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::RT_DrawStaticMeshMeshlets(Ref<RenderCommandBuffer> cmd, const StaticDrawCommand& dc, MeshDrawParams params)
	{
		Ref<MeshSource> meshSource = dc.MeshSource;
		if (!meshSource || dc.SubmeshIndex >= meshSource->GetSubmeshes().size())
			return;
		if (dc.InstanceCount == 0)
			return;

		const SubmeshLOD lod = meshSource->GetSubmeshLOD(dc.SubmeshIndex, dc.LODIndex);
		if (lod.MeshletCount == 0 || !meshSource->HasMeshlets())
			return; // still streaming, or built before meshlet support was known

		Ref<Pipeline> pipeline = m_PreDepthMeshletPipeline;
		Ref<VulkanShader> shader = Ref<VulkanShader>(pipeline->GetShader());
		const auto& bindingLayouts = shader->GetAllDescriptorSetLayouts();
		if (bindingLayouts.empty())
			return;

		nvrhi::BindingSetHandle meshletBindingSet = meshSource->RT_GetOrCreateMeshletBindingSet(bindingLayouts[0]);
		if (!meshletBindingSet)
			return;

		Ref<Framebuffer> framebuffer = pipeline->GetSpecification().TargetFramebuffer;

		nvrhi::MeshletState meshletState;
		meshletState.pipeline = pipeline->GetMeshletHandle();
		meshletState.framebuffer = framebuffer->GetHandle();
		meshletState.viewport.addViewport(nvrhi::Viewport((float)framebuffer->GetWidth(), (float)framebuffer->GetHeight()));
		meshletState.viewport.addScissorRect(nvrhi::Rect((int)framebuffer->GetWidth(), (int)framebuffer->GetHeight()));
		meshletState.bindings = m_PreDepthMeshletPass->GetBindingSets(Renderer::RT_GetCurrentFrameIndex());
		if (meshletState.bindings.empty())
			meshletState.bindings.resize(1);
		meshletState.bindings[0] = meshletBindingSet;

		cmd->GetActive()->setMeshletState(meshletState);

		struct MeshletPushConstants
		{
			uint32_t ObjectIndexBase = 0;
			uint32_t MeshletOffset = 0;
			uint32_t MeshletCount = 0;
			uint32_t _pad0 = 0;
		} pushConstants;
		pushConstants.ObjectIndexBase = params.ObjectIndexBase;
		pushConstants.MeshletOffset = lod.MeshletOffset;
		pushConstants.MeshletCount = lod.MeshletCount;
		cmd->GetActive()->setPushConstants(&pushConstants, sizeof(pushConstants));

		// One task workgroup culls 32 meshlets; Y = instance index.
		cmd->GetActive()->dispatchMesh(DivideRoundUp(lod.MeshletCount, 32u), dc.InstanceCount, 1);
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

		// The cluster AABBs are consumed only by the light-culling dispatch, which
		// is skipped when there are no local lights (it zero-fills the grids
		// instead) — so with no lights the froxel rebuild has no consumer either.
		// The grid is rebuilt every frame while lights exist, so lights appearing
		// next frame regenerate it before it is read.
		if (m_PointLightsUB.Count == 0 && m_SpotLightsUB.Count == 0)
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

		// When async, this records onto the compute-queue command buffer (submitted
		// separately in FlushDrawList); the GPU perf markers/timer queries target the
		// graphics command buffer, so skip them on the async path.
		const bool async = m_Options.EnableAsyncCompute;
		Ref<RenderCommandBuffer> cb = async ? m_ComputeCommandBuffer : m_CommandBuffer;

		if (!async) BeginProfiledGPU("ClusterBuildPass");
		Renderer::BeginComputePass(cb, m_ClusterBuildPass);
		Renderer::DispatchCompute(cb, m_ClusterBuildPass, nullptr, groups, Buffer(&push, sizeof(push)));
		m_ClusterBuildPass->GetPipeline()->BufferMemoryBarrier(cb, m_SBSClusterAABBs->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndComputePass(cb, m_ClusterBuildPass);
		if (!async) EndProfiledGPU();
	}

	void SceneRenderer::ClusterLightCullingPass()
	{
		ScopedCPUProfile cpuProfile(*this, "ClusterLightCullingPass");
		if (!m_ClusterLightCullingPass || m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		// When async, record onto the compute-queue command buffer (see ClusterBuildPass).
		const bool async = m_Options.EnableAsyncCompute;
		Ref<RenderCommandBuffer> cb = async ? m_ComputeCommandBuffer : m_CommandBuffer;

		// With no local lights, skip the cull dispatch entirely: zero-fill the
		// per-cluster grids so the lighting shaders read count=0 everywhere. The
		// index lists need no clear — nothing reads past a zero count.
		if (m_PointLightsUB.Count == 0 && m_SpotLightsUB.Count == 0)
		{
			Ref<RenderCommandBuffer> commandBuffer = cb;
			Ref<StorageBufferSet> pointGrid = m_SBSPointLightGrid;
			Ref<StorageBufferSet> spotGrid = m_SBSSpotLightGrid;
			Ref<StorageBufferSet> counter = m_SBSClusterLightCounter;
			Renderer::Submit([commandBuffer, pointGrid, spotGrid, counter]() mutable
			{
				commandBuffer->GetActive()->clearBufferUInt(pointGrid->RT_Get()->GetHandle(), 0u);
				commandBuffer->GetActive()->clearBufferUInt(spotGrid->RT_Get()->GetHandle(), 0u);
				commandBuffer->GetActive()->clearBufferUInt(counter->RT_Get()->GetHandle(), 0u);
			});
			return;
		}

		// Reset the dynamic-allocation cursors ([0]=point, [1]=spot) before the
		// assignment dispatch atomically appends into the packed index lists.
		Ref<RenderCommandBuffer> commandBuffer = cb;
		Ref<StorageBufferSet> counter = m_SBSClusterLightCounter;
		Renderer::Submit([commandBuffer, counter]() mutable
		{
			commandBuffer->GetActive()->clearBufferUInt(counter->RT_Get()->GetHandle(), 0u);
		});

		constexpr uint32_t kThreadsPerGroup = 64;
		const glm::uvec3 groups = { (ClusterCount + kThreadsPerGroup - 1u) / kThreadsPerGroup, 1u, 1u };

		if (!async) BeginProfiledGPU("ClusterLightCullingPass");
		Renderer::BeginComputePass(cb, m_ClusterLightCullingPass);
		Renderer::DispatchCompute(cb, m_ClusterLightCullingPass, nullptr, groups, Buffer());

		Ref<PipelineCompute> pipeline = m_ClusterLightCullingPass->GetPipeline();
		pipeline->BufferMemoryBarrier(cb, m_SBSPointLightGrid->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		pipeline->BufferMemoryBarrier(cb, m_SBSSpotLightGrid->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		pipeline->BufferMemoryBarrier(cb, m_SBSPointLightIndexList->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		pipeline->BufferMemoryBarrier(cb, m_SBSSpotLightIndexList->Get(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndComputePass(cb, m_ClusterLightCullingPass);
		if (!async) EndProfiledGPU();
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
		const uint32_t hzbMipCount = m_HierarchicalDepthTexture.Texture ? m_HierarchicalDepthTexture.Texture->GetMipLevelCount() : 0u;
		const bool hzbOcclusionReady = m_Options.EnableOcclusionCulling && m_HZBPrimed && hzbMipCount > 1;
		pushConstants.OcclusionCullingEnabled = hzbOcclusionReady ? 1u : 0u;
		pushConstants.NumDepthMips = hzbOcclusionReady ? hzbMipCount : 0u;
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
		ApplyCoarseFragmentShadingRate();
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
		ApplyCoarseFragmentShadingRate();
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_VolumetricCloudPass->GetPipeline(), m_VolumetricCloudMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::VolumetricCloudCompositePass()
	{
		ScopedCPUProfile cpuProfile(*this, "VolumetricCloudCompositePass");
		if (!m_VolumetricCloudCompositePass || !m_VolumetricCloudCompositeMaterial || !m_VolumetricCloudPass || !m_FrameEnvironment.VolumetricCloudsEnabled)
			return;

		// Always the raw half-res raymarch. The temporally-accumulated variant is gone, so
		// clouds are softer but cannot trail behind camera or cloud motion.
		Ref<Image2D> cloudColor = m_VolumetricCloudPass->GetOutput(0);
		SetRenderPassInputIfValid(m_VolumetricCloudCompositePass, "u_CloudTexture", cloudColor);
		SetRenderPassInputIfValid(m_VolumetricCloudCompositePass, "u_CloudDepthTexture", m_VolumetricCloudPass->GetOutput(1));

		BeginProfiledGPU("VolumetricCloudCompositePass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_VolumetricCloudCompositePass);
		ApplyCoarseFragmentShadingRate();
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
		ApplyCoarseFragmentShadingRate();
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_AtmosphericFogPass->GetPipeline(), m_AtmosphericFogMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SelectedGeometryPass()
	{
		ScopedCPUProfile cpuProfile(*this, "SelectedGeometryPass");
		if (!m_SelectedGeometryPass) // editor-only target not created (runtime)
			return;

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
					instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/false, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering);
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
					instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/false, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering);
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
					instance->m_CommandBuffer, drawCmd, params, /*bindMaterial=*/false, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering);
				});
		}

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GeometryWireframePass()
	{
		ScopedCPUProfile cpuProfile(*this, "GeometryWireframePass");
		if (!m_GeometryWireframePass) // editor-only target not created (runtime)
			return;

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
			if (m_AOCompositePass && m_AOCompositePass->IsInputValid("u_GTAOTex"))
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
		if (m_AOCompositePass && m_AOCompositePass->IsInputValid("u_GTAOTex"))
		{
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
		}
		if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
			m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::RebakeAOPassInputs()
	{
		// The GTAO-on variant newly declares Camera (set 2), the samplers, and the
		// GTAO textures. Rebind the full set and rebake so the pass validates. No-ops
		// until the recompiled variant is live (Camera declared), so it's safe to call
		// while the shader reload is still pending — it just retries next frame.
		auto rebind = [&](Ref<RenderPass>& pass) -> bool
		{
			if (!pass || !pass->IsInputValid("Camera"))
				return false;

			pass->SetInput("Camera", m_UBSCamera);
			pass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			pass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			pass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			if (pass->IsInputValid("u_GTAOTex"))
			{
				pass->SetInput("u_GTAOTex", m_GTAOFinalImage);
				pass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
				pass->SetInput("u_Normal", GetGeometryNormalOutput());
			}
			pass->Bake();
			return true;
		};

		const bool compositeReady = rebind(m_AOCompositePass);
		rebind(m_AODebugPass);

		// Clear only once the composite pass's new variant is live; otherwise leave the
		// flag set and retry next frame (the shader reload may not have landed yet).
		if (compositeReady)
			m_AOPassInputsDirty = false;
	}

	void SceneRenderer::AOComposite()
	{
		ScopedCPUProfile cpuProfile(*this, "AOComposite");
		if (!m_AOCompositePass || !m_AOCompositeMaterial || !m_GTAOFinalImage)
			return;

		if (m_AOPassInputsDirty)
			RebakeAOPassInputs();

		if (m_AOCompositePass->IsInputValid("u_GTAOTex"))
		{
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_AOCompositePass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_AOCompositePass->SetInput("u_Normal", GetGeometryNormalOutput());
		}

		// Tell the shader which byte the visibility term lives in (high byte for
		// bent normals, low byte otherwise). Sourced from the same option that
		// drives the GTAO producer's packing so the decode can't desync.
		if (m_Options.EnableGTAO)
			m_AOCompositeMaterial->Set("u_AOSettings.BentNormals", m_Options.GTAOBentNormals ? 1u : 0u);

		BeginProfiledGPU("AOComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_AOCompositePass);
		ApplyCoarseFragmentShadingRate();
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_AOCompositePass->GetPipeline(), m_AOCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::AODebugPass()
	{
		ScopedCPUProfile cpuProfile(*this, "AODebug");
		if (!m_AODebugPass || !m_AODebugMaterial || !m_GTAOFinalImage)
			return;

		if (m_AODebugPass->IsInputValid("u_GTAOTex"))
		{
			m_AODebugPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_AODebugPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			m_AODebugPass->SetInput("u_Normal", GetGeometryNormalOutput());
		}

		if (m_Options.EnableGTAO)
			m_AODebugMaterial->Set("u_AOSettings.BentNormals", m_Options.GTAOBentNormals ? 1u : 0u);

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
				// During a viewport resize the pre-convolution image can be mid-recreation with a
				// null GPU handle; a barrier on an unallocated resource is meaningless, so skip it
				// rather than passing null into nvrhi (crashes in requireTextureState). Reading into
				// a local also keeps the texture alive across the barrier.
				nvrhi::TextureHandle handle = preConvolutedImage ? preConvolutedImage->GetHandle() : nullptr;
				if (!handle)
					return;
				nvrhi::CommandListHandle commandList = commandBuffer->GetActive();
				commandBuffer->RT_BeginMarker(markerName);
				commandList->setTextureState(handle, nvrhi::TextureSubresourceSet(mip, 1, 0, 1), state);
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
		ssrOptions.BentNormals = m_Options.GTAOBentNormals ? 1u : 0u;

		if (m_SSRPass->IsInputValid("u_GTAOTex") && m_GTAOFinalImage)
			m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);

		BeginProfiledGPU("SSR");
		Renderer::BeginComputePass(m_CommandBuffer, m_SSRPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_SSRPass, nullptr, m_SSRWorkGroups, Buffer(&ssrOptions, sizeof(ssrOptions)));
		Renderer::EndComputePass(m_CommandBuffer, m_SSRPass);
		m_SSRPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_SSRImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		m_SSRFinalImage = m_SSRImage;
		if (m_SSRCompositePass)
			m_SSRCompositePass->SetInput("u_SSR", m_SSRFinalImage);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SSRCompositePass()
	{
		ScopedCPUProfile cpuProfile(*this, "SSRComposite");
		if (!m_Options.EnableSSR || !m_SSRCompositePass || !m_SSRCompositeMaterial)
			return;

		// The composite is a straight upsampling sample of the SSR buffer, matching Hazel.
		// The bilateral upscale that used to live here weighted each tap by its own alpha and
		// then normalised, so a pixel with no reflection (alpha 0) inherited the colour AND
		// the alpha of a neighbour that did have one - bleeding dark, high-confidence samples
		// a texel wide and producing blocky dark patches at reduced SSR resolution.
		m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);

		BeginProfiledGPU("SSRComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_SSRCompositePass);
		ApplyCoarseFragmentShadingRate();
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
		ApplyCoarseFragmentShadingRate();
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_DOFPass->GetPipeline(), m_DOFMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		if (CanCompositeDOFIntoFinalTarget())
			Renderer::CopyImage(m_CommandBuffer, m_DOFPass->GetOutput(0), m_CompositePass->GetOutput(0));

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SMAAPass()
	{
		ScopedCPUProfile cpuProfile(*this, "SMAA");
		if (!IsSMAAReady())
			return;

		Ref<Image2D> input = GetPostProcessInputImage();
		if (!input)
			return;

		const glm::vec4 rtMetrics = {
			m_InvViewportWidth, m_InvViewportHeight,
			(float)m_ViewportWidth, (float)m_ViewportHeight
		};

		// Rebound per frame: the image SMAA antialiases is the composite output, or the
		// DOF output when DOF resolves into its own target.
		m_SMAAEdgeComputePass->SetInput("u_InputTex", input);
		m_SMAAWeightAndBlendComputePass->SetInput("u_InputTex", input);

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "SMAA");

		// Pass 1: edge detection.
		{
			struct { glm::vec4 RTMetrics; float Threshold; float LocalContrast; float Pad0; float Pad1; } constants;
			constants.RTMetrics = rtMetrics;
			constants.Threshold = glm::max(0.001f, m_Options.SMAAThreshold);
			constants.LocalContrast = glm::max(1.0f, m_Options.SMAALocalContrastAdaptationFactor);
			constants.Pad0 = 0.0f;
			constants.Pad1 = 0.0f;

			Renderer::BeginComputePass(m_CommandBuffer, m_SMAAEdgeComputePass);
			Renderer::DispatchCompute(m_CommandBuffer, m_SMAAEdgeComputePass, nullptr, m_SMAAWorkGroups, Buffer(&constants, sizeof(constants)));
			Renderer::EndComputePass(m_CommandBuffer, m_SMAAEdgeComputePass);
		}

		// Pass 2: blending weights followed by neighbourhood blending, merged into one
		// dispatch.
		{
			struct { glm::vec4 RTMetrics; glm::vec4 SubsampleIndices; } constants;
			constants.RTMetrics = rtMetrics;
			// Always zero: that is the SMAA 1x value. The reference only uses a non-zero
			// subsample index for the jittered T2x variant, which this engine does not
			// ship. It must still be written - the shader feeds it to SMAAArea as an
			// AreaTex subtexture offset, so leaving it uninitialised uploads stack
			// garbage and the lookup reads far outside the table.
			constants.SubsampleIndices = glm::vec4(0.0f);

			Renderer::BeginComputePass(m_CommandBuffer, m_SMAAWeightAndBlendComputePass);
			Renderer::DispatchCompute(m_CommandBuffer, m_SMAAWeightAndBlendComputePass, nullptr, m_SMAAWorkGroups, Buffer(&constants, sizeof(constants)));
			Renderer::EndComputePass(m_CommandBuffer, m_SMAAWeightAndBlendComputePass);
		}

		// Copy back over the image the rest of the engine treats as final, so nothing
		// downstream (DOF, debug views, the viewport) has to know SMAA ran.
		Renderer::CopyImage(m_CommandBuffer, m_SMAAOutputImage, input);

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
				// Skip a null handle: during a resize the bloom mip image can be mid-recreation,
					// and passing null to nvrhi crashes in requireTextureState (see PreConvolutionCompute).
					if (nvrhi::TextureHandle handle = image->GetHandle())
						commandList->setTextureState(handle, nvrhi::TextureSubresourceSet(mip, 1, 0, 1), state);
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
		const SubmeshLOD lod = dc.MeshSource->GetSubmeshLOD(dc.SubmeshIndex, dc.LODIndex);

		nvrhi::DrawIndexedIndirectArguments args{};
		args.indexCount = lod.IndexCount;
		args.instanceCount = tmd.VisibleInstanceCount;
		args.startIndexLocation = lod.BaseIndex;
		args.baseVertexLocation = (int32_t)lod.BaseVertex;
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

#ifndef LUX_DIST
		// Stats-panel counters only: re-walking every draw list has no consumer
		// in shipping builds. (UpdateDynamicRenderResolution below is functional
		// and stays in all builds.)
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
#endif

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

		// ── Legacy material descriptor set 0 ─────────────────────────────────
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

		const SubmeshLOD lod = meshSource->GetSubmeshLOD(dc.SubmeshIndex, dc.LODIndex);

		nvrhi::DrawArguments drawArgs{};
		drawArgs.vertexCount = lod.IndexCount;
		drawArgs.startIndexLocation = lod.BaseIndex;
		drawArgs.startVertexLocation = lod.BaseVertex;
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
		// Packed RG32UI target: material ID in .x, object ID in .y.
		return m_GeometryPassFramebuffer ? m_GeometryPassFramebuffer->GetImage(3) : nullptr;
	}

	Ref<Image2D> SceneRenderer::GetGeometryObjectIDOutput() const
	{
		// Same packed RG32UI target as the material IDs (object ID in .y).
		return m_GeometryPassFramebuffer ? m_GeometryPassFramebuffer->GetImage(3) : nullptr;
	}

	Ref<Image2D> SceneRenderer::GetGeometryVelocityOutput() const
	{
		return m_GeometryPassFramebuffer ? m_GeometryPassFramebuffer->GetImage(4) : nullptr;
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
	}

} // namespace Lux
