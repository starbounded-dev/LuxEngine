// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Ray-traced GI, GI5 (docs/GI_PLAN.md): per-pixel indirect diffuse by ray queries, denoised by an edge-aware a-trous
// filter and added to scene colour after the raster lighting (RTGITrace.glsl, RTGIFilter.glsl). RT-only and opt-in
// (SceneRendererOptions::EnableRTGI); nothing here exists while it is off.

#include "lpch.h"
#include "Lux/Renderer/SceneRenderer.h"

#include "Lux/Renderer/ComputePass.h"
#include "Lux/Renderer/PipelineCompute.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RayTracing/PathTraceLights.h"
#include "Lux/Renderer/RenderScene.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"

namespace Lux {

	struct SceneRenderer::RTGIState
	{
		uint32_t Frame = 0; // render thread: frames since the camera, lights or scene last changed
		uint64_t Signature = 0;
		uint32_t Parity = 0; // which reservoir set the ReSTIR GI trace writes
		uint64_t ReuseSignature = 0; // everything but the camera: reservoirs are reprojected, so a moving camera keeps them
		bool ReuseValid = false;
	};

	namespace {

		struct RTGITracePush
		{
			glm::uvec4 SizeFrameBounces;
			glm::uvec4 Counts;
			glm::vec4 SkyRadiance;
			glm::vec4 GroundRadiance;
		};

		struct RTGIFilterPush
		{
			glm::uvec4 SizeModeStep;
			glm::vec4 Params;
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

	bool SceneRenderer::EnsureRTGI()
	{
		if (m_RTGITracePass)
			return true;
		if (m_RTGIUnavailable || !RHIDevice::SupportsRayQuery() || !ShaderCompiler::IsAvailable())
			return false;

		auto library = Renderer::GetShaderLibrary();
		for (const char* name : { "RTGITrace", "RTGIFilter" })
		{
			if (library->GetShaders().find(name) == library->GetShaders().end())
				library->Load(std::string("Resources/Shaders/PathTracing/") + name + ".glsl");
			if (library->GetShaders().find(name) == library->GetShaders().end())
			{
				LUX_CORE_ERROR_TAG("Renderer", "[RTGI] {}.glsl did not compile; ray-traced GI is off for this session", name);
				m_RTGIUnavailable = true;
				return false;
			}
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
		m_RTGIState = std::make_shared<RTGIState>();

		auto createImage = [&](const char* name) -> Ref<Image2D>
		{
			ImageSpecification spec;
			spec.Format = ImageFormat::RGBA16F;
			spec.Usage = ImageUsage::Storage;
			spec.Width = std::max(1u, m_ViewportWidth);
			spec.Height = std::max(1u, m_ViewportHeight);
			spec.DebugName = name;
			Ref<Image2D> image = Image2D::Create(spec);
			image->Invalidate();
			return image;
		};
		m_RTGIAccum = createImage("RTGI-Accum");
		m_RTGIAlbedo = createImage("RTGI-Albedo");
		m_RTGINormal = createImage("RTGI-Normal");
		m_RTGITempA = createImage("RTGI-TempA");
		m_RTGITempB = createImage("RTGI-TempB");
		m_RTGIResult = createImage("RTGI-Result");

		ComputePassSpecification traceSpec;
		traceSpec.DebugName = "RTGITrace";
		traceSpec.Pipeline = PipelineCompute::Create(library->Get("RTGITrace"));
		m_RTGITracePass = ComputePass::Create(traceSpec);
		m_RTGITracePass->SetInput("Camera", m_UBSCamera);
		m_RTGITracePass->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
		m_RTGITracePass->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
		m_RTGITracePass->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
		m_RTGITracePass->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
		m_RTGITracePass->SetInput("RTLights", m_PathTraceLights);
		m_RTGITracePass->SetInput("o_RTGIAccum", m_RTGIAccum);
		m_RTGITracePass->SetInput("o_RTGIAlbedo", m_RTGIAlbedo);
		m_RTGITracePass->SetInput("o_RTGINormal", m_RTGINormal);
		PreparePathTraceEnvironment(m_RTGITracePass);
		bool valid = m_RTGITracePass->Validate();
		if (valid)
			m_RTGITracePass->Bake();

		// Iteration 0: accum -> A. Iteration 1: A -> B. Final: B + scene colour -> result.
		const Ref<Image2D> inputs[3] = { m_RTGIAccum, m_RTGITempA, m_RTGITempB };
		const Ref<Image2D> outputs[3] = { m_RTGITempA, m_RTGITempB, m_RTGIResult };
		for (int i = 0; i < 3 && valid; i++)
		{
			ComputePassSpecification filterSpec;
			filterSpec.DebugName = i == 2 ? "RTGIFilterFinal" : "RTGIFilter";
			filterSpec.Pipeline = PipelineCompute::Create(library->Get("RTGIFilter"));
			m_RTGIFilterPasses[i] = ComputePass::Create(filterSpec);
			m_RTGIFilterPasses[i]->SetInput("u_RTGIInput", inputs[i]);
			m_RTGIFilterPasses[i]->SetInput("u_RTGIAlbedoTex", m_RTGIAlbedo);
			m_RTGIFilterPasses[i]->SetInput("u_RTGINormalTex", m_RTGINormal);
			m_RTGIFilterPasses[i]->SetInput("u_RTGISceneColor", GetSceneColorOutput());
			m_RTGIFilterPasses[i]->SetInput("o_RTGIOutput", outputs[i]);
			valid = m_RTGIFilterPasses[i]->Validate();
			if (valid)
				m_RTGIFilterPasses[i]->Bake();
		}
		if (!valid)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTGI] pass inputs do not match the shaders; ray-traced GI is off for this session");
			m_RTGITracePass = nullptr;
			m_RTGIUnavailable = true;
			return false;
		}
		LUX_CORE_INFO_TAG("Renderer", "[RTGI] ready");
		return true;
	}

