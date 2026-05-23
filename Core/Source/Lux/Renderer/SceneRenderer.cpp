#include "lpch.h"
#include "SceneRenderer.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Renderer2D.h"
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
#include <limits>
#include <unordered_map>
#include <unordered_set>

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

		constexpr std::array<const char*, 24> s_ProfiledSceneRendererPasses = {
			"ShadowMapPass",
			"SpotShadowMapPass",
			"MeshCullingPass",
			"PreDepthPass",
			"HZB",
			"PreIntegration",
			"LightCullingPass",
			"SkyboxPass",
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

		// Apply quality preset based on tiering or use Medium as default
		QualityPreset preset = QualityPreset::Medium;
		if (Ref<Project> project = Project::GetActive())
		{
			ApplyProjectSettings(project->GetConfig().SceneRenderer);
			
			// Override with project quality setting if available
			// For now, we'll use Medium as default since ProjectSceneRendererSettings
			// doesn't have a quality preset field yet
			preset = QualityPreset::Medium;
		}
		
		ApplyQualityPreset(preset);
		UpdateGTAOData();
	}

	void SceneRenderer::ApplyQualityPreset(QualityPreset preset)
	{
		switch (preset)
		{
		case QualityPreset::Low:
			// Low settings
			m_Options.EnableSSR = false;
			m_Options.EnableGTAO = false;
			m_BloomSettings.Enabled = true;
			m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
			m_BloomSettings.Intensity = 0.5f;
			m_BloomSettings.Threshold = 2.0f;
			m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Scale75;
			m_Options.TextureMipBias = 0.5f;
			m_Options.EnableDistanceMipBias = false;
			m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_1K;
			break;
		case QualityPreset::Medium:
			// Medium settings
			m_Options.EnableSSR = true;
			m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::HalfBilateral;
			m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
			m_Options.EnableGTAO = true;
			m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
			m_GTAODataCB.ResolutionScale = 2;
			m_BloomSettings.Enabled = true;
			m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
			m_BloomSettings.Intensity = 1.0f;
			m_BloomSettings.Threshold = 1.0f;
			m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Native;
			m_Options.TextureMipBias = 0.0f;
			m_Options.EnableDistanceMipBias = true;
			m_Options.DistanceMipBiasStart = 50.0f;
			m_Options.DistanceMipBiasEnd = 250.0f;
			m_Options.DistanceMipBiasMax = 2.0f;
			m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_2K;
			break;
		case QualityPreset::High:
			// High settings
			m_Options.EnableSSR = true;
			m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::Full;
			m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
			m_Options.EnableGTAO = true;
			m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
			m_GTAODataCB.ResolutionScale = 1;
			m_BloomSettings.Enabled = true;
			m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
			m_BloomSettings.Intensity = 1.5f;
			m_BloomSettings.Threshold = 0.8f;
			m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Native;
			m_Options.TextureMipBias = -0.5f;
			m_Options.EnableDistanceMipBias = true;
			m_Options.DistanceMipBiasStart = 50.0f;
			m_Options.DistanceMipBiasEnd = 250.0f;
			m_Options.DistanceMipBiasMax = 2.0f;
			m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_4K;
			break;
		case QualityPreset::Ultra:
			// Ultra settings
			m_Options.EnableSSR = true;
			m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::Full;
			m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
			m_Options.EnableGTAO = true;
			m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
			m_GTAODataCB.ResolutionScale = 1;
			m_Options.GTAOBentNormals = true;
			m_Options.EnableGTAOTemporalAccumulation = true;
			m_Options.GTAOTemporalBlend = 0.85f;
			m_BloomSettings.Enabled = true;
			m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
			m_BloomSettings.Intensity = 2.0f;
			m_BloomSettings.Threshold = 0.6f;
			m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Native;
			m_Options.TextureMipBias = -1.0f;
			m_Options.EnableDistanceMipBias = true;
			m_Options.DistanceMipBiasStart = 25.0f;
			m_Options.DistanceMipBiasEnd = 150.0f;
			m_Options.DistanceMipBiasMax = 3.0f;
			m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_8K;
			break;
		case QualityPreset::Cinematic:
			// Cinematic settings
			m_Options.EnableSSR = true;
			m_Options.SSRQuality = SceneRendererOptions::SSRQualityPreset::Full;
			m_Options.SSRResolutionScale = GetSSRQualityResolutionScale(m_Options.SSRQuality);
			m_Options.EnableSSRTemporalAccumulation = true;
			m_Options.SSRTemporalBlend = 0.90f;
			m_Options.EnableGTAO = true;
			m_Options.GTAOResolutionScale = SceneRendererOptions::EffectResolutionScale::Full;
			m_GTAODataCB.ResolutionScale = 1;
			m_Options.GTAOBentNormals = true;
			m_Options.EnableGTAOTemporalAccumulation = true;
			m_Options.GTAOTemporalBlend = 0.90f;
			m_BloomSettings.Enabled = true;
			m_BloomSettings.ResolutionScale = SceneRendererOptions::EffectResolutionScale::Half;
			m_BloomSettings.Intensity = 3.0f;
			m_BloomSettings.Threshold = 0.4f;
			m_Options.ResolutionScaleMode = SceneRendererOptions::RenderResolutionScaleMode::Native;
			m_Options.TextureMipBias = -1.5f;
			m_Options.EnableDistanceMipBias = true;
			m_Options.DistanceMipBiasStart = 10.0f;
			m_Options.DistanceMipBiasEnd = 100.0f;
			m_Options.DistanceMipBiasMax = 4.0f;
			m_Options.ShadowResolution = SceneRendererOptions::ShadowResolutionTier::Tier_8K;
			break;
		}

		// Update internal data that depends on the options
		UpdateGTAOData();

		// Update SSROptions based on SSR settings
		if (m_Options.EnableSSR)
		{
			switch (m_Options.SSRQuality)
			{
			case SceneRendererOptions::SSRQualityPreset::HalfBilateral:
				m_SSROptions.HalfRes = true;
				m_SSROptions.ResolutionScale = 2;
				break;
			case SceneRendererOptions::SSRQualityPreset::Full:
				m_SSROptions.HalfRes = false;
				m_SSROptions.ResolutionScale = 1;
				break;
			default:
				m_SSROptions.HalfRes = true;
				m_SSROptions.ResolutionScale = 2;
				break;
			}
		}
		else
		{
			m_SSROptions.HalfRes = true;
			m_SSROptions.ResolutionScale = 2;
		}
	}

	void SceneRenderer::UpdateGTAOData()
	{
		const bool gtaoEnabled = m_Options.EnableGTAO;
		Renderer::SetGlobalMacroInShaders("__HZ_AO_METHOD", std::to_string((int)ShaderDef::GetAOMethod(gtaoEnabled)));
		Renderer::SetGlobalMacroInShaders("__HZ_GTAO_COMPUTE_BENT_NORMALS", m_Options.GTAOBentNormals ? "1" : "0");

		m_Options.ReflectionOcclusionMethod = ShaderDef::AOMethod::None;
		if (gtaoEnabled && m_Options.EnableSSR)
			m_Options.ReflectionOcclusionMethod = ShaderDef::AOMethod::GTAO;

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
		}
	}

	void SceneRenderer::WriteProjectSettings(ProjectSceneRendererSettings& settings) const
	{
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

		// ── Storage buffer sets (start with generous initial capacity) ─────────
		{
			StorageBufferSpecification spec;
			spec.GPUOnly = false;
			spec.DebugName = "InstanceTransforms";
			// Pre-allocate 4096 transform slots  (each is 48 bytes)
			m_SBSInstanceTransforms = StorageBufferSet::Create(spec, sizeof(TransformVertexData) * 4096);
		}
		{
			StorageBufferSpecification spec;
			spec.GPUOnly = false;
			spec.DebugName = "ObjectIndexes";
			m_SBSObjectIndexes = StorageBufferSet::Create(spec, sizeof(uint32_t) * 4096);

			spec.DebugName = "VisibleObjectIndexes";
			m_SBSVisibleObjectIndexes = StorageBufferSet::Create(spec, sizeof(uint32_t) * 4096);

			spec.DebugName = "InstanceBounds";
			m_SBSInstanceBounds = StorageBufferSet::Create(spec, sizeof(InstanceBoundsData) * 4096);

			spec.DebugName = "MeshCullDrawData";
			m_SBSMeshCullDrawData = StorageBufferSet::Create(spec, sizeof(MeshCullDrawData) * 4096);

			spec.DrawIndirect = true;
			spec.DebugName = "IndirectDrawCommands";
			m_SBSIndirectDrawCommands = StorageBufferSet::Create(spec, sizeof(nvrhi::DrawIndexedIndirectArguments) * 4096);
		}
		{
			StorageBufferSpecification indexSpec;
			indexSpec.GPUOnly = true;
			indexSpec.DebugName = "VisiblePointLightIndices";
			m_SBSVisiblePointLightIndices = StorageBufferSet::Create(indexSpec, sizeof(int32_t) * MaxVisibleLightsPerTile);

			indexSpec.DebugName = "VisibleSpotLightIndices";
			m_SBSVisibleSpotLightIndices = StorageBufferSet::Create(indexSpec, sizeof(int32_t) * MaxVisibleLightsPerTile);
			ResizeLightCullingResources();
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
				m_ShadowMapPasses[cascade]->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
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
			m_SpotShadowMapPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
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
			m_PreDepthPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_PreDepthPass->SetInput("ObjectIndexes", m_SBSVisibleObjectIndexes);
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
			m_MeshCullingPass->SetInput("InstanceBounds", m_SBSInstanceBounds);
			m_MeshCullingPass->SetInput("VisibleObjectIndexes", m_SBSVisibleObjectIndexes);
			m_MeshCullingPass->SetInput("IndirectDrawCommands", m_SBSIndirectDrawCommands);
			m_MeshCullingPass->SetInput("u_HZB", m_HierarchicalDepthTexture.Texture);
			m_MeshCullingPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			LUX_CORE_VERIFY(m_MeshCullingPass->Validate());
			m_MeshCullingPass->Bake();
		}

		// ── Tiled light culling pass ─────────────────────────────────────────
		{
			ComputePassSpecification computeSpec;
			computeSpec.DebugName = "LightCulling";
			computeSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("LightCulling"));

			m_LightCullingPass = ComputePass::Create(computeSpec);
			m_LightCullingPass->SetInput("u_DepthMap", m_PreDepthPass->GetDepthOutput());
			m_LightCullingPass->SetInput("ShadowData", m_UBSShadow);
			m_LightCullingPass->SetInput("SceneData", m_UBSScene);
			m_LightCullingPass->SetInput("PointLightData", m_UBSPointLights);
			m_LightCullingPass->SetInput("SpotLightData", m_UBSSpotLights);
			m_LightCullingPass->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_LightCullingPass->SetInput("VisiblePointLightIndicesBuffer", m_SBSVisiblePointLightIndices);
			m_LightCullingPass->SetInput("VisibleSpotLightIndicesBuffer", m_SBSVisibleSpotLightIndices);
			m_LightCullingPass->SetInput("Camera", m_UBSCamera);
			m_LightCullingPass->SetInput("RendererData", m_UBSRendererData);
			m_LightCullingPass->SetInput("ScreenData", m_UBSScreenData);
			m_LightCullingPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_LightCullingPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_LightCullingPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_LightCullingPass->Validate());
			m_LightCullingPass->Bake();
		}

		// ── Geometry framebuffer (owns the color + normal + material images) ──
		{
			FramebufferSpecification geoFBSpec;
			geoFBSpec.Width = m_ViewportWidth;
			geoFBSpec.Height = m_ViewportHeight;
			// color | view-space normals | metalness+roughness | depth (shared with pre-depth)
			geoFBSpec.Attachments = {
				ImageFormat::RGBA32F,
				ImageFormat::RGBA16F,
				ImageFormat::RGBA,
				ImageFormat::DEPTH32FSTENCIL8UINT
			};
			geoFBSpec.ExistingImages[3] = m_PreDepthPass->GetDepthOutput();
			geoFBSpec.Attachments.Attachments[0].LoadOp = AttachmentLoadOp::Load; // skybox writes first
			geoFBSpec.Attachments.Attachments[1].Blend = false;
			geoFBSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			geoFBSpec.ClearDepthOnLoad = false;
			geoFBSpec.DebugName = "Geometry";
			m_GeometryPassFramebuffer = Framebuffer::Create(geoFBSpec);

			// "Load" variant: subsequent draws share the same images without clearing.
			geoFBSpec.ClearColorOnLoad = false;
			geoFBSpec.ExistingImages[0] = m_GeometryPassFramebuffer->GetImage(0);
			geoFBSpec.ExistingImages[1] = m_GeometryPassFramebuffer->GetImage(1);
			geoFBSpec.ExistingImages[2] = m_GeometryPassFramebuffer->GetImage(2);
			Ref<Framebuffer> loadFB = Framebuffer::Create(geoFBSpec);

			// Opaque PBR pipeline
			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "PBR-Static";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("HazelPBR_Static");
			pipelineSpec.TargetFramebuffer = loadFB;
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::Equal; // rely on pre-depth
			pipelineSpec.DepthWrite = false;
			m_GeometryPipeline = Pipeline::Create(pipelineSpec);

			// Transparent PBR pipeline (alpha-blend, depth-test but no pre-depth Equal trick)
			pipelineSpec.DebugName = "PBR-Transparent";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("HazelPBR_Transparent");
			pipelineSpec.DepthOperator = DepthCompareOperator::GreaterOrEqual;
			pipelineSpec.DepthWrite = false;
			m_TransparentGeometryPipeline = Pipeline::Create(pipelineSpec);
		}

		// ── Geometry render passes ────────────────────────────────────────────
		{
			// Opaque pass
			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "GeometryPass";
			rpSpec.Pipeline = m_GeometryPipeline;
			m_GeometryPass = RenderPass::Create(rpSpec);

			m_GeometryPass->SetInput("Camera", m_UBSCamera);
			m_GeometryPass->SetInput("SceneData", m_UBSScene);
			m_GeometryPass->SetInput("ShadowData", m_UBSShadow);
			m_GeometryPass->SetInput("RendererData", m_UBSRendererData);
			m_GeometryPass->SetInput("PointLightData", m_UBSPointLights);
			m_GeometryPass->SetInput("SpotLightData", m_UBSSpotLights);
			m_GeometryPass->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_GeometryPass->SetInput("VisiblePointLightIndicesBuffer", m_SBSVisiblePointLightIndices);
			m_GeometryPass->SetInput("VisibleSpotLightIndicesBuffer", m_SBSVisibleSpotLightIndices);
			m_GeometryPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_GeometryPass->SetInput("ObjectIndexes", m_SBSVisibleObjectIndexes);
			// Environment textures – overridden each frame in BeginScene once env is set
			m_GeometryPass->SetInput("u_EnvRadianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPass->SetInput("u_EnvIrradianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPass->SetInput("u_BRDFLUTTexture", Renderer::GetBRDFLutTexture());
			m_GeometryPass->SetInput("r_MaterialSampler", Renderer::GetRepeatSampler());
			// Shadow map output from the shadow pass above
			m_GeometryPass->SetInput("u_ShadowMapTexture", m_ShadowMapPass->GetDepthOutput());
			m_GeometryPass->SetInput("u_SpotShadowTexture", m_SpotShadowMapImage);
			LUX_CORE_VERIFY(m_GeometryPass->Validate());
			m_GeometryPass->Bake();

			// Transparent pass (same bindings, different pipeline)
			rpSpec.DebugName = "GeometryPass-Transparent";
			rpSpec.Pipeline = m_TransparentGeometryPipeline;
			m_GeometryPassTransparent = RenderPass::Create(rpSpec);

			m_GeometryPassTransparent->SetInput("Camera", m_UBSCamera);
			m_GeometryPassTransparent->SetInput("SceneData", m_UBSScene);
			m_GeometryPassTransparent->SetInput("ShadowData", m_UBSShadow);
			m_GeometryPassTransparent->SetInput("RendererData", m_UBSRendererData);
			m_GeometryPassTransparent->SetInput("PointLightData", m_UBSPointLights);
			m_GeometryPassTransparent->SetInput("SpotLightData", m_UBSSpotLights);
			m_GeometryPassTransparent->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_GeometryPassTransparent->SetInput("VisiblePointLightIndicesBuffer", m_SBSVisiblePointLightIndices);
			m_GeometryPassTransparent->SetInput("VisibleSpotLightIndicesBuffer", m_SBSVisibleSpotLightIndices);
			m_GeometryPassTransparent->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_GeometryPassTransparent->SetInput("ObjectIndexes", m_SBSVisibleObjectIndexes);
			// Environment textures – overridden each frame in BeginScene once env is set
			m_GeometryPassTransparent->SetInput("u_EnvRadianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPassTransparent->SetInput("u_EnvIrradianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPassTransparent->SetInput("u_BRDFLUTTexture", Renderer::GetBRDFLutTexture());
			m_GeometryPassTransparent->SetInput("r_MaterialSampler", Renderer::GetRepeatSampler());
			// Shadow map output from the shadow pass above
			m_GeometryPassTransparent->SetInput("u_ShadowMapTexture", m_ShadowMapPass->GetDepthOutput());
			m_GeometryPassTransparent->SetInput("u_SpotShadowTexture", m_SpotShadowMapImage);
			LUX_CORE_VERIFY(m_GeometryPassTransparent->Validate());
			m_GeometryPassTransparent->Bake();
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
			m_GTAOComputePass->SetInput("u_ViewNormal", m_GeometryPass->GetOutput(1));
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
			aoFramebufferSpec.Attachments = { ImageFormat::RGBA32F };
			aoFramebufferSpec.ExistingImages[0] = m_GeometryPass->GetOutput(0);
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
			m_SSRPass->SetInput("u_Normal", m_GeometryPass->GetOutput(1));
			m_SSRPass->SetInput("u_HiZBuffer", m_HierarchicalDepthTexture.Texture);
			m_SSRPass->SetInput("u_MetalnessRoughness", m_GeometryPass->GetOutput(2));
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
			ssrCompositeFBSpec.Attachments = { ImageFormat::RGBA32F };
			ssrCompositeFBSpec.ExistingImages[0] = m_GeometryPass->GetOutput(0);
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
			m_SSRCompositePass->SetInput("u_Normal", m_GeometryPass->GetOutput(1));
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
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::RGBA32F, ImageFormat::Depth };
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
			m_SelectedGeometryPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_SelectedGeometryPass->SetInput("ObjectIndexes", m_SBSVisibleObjectIndexes);
			LUX_CORE_VERIFY(m_SelectedGeometryPass->Validate());
			m_SelectedGeometryPass->Bake();

			m_SelectedGeometryMaterial = Material::Create(pipelineSpec.Shader, "SelectedGeometry");
		}

		// ── Jump flood outline buffers ────────────────────────────────────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::RGBA32F };
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
			fbSpec.ExistingImages[0] = m_GeometryPassFramebuffer->GetImage(0);
			fbSpec.Attachments = { ImageFormat::RGBA32F };
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
			m_GeometryWireframePass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_GeometryWireframePass->SetInput("ObjectIndexes", m_SBSVisibleObjectIndexes);
			LUX_CORE_VERIFY(m_GeometryWireframePass->Validate());
			m_GeometryWireframePass->Bake();

			m_WireframeMaterial = Material::Create(pipelineSpec.Shader, "Wireframe");
			m_WireframeMaterial->Set("u_MaterialUniforms.Color", glm::vec4{ 1.0f, 0.5f, 0.0f, 1.0f });
		}

		// ── Skybox (renders into geometry color attachment before geometry) ────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.ExistingImages[0] = m_GeometryPassFramebuffer->GetImage(0);
			fbSpec.Attachments = { ImageFormat::RGBA32F };
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
			// The geometry color output feeds the composite shader
			m_CompositePass->SetInput("u_Texture", m_GeometryPass->GetOutput(0));
			m_CompositePass->SetInput("u_BloomTexture", m_BloomComputeTextures[2].Texture);
			m_CompositePass->SetInput("u_BloomDirtTexture", m_BloomDirtTexture);
			m_CompositePass->SetInput("u_DepthTexture", m_PreDepthPass->GetDepthOutput());
			m_CompositePass->SetInput("u_TransparentDepthTexture", m_GeometryPassTransparent->GetDepthOutput());
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

	void SceneRenderer::ResizeLightCullingResources()
	{
		m_LightTilesCountX = glm::max(1u, (m_ViewportWidth + LightCullingTileSize - 1u) / LightCullingTileSize);
		m_LightTilesCountY = glm::max(1u, (m_ViewportHeight + LightCullingTileSize - 1u) / LightCullingTileSize);

		const uint64_t tileCount = static_cast<uint64_t>(m_LightTilesCountX) * static_cast<uint64_t>(m_LightTilesCountY);
		const uint64_t bufferSize = tileCount * MaxVisibleLightsPerTile * sizeof(int32_t);
		LUX_CORE_ASSERT(bufferSize <= std::numeric_limits<uint32_t>::max(), "Light culling index buffer is too large");

		const uint32_t newSize = static_cast<uint32_t>(bufferSize);
		if (newSize == m_VisibleLightIndexBufferSize)
			return;

		m_VisibleLightIndexBufferSize = newSize;
		m_SBSVisiblePointLightIndices->Resize(newSize);
		m_SBSVisibleSpotLightIndices->Resize(newSize);
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
		if (!m_BloomComputePass || !m_SkyboxPass || !m_BloomComputeTextures[0].Texture)
			return;

		Ref<Image2D> inputImage = m_SkyboxPass->GetOutput(0);
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
			m_BloomComputeMaterials.UpsampleMaterials[mip]->Set("u_BloomTexture", m_BloomComputeTextures[2].Texture);
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
		resizePass(m_JumpFloodInitPass, viewportSize);
		resizePass(m_JumpFloodPasses[0], viewportSize);
		resizePass(m_JumpFloodPasses[1], viewportSize);
		resizePass(m_JumpFloodCompositePass, viewportSize);
		resizePass(m_DOFPass, GetScaledExtent(viewportSize, m_DOFSettings.ResolutionScale));

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
				m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			if (m_AODebugPass)
				m_AODebugPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
				m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			if (m_GTAOTemporalPass)
			{
				m_GTAOTemporalPass->SetInput("u_CurrentAO", m_GTAOFinalImage);
				m_GTAOTemporalPass->SetInput("u_Depth", m_PreDepthPass->GetDepthOutput());
			}
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
				m_SSRCompositePass->SetInput("u_Normal", m_GeometryPass->GetOutput(1));
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
			material->Set("u_Input", mip == 0 ? m_SkyboxPass->GetOutput(0) : m_PreConvolutedTexture.Texture->GetImage());
			m_PreConvolutionMaterials[mip] = material;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Per-frame scene data setters (call between BeginScene and EndScene)
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::SetLightEnvironment(const LightEnvironment& lightEnvironment)
	{
		m_SceneData.SceneLightEnvironment = lightEnvironment;
	}

	void SceneRenderer::SetEnvironment(Ref<Environment> environment, float intensity, float skyboxLod)
	{
		m_SceneData.SceneEnvironment = environment;
		m_SceneData.SceneEnvironmentIntensity = intensity;
		m_SceneData.SkyboxLod = skyboxLod;
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
	}

	SceneRenderer::ScopedCPUProfile::~ScopedCPUProfile()
	{
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
		addUniformBufferSet(m_UBSPointLights);
		addUniformBufferSet(m_UBSSpotLights);
		addStorageBufferSet(m_SBSInstanceTransforms);
		addStorageBufferSet(m_SBSObjectIndexes);
		addStorageBufferSet(m_SBSVisibleObjectIndexes);
		addStorageBufferSet(m_SBSInstanceBounds);
		addStorageBufferSet(m_SBSMeshCullDrawData);
		addStorageBufferSet(m_SBSIndirectDrawCommands);
		addStorageBufferSet(m_SBSVisiblePointLightIndices);
		addStorageBufferSet(m_SBSVisibleSpotLightIndices);

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
		addRenderPass(m_SelectedGeometryPass);
		addRenderPass(m_GeometryWireframePass);
		addRenderPass(m_SkyboxPass);
		addRenderPass(m_CompositePass);
		addRenderPass(m_GridRenderPass);

		addFramebuffer(m_GeometryPassFramebuffer);
		addFramebuffer(m_CompositingFramebuffer);

		addComputePass(m_MeshCullingPass);
		addComputePass(m_LightCullingPass);
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

	void SceneRenderer::BuildRenderGraph()
	{
		m_RenderGraph.Reset();
		std::unordered_map<const Image2D*, RenderGraph::ResourceHandle> resourceLookup;

		auto addTexture = [&](const std::string& name, const Ref<Image2D>& image) -> RenderGraph::ResourceHandle
			{
				if (!image || !image->IsValid())
					return RenderGraph::InvalidResource;

				const Image2D* key = image.Raw();
				if (auto it = resourceLookup.find(key); it != resourceLookup.end())
					return it->second;

				const ImageSpecification& spec = image->GetSpecification();
				const bool allowAlias = IsRenderGraphAliasCandidate(image);
				RenderGraph::TextureDesc desc;
				desc.Name = name;
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

		auto addFramebufferResources = [&](const std::string& name, const Ref<Framebuffer>& framebuffer)
			{
				std::vector<RenderGraph::ResourceHandle> resources;
				if (!framebuffer)
					return resources;

				for (uint32_t attachment = 0; attachment < framebuffer->GetColorAttachmentCount(); attachment++)
					resources.push_back(addTexture(std::format("{} Color {}", name, attachment), framebuffer->GetImage(attachment)));
				if (framebuffer->HasDepthAttachment())
					resources.push_back(addTexture(std::format("{} Depth", name), framebuffer->GetDepthImage()));

				return resources;
			};

		auto addRenderPassResources = [&](const std::string& name, const Ref<RenderPass>& pass)
			{
				return pass ? addFramebufferResources(name, pass->GetTargetFramebuffer()) : std::vector<RenderGraph::ResourceHandle>{};
			};

		auto appendResources = [](std::vector<RenderGraph::ResourceHandle>& dst, const std::vector<RenderGraph::ResourceHandle>& src)
			{
				dst.insert(dst.end(), src.begin(), src.end());
			};

		auto addPass = [&](std::string name, std::vector<RenderGraph::ResourceHandle> reads, std::vector<RenderGraph::ResourceHandle> writes)
			{
				auto removeInvalid = [](std::vector<RenderGraph::ResourceHandle>& resources)
					{
						resources.erase(std::remove(resources.begin(), resources.end(), RenderGraph::InvalidResource), resources.end());
					};
				removeInvalid(reads);
				removeInvalid(writes);
				if (reads.empty() && writes.empty())
					return;

				RenderGraph::PassDesc pass;
				pass.Name = std::move(name);
				pass.Reads = std::move(reads);
				pass.Writes = std::move(writes);
				m_RenderGraph.AddPass(pass);
			};

		std::vector<RenderGraph::ResourceHandle> shadowOutputs;
		shadowOutputs.push_back(addTexture("Directional Shadow Atlas", m_ShadowMapImage));
		shadowOutputs.push_back(addTexture("Spot Shadow Atlas", m_SpotShadowMapImage));
		addPass("Shadow Maps", {}, shadowOutputs);

		std::vector<RenderGraph::ResourceHandle> hzbOutputs;
		hzbOutputs.push_back(m_HierarchicalDepthTexture.Texture ? addTexture("HZB", m_HierarchicalDepthTexture.Texture->GetImage()) : RenderGraph::InvalidResource);
		addPass("Mesh Culling", hzbOutputs, {});

		std::vector<RenderGraph::ResourceHandle> preDepthOutputs = addRenderPassResources("PreDepth", m_PreDepthPass);
		addPass("PreDepth", {}, preDepthOutputs);
		addPass("HZB", preDepthOutputs, hzbOutputs);

		std::vector<RenderGraph::ResourceHandle> preIntegrationOutputs;
		preIntegrationOutputs.push_back(m_PreIntegrationVisibilityTexture.Texture ? addTexture("PreIntegration Visibility", m_PreIntegrationVisibilityTexture.Texture->GetImage()) : RenderGraph::InvalidResource);
		addPass("PreIntegration", hzbOutputs, preIntegrationOutputs);

		std::vector<RenderGraph::ResourceHandle> lightCullingReads = preDepthOutputs;
		appendResources(lightCullingReads, shadowOutputs);
		addPass("Light Culling", lightCullingReads, {});

		std::vector<RenderGraph::ResourceHandle> geometryOutputs = addFramebufferResources("Geometry", m_GeometryPassFramebuffer);
		std::vector<RenderGraph::ResourceHandle> skyboxOutputs = addRenderPassResources("Skybox", m_SkyboxPass);
		addPass("Skybox", {}, skyboxOutputs);

		std::vector<RenderGraph::ResourceHandle> selectedOutputs = addRenderPassResources("SelectedGeometry", m_SelectedGeometryPass);
		addPass("SelectedGeometry", preDepthOutputs, selectedOutputs);

		std::vector<RenderGraph::ResourceHandle> geometryReads = shadowOutputs;
		appendResources(geometryReads, preDepthOutputs);
		appendResources(geometryReads, skyboxOutputs);
		addPass("Geometry", geometryReads, addRenderPassResources("Geometry Pass", m_GeometryPass));
		addPass("Transparent Geometry", geometryOutputs, addRenderPassResources("Transparent Geometry", m_GeometryPassTransparent));

		std::vector<RenderGraph::ResourceHandle> gtaoOutputs;
		const RenderGraph::ResourceHandle gtaoOutput = addTexture("GTAO Output", m_GTAOOutputImage);
		const RenderGraph::ResourceHandle gtaoDenoise = addTexture("GTAO Denoise", m_GTAODenoiseImage);
		const RenderGraph::ResourceHandle gtaoEdges = addTexture("GTAO Edges", m_GTAOEdgesOutputImage);
		const RenderGraph::ResourceHandle gtaoHistoryA = addTexture("GTAO History A", m_GTAOHistoryImages[0]);
		const RenderGraph::ResourceHandle gtaoHistoryB = addTexture("GTAO History B", m_GTAOHistoryImages[1]);
		gtaoOutputs.push_back(gtaoOutput);
		gtaoOutputs.push_back(gtaoEdges);
		std::vector<RenderGraph::ResourceHandle> gtaoReads = geometryOutputs;
		appendResources(gtaoReads, hzbOutputs);
		addPass("GTAO", gtaoReads, gtaoOutputs);
		addPass("GTAO Denoise A", { gtaoOutput, gtaoEdges }, { gtaoDenoise });
		addPass("GTAO Denoise B", { gtaoDenoise, gtaoEdges }, { gtaoOutput });
		addPass("GTAO Temporal", { gtaoOutput, gtaoDenoise, gtaoHistoryA }, { gtaoHistoryB });

		std::vector<RenderGraph::ResourceHandle> aoFinalOutputs = { gtaoOutput, gtaoDenoise, gtaoHistoryA, gtaoHistoryB };
		std::vector<RenderGraph::ResourceHandle> aoCompositeReads = geometryOutputs;
		appendResources(aoCompositeReads, aoFinalOutputs);
		addPass("AO Composite", aoCompositeReads, addRenderPassResources("AO Composite", m_AOCompositePass));
		addPass("AO Debug", aoFinalOutputs, addRenderPassResources("AO Debug", m_AODebugPass));

		std::vector<RenderGraph::ResourceHandle> preConvolutionOutputs;
		preConvolutionOutputs.push_back(m_PreConvolutedTexture.Texture ? addTexture("Pre-Convoluted Scene", m_PreConvolutedTexture.Texture->GetImage()) : RenderGraph::InvalidResource);
		addPass("Pre-Convolution", skyboxOutputs.empty() ? geometryOutputs : skyboxOutputs, preConvolutionOutputs);

		std::vector<RenderGraph::ResourceHandle> ssrOutputs;
		const RenderGraph::ResourceHandle ssrImage = addTexture("SSR", m_SSRImage);
		const RenderGraph::ResourceHandle ssrHistoryA = addTexture("SSR History A", m_SSRHistoryImages[0]);
		const RenderGraph::ResourceHandle ssrHistoryB = addTexture("SSR History B", m_SSRHistoryImages[1]);
		ssrOutputs.push_back(ssrImage);
		std::vector<RenderGraph::ResourceHandle> ssrReads = geometryOutputs;
		appendResources(ssrReads, hzbOutputs);
		appendResources(ssrReads, preIntegrationOutputs);
		appendResources(ssrReads, preConvolutionOutputs);
		appendResources(ssrReads, aoFinalOutputs);
		addPass("SSR", ssrReads, ssrOutputs);
		addPass("SSR Temporal", { ssrImage, ssrHistoryA }, { ssrHistoryB });
		std::vector<RenderGraph::ResourceHandle> ssrCompositeReads = geometryOutputs;
		appendResources(ssrCompositeReads, ssrOutputs);
		ssrCompositeReads.push_back(ssrHistoryA);
		ssrCompositeReads.push_back(ssrHistoryB);
		addPass("SSR Composite", ssrCompositeReads, addRenderPassResources("SSR Composite", m_SSRCompositePass));

		std::vector<RenderGraph::ResourceHandle> jumpFloodInitOutputs = addRenderPassResources("JumpFlood Init", m_JumpFloodInitPass);
		addPass("JumpFlood Init", selectedOutputs, jumpFloodInitOutputs);
		std::vector<RenderGraph::ResourceHandle> jumpFloodAOutputs = addRenderPassResources("JumpFlood A", m_JumpFloodPasses[0]);
		std::vector<RenderGraph::ResourceHandle> jumpFloodBOutputs = addRenderPassResources("JumpFlood B", m_JumpFloodPasses[1]);
		addPass("JumpFlood A", jumpFloodInitOutputs, jumpFloodAOutputs);
		addPass("JumpFlood B", jumpFloodAOutputs, jumpFloodBOutputs);
		addPass("JumpFlood A Resolve", jumpFloodBOutputs, jumpFloodAOutputs);

		std::vector<RenderGraph::ResourceHandle> bloomOutputs;
		for (uint32_t index = 0; index < m_BloomComputeTextures.size(); index++)
		{
			if (m_BloomComputeTextures[index].Texture)
				bloomOutputs.push_back(addTexture(std::format("Bloom {}", index), m_BloomComputeTextures[index].Texture->GetImage()));
		}
		addPass("Bloom", geometryOutputs, bloomOutputs);

		std::vector<RenderGraph::ResourceHandle> compositeReads = geometryOutputs;
		appendResources(compositeReads, ssrOutputs);
		appendResources(compositeReads, bloomOutputs);
		appendResources(compositeReads, preDepthOutputs);
		std::vector<RenderGraph::ResourceHandle> compositeOutputs = addFramebufferResources("Composite", m_CompositingFramebuffer);
		addPass("Composite", compositeReads, compositeOutputs);

		std::vector<RenderGraph::ResourceHandle> jumpFloodCompositeReads = compositeOutputs;
		appendResources(jumpFloodCompositeReads, jumpFloodAOutputs);
		appendResources(jumpFloodCompositeReads, jumpFloodBOutputs);
		addPass("JumpFlood Composite", jumpFloodCompositeReads, addRenderPassResources("JumpFlood Composite", m_JumpFloodCompositePass));
		addPass("Grid", compositeOutputs, addRenderPassResources("Grid", m_GridRenderPass));
		addPass("Renderer2D Overlay", compositeOutputs, compositeOutputs);
		addPass("DOF", compositeOutputs, addRenderPassResources("DOF", m_DOFPass));
	}

	SceneRenderer::RenderGraphDebugSnapshot SceneRenderer::GetRenderGraphDebugSnapshot()
	{
		BuildRenderGraph();

		RenderGraphDebugSnapshot snapshot;
		const auto lifetimes = m_RenderGraph.BuildAliasPlan();
		const auto& textures = m_RenderGraph.GetTextures();
		const auto& passes = m_RenderGraph.GetPasses();

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

			if (resource < lifetimes.size())
			{
				const RenderGraph::ResourceLifetime& lifetime = lifetimes[resource];
				textureInfo.FirstPass = lifetime.FirstPass;
				textureInfo.LastPass = lifetime.LastPass;
				textureInfo.AliasGroup = lifetime.AliasIndex;
			}

			if (texture.Image)
			{
				textureInfo.AliasedNow = texture.Image->IsTransientAlias();
				textureInfo.CurrentState = texture.Image->GetImageInfo().State;
			}
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

		snapshot.Passes.reserve(passes.size());
		for (const RenderGraph::PassDesc& pass : passes)
		{
			RenderGraphPassDebugInfo& passInfo = snapshot.Passes.emplace_back();
			passInfo.Name = pass.Name;

			for (RenderGraph::ResourceHandle resource : pass.Reads)
			{
				if (resource >= textures.size())
					continue;
				passInfo.Inputs.push_back({ resource, accessState(pass, resource, true) });
			}

			for (RenderGraph::ResourceHandle resource : pass.Writes)
			{
				if (resource >= textures.size())
					continue;
				passInfo.Outputs.push_back({ resource, accessState(pass, resource, false) });
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

		BuildRenderGraph();

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
		recreateFramebuffer(m_CompositingFramebuffer);

		recreatePassFramebuffer(m_PreDepthPass);
		recreatePassFramebuffer(m_GeometryPass);
		recreatePassFramebuffer(m_GeometryPassTransparent);
		recreatePassFramebuffer(m_SkyboxPass);
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

		if (isFramebufferImage(m_CompositingFramebuffer))
			return false;

		return true;
	}

	void SceneRenderer::BeginScene(const SceneRendererCamera& camera)
	{
		LUX_CORE_ASSERT(m_Scene, "No scene attached to SceneRenderer");
		LUX_CORE_ASSERT(!m_Active, "BeginScene called twice without EndScene");
		m_Active = true;
		ResetProfilingData();
		m_FrameCullingStats = {};
		m_ShadowCascadeFrustumCount = 0;
		m_SpotShadowFrustumCount = 0;

		if (Ref<Project> project = Project::GetActive())
			m_RenderingTechnique = project->GetConfig().RendererTechnique;

		if (m_ResourcesCreatedGPU)
			m_ResourcesCreated = true;

		if (!m_ResourcesCreated)
			return; // GPU resources not yet available

		// Open the upload command buffer for uniform/storage buffer writes
		m_UploadCommandBuffer->Begin();

		m_SceneData.SceneCamera = camera;
		m_SceneData.CameraFrustum = Frustum::FromViewProjection(camera.Camera.GetProjectionMatrix() * camera.ViewMatrix);

		// ── Handle viewport resize ────────────────────────────────────────────
		if (m_NeedsResize)
		{
			m_NeedsResize = false;
			m_ScreenSpaceProjectionMatrix = glm::ortho(0.0f, (float)m_ViewportWidth, 0.0f, (float)m_ViewportHeight);
			ClearRenderTargetAliasing(false);

			m_PreDepthPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPassFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPassTransparent->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SkyboxPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SelectedGeometryPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryWireframePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_CompositingFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_CompositePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GridRenderPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			ResizeLightCullingResources();
			ResizeScreenSpaceEffectResources();
			ApplyRenderTargetAliasing();
			m_ShadowCascadeCacheValid = false;
			m_DirectionalShadowMapNeedsRender = true;
		}

		// ── Camera uniform buffer ─────────────────────────────────────────────
		{
			const glm::mat4 viewProj = camera.Camera.GetProjectionMatrix() * camera.ViewMatrix;
			const glm::mat4 viewInverse = glm::inverse(camera.ViewMatrix);
			const glm::mat4 projInverse = glm::inverse(camera.Camera.GetProjectionMatrix());

			m_CurrentViewProjection = viewProj;
			if (!m_TemporalHistoryValid)
				m_PreviousViewProjection = viewProj;

			m_CameraUB.ViewProjection = viewProj;
			m_CameraUB.InverseViewProjection = viewInverse * projInverse;
			m_CameraUB.Projection = camera.Camera.GetProjectionMatrix();
			m_CameraUB.InverseProjection = projInverse;
			m_CameraUB.View = camera.ViewMatrix;
			m_CameraUB.InverseView = viewInverse;

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
			m_SceneUB.EnvironmentMapIntensity = m_SceneData.SceneEnvironmentIntensity;

			auto sceneData = m_SceneUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, sceneData]() mutable {
				instance->m_UBSScene->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &sceneData, sizeof(UBScene));
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
			m_RendererDataUB.TilesCountX = m_LightTilesCountX;
			m_RendererDataUB.TextureMipBias = m_Options.TextureMipBias;
			m_RendererDataUB.EnableDistanceMipBias = m_Options.EnableDistanceMipBias;
			m_RendererDataUB.DistanceMipBiasStart = m_Options.DistanceMipBiasStart;
			m_RendererDataUB.DistanceMipBiasEnd = glm::max(m_Options.DistanceMipBiasEnd, m_Options.DistanceMipBiasStart + 1.0f);
			m_RendererDataUB.DistanceMipBiasMax = m_Options.DistanceMipBiasMax;

			auto rdData = m_RendererDataUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, rdData]() mutable {
				instance->m_UBSRendererData->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &rdData, sizeof(UBRendererData));
				});
		}

		// ── Update environment texture bindings in geometry passes ────────────
		Ref<TextureCube> radianceMap = GetEnvironmentRadianceMap(m_SceneData.SceneEnvironment);
		Ref<TextureCube> irradianceMap = GetEnvironmentIrradianceMap(m_SceneData.SceneEnvironment);
		m_GeometryPass->SetInput("u_EnvRadianceTex", radianceMap);
		m_GeometryPass->SetInput("u_EnvIrradianceTex", irradianceMap);
		m_GeometryPassTransparent->SetInput("u_EnvRadianceTex", radianceMap);
		m_GeometryPassTransparent->SetInput("u_EnvIrradianceTex", irradianceMap);

		m_UploadCommandBuffer->End();
		m_UploadCommandBuffer->Submit();
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Mesh submission
	// ─────────────────────────────────────────────────────────────────────────

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

	uint64_t SceneRenderer::CalculateShadowCasterHash() const
	{
		uint64_t hash = 1469598103934665603ull;
		for (const MeshKey& key : m_StaticMeshShadowPassDrawOrder)
		{
			const auto drawIt = m_StaticMeshShadowPassDrawList.find(key);
			const auto transformIt = m_ShadowMeshTransformMap.find(key);
			if (drawIt == m_StaticMeshShadowPassDrawList.end() || transformIt == m_ShadowMeshTransformMap.end())
				continue;

			const StaticDrawCommand& dc = drawIt->second;
			const TransformMapData& tmd = transformIt->second.Cascade;
			hash = HashCombine(hash, (uint64_t)key.MeshHandle);
			hash = HashCombine(hash, (uint64_t)key.MaterialHandle);
			hash = HashCombine(hash, key.SubmeshIndex);
			hash = HashCombine(hash, dc.MeshSortKey);
			hash = HashCombine(hash, dc.MaterialSortKey);
			hash = HashCombine(hash, tmd.ObjectIndices.size());

			for (uint32_t transformIndex : tmd.ObjectIndices)
			{
				if (transformIndex >= m_TransformData.size())
					continue;

				const TransformVertexData& transformData = m_TransformData[transformIndex];
				hash = HashVec4(hash, transformData.MRow[0]);
				hash = HashVec4(hash, transformData.MRow[1]);
				hash = HashVec4(hash, transformData.MRow[2]);
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
		bool               isSelected)
	{
		LUX_CORE_ASSERT(m_Active, "SubmitStaticMesh called outside BeginScene/EndScene");

		const auto& submeshData = meshSource->GetSubmeshes();

		for (uint32_t submeshIndex : staticMesh->GetSubmeshes())
		{
			const auto& submesh = submeshData[submeshIndex];

			// Combine the entity transform with the submesh local transform
			const glm::mat4 submeshTransform = transform * submesh.Transform;

			// Resolve material handle
			AssetHandle materialHandle = 0;
			Ref<MaterialAsset> materialAsset;
			Ref<Material> resolvedOverrideMaterial = overrideMaterial;

			if (!resolvedOverrideMaterial)
			{
				materialHandle = ResolveStaticMeshMaterialHandle(materialTable, staticMesh, meshSource, submesh.MaterialIndex);

				if (materialHandle)
					materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);

				if (!materialAsset)
					resolvedOverrideMaterial = Renderer::GetDefaultWhiteMaterial();
			}

			LUX_CORE_ASSERT(resolvedOverrideMaterial || materialAsset, "No material found for submesh {}", submeshIndex);

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

			auto populateDrawCommand = [&](StaticDrawCommand& dc, uint64_t pipelineSortKey, uint64_t shaderSortKeyOverride, uint64_t materialSortKeyOverride)
				{
					dc.StaticMesh = staticMesh;
					dc.MeshSource = meshSource;
					dc.SubmeshIndex = submeshIndex;
					dc.MaterialHandle = materialHandle;
					dc.MaterialTable = materialTable;
					dc.OverrideMaterial = resolvedOverrideMaterial;
					dc.PipelineSortKey = pipelineSortKey;
					dc.ShaderSortKey = shaderSortKeyOverride;
					dc.MaterialSortKey = materialSortKeyOverride;
					dc.MeshSortKey = meshSortKey;
				};

			// ── Shadow pass list ──────────────────────────────────────────────
			const bool castsShadows = resolvedOverrideMaterial
				? true // override materials always cast shadows
				: (materialAsset && materialAsset->IsShadowCasting());

			const glm::vec4 boundsSphereData = CalculateWorldBoundsSphere(submesh.BoundingBox, submeshTransform);
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
				continue;
			}

			// ── Store transform ───────────────────────────────────────────────
			const uint32_t transformIndex = (uint32_t)m_TransformData.size();
			auto& td = m_TransformData.emplace_back();
			td.MRow[0] = { submeshTransform[0][0], submeshTransform[1][0], submeshTransform[2][0], submeshTransform[3][0] };
			td.MRow[1] = { submeshTransform[0][1], submeshTransform[1][1], submeshTransform[2][1], submeshTransform[3][1] };
			td.MRow[2] = { submeshTransform[0][2], submeshTransform[1][2], submeshTransform[2][2], submeshTransform[3][2] };
			m_InstanceBoundsData.push_back({ boundsSphereData });

			if (mainViewVisible)
			{
				// ── Main draw list ────────────────────────────────────────────────
				m_MeshTransformMap[key].ObjectIndices.push_back(transformIndex);

				const bool isTransparent = materialAsset ? materialAsset->IsTransparent() : false;
				auto& destList = isTransparent ? m_TransparentStaticMeshDrawList : m_StaticMeshDrawList;
				auto& dc = destList[key];
				Ref<Pipeline> geometryPipeline = isTransparent ? m_TransparentGeometryPipeline : m_GeometryPipeline;
				populateDrawCommand(dc, SortKeyFromRef(geometryPipeline.Raw()), shaderSortKey, materialSortKey);
				dc.InstanceCount++;

				// ── Selected list ─────────────────────────────────────────────────
				if (isSelected)
				{
					auto& selDc = m_SelectedStaticMeshDrawList[key];
					const uint64_t selectedShaderSortKey = m_SelectedGeometryMaterial && m_SelectedGeometryMaterial->GetShader()
						? SortKeyFromRef(m_SelectedGeometryMaterial->GetShader().Raw()) : 0;
					populateDrawCommand(selDc,
						m_SelectedGeometryPass ? SortKeyFromRef(m_SelectedGeometryPass->GetPipeline().Raw()) : 0,
						selectedShaderSortKey,
						SortKeyFromRef(m_SelectedGeometryMaterial.Raw()));
					selDc.InstanceCount++;
				}
			}

			if (shadowVisible)
			{
				// ── Shadow pass list ──────────────────────────────────────────────
				m_ShadowMeshTransformMap[key].Cascade.ObjectIndices.push_back(transformIndex);

				auto& shadowDc = m_StaticMeshShadowPassDrawList[key];
				const uint64_t shadowShaderSortKey = m_ShadowPassMaterial && m_ShadowPassMaterial->GetShader()
					? SortKeyFromRef(m_ShadowPassMaterial->GetShader().Raw()) : 0;
				populateDrawCommand(shadowDc,
					m_ShadowMapPass ? SortKeyFromRef(m_ShadowMapPass->GetPipeline().Raw()) : 0,
					shadowShaderSortKey,
					SortKeyFromRef(m_ShadowPassMaterial.Raw()));
				shadowDc.InstanceCount++;
			}
		}
	}

	void SceneRenderer::SubmitPhysicsStaticDebugMesh(Ref<StaticMesh> staticMesh,
		Ref<MeshSource> meshSource,
		const glm::mat4& transform,
		bool isSimpleCollider)
	{
		SubmitStaticDebugMesh(m_StaticColliderDrawList, staticMesh, meshSource, transform,
			isSimpleCollider ? m_SimpleColliderMaterial : m_ComplexColliderMaterial);
	}

	void SceneRenderer::SubmitStaticDebugMesh(DrawCommandList& drawList,
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

			const uint32_t transformIndex = (uint32_t)m_TransformData.size();
			m_MeshTransformMap[key].ObjectIndices.push_back(transformIndex);

			auto& td = m_TransformData.emplace_back();
			td.MRow[0] = { submeshTransform[0][0], submeshTransform[1][0], submeshTransform[2][0], submeshTransform[3][0] };
			td.MRow[1] = { submeshTransform[0][1], submeshTransform[1][1], submeshTransform[2][1], submeshTransform[3][1] };
			td.MRow[2] = { submeshTransform[0][2], submeshTransform[1][2], submeshTransform[2][2], submeshTransform[3][2] };
			m_InstanceBoundsData.push_back({ CalculateWorldBoundsSphere(submesh.BoundingBox, submeshTransform) });

			auto& dc = drawList[key];
			dc.StaticMesh = staticMesh;
			dc.MeshSource = meshSource;
			dc.SubmeshIndex = submeshIndex;
			dc.MaterialHandle = fakeHandle;
			dc.OverrideMaterial = material;
			dc.PipelineSortKey = SortKeyFromRef(m_GeometryPipeline.Raw());
			dc.ShaderSortKey = material && material->GetShader() ? SortKeyFromRef(material->GetShader().Raw()) : 0;
			dc.MaterialSortKey = SortKeyFromRef(material.Raw());
			dc.MeshSortKey = (uint64_t)GetStaticMeshKeyHandle(staticMesh);
			dc.InstanceCount++;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// EndScene → FlushDrawList
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::EndScene()
	{
		LUX_CORE_ASSERT(m_Active);
		FlushDrawList();
		m_Active = false;
	}

	void SceneRenderer::WaitForThreads()
	{
		AssetManager::SyncWithAssetThread();
	}

	void SceneRenderer::FlushDrawList()
	{
		// Clear lists and bail if GPU resources not ready
		auto clearAll = [this]()
			{
				m_TransformData.clear();
				m_InstanceBoundsData.clear();
				m_MeshTransformMap.clear();
				m_ShadowMeshTransformMap.clear();
				m_StaticMeshDrawList.clear();
				m_TransparentStaticMeshDrawList.clear();
				m_SelectedStaticMeshDrawList.clear();
				m_StaticMeshShadowPassDrawList.clear();
				m_StaticColliderDrawList.clear();
				m_StaticMeshDrawOrder.clear();
				m_TransparentStaticMeshDrawOrder.clear();
				m_SelectedStaticMeshDrawOrder.clear();
				m_StaticMeshShadowPassDrawOrder.clear();
				m_StaticColliderDrawOrder.clear();
				m_MeshCullDrawCount = 0;
			};

		if (!m_ResourcesCreated)
		{
			clearAll();
			return;
		}

		// ── 1. Build the flat ObjectIndex array and assign base offsets ────────
		// Each MeshKey in m_MeshTransformMap gets a contiguous block of object indices.
		// The shader uses  objectIndex = ObjectIndexBase + gl_InstanceIndex
		// to look up its row in the InstanceTransforms SSBO.
		uint32_t cursor = 0;
		uint32_t visibleCursor = 0;
		std::vector<uint32_t> objectIndexData;
		std::vector<uint32_t> visibleObjectIndexData;
		std::vector<MeshCullDrawData> meshCullDrawData;
		std::vector<nvrhi::DrawIndexedIndirectArguments> indirectDrawData;

		BuildSortedDrawCommandOrder(m_SelectedStaticMeshDrawList, m_SelectedStaticMeshDrawOrder);
		BuildSortedDrawCommandOrder(m_StaticMeshDrawList, m_StaticMeshDrawOrder);
		BuildSortedDrawCommandOrder(m_TransparentStaticMeshDrawList, m_TransparentStaticMeshDrawOrder);
		BuildSortedDrawCommandOrder(m_StaticColliderDrawList, m_StaticColliderDrawOrder);
		BuildSortedDrawCommandOrder(m_StaticMeshShadowPassDrawList, m_StaticMeshShadowPassDrawOrder);

		auto isInstanceVisible = [this](uint32_t transformIndex)
			{
				if (!m_Options.EnableFrustumCulling)
					return true;

				if (transformIndex >= m_InstanceBoundsData.size())
					return true;

				const glm::vec4& sphereData = m_InstanceBoundsData[transformIndex].Sphere;
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
				const uint32_t objectIndex = idx * 3u;
				objectIndexData.push_back(idx * 3u);

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
				objectIndexData.push_back(idx * 3u);
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

		registerIndirectDraws(m_SelectedStaticMeshDrawList, m_SelectedStaticMeshDrawOrder);
		registerIndirectDraws(m_StaticMeshDrawList, m_StaticMeshDrawOrder);
		registerIndirectDraws(m_TransparentStaticMeshDrawList, m_TransparentStaticMeshDrawOrder);
		registerIndirectDraws(m_StaticColliderDrawList, m_StaticColliderDrawOrder);
		m_MeshCullDrawCount = (uint32_t)meshCullDrawData.size();

		const uint64_t shadowCasterHash = CalculateShadowCasterHash();
		const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
		const bool directionalShadowsEnabled = dirLight.Intensity > 0.0f && dirLight.CastShadows;
		if (directionalShadowsEnabled && (!m_DirectionalShadowMapCacheValid || shadowCasterHash != m_LastShadowCasterHash))
			m_DirectionalShadowMapNeedsRender = true;
		if (m_SpotShadowCount > 0 && (!m_SpotShadowMapCacheValid || shadowCasterHash != m_LastShadowCasterHash))
			m_SpotShadowMapNeedsRender = true;
		m_LastShadowCasterHash = shadowCasterHash;

		// ── 2. Upload InstanceTransforms, ObjectIndexes, culling data, and indirect args
		m_UploadCommandBuffer->Begin();

		if (!m_TransformData.empty())
		{
			const auto transformData = m_TransformData;
			const auto boundsData = m_InstanceBoundsData;
			const auto indexData = objectIndexData;
			const auto visibleIndexData = visibleObjectIndexData;
			const auto cullDrawData = meshCullDrawData;
			const auto indirectCommands = indirectDrawData;
			Ref<SceneRenderer> instance = this;

			Renderer::Submit([instance, transformData, boundsData, indexData, visibleIndexData, cullDrawData, indirectCommands]() mutable {

				Ref<RenderCommandBuffer> cmd = instance->m_UploadCommandBuffer;

				// Grow InstanceTransforms SSBO if needed
				const uint32_t transformBytes = (uint32_t)(sizeof(TransformVertexData) * transformData.size());
				if (instance->m_SBSInstanceTransforms->RT_Get()->GetHandle()->getDesc().byteSize < transformBytes)
					instance->m_SBSInstanceTransforms->Resize(transformBytes * 2u);
				instance->m_SBSInstanceTransforms->RT_Get()->RT_SetData(cmd, transformData.data(), transformBytes);

				const uint32_t boundsBytes = (uint32_t)(sizeof(InstanceBoundsData) * boundsData.size());
				if (boundsBytes > 0)
				{
					if (instance->m_SBSInstanceBounds->RT_Get()->GetHandle()->getDesc().byteSize < boundsBytes)
						instance->m_SBSInstanceBounds->Resize(boundsBytes * 2u);
					instance->m_SBSInstanceBounds->RT_Get()->RT_SetData(cmd, boundsData.data(), boundsBytes);
				}

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
			});
		}

		m_UploadCommandBuffer->End();
		m_UploadCommandBuffer->Submit();

		// ── 3. Execute render passes ──────────────────────────────────────────
		m_CommandBuffer->Begin();

		ShadowMapPass();
		SpotShadowMapPass();
		MeshCullingPass();
		PreDepthPass();
		HZBCompute();
		PreIntegration();
		LightCullingPass();
		SkyboxPass();
		GeometryPass();
		if (m_Options.EnableGTAO)
		{
			GTAOCompute();
			GTAODenoiseCompute();
			GTAOTemporalAccumulationCompute();
			AOComposite();
			if (m_DebugViewMode == DebugViewMode::AO)
				AODebugPass();
		}
		PreConvolutionCompute();
		if (m_Options.EnableSSR)
		{
			SSRCompute();
			SSRTemporalAccumulationCompute();
			SSRCompositePass();
		}
		if (m_Options.EnableJumpFlood && !m_SelectedStaticMeshDrawList.empty())
			JumpFloodPass();
		BloomCompute();
		CompositePass();
		if (m_Options.EnableJumpFlood && !m_SelectedStaticMeshDrawList.empty())
			JumpFloodCompositePass();

		if (m_Options.ShowGrid)
			GridPass();

		// ── 4. Renderer2D debug overlay ───────────────────────────────────────
		if (m_DebugRenderer && !m_DebugRenderer->GetRenderQueue().empty())
		{
			ScopedCPUProfile cpuProfile(*this, "Renderer2D");

			const auto& sceneCamera = m_SceneData.SceneCamera;
			const glm::mat4 viewProj = sceneCamera.Camera.GetProjectionMatrix() * sceneCamera.ViewMatrix;

			Ref<Renderer2D> overlayRenderer = m_Renderer2DScreenSpace ? m_Renderer2DScreenSpace : m_Renderer2D;
			overlayRenderer->SetTargetFramebuffer(m_CompositingFramebuffer);
			overlayRenderer->ResetStats();
			overlayRenderer->BeginScene(viewProj, sceneCamera.ViewMatrix);

			// Flush any queued DebugRenderer work
			for (auto& fn : m_DebugRenderer->GetRenderQueue())
				fn(overlayRenderer);
			m_DebugRenderer->ClearRenderQueue();

			// Physics collider outlines (2D wireframe lines)
			if (m_Options.ShowPhysicsColliders)
			{
				// Future: iterate m_StaticColliderDrawList and draw via Renderer2D AABB
				// For now the 3D collider meshes are drawn in GeometryPass via m_StaticColliderDrawList.
			}

			overlayRenderer->EndScene();
		}

		if (m_DOFSettings.Enabled)
			DOFPass();

		m_CommandBuffer->End();
		m_CommandBuffer->Submit();

		m_PreviousViewProjection = m_CurrentViewProjection;
		m_TemporalHistoryValid = true;

		// ── 5. Update statistics ──────────────────────────────────────────────
		UpdateStatistics();

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
		for (uint32_t cascade = 0; cascade < ShadowCascadeCount; cascade++)
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_ShadowMapPasses[cascade], /*explicitClear=*/true);

			for (const MeshKey& key : m_StaticMeshShadowPassDrawOrder)
			{
				const auto drawIt = m_StaticMeshShadowPassDrawList.find(key);
				if (drawIt == m_StaticMeshShadowPassDrawList.end()) continue;
				auto it = m_ShadowMeshTransformMap.find(key);
				if (it == m_ShadowMeshTransformMap.end()) continue;

				const auto& cascadeTmd = it->second.Cascade;
				const uint32_t instCount = (uint32_t)cascadeTmd.ObjectIndices.size();
				if (instCount == 0) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				drawCmd.InstanceCount = instCount;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, cascadeTmd, cascade]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, cascadeTmd, /*bindMaterial=*/false, cascade);
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

		for (uint32_t shadowIndex = 0; shadowIndex < m_SpotShadowCount; shadowIndex++)
		{
			const uint32_t tileX = shadowIndex % tilesPerRow;
			const uint32_t tileY = shadowIndex / tilesPerRow;
			Renderer::SetViewport(m_CommandBuffer, tileX * tileSize, tileY * tileSize, tileSize, tileSize);

			for (const MeshKey& key : m_StaticMeshShadowPassDrawOrder)
			{
				const auto drawIt = m_StaticMeshShadowPassDrawList.find(key);
				if (drawIt == m_StaticMeshShadowPassDrawList.end()) continue;
				auto it = m_ShadowMeshTransformMap.find(key);
				if (it == m_ShadowMeshTransformMap.end()) continue;
				const auto& cascadeTmd = it->second.Cascade;
				const uint32_t instCount = (uint32_t)cascadeTmd.ObjectIndices.size();
				if (instCount == 0) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				drawCmd.InstanceCount = instCount;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, cascadeTmd, shadowIndex]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, cascadeTmd, /*bindMaterial=*/false, shadowIndex);
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

		for (const MeshKey& key : m_StaticMeshDrawOrder)
		{
			const auto drawIt = m_StaticMeshDrawList.find(key);
			if (drawIt == m_StaticMeshDrawList.end()) continue;
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			const auto& tmd = it->second;
			StaticDrawCommand drawCmd = drawIt->second;

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, tmd]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/false, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering);
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

	void SceneRenderer::LightCullingPass()
	{
		ScopedCPUProfile cpuProfile(*this, "LightCullingPass");
		if (!m_LightCullingPass || m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		BeginProfiledGPU("LightCullingPass");
		Renderer::LightCulling(m_CommandBuffer, m_LightCullingPass, nullptr, { m_LightTilesCountX, m_LightTilesCountY, 1 });

		Ref<RenderCommandBuffer> commandBuffer = m_CommandBuffer;
		Ref<StorageBufferSet> visiblePointLightIndices = m_SBSVisiblePointLightIndices;
		Ref<StorageBufferSet> visibleSpotLightIndices = m_SBSVisibleSpotLightIndices;
		Renderer::Submit([commandBuffer, visiblePointLightIndices, visibleSpotLightIndices]() mutable
		{
			nvrhi::CommandListHandle commandList = commandBuffer->GetActive();
			commandList->setBufferState(visiblePointLightIndices->RT_Get()->GetHandle(), nvrhi::ResourceStates::ShaderResource);
			commandList->setBufferState(visibleSpotLightIndices->RT_Get()->GetHandle(), nvrhi::ResourceStates::ShaderResource);
		});

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
		Ref<TextureCube> radianceMap = GetEnvironmentRadianceMap(m_SceneData.SceneEnvironment);
		if (!radianceMap)
			return;

		BeginProfiledGPU("SkyboxPass");

		m_SkyboxMaterial->Set("u_Uniforms.TextureLod", m_SceneData.SkyboxLod);
		m_SkyboxMaterial->Set("u_Uniforms.Intensity", m_SceneData.SceneEnvironmentIntensity);
		m_SkyboxMaterial->Set("u_Texture", radianceMap);

		Renderer::BeginRenderPass(m_CommandBuffer, m_SkyboxPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_SkyboxPass->GetPipeline(), m_SkyboxMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GeometryPass()
	{
		ScopedCPUProfile cpuProfile(*this, "GeometryPass");
		BeginProfiledGPU("GeometryPass");

		// Selected geometry mask, matching Hazel's static selected path. Lux does
		// not run animation or jump-flood outline passes here.
		if (!m_SelectedStaticMeshDrawList.empty())
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_SelectedGeometryPass);

			for (const MeshKey& key : m_SelectedStaticMeshDrawOrder)
			{
				const auto drawIt = m_SelectedStaticMeshDrawList.find(key);
				if (drawIt == m_SelectedStaticMeshDrawList.end()) continue;
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				drawCmd.OverrideMaterial = m_SelectedGeometryMaterial;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering);
					});
			}

			Renderer::EndRenderPass(m_CommandBuffer);
		}

		// ── Opaque geometry ───────────────────────────────────────────────────
		Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryPass);

		for (const MeshKey& key : m_StaticMeshDrawOrder)
		{
			const auto drawIt = m_StaticMeshDrawList.find(key);
			if (drawIt == m_StaticMeshDrawList.end()) continue;
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			StaticDrawCommand drawCmd = drawIt->second;
			const auto& tmd = it->second;

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, tmd]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering);
				});
		}

		// Physics debug meshes drawn in the opaque geometry pass
		if (m_Options.ShowPhysicsColliders)
		{
			for (const MeshKey& key : m_StaticColliderDrawOrder)
			{
				const auto drawIt = m_StaticColliderDrawList.find(key);
				if (drawIt == m_StaticColliderDrawList.end()) continue;
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true, 0, /*useVisibleObjectIndexes=*/true, false);
					});
			}
		}

		Renderer::EndRenderPass(m_CommandBuffer);

		// ── Transparent geometry ──────────────────────────────────────────────
		if (!m_TransparentStaticMeshDrawList.empty())
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryPassTransparent);

			for (const MeshKey& key : m_TransparentStaticMeshDrawOrder)
			{
				const auto drawIt = m_TransparentStaticMeshDrawList.find(key);
				if (drawIt == m_TransparentStaticMeshDrawList.end()) continue;
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering);
					});
			}

			Renderer::EndRenderPass(m_CommandBuffer);
		}

		// ── Selected wireframe overlay ────────────────────────────────────────
		if (m_Options.ShowSelectedInWireframe && !m_SelectedStaticMeshDrawList.empty())
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryWireframePass);

			for (const MeshKey& key : m_SelectedStaticMeshDrawOrder)
			{
				const auto drawIt = m_SelectedStaticMeshDrawList.find(key);
				if (drawIt == m_SelectedStaticMeshDrawList.end()) continue;
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = drawIt->second;
				drawCmd.OverrideMaterial = m_WireframeMaterial;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true, 0, /*useVisibleObjectIndexes=*/true, instance->m_Options.EnableGPUDrivenRendering);
					});
			}

			Renderer::EndRenderPass(m_CommandBuffer);
		}

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
				m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
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
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
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
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
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
		BeginProfiledGPU("PreConvolution");
		Renderer::BeginComputePass(m_CommandBuffer, m_PreConvolutionComputePass);

		if (m_PreConvolutionMaterials[0])
		{
			auto [width, height] = m_PreConvolutedTexture.Texture->GetMipSize(0);
			const glm::uvec3 workGroups = { DivideRoundUp(glm::max(1u, width), 16u), DivideRoundUp(glm::max(1u, height), 16u), 1 };
			pushConstants.PrevLod = 0;
			pushConstants.Mode = 0;
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[0], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			m_PreConvolutionComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, preConvolutedImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
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
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[mip], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			m_PreConvolutionComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, preConvolutedImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

			pushConstants.Mode = 2;
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[mip], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			m_PreConvolutionComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, preConvolutedImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
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
			m_SSRCompositePass->SetInput("u_Normal", m_GeometryPass->GetOutput(1));
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
			m_SSRCompositePass->SetInput("u_Normal", m_GeometryPass->GetOutput(1));
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
		if (!m_DOFSettings.Enabled || !m_DOFPass || !m_DOFMaterial)
			return;

		const float focusDistance = glm::max(0.001f, m_DOFSettings.FocusDistance);
		m_DOFMaterial->Set("u_Uniforms.DOFParams", glm::vec2(focusDistance, m_DOFSettings.BlurSize));

		BeginProfiledGPU("DOF");
		Renderer::BeginRenderPass(m_CommandBuffer, m_DOFPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_DOFPass->GetPipeline(), m_DOFMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
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
		if (!m_BloomSettings.Enabled || !m_BloomComputePass || !m_BloomComputePipeline || !m_BloomComputeMaterials.PrefilterMaterial)
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

		const float knee = glm::max(m_BloomSettings.Knee, 0.0001f);
		pushConstants.Params = {
			m_BloomSettings.Threshold,
			m_BloomSettings.Threshold - knee,
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

		BeginProfiledGPU("BloomCompute");
		Renderer::BeginComputePass(m_CommandBuffer, m_BloomComputePass);

		// Prefilter
		pushConstants.Mode = 0;
		pushConstants.LOD = 0.0f;
		setTexSize(0);
		dispatchForMip(m_BloomComputeMaterials.PrefilterMaterial, 0);
		m_BloomComputePipeline->ImageMemoryBarrier(m_CommandBuffer, m_BloomComputeTextures[0].Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

		// Downsample, ping-ponging between texture 0 and texture 1.
		pushConstants.Mode = 1;
		for (uint32_t i = 1; i < mips; i++)
		{
			setTexSize(i);
			pushConstants.LOD = (float)i - 1.0f;
			dispatchForMip(m_BloomComputeMaterials.DownsampleAMaterials[i], i);
			m_BloomComputePipeline->ImageMemoryBarrier(m_CommandBuffer, m_BloomComputeTextures[1].Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

			pushConstants.LOD = (float)i;
			dispatchForMip(m_BloomComputeMaterials.DownsampleBMaterials[i], i);
			m_BloomComputePipeline->ImageMemoryBarrier(m_CommandBuffer, m_BloomComputeTextures[0].Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		// First upsample from the smallest downsampled mip.
		pushConstants.Mode = 2;
		pushConstants.LOD = (float)mips - 2.0f;
		setTexSize(mips - 1);
		dispatchForMip(m_BloomComputeMaterials.FirstUpsampleMaterial, mips - 2);
		m_BloomComputePipeline->ImageMemoryBarrier(m_CommandBuffer, m_BloomComputeTextures[2].Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

		// Upsample back to mip 0.
		pushConstants.Mode = 3;
		for (int32_t mip = (int32_t)mips - 3; mip >= 0; mip--)
		{
			pushConstants.LOD = (float)mip;
			setTexSize((uint32_t)mip + 1u);
			dispatchForMip(m_BloomComputeMaterials.UpsampleMaterials[mip], (uint32_t)mip);
			m_BloomComputePipeline->ImageMemoryBarrier(m_CommandBuffer, m_BloomComputeTextures[2].Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		Renderer::EndComputePass(m_CommandBuffer, m_BloomComputePass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::CompositePass()
	{
		ScopedCPUProfile cpuProfile(*this, "CompositePass");
		BeginProfiledGPU("CompositePass");

		m_CompositeMaterial->Set("u_Uniforms.Exposure", m_SceneData.SceneCamera.Camera.GetExposure());
		m_CompositeMaterial->Set("u_Uniforms.BloomIntensity", m_BloomSettings.Enabled ? m_BloomSettings.Intensity : 0.0f);
		m_CompositeMaterial->Set("u_Uniforms.BloomDirtIntensity", m_BloomSettings.Enabled ? m_BloomSettings.DirtIntensity : 0.0f);
		m_CompositeMaterial->Set("u_Uniforms.Opacity", m_Opacity);
		m_CompositeMaterial->Set("u_Uniforms.Time", Application::Get().GetTime());

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

		accumulate(m_SelectedStaticMeshDrawList, m_SelectedStaticMeshDrawOrder);
		accumulate(m_StaticMeshDrawList, m_StaticMeshDrawOrder);
		accumulate(m_TransparentStaticMeshDrawList, m_TransparentStaticMeshDrawOrder);

		if (m_Options.ShowPhysicsColliders)
			accumulate(m_StaticColliderDrawList, m_StaticColliderDrawOrder);

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
		const TransformMapData& tmd,
		bool                      bindMaterial,
		uint32_t                  lightIndex,
		bool                      useVisibleObjectIndexes,
		bool                      useIndirect)
	{
		// Non-const copy of the MeshSource Ref: MeshSource::GetVertexBuffer /
		// GetIndexBuffer / GetMaterials are not marked const, so we cannot call
		// them through a const Ref (which is what we get from a const StaticDrawCommand&).
		Ref<MeshSource> meshSource = dc.MeshSource;

		const auto& submesh = meshSource->GetSubmeshes()[dc.SubmeshIndex];
		nvrhi::GraphicsState& gs = cmd->GetGraphicsState();

		// ── Vertex buffer ─────────────────────────────────────────────────────
		nvrhi::VertexBufferBinding vbb;
		vbb.buffer = meshSource->GetVertexBuffer()->GetHandle();
		vbb.slot = 0;
		vbb.offset = 0;
		gs.vertexBuffers = { vbb };

		// ── Index buffer ──────────────────────────────────────────────────────
		nvrhi::IndexBufferBinding ibb;
		ibb.buffer = meshSource->GetIndexBuffer()->GetHandle();
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
						material = matAsset->GetMaterial();
			}

			if (!material)
				material = Renderer::GetDefaultWhiteMaterial();

			if (material)
			{
				material->Prepare();
				auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
				if (bindingSet)
					gs.bindings[0] = bindingSet;
			}
		}

		cmd->RT_CommitGraphicsState();

		// ── Push constants ────────────────────────────────────────────────────
		Buffer materialUniforms = material ? material->GetUniformStorageBuffer() : Buffer();
		const uint64_t pushConstantSize = std::max<uint64_t>(sizeof(MeshDrawPushConstants), materialUniforms.Size);
		std::vector<uint8_t> pushConstants(pushConstantSize);

		if (materialUniforms)
			std::memcpy(pushConstants.data(), materialUniforms.Data, materialUniforms.Size);

		auto& pc = *reinterpret_cast<MeshDrawPushConstants*>(pushConstants.data());
		pc.ObjectIndexBase = useVisibleObjectIndexes ? tmd.VisibleObjectIndexBase : tmd.ObjectIndexBase;
		pc.LightIndex = lightIndex;
		pc.BoneTransformBase = 0;
		pc.BoneTransformStride = 0;
		cmd->GetActive()->setPushConstants(pushConstants.data(), pushConstants.size());

		if (useIndirect && tmd.IndirectDrawOffsetBytes != std::numeric_limits<uint32_t>::max())
		{
			gs.indirectParams = m_SBSIndirectDrawCommands->RT_Get()->GetHandle();
			cmd->RT_CommitGraphicsState();
			cmd->GetActive()->drawIndexedIndirect(tmd.IndirectDrawOffsetBytes, 1);
			return;
		}

		const uint32_t instanceCount = useVisibleObjectIndexes ? tmd.VisibleInstanceCount : dc.InstanceCount;
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
				if (m_DOFSettings.Enabled && m_DOFPass)
					return m_DOFPass->GetOutput(0);
				if (m_CompositePass)
					return m_CompositePass->GetOutput(0);
				return nullptr;

			case DebugViewMode::Geometry:
				return m_GeometryPass ? m_GeometryPass->GetOutput(0) : nullptr;

			case DebugViewMode::Depth:
				return m_PreDepthPass ? m_PreDepthPass->GetDepthOutput() : nullptr;

			case DebugViewMode::Normals:
				return m_GeometryPass ? m_GeometryPass->GetOutput(1) : nullptr;

			case DebugViewMode::SSR:
				if (!m_Options.EnableSSR)
					return nullptr;
				return m_SSRFinalImage ? m_SSRFinalImage : m_SSRImage;

			case DebugViewMode::AO:
				if (!m_Options.EnableGTAO)
					return nullptr;
				return m_AODebugPass ? m_AODebugPass->GetOutput(0) : nullptr;

			case DebugViewMode::Bloom:
				if (!m_BloomSettings.Enabled)
					return nullptr;
				if (m_BloomComputeTextures.size() > 2 && m_BloomComputeTextures[2].Texture)
					return m_BloomComputeTextures[2].Texture->GetImage();
				return nullptr;

			case DebugViewMode::Composite:
				return m_CompositePass ? m_CompositePass->GetOutput(0) : nullptr;
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
		if (m_DOFSettings.Enabled && m_DOFPass)
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

	void SceneRenderer::SetLineWidth(float width)
	{
		m_LineWidth = width;
	}

} // namespace Lux
