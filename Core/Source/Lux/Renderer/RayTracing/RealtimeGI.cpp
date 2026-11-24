// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Realtime GI, GI4 (docs/GI_PLAN.md): a probe volume over the scene bounds updated by ray queries every frame
// (GIProbeUpdate.glsl) and read by the G-buffer pass as indirect diffuse (GBuffer_Static.glsl). RT-only and
// opt-in (SceneRendererOptions::EnableRealtimeGI); nothing here exists while it is off.

#include "lpch.h"
#include "Lux/Renderer/SceneRenderer.h"

#include "Lux/Renderer/ComputePass.h"
#include "Lux/Renderer/PipelineCompute.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RayTracing/PathTraceLights.h"
#include "Lux/Renderer/RenderScene.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"

#include <atomic>
#include <cstring>

namespace Lux {

	struct SceneRenderer::RealtimeGIState
	{
		uint32_t Frame = 0;       // render thread: frames since the scene, lights or grid last changed
		uint64_t Signature = 0;
		std::atomic<uint32_t> Updates{ 0 };
	};

	namespace {

		constexpr glm::uvec3 k_ProbeDims = { 16u, 8u, 16u };

		struct GIPush
		{
			glm::uvec4 SizeFrameBounces; // probe count, rays per probe, frame, bounces
			glm::uvec4 Counts;
			glm::vec4 SkyRadiance;
			glm::vec4 GroundRadiance;
			glm::vec4 Medium;       // fog: density, anisotropy, radius (PathTraceCommon.glslh)
			glm::vec4 MediumAlbedo;
			glm::vec4 MediumCenter;
		};

		uint64_t HashBytes(uint64_t hash, const void* data, size_t size)
		{
			const uint8_t* bytes = static_cast<const uint8_t*>(data);
			for (size_t i = 0; i < size; i++)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
			return hash;
		}

	}

	uint32_t SceneRenderer::GetRealtimeGIUpdateCount() const
	{
		return m_RealtimeGIState ? m_RealtimeGIState->Updates.load() : 0u;
	}

	// Switching it off clears the grid's valid flag (the G-buffer pass then reads nothing). The probes are rewritten
	// by the pass each frame it runs, so only the flag needs care.
	void SceneRenderer::SyncRealtimeGI()
	{
		if (m_Options.EnableRealtimeGI && RHIDevice::SupportsRayQuery())
		{
			m_GIProbesActive = true;
			return;
		}
		if (!m_GIProbesActive || !m_GIProbes)
			return;
		m_GIProbesActive = false;
		if (void* mapped = m_GIProbes->GetBuffer().Map(0, sizeof(glm::vec4)))
		{
			std::memset(mapped, 0, sizeof(glm::vec4));
			m_GIProbes->GetBuffer().Unmap();
		}
	}