	bool SceneRenderer::EnsureRTGIReSTIR()
	{
		if (m_RTGIReSTIRPasses[0])
			return true;
		if (m_RTGIReSTIRUnavailable || !m_RTGITracePass)
			return false;
		auto library = Renderer::GetShaderLibrary();
		if (library->GetShaders().find("RTGITraceReSTIR") == library->GetShaders().end())
			library->Load("Resources/Shaders/PathTracing/RTGITraceReSTIR.glsl");
		if (library->GetShaders().find("RTGITraceReSTIR") == library->GetShaders().end())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTGI] RTGITraceReSTIR.glsl did not compile; ReSTIR GI reuse is off for this session");
			m_RTGIReSTIRUnavailable = true;
			return false;
		}
		auto createImage = [&](ImageFormat format, const char* name) -> Ref<Image2D>
		{
			ImageSpecification spec;
			spec.Format = format;
			spec.Usage = ImageUsage::Storage;
			spec.Width = std::max(1u, m_ViewportWidth);
			spec.Height = std::max(1u, m_ViewportHeight);
			spec.DebugName = name;
			Ref<Image2D> image = Image2D::Create(spec);
			image->Invalidate();
			return image;
		};
		for (int i = 0; i < 2; i++)
		{
			m_RGSample[i] = createImage(ImageFormat::RGBA32F, "RTGI-RSample");
			m_RGSampleNormal[i] = createImage(ImageFormat::RGBA32F, "RTGI-RSampleNormal");
			m_RGRadiance[i] = createImage(ImageFormat::RGBA16F, "RTGI-RRadiance");
			m_RGGPos[i] = createImage(ImageFormat::RGBA32F, "RTGI-RGPos");
			m_RGGNormal[i] = createImage(ImageFormat::RGBA16F, "RTGI-RGNormal");
		}
		bool valid = true;
		for (int i = 0; i < 2 && valid; i++)
		{
			const int prev = 1 - i;
			ComputePassSpecification spec;
			spec.DebugName = "RTGITraceReSTIR";
			spec.Pipeline = PipelineCompute::Create(library->Get("RTGITraceReSTIR"));
			Ref<ComputePass> pass = ComputePass::Create(spec);
			pass->SetInput("Camera", m_UBSCamera);
			pass->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
			pass->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
			pass->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
			pass->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
			pass->SetInput("RTLights", m_PathTraceLights);
			pass->SetInput("o_RTGIAccum", m_RTGIAccum);
			pass->SetInput("o_RTGIAlbedo", m_RTGIAlbedo);
			pass->SetInput("o_RTGINormal", m_RTGINormal);
			pass->SetInput("o_RGSample", m_RGSample[i]);
			pass->SetInput("o_RGSampleNormal", m_RGSampleNormal[i]);
			pass->SetInput("o_RGRadiance", m_RGRadiance[i]);
			pass->SetInput("o_RGGPos", m_RGGPos[i]);
			pass->SetInput("o_RGGNormal", m_RGGNormal[i]);
			pass->SetInput("u_RGPrevSample", m_RGSample[prev]);
			pass->SetInput("u_RGPrevSampleNormal", m_RGSampleNormal[prev]);
			pass->SetInput("u_RGPrevRadiance", m_RGRadiance[prev]);
			pass->SetInput("u_RGPrevGPos", m_RGGPos[prev]);
			pass->SetInput("u_RGPrevGNormal", m_RGGNormal[prev]);
			PreparePathTraceEnvironment(pass);
			valid = pass->Validate();
			if (valid)
				pass->Bake();
			m_RTGIReSTIRPasses[i] = pass;
		}
		if (!valid)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTGI] ReSTIR GI pass inputs do not match the shader; reuse is off for this session");
			m_RTGIReSTIRPasses[0] = m_RTGIReSTIRPasses[1] = nullptr;
			m_RTGIReSTIRUnavailable = true;
			return false;
		}
		LUX_CORE_INFO_TAG("Renderer", "[RTGI] ReSTIR GI ready");
		return true;
	}

	void SceneRenderer::RTGIPass()
	{
		ScopedCPUProfile cpuProfile(*this, "RTGIPass");
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0 || !m_SubmittedRenderScene || !EnsureRTGI())
			return;

		if (m_RTGIAccum->GetWidth() != m_ViewportWidth || m_RTGIAccum->GetHeight() != m_ViewportHeight)
		{
			for (Ref<Image2D> image : { m_RTGIAccum, m_RTGIAlbedo, m_RTGINormal, m_RTGITempA, m_RTGITempB, m_RTGIResult })
				image->Resize(m_ViewportWidth, m_ViewportHeight);
		}
		const bool reuse = m_Options.RTGIReuse && EnsureRTGIReSTIR();
		if (reuse && (m_RGSample[0]->GetWidth() != m_ViewportWidth || m_RGSample[0]->GetHeight() != m_ViewportHeight))
		{
			for (int i = 0; i < 2; i++)
				for (Ref<Image2D> image : { m_RGSample[i], m_RGSampleNormal[i], m_RGRadiance[i], m_RGGPos[i], m_RGGNormal[i] })
					image->Resize(m_ViewportWidth, m_ViewportHeight);
		}
		// Scene colour may have been recreated (a resize); the final pass samples it.
		m_RTGIFilterPasses[2]->SetInput("u_RTGISceneColor", GetSceneColorOutput());

		std::vector<const StaticMeshRenderProxy*> proxies = m_SubmittedRenderScene->GetStaticMeshProxies();
		std::vector<RayTracingScene::InstanceInput> instances = RayTracingScene::GatherInstances(proxies);
		std::vector<glm::vec4> lights = PackLights(m_SceneData.SceneLightEnvironment, m_Options.PathTracerPowerLightSampling);
		const uint32_t bounces = std::clamp(m_Options.RTGIBounces, 1u, 4u);
		const float environmentIntensity = m_FrameEnvironment.EnvironmentIntensity;
		const uint32_t width = m_ViewportWidth, height = m_ViewportHeight;

		uint64_t signature = HashBytes(1469598103934665603ull, &m_SceneData.SceneCamera.ViewMatrix, sizeof(glm::mat4));
		const glm::mat4 projection = m_SceneData.SceneCamera.Camera.GetProjectionMatrix();
		signature = HashBytes(signature, &projection, sizeof(projection));
		if (!lights.empty())
			signature = HashBytes(signature, lights.data(), lights.size() * sizeof(glm::vec4));
		signature = HashBytes(signature, &bounces, sizeof(bounces));
		signature = HashBytes(signature, &width, sizeof(width));
		signature = HashBytes(signature, &height, sizeof(height));
		signature = HashBytes(signature, &reuse, sizeof(reuse));
		const uint32_t historyLength = std::clamp(m_Options.RTGIHistoryLength, 1u, 256u);
		signature = HashBytes(signature, &historyLength, sizeof(historyLength));
		uint64_t reuseSignature = HashBytes(1469598103934665603ull, &bounces, sizeof(bounces));
		reuseSignature = HashBytes(reuseSignature, &width, sizeof(width));
		reuseSignature = HashBytes(reuseSignature, &height, sizeof(height));
		if (!lights.empty())
			reuseSignature = HashBytes(reuseSignature, lights.data(), lights.size() * sizeof(glm::vec4));

		Ref<RenderCommandBuffer> cb = m_CommandBuffer;
		Ref<RayTracingScene> scene = m_RayTracingScene;
		Ref<ComputePass> trace = m_RTGITracePass;
		Ref<ComputePass> reusePasses[2] = { reuse ? m_RTGIReSTIRPasses[0] : nullptr, reuse ? m_RTGIReSTIRPasses[1] : nullptr };
		Ref<ComputePass> filters[3] = { m_RTGIFilterPasses[0], m_RTGIFilterPasses[1], m_RTGIFilterPasses[2] };
		Ref<StorageBuffer> lightBuffer = m_PathTraceLights;
		Ref<Image2D> result = m_RTGIResult;
		Ref<Image2D> sceneColor = GetSceneColorOutput();
		std::shared_ptr<RTGIState> state = m_RTGIState;
		const PathTraceEnvParams envParams = PreparePathTraceEnvironment(m_RTGITracePass);
		signature = HashBytes(signature, &envParams.Flags, sizeof(envParams.Flags));

		Renderer::Submit([=, instances = std::move(instances), lights = std::move(lights)]() mutable
		{
			const bool changed = scene->RT_Update(cb, instances);
			if (changed || state->ReuseSignature != reuseSignature)
			{
				state->ReuseValid = false;
				state->ReuseSignature = reuseSignature;
			}
			if (changed || state->Signature != signature)
			{
				state->Frame = 0;
				state->Signature = signature;
			}
			if (!scene->IsValid() || !sceneColor)
				return;

			const uint32_t lightBytes = static_cast<uint32_t>(lights.size() * sizeof(glm::vec4));
			if (lightBuffer->GetSize() < lightBytes)
				lightBuffer->Resize(lightBytes);
			if (lightBytes > 0)
				lightBuffer->RT_SetData(cb, lights.data(), lightBytes);

			const glm::uvec3 groups((width + 7u) / 8u, (height + 7u) / 8u, 1u);
			RTGITracePush tracePush = {};
			tracePush.SizeFrameBounces = glm::uvec4(width, height, state->Frame, bounces);
			tracePush.Counts = glm::uvec4(static_cast<uint32_t>(lights.size() / 4), 0u, 0u, historyLength);
			tracePush.SkyRadiance = glm::vec4(glm::vec3(0.35f, 0.5f, 0.8f) * environmentIntensity, 0.0f);
			tracePush.GroundRadiance = glm::vec4(glm::vec3(0.12f, 0.1f, 0.09f) * environmentIntensity, 0.0f);
			ApplyPathTraceEnvironment(tracePush, envParams);
			cb->RT_BeginMarker("RTGI");
			if (reusePasses[0])
			{
				tracePush.Counts.z = state->ReuseValid ? 1u : 0u;
				reusePasses[state->Parity]->Prepare();
				Renderer::RT_DispatchCompute(*cb, *reusePasses[state->Parity], groups, Buffer(&tracePush, sizeof(tracePush)));
				state->Parity ^= 1u;
				state->ReuseValid = true;
			}
			else
			{
				trace->Prepare();
				Renderer::RT_DispatchCompute(*cb, *trace, groups, Buffer(&tracePush, sizeof(tracePush)));
			}

			const uint32_t steps[3] = { 1u, 2u, 4u };
			for (uint32_t i = 0; i < 3; i++)
			{
				RTGIFilterPush push = {};
				push.SizeModeStep = glm::uvec4(width, height, i == 2 ? 1u : 0u, steps[i]);
				push.Params = glm::vec4(32.0f, 0.04f, 0.0f, 0.0f);
				filters[i]->Prepare();
				Renderer::RT_DispatchCompute(*cb, *filters[i], groups, Buffer(&push, sizeof(push)));
			}
			cb->RT_EndMarker();
			state->Frame++;
			Renderer::RT_CopyImage(cb, result, sceneColor);
		});
	}

}
