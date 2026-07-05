#pragma once

// Internal helpers shared by the SceneRenderer translation units (the split
// SceneRenderer*.cpp files). Not part of the public SceneRenderer API.

#include "SceneRenderer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RenderScene.h"
#include "Lux/Renderer/Exposure.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>

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

		uint64_t HashMat4(uint64_t seed, const glm::mat4& value)
		{
			for (uint32_t column = 0; column < 4; column++)
				seed = HashVec4(seed, value[column]);
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

}