	bool SceneRenderer::EnsureRealtimeGI()
	{
		if (m_GIProbePass)
			return true;
		if (m_RealtimeGIUnavailable || !RHIDevice::SupportsRayQuery() || !ShaderCompiler::IsAvailable() || !m_GIProbes)
			return false;

		auto library = Renderer::GetShaderLibrary();
		if (library->GetShaders().find("GIProbeUpdate") == library->GetShaders().end())
			library->Load("Resources/Shaders/PathTracing/GIProbeUpdate.glsl");
		if (library->GetShaders().find("GIProbeUpdate") == library->GetShaders().end())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RealtimeGI] GIProbeUpdate.glsl did not compile; realtime GI is off for this session");
			m_RealtimeGIUnavailable = true;
			return false;
		}
		if (!m_RayTracingScene)
			m_RayTracingScene = Ref<RayTracingScene>::Create();
		if (!m_PathTraceLights)
		{
			StorageBufferSpecification lightSpec;
			lightSpec.GPUOnly = false;
			lightSpec.DebugName = "PathTrace Lights";
			m_PathTraceLights = StorageBuffer::Create(256, lightSpec);
		}
		m_RealtimeGIState = std::make_shared<RealtimeGIState>();

		ComputePassSpecification passSpec;
		passSpec.DebugName = "GIProbeUpdate";
		passSpec.Pipeline = PipelineCompute::Create(library->Get("GIProbeUpdate"));
		m_GIProbePass = ComputePass::Create(passSpec);
		m_GIProbePass->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
		m_GIProbePass->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
		m_GIProbePass->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
		m_GIProbePass->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
		m_GIProbePass->SetInput("RTLights", m_PathTraceLights);
		m_GIProbePass->SetInput("GIProbes", m_GIProbes);
		PreparePathTraceEnvironment(m_GIProbePass);
		if (!m_GIProbePass->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RealtimeGI] the probe pass inputs do not match its shader; realtime GI is off for this session");
			m_GIProbePass = nullptr;
			m_RealtimeGIUnavailable = true;
			return false;
		}
		m_GIProbePass->Bake();
		LUX_CORE_INFO_TAG("Renderer", "[RealtimeGI] ready: {}x{}x{} probes", k_ProbeDims.x, k_ProbeDims.y, k_ProbeDims.z);
		return true;
	}

	void SceneRenderer::RealtimeGIPass()
	{
		ScopedCPUProfile cpuProfile(*this, "RealtimeGIPass");
		ApplyRTTuning();
		if (!m_SubmittedRenderScene || !EnsureRealtimeGI())
			return;

		// The grid covers the scene's static meshes with a margin.
		glm::vec3 boundsMin(1e30f), boundsMax(-1e30f);
		std::vector<const StaticMeshRenderProxy*> proxies = m_SubmittedRenderScene->GetStaticMeshProxies();
		for (const StaticMeshRenderProxy* proxy : proxies)
		{
			if (!proxy || !proxy->Visible || !proxy->MeshSource || !proxy->StaticMesh || proxy->IsSkinned())
				continue;
			const auto& submeshes = proxy->MeshSource->GetSubmeshes();
			for (uint32_t submeshIndex : proxy->StaticMesh->GetSubmeshes())
			{
				if (submeshIndex >= submeshes.size())
					continue;
				const AABB& box = submeshes[submeshIndex].BoundingBox;
				const glm::mat4 transform = proxy->WorldTransform * submeshes[submeshIndex].Transform;
				for (int corner = 0; corner < 8; corner++)
				{
					const glm::vec3 local((corner & 1) ? box.Max.x : box.Min.x, (corner & 2) ? box.Max.y : box.Min.y, (corner & 4) ? box.Max.z : box.Min.z);
					const glm::vec3 world = glm::vec3(transform * glm::vec4(local, 1.0f));
					boundsMin = glm::min(boundsMin, world);
					boundsMax = glm::max(boundsMax, world);
				}
			}
		}
		if (boundsMin.x > boundsMax.x)
			return;
		const glm::vec3 margin = glm::max((boundsMax - boundsMin) * 0.02f, glm::vec3(0.05f));
		boundsMin -= margin;
		boundsMax += margin;
		const glm::vec3 cell = (boundsMax - boundsMin) / glm::vec3(k_ProbeDims - glm::uvec3(1));

		// Grid header, written straight into the mapped buffer (the probe region is written by the GPU, so the
		// buffer's CPU copy must never be uploaded over it).
		const glm::vec4 header[3] = { glm::vec4(boundsMin, 1.0f), glm::vec4(cell, 0.0f), glm::vec4(glm::vec3(k_ProbeDims), 0.0f) };
		const uint32_t probeCount = k_ProbeDims.x * k_ProbeDims.y * k_ProbeDims.z;
		const uint32_t bufferBytes = static_cast<uint32_t>(sizeof(header) + probeCount * 36 * sizeof(glm::vec4));
		if (m_GIProbes->GetSize() < bufferBytes)
		{
			m_GIProbes->Resize(bufferBytes);
			if (void* mapped = m_GIProbes->GetBuffer().Map(0, bufferBytes))
			{
				std::memset(mapped, 0, bufferBytes);
				m_GIProbes->GetBuffer().Unmap();
			}
		}
		if (void* mapped = m_GIProbes->GetBuffer().Map(0, sizeof(header)))
		{
			std::memcpy(mapped, header, sizeof(header));
			m_GIProbes->GetBuffer().Unmap();
		}

		std::vector<RayTracingScene::InstanceInput> instances = RayTracingScene::GatherInstances(proxies);
		std::vector<glm::vec4> lights = PackLights(m_SceneData.SceneLightEnvironment, m_Options.PathTracerPowerLightSampling);
		const uint32_t rays = std::clamp(m_Options.RealtimeGIRaysPerProbe, 8u, 128u);
		const uint32_t bounces = std::clamp(m_Options.RealtimeGIBounces, 1u, 4u);
		const float environmentIntensity = m_FrameEnvironment.EnvironmentIntensity;

		uint64_t signature = HashBytes(1469598103934665603ull, header, sizeof(header));
		if (!lights.empty())
			signature = HashBytes(signature, lights.data(), lights.size() * sizeof(glm::vec4));
		signature = HashBytes(signature, &bounces, sizeof(bounces));

		Ref<RenderCommandBuffer> cb = m_CommandBuffer;
		Ref<RayTracingScene> scene = m_RayTracingScene;
		Ref<ComputePass> pass = m_GIProbePass;
		Ref<StorageBuffer> lightBuffer = m_PathTraceLights;
		std::shared_ptr<RealtimeGIState> state = m_RealtimeGIState;
		const PathTraceEnvParams envParams = PreparePathTraceEnvironment(m_GIProbePass);
		Renderer::Submit([=, instances = std::move(instances), lights = std::move(lights)]() mutable
		{
			const bool changed = scene->RT_Update(cb, instances);
			if (changed || state->Signature != signature)
			{
				state->Frame = 0; // the next few updates replace the history outright
				state->Signature = signature;
			}
			if (!scene->IsValid())
				return;

			const uint32_t lightBytes = static_cast<uint32_t>(lights.size() * sizeof(glm::vec4));
			if (lightBuffer->GetSize() < lightBytes)
				lightBuffer->Resize(lightBytes);
			if (lightBytes > 0)
				lightBuffer->RT_SetData(cb, lights.data(), lightBytes);

			GIPush push = {};
			push.SizeFrameBounces = glm::uvec4(probeCount, rays, state->Frame, bounces);
			push.Counts = glm::uvec4(static_cast<uint32_t>(lights.size() / 4), 0u, 0u, 0u);
			push.SkyRadiance = glm::vec4(glm::vec3(0.35f, 0.5f, 0.8f) * environmentIntensity, 0.0f);
			push.GroundRadiance = glm::vec4(glm::vec3(0.12f, 0.1f, 0.09f) * environmentIntensity, 0.0f);
			ApplyPathTraceEnvironment(push, envParams);
			cb->RT_BeginMarker("GIProbeUpdate");
			pass->Prepare();
			Renderer::RT_DispatchCompute(*cb, *pass, glm::uvec3((probeCount + 63u) / 64u, 1u, 1u), Buffer(&push, sizeof(push)));
			cb->RT_EndMarker();
			state->Frame++;
			state->Updates++;
		});
	}

}
