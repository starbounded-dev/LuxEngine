// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Baked lightmaps (docs/GI_PLAN.md, GI3): the editor-only baker and the renderer's use of the result.
//
// Bake: each drawn (entity, submesh) gets a rectangle in one scene atlas, sized from its world surface area;
// the CPU rasterises the mesh's lightmap UVs (UV1, which is UV0 for meshes without a second UV set) into the
// rectangle to find a world position and normal per texel, and PathTraceBake.glsl then adds one cosine-
// weighted indirect path per texel per frame. After the target samples the atlas is read back, dilated
// into the gutters, saved as a sidecar (LightmapData) and put to use. Needs ray queries and the shader compiler.
//
// Use: GBuffer_Static.glsl adds albedo * (1 - metalness) * the lightmap to the emissive target. The renderer
// keeps two storage buffers (rects per GPU-scene instance, the atlas) bound to the G-buffer pass; empty rects
// mean "no lightmap" and the pass is bit-identical to before.

#include "lpch.h"
#include "Lux/Renderer/SceneRenderer.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/ComputePass.h"
#include "Lux/Renderer/Lightmaps/Lightmap.h"
#include "Lux/Renderer/PipelineCompute.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RayTracing/PathTraceLights.h"
#include "Lux/Renderer/RenderScene.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"
#include "Lux/Scene/Scene.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace Lux {

	namespace {

		struct BakeTexel
		{
			glm::vec4 Position;
			glm::vec4 Normal; // w = 1 where geometry covers the texel
		};

		struct BakePush
		{
			glm::uvec4 SizeFrameBounces;
			glm::uvec4 Counts;
			glm::vec4 SkyRadiance;
			glm::vec4 GroundRadiance;
		};

		struct BakeInstance
		{
			uint64_t EntityID = 0;
			uint32_t Submesh = 0;
			Ref<MeshSource> Source;
			glm::mat4 ObjectToWorld{ 1.0f };
			uint32_t Side = 0; // rectangle side in texels
			uint32_t X = 0, Y = 0;
		};

		glm::vec2 LightmapUV(const MeshSource& source, uint32_t vertexIndex)
		{
			const auto& attributes = source.GetVertexAttributes();
			glm::vec2 uv;
			if (vertexIndex < attributes.size())
				uv = glm::unpackHalf2x16(attributes[vertexIndex].UV1);
			else
				uv = source.GetVertices()[vertexIndex].Texcoord;
			return { uv.x, 1.0f - uv.y }; // GBuffer_Static.glsl flips v
		}

		// Triangle k of a submesh as vertex indices into the mesh source.
		bool SubmeshTriangle(const MeshSource& source, const Submesh& submesh, uint32_t triangle, uint32_t out[3])
		{
			const uint32_t scalar = submesh.BaseIndex + triangle * 3;
			for (uint32_t corner = 0; corner < 3; corner++)
			{
				const uint32_t s = scalar + corner;
				const uint32_t t = s / 3;
				if (t >= source.GetIndices().size())
					return false;
				const Index& index = source.GetIndices()[t];
				const uint32_t local = (s % 3) == 0 ? index.V1 : ((s % 3) == 1 ? index.V2 : index.V3);
				const uint32_t vertex = submesh.BaseVertex + local;
				if (vertex >= source.GetVertices().size())
					return false;
				out[corner] = vertex;
			}
			return true;
		}

	}

	// Everything one bake needs, shared between the thread that records the pass and the render thread.
	struct SceneRenderer::LightmapBakeState
	{
		enum class Stage { Prepare, Run, Settle, Done, Failed };
		Stage CurrentStage = Stage::Prepare;
		uint32_t TargetSamples = 128;
		float TexelsPerMetre = 16.0f;
		uint32_t AtlasSize = 1024;
		std::atomic<uint32_t> Frame{ 0 };
		uint32_t SettleFrames = 0;
		std::vector<BakeInstance> Instances;
		std::string Status = "starting";
		std::vector<LightmapData::Entry> Entries;
	};

	bool SceneRenderer::StartLightmapBake(uint32_t samples, float texelsPerMetre)
	{
		if (m_LightmapBake && m_LightmapBake->CurrentStage != LightmapBakeState::Stage::Done && m_LightmapBake->CurrentStage != LightmapBakeState::Stage::Failed)
			return false;
		if (!RHIDevice::SupportsRayQuery() || !ShaderCompiler::IsAvailable())
			return false;
		m_LightmapBake = std::make_shared<LightmapBakeState>();
		m_LightmapBake->TargetSamples = std::max(1u, samples);
		m_LightmapBake->TexelsPerMetre = std::max(1.0f, texelsPerMetre);
		return true;
	}

	bool SceneRenderer::IsLightmapBaking() const
	{
		return m_LightmapBake && m_LightmapBake->CurrentStage != LightmapBakeState::Stage::Done && m_LightmapBake->CurrentStage != LightmapBakeState::Stage::Failed;
	}

	float SceneRenderer::GetLightmapBakeProgress() const
	{
		if (!m_LightmapBake)
			return 0.0f;
		if (m_LightmapBake->CurrentStage == LightmapBakeState::Stage::Done)
			return 1.0f;
		return std::min(1.0f, static_cast<float>(m_LightmapBake->Frame.load()) / m_LightmapBake->TargetSamples);
	}

	std::string SceneRenderer::GetLightmapBakeStatus() const
	{
		return m_LightmapBake ? m_LightmapBake->Status : std::string();
	}

	void SceneRenderer::ClearLightmap()
	{
		m_Lightmap = nullptr;
		m_LightmapRectsSignature = 0;
		if (m_LightmapRects)
		{
			if (void* mapped = m_LightmapRects->GetBuffer().Map(0, 256))
			{
				std::memset(mapped, 0, 256);
				m_LightmapRects->GetBuffer().Unmap();
			}
		}
	}

	void SceneRenderer::RefreshLightmap()
	{
		if (!m_Scene)
			return;
		const std::filesystem::path path = LightmapData::PathForScene(m_Scene->GetName());
		Ref<LightmapData> loaded = path.empty() ? nullptr : LightmapData::Load(path);
		if (!loaded)
		{
			ClearLightmap();
			return;
		}
		SetLightmap(loaded);
	}

	void SceneRenderer::SetLightmap(Ref<LightmapData> lightmap)
	{
		if (!lightmap)
		{
			ClearLightmap();
			return;
		}
		m_Lightmap = lightmap;
		m_LightmapRectsSignature = 0;

		// The atlas buffer follows the lightmap; the render thread may still be reading the old one
		// (StorageBuffer::Resize replaces the NRI buffer through the deletion queue).
		const std::vector<uint32_t> packed = lightmap->PackedForGPU();
		const uint32_t bytes = static_cast<uint32_t>(packed.size() * sizeof(uint32_t));
		if (m_LightmapAtlas->GetSize() < bytes)
			m_LightmapAtlas->Resize(bytes);
		m_LightmapAtlas->SetData(m_CommandBuffer, packed.data(), bytes);
		LUX_CORE_INFO_TAG("Core", "[Lightmap] using {}x{} atlas with {} lit submeshes", lightmap->Width, lightmap->Height, lightmap->Entries.size());
	}

	// Writes the per-instance rectangles when they changed (instance IDs move as entities come and go).
	void SceneRenderer::UpdateLightmapRects()
	{
		if (!m_Lightmap || !m_Options.EnableLightmaps)
		{
			// Off: leave the rects zero so the G-buffer pass adds nothing.
			if (m_LightmapRectsSignature != 0)
			{
				m_LightmapRectsSignature = 0;
				const std::vector<uint8_t> zeros(m_LightmapRects->GetSize(), 0);
				m_LightmapRects->SetData(m_CommandBuffer, zeros.data(), static_cast<uint32_t>(zeros.size()));
			}
			return;
		}
		if (!m_SubmittedRenderScene)
			return;

		struct Item { uint32_t InstanceID; glm::vec4 Rect; };
		std::vector<Item> items;
		uint32_t maxID = 0;
		for (const StaticMeshRenderProxy* proxy : m_SubmittedRenderScene->GetStaticMeshProxies())
		{
			if (!proxy)
				continue;
			for (const GPUSceneInstanceRef& ref : proxy->SubmeshInstances)
			{
				if (ref.InstanceID == InvalidGPUSceneInstanceID)
					continue;
				for (const LightmapData::Entry& entry : m_Lightmap->Entries)
				{
					if (entry.EntityID == static_cast<uint64_t>(proxy->EntityID) && entry.Submesh == ref.Data.Metadata.y)
					{
						items.push_back({ ref.InstanceID, entry.Rect });
						maxID = std::max(maxID, ref.InstanceID);
						break;
					}
				}
			}
		}

		uint64_t signature = 1469598103934665603ull;
		for (const Item& item : items)
		{
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&item);
			for (size_t i = 0; i < sizeof(Item); i++)
			{
				signature ^= bytes[i];
				signature *= 1099511628211ull;
			}
		}
		signature ^= m_Lightmap.Raw() ? reinterpret_cast<uintptr_t>(m_Lightmap.Raw()) : 0;
		if (signature == m_LightmapRectsSignature)
			return;
		m_LightmapRectsSignature = signature;

		// Always the whole buffer, zero-padded: a host-visible StorageBuffer rewrites its entire local copy, so anything
		// past what this writes would otherwise be uninitialised memory read as rectangles.
		if (m_LightmapRects->GetSize() < (static_cast<size_t>(maxID) + 2) * sizeof(glm::vec4))
			m_LightmapRects->Resize(static_cast<uint32_t>((static_cast<size_t>(maxID) + 2) * sizeof(glm::vec4)));
		std::vector<glm::vec4> rects(m_LightmapRects->GetSize() / sizeof(glm::vec4), glm::vec4(0.0f));
		rects[0] = glm::vec4(static_cast<float>(m_Lightmap->Width), static_cast<float>(m_Lightmap->Height), 0.0f, 0.0f);
		for (const Item& item : items)
			rects[1 + item.InstanceID] = item.Rect;
		const uint32_t bytes = static_cast<uint32_t>(rects.size() * sizeof(glm::vec4));
		m_LightmapRects->SetData(m_CommandBuffer, rects.data(), bytes);
	}

	void SceneRenderer::LightmapBakePass()
	{
		ScopedCPUProfile cpuProfile(*this, "LightmapBakePass");
		if (!m_LightmapBake)
			return;
		LightmapBakeState& bake = *m_LightmapBake;
		using Stage = LightmapBakeState::Stage;
		if (bake.CurrentStage == Stage::Done || bake.CurrentStage == Stage::Failed)
			return;

		auto fail = [&](const char* message)
		{
			bake.CurrentStage = Stage::Failed;
			bake.Status = message;
			LUX_CORE_ERROR_TAG("Core", "[Lightmap] bake failed: {}", message);
		};

		if (!m_SubmittedRenderScene)
			return; // nothing drawn yet: try next frame

		// ---- Prepare: rectangles, texels and the GPU resources ------------------------------------------------
		if (bake.CurrentStage == Stage::Prepare)
		{
			bake.Status = "preparing";
			for (const StaticMeshRenderProxy* proxy : m_SubmittedRenderScene->GetStaticMeshProxies())
			{
				if (!proxy || !proxy->Visible || !proxy->MeshSource || !proxy->StaticMesh || proxy->IsSkinned())
					continue;
				const auto& submeshes = proxy->MeshSource->GetSubmeshes();
				for (uint32_t submeshIndex : proxy->StaticMesh->GetSubmeshes())
				{
					if (submeshIndex >= submeshes.size() || submeshes[submeshIndex].IndexCount < 3)
						continue;
					BakeInstance instance;
					instance.EntityID = static_cast<uint64_t>(proxy->EntityID);
					instance.Submesh = submeshIndex;
					instance.Source = proxy->MeshSource;
					instance.ObjectToWorld = proxy->WorldTransform * submeshes[submeshIndex].Transform;
					bake.Instances.push_back(std::move(instance));
				}
			}
			if (bake.Instances.empty())
				return fail("the scene has no static meshes");

			// Rectangle sizes from world area / UV coverage so texel density is uniform across the scene.
			for (BakeInstance& instance : bake.Instances)
			{
				const Submesh& submesh = instance.Source->GetSubmeshes()[instance.Submesh];
				double worldArea = 0.0, uvArea = 0.0;
				for (uint32_t t = 0; t < submesh.IndexCount / 3; t++)
				{
					uint32_t v[3];
					if (!SubmeshTriangle(*instance.Source, submesh, t, v))
						continue;
					glm::vec3 p[3];
					glm::vec2 uv[3];
					for (int i = 0; i < 3; i++)
					{
						p[i] = glm::vec3(instance.ObjectToWorld * glm::vec4(instance.Source->GetVertices()[v[i]].Position, 1.0f));
						uv[i] = LightmapUV(*instance.Source, v[i]);
					}
					worldArea += 0.5 * glm::length(glm::cross(p[1] - p[0], p[2] - p[0]));
					uvArea += 0.5 * std::abs((uv[1].x - uv[0].x) * (uv[2].y - uv[0].y) - (uv[2].x - uv[0].x) * (uv[1].y - uv[0].y));
				}
				const double coverage = std::max(uvArea, 0.02);
				const double side = std::sqrt(worldArea * bake.TexelsPerMetre * bake.TexelsPerMetre / coverage);
				instance.Side = static_cast<uint32_t>(std::clamp(std::ceil(side), 16.0, 512.0));
			}

			// Shelf packing (largest first) with a 2-texel gutter, doubling the atlas until everything fits.
			std::vector<size_t> order(bake.Instances.size());
			for (size_t i = 0; i < order.size(); i++)
				order[i] = i;
			std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return bake.Instances[a].Side > bake.Instances[b].Side; });
			bool packed = false;
			for (uint32_t size = 1024; size <= 4096 && !packed; size *= 2)
			{
				uint32_t x = 0, y = 0, shelf = 0;
				packed = true;
				for (size_t index : order)
				{
					BakeInstance& instance = bake.Instances[index];
					const uint32_t footprint = instance.Side + 2;
					if (x + footprint > size)
					{
						x = 0;
						y += shelf;
						shelf = 0;
					}
					if (y + footprint > size || footprint > size)
					{
						packed = false;
						break;
					}
					instance.X = x + 1;
					instance.Y = y + 1;
					x += footprint;
					shelf = std::max(shelf, footprint);
				}
				if (packed)
					bake.AtlasSize = size;
			}
			if (!packed)
				return fail("the lightmap atlas does not fit in 4096x4096; lower the texel density");

			const uint32_t size = bake.AtlasSize;
			std::vector<BakeTexel> texels(static_cast<size_t>(size) * size, BakeTexel{ glm::vec4(0.0f), glm::vec4(0.0f) });
			for (const BakeInstance& instance : bake.Instances)
			{
				const Submesh& submesh = instance.Source->GetSubmeshes()[instance.Submesh];
				const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(instance.ObjectToWorld)));
				for (uint32_t t = 0; t < submesh.IndexCount / 3; t++)
				{
					uint32_t v[3];
					if (!SubmeshTriangle(*instance.Source, submesh, t, v))
						continue;
					glm::vec2 uv[3];
					glm::vec3 position[3], normal[3];
					for (int i = 0; i < 3; i++)
					{
						const Vertex& vertex = instance.Source->GetVertices()[v[i]];
						uv[i] = LightmapUV(*instance.Source, v[i]) * static_cast<float>(instance.Side);
						position[i] = glm::vec3(instance.ObjectToWorld * glm::vec4(vertex.Position, 1.0f));
						normal[i] = normalMatrix * vertex.Normal;
					}
					const float denominator = (uv[1].y - uv[2].y) * (uv[0].x - uv[2].x) + (uv[2].x - uv[1].x) * (uv[0].y - uv[2].y);
					if (std::abs(denominator) < 1e-9f)
						continue;
					const glm::vec2 minUV = glm::min(uv[0], glm::min(uv[1], uv[2]));
					const glm::vec2 maxUV = glm::max(uv[0], glm::max(uv[1], uv[2]));
					const int x0 = std::max(0, static_cast<int>(std::floor(minUV.x)) - 1), x1 = std::min(static_cast<int>(instance.Side) - 1, static_cast<int>(std::ceil(maxUV.x)) + 1);
					const int y0 = std::max(0, static_cast<int>(std::floor(minUV.y)) - 1), y1 = std::min(static_cast<int>(instance.Side) - 1, static_cast<int>(std::ceil(maxUV.y)) + 1);
					for (int y = y0; y <= y1; y++)
					{
						for (int x = x0; x <= x1; x++)
						{
							const glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
							const float w0 = ((uv[1].y - uv[2].y) * (p.x - uv[2].x) + (uv[2].x - uv[1].x) * (p.y - uv[2].y)) / denominator;
							const float w1 = ((uv[2].y - uv[0].y) * (p.x - uv[2].x) + (uv[0].x - uv[2].x) * (p.y - uv[2].y)) / denominator;
							const float w2 = 1.0f - w0 - w1;
							constexpr float k_Epsilon = -1e-3f;
							if (w0 < k_Epsilon || w1 < k_Epsilon || w2 < k_Epsilon)
								continue;
							BakeTexel& texel = texels[static_cast<size_t>(instance.Y + y) * size + (instance.X + x)];
							if (texel.Normal.w > 0.5f)
								continue; // first triangle wins (overlapping UVs are a documented limit)
							const glm::vec3 n = glm::normalize(normal[0] * w0 + normal[1] * w1 + normal[2] * w2);
							texel.Position = glm::vec4(position[0] * w0 + position[1] * w1 + position[2] * w2, 0.0f);
							texel.Normal = glm::vec4(n, 1.0f);
						}
					}
				}
				LightmapData::Entry entry;
				entry.EntityID = instance.EntityID;
				entry.Submesh = instance.Submesh;
				const float inv = 1.0f / static_cast<float>(size);
				entry.Rect = glm::vec4(instance.Side * inv, instance.Side * inv, instance.X * inv, instance.Y * inv);
				bake.Entries.push_back(entry);
			}

			// GPU resources for this bake.
			auto library = Renderer::GetShaderLibrary();
			if (library->GetShaders().find("PathTraceBake") == library->GetShaders().end())
				library->Load("Resources/Shaders/PathTracing/PathTraceBake.glsl");
			if (library->GetShaders().find("PathTraceBake") == library->GetShaders().end())
				return fail("PathTraceBake.glsl did not compile");
			if (!m_RayTracingScene)
				m_RayTracingScene = Ref<RayTracingScene>::Create();

			StorageBufferSpecification spec;
			spec.GPUOnly = false;
			spec.DebugName = "Lightmap Texels";
			m_LightmapBakeTexels = StorageBuffer::Create(static_cast<uint32_t>(texels.size() * sizeof(BakeTexel)), spec);
			m_LightmapBakeTexels->SetData(m_CommandBuffer, texels.data(), static_cast<uint32_t>(texels.size() * sizeof(BakeTexel)));
			spec.DebugName = "Lightmap Accum";
			m_LightmapBakeAccum = StorageBuffer::Create(static_cast<uint32_t>(texels.size() * sizeof(glm::vec4)), spec);
			if (!m_PathTraceLights)
			{
				StorageBufferSpecification lightSpec;
				lightSpec.GPUOnly = false;
				lightSpec.DebugName = "PathTrace Lights";
				m_PathTraceLights = StorageBuffer::Create(256, lightSpec);
			}

			ComputePassSpecification passSpec;
			passSpec.DebugName = "LightmapBake";
			passSpec.Pipeline = PipelineCompute::Create(library->Get("PathTraceBake"));
			m_LightmapBakePass = ComputePass::Create(passSpec);
			m_LightmapBakePass->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
			m_LightmapBakePass->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
			m_LightmapBakePass->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
			m_LightmapBakePass->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
			m_LightmapBakePass->SetInput("RTLights", m_PathTraceLights);
			m_LightmapBakePass->SetInput("LMTexels", m_LightmapBakeTexels);
			m_LightmapBakePass->SetInput("LMAccum", m_LightmapBakeAccum);
			PreparePathTraceEnvironment(m_LightmapBakePass);
			if (!m_LightmapBakePass->Validate())
				return fail("the bake pass inputs do not match its shader");
			m_LightmapBakePass->Bake();
			bake.CurrentStage = Stage::Run;
			bake.Status = "baking";
			return;
		}

		// ---- Run: one sample per texel per frame -----------------------------------------------------------------
		if (bake.CurrentStage == Stage::Run)
		{
			std::vector<RayTracingScene::InstanceInput> instances = RayTracingScene::GatherInstances(m_SubmittedRenderScene->GetStaticMeshProxies());
			std::vector<glm::vec4> lights = PackLights(m_SceneData.SceneLightEnvironment);
			const uint32_t size = bake.AtlasSize;
			const uint32_t target = bake.TargetSamples;
			const uint32_t maxBounces = std::clamp(m_Options.PathTracerMaxBounces, 1u, 16u);
			Ref<RenderCommandBuffer> cb = m_CommandBuffer;
			Ref<RayTracingScene> scene = m_RayTracingScene;
			Ref<ComputePass> pass = m_LightmapBakePass;
			Ref<StorageBuffer> lightBuffer = m_PathTraceLights;
			std::shared_ptr<LightmapBakeState> state = m_LightmapBake;
			const PathTraceEnvParams envParams = PreparePathTraceEnvironment(m_LightmapBakePass);
			if (state->Frame.load() < target)
			{
				Renderer::Submit([=, instances = std::move(instances), lights = std::move(lights)]() mutable
				{
					scene->RT_Update(cb, instances);
					if (!scene->IsValid() || state->Frame.load() >= target)
						return;
					const uint32_t lightBytes = static_cast<uint32_t>(lights.size() * sizeof(glm::vec4));
					if (lightBuffer->GetSize() < lightBytes)
						lightBuffer->Resize(lightBytes);
					if (lightBytes > 0)
						lightBuffer->RT_SetData(cb, lights.data(), lightBytes);

					BakePush push = {};
					push.SizeFrameBounces = glm::uvec4(size * size, 0u, state->Frame.load(), maxBounces);
					push.Counts = glm::uvec4(static_cast<uint32_t>(lights.size() / 4), 0u, 0u, 0u);
					push.SkyRadiance = glm::vec4(0.35f, 0.5f, 0.8f, 0.0f);
					push.GroundRadiance = glm::vec4(0.12f, 0.1f, 0.09f, 0.0f);
					ApplyPathTraceEnvironment(push, envParams);
					cb->RT_BeginMarker("LightmapBake");
					pass->Prepare();
					Renderer::RT_DispatchCompute(*cb, *pass, glm::uvec3((size * size + 63u) / 64u, 1u, 1u), Buffer(&push, sizeof(push)));
					cb->RT_EndMarker();
					state->Frame++;
				});
				return;
			}
			bake.CurrentStage = Stage::Settle;
			bake.SettleFrames = 0;
		}

		// ---- Settle, then read back, dilate, save and use ------------------------------------------------------
		if (bake.CurrentStage == Stage::Settle)
		{
			if (++bake.SettleFrames < 6)
				return; // let the GPU finish the last dispatches (host-visible results)
			const uint32_t size = bake.AtlasSize;
			const size_t texelCount = static_cast<size_t>(size) * size;
			const void* accumMapped = m_LightmapBakeAccum->GetBuffer().Map(0, texelCount * sizeof(glm::vec4));
			const void* texelMapped = m_LightmapBakeTexels->GetBuffer().Map(0, texelCount * sizeof(BakeTexel));
			if (!accumMapped || !texelMapped)
				return fail("could not read the bake results back");

			std::vector<glm::vec3> radiance(texelCount, glm::vec3(0.0f));
			std::vector<uint8_t> covered(texelCount, 0);
			for (size_t i = 0; i < texelCount; i++)
			{
				if (static_cast<const BakeTexel*>(texelMapped)[i].Normal.w > 0.5f)
				{
					radiance[i] = glm::vec3(static_cast<const glm::vec4*>(accumMapped)[i]);
					covered[i] = 1;
				}
			}
			m_LightmapBakeTexels->GetBuffer().Unmap();
			m_LightmapBakeAccum->GetBuffer().Unmap();

			// Fill the gutters (and any uncovered texel next to covered ones) so bilinear taps never reach black.
			for (int pass = 0; pass < 3; pass++)
			{
				std::vector<glm::vec3> nextRadiance = radiance;
				std::vector<uint8_t> nextCovered = covered;
				for (int y = 0; y < static_cast<int>(size); y++)
				{
					for (int x = 0; x < static_cast<int>(size); x++)
					{
						const size_t index = static_cast<size_t>(y) * size + x;
						if (covered[index])
							continue;
						glm::vec3 sum(0.0f);
						int count = 0;
						for (int dy = -1; dy <= 1; dy++)
							for (int dx = -1; dx <= 1; dx++)
							{
								const int nx = x + dx, ny = y + dy;
								if (nx < 0 || ny < 0 || nx >= static_cast<int>(size) || ny >= static_cast<int>(size))
									continue;
								const size_t neighbour = static_cast<size_t>(ny) * size + nx;
								if (covered[neighbour])
								{
									sum += radiance[neighbour];
									count++;
								}
							}
						if (count > 0)
						{
							nextRadiance[index] = sum / static_cast<float>(count);
							nextCovered[index] = 1;
						}
					}
				}
				radiance.swap(nextRadiance);
				covered.swap(nextCovered);
			}

			Ref<LightmapData> result = Ref<LightmapData>::Create();
			result->Width = size;
			result->Height = size;
			result->Entries = bake.Entries;
			result->Texels.resize(texelCount * 4);
			for (size_t i = 0; i < texelCount; i++)
			{
				result->Texels[i * 4 + 0] = glm::packHalf1x16(radiance[i].r);
				result->Texels[i * 4 + 1] = glm::packHalf1x16(radiance[i].g);
				result->Texels[i * 4 + 2] = glm::packHalf1x16(radiance[i].b);
				result->Texels[i * 4 + 3] = glm::packHalf1x16(1.0f);
			}

			const std::filesystem::path path = m_Scene ? LightmapData::PathForScene(m_Scene->GetName()) : std::filesystem::path();
			if (!path.empty() && !result->Save(path))
				LUX_CORE_WARN_TAG("Core", "[Lightmap] could not save {}", path.string());
			SetLightmap(result);

			m_LightmapBakeTexels = nullptr;
			m_LightmapBakeAccum = nullptr;
			m_LightmapBakePass = nullptr;
			bake.Instances.clear();
			bake.CurrentStage = Stage::Done;
			bake.Status = "done";
			LUX_CORE_INFO_TAG("Core", "[Lightmap] bake done: {} submeshes, {}x{} atlas, {} samples", result->Entries.size(), size, size, bake.TargetSamples);
		}
	}

}
