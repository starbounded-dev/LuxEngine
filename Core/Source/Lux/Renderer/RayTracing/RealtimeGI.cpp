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
		glm::uvec3 LastDims{ 0u };
	};

	namespace {

		constexpr glm::uvec3 k_ProbeDims = { 16u, 8u, 16u };
		constexpr uint32_t k_ProbeStride = 36;
		constexpr uint32_t k_PlacementWarmupFrames = 32;
		// GIPush::Counts.w flags shared with GIProbeUpdate.glsl.
		constexpr uint32_t k_AutoRefresh = 1;
		constexpr uint32_t k_MeasureError = 2;
		constexpr uint32_t k_UsePlacement = 4;
		constexpr uint32_t k_BuildPlacement = 8;

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

		void RT_ProbeBarrier(RenderCommandBuffer& commandBuffer, nri::StageBits from, nri::StageBits to)
		{
			// The CPU-visible buffer is not tracked by descriptor binding. Both GPU stages use
			// its storage data, so order the adaptive readers and writers explicitly.
			if (nri::CommandBuffer* commands = commandBuffer.RT_BeginNRITransfer())
			{
				nri::GlobalBarrierDesc global{};
				global.before = { nri::AccessBits::SHADER_RESOURCE_STORAGE, from };
				global.after = { nri::AccessBits::SHADER_RESOURCE_STORAGE, to };
				nri::BarrierDesc barrier{};
				barrier.globals = &global;
				barrier.globalNum = 1;
				RHIDevice::API().CmdBarrier(*commands, barrier);
				commandBuffer.RT_EndNRISegment();
			}
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
		// Auto placement: dimensions from the scene extent and the requested spacing; otherwise the fixed grid.
		glm::uvec3 dims = k_ProbeDims;
		if (m_Options.RealtimeGIProbeSpacing > 0.0f)
		{
			const glm::vec3 extent = boundsMax - boundsMin;
			const float spacing = std::max(m_Options.RealtimeGIProbeSpacing, 0.05f);
			glm::vec3 counts = glm::clamp(glm::ceil(extent / spacing) + glm::vec3(1.0f), glm::vec3(2.0f), glm::vec3(64.0f));
			while (counts.x * counts.y * counts.z > 16384.0f)
				counts = glm::max(glm::floor(counts * 0.9f), glm::vec3(2.0f));
			dims = glm::uvec3(counts);
		}
		if (dims != m_RealtimeGIState->LastDims)
		{
			m_RealtimeGIState->LastDims = dims;
			LUX_CORE_INFO_TAG("Renderer", "[RealtimeGI] probe grid {}x{}x{} ({} probes)", dims.x, dims.y, dims.z, dims.x * dims.y * dims.z);
		}
		const glm::vec3 cell = (boundsMax - boundsMin) / glm::vec3(dims - glm::uvec3(1));
		const bool adaptivePlacement = m_Options.RealtimeGIAdaptiveProbes;

		// Grid header, written straight into the mapped buffer (the probe region is written by the GPU, so the
		// buffer's CPU copy must never be uploaded over it).
		const glm::vec4 header[3] = { glm::vec4(boundsMin, 1.0f), glm::vec4(cell, 0.0f), glm::vec4(glm::vec3(dims), adaptivePlacement ? 1.0f : 0.0f) };
		const uint32_t probeCount = dims.x * dims.y * dims.z;
		const uint32_t placementSlots = adaptivePlacement ? 1 + dims.x + dims.y + dims.z : 0;
		const uint32_t bufferBytes = static_cast<uint32_t>(sizeof(header) + (probeCount * k_ProbeStride + placementSlots) * sizeof(glm::vec4));
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
		const bool autoPlacement = m_Options.RealtimeGIProbeSpacing > 0.0f;
		const uint32_t rays = std::clamp(m_Options.RealtimeGIRaysPerProbe, 8u, 128u);
		const uint32_t bounces = std::clamp(m_Options.RealtimeGIBounces, 1u, 4u);
		const float environmentIntensity = m_FrameEnvironment.EnvironmentIntensity;

		uint64_t signature = HashBytes(1469598103934665603ull, header, sizeof(header));
		if (!lights.empty())
			signature = HashBytes(signature, lights.data(), lights.size() * sizeof(glm::vec4));
		signature = HashBytes(signature, &bounces, sizeof(bounces));
		signature = HashBytes(signature, &autoPlacement, sizeof(autoPlacement));

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

			const bool usePlacement = adaptivePlacement && state->Frame >= k_PlacementWarmupFrames;
			const uint32_t framesAtCurrentPositions = usePlacement ? state->Frame - k_PlacementWarmupFrames : state->Frame;
			GIPush push = {};
			push.SizeFrameBounces = glm::uvec4(probeCount, rays, framesAtCurrentPositions, bounces);
			push.Counts = glm::uvec4(static_cast<uint32_t>(lights.size() / 4), 0u, 0u, autoPlacement ? k_AutoRefresh : 0u);
			if (adaptivePlacement)
				push.Counts.w |= usePlacement ? k_UsePlacement : k_MeasureError;
			push.SkyRadiance = glm::vec4(glm::vec3(0.35f, 0.5f, 0.8f) * environmentIntensity, 0.0f);
			push.GroundRadiance = glm::vec4(glm::vec3(0.12f, 0.1f, 0.09f) * environmentIntensity, 0.0f);
			ApplyPathTraceEnvironment(push, envParams);
			cb->RT_BeginMarker("GIProbeUpdate");
			pass->Prepare();
			constexpr auto probeReadStages = nri::StageBits::FRAGMENT_SHADER | nri::StageBits::COMPUTE_SHADER;
			if (adaptivePlacement)
				RT_ProbeBarrier(*cb, probeReadStages, nri::StageBits::COMPUTE_SHADER);
			if (usePlacement && framesAtCurrentPositions == 0)
			{
				GIPush placement = push;
				placement.Counts.w = k_BuildPlacement;
				Renderer::RT_DispatchCompute(*cb, *pass, glm::uvec3(1u), Buffer(&placement, sizeof(placement)));
				RT_ProbeBarrier(*cb, nri::StageBits::COMPUTE_SHADER, nri::StageBits::COMPUTE_SHADER);
				LUX_CORE_INFO_TAG("Renderer", "[RealtimeGI] adapted {} probes from measured radiance variation", probeCount);
			}
			Renderer::RT_DispatchCompute(*cb, *pass, glm::uvec3((probeCount + 63u) / 64u, 1u, 1u), Buffer(&push, sizeof(push)));
			if (adaptivePlacement)
				RT_ProbeBarrier(*cb, nri::StageBits::COMPUTE_SHADER, probeReadStages);
			cb->RT_EndMarker();
			state->Frame++;
			state->Updates++;
		});
	}

}
