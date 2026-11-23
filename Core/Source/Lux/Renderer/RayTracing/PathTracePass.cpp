// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// The opt-in path tracer (docs/GI_PLAN.md, GI1). It renders the whole frame by ray queries and replaces
// the raster result in scene colour just before post-processing, so bloom, exposure and tone mapping
// treat it like any other frame. The raster renderer is unchanged and stays the default: nothing here
// is created or run unless SceneRendererOptions::EnablePathTracer is on and the device has ray queries.

#include "lpch.h"
#include "Lux/Renderer/SceneRenderer.h"

#include "Lux/Renderer/ComputePass.h"
#include "Lux/Renderer/PipelineCompute.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RenderScene.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"

#include <glm/gtc/constants.hpp>

#include <atomic>
#include <cstring>

namespace Lux {

	// Render-thread accumulation state. Written only by the lambdas SceneRenderer::PathTracePass queues.
	struct SceneRenderer::PathTraceState
	{
		std::atomic<uint32_t> Frame{ 0 }; // samples already in the accumulation image (read by the editor panel)
		uint32_t ResetCounter = 0;
		uint64_t Signature = 0;
	};

	namespace {

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

		// 4 vec4 per light, mirrored by DirectLight in PathTrace.glsl:
		//   directional: A = (toward light, 0)  B = (radiance * intensity)
		//   point:       A = (position, 1)      B = (radiance * intensity, range)
		//   spot:        A = (position, 2)      B = (radiance * intensity, range)   C = (direction, cos half angle)
		//   rect area:   A = (centre, 3)        B = (radiance * intensity)          C = half right   D = half up
		std::vector<glm::vec4> PackLights(const LightEnvironment& environment)
		{
			std::vector<glm::vec4> packed;
			auto add = [&](const glm::vec4& a, const glm::vec4& b, const glm::vec4& c, const glm::vec4& d)
			{
				packed.push_back(a);
				packed.push_back(b);
				packed.push_back(c);
				packed.push_back(d);
			};

			for (const DirectionalLight& light : environment.DirectionalLights)
			{
				if (light.Intensity > 0.0f)
					add(glm::vec4(glm::normalize(light.Direction), 0.0f), glm::vec4(light.Radiance * light.Intensity, 0.0f), glm::vec4(0.0f), glm::vec4(0.0f));
			}
			for (const PointLight& light : environment.PointLights)
			{
				if (light.Intensity > 0.0f)
					add(glm::vec4(light.Position, 1.0f), glm::vec4(light.Radiance * light.Intensity, light.Radius), glm::vec4(0.0f), glm::vec4(0.0f));
			}
			for (const SpotLight& light : environment.SpotLights)
			{
				if (light.Intensity > 0.0f)
					add(glm::vec4(light.Position, 2.0f), glm::vec4(light.Radiance * light.Intensity, light.Range),
						glm::vec4(glm::normalize(light.Direction), std::cos(glm::radians(light.Angle))), glm::vec4(0.0f));
			}
			for (const AreaLight& light : environment.AreaLights)
			{
				// Rectangles only in v1; other shapes need their own sampling.
				if (light.Intensity > 0.0f && light.Shape == 0)
					add(glm::vec4(light.Position, 3.0f), glm::vec4(light.Radiance * light.Intensity, 0.0f), glm::vec4(light.HalfRight, 0.0f), glm::vec4(light.HalfUp, 0.0f));
			}
			return packed;
		}

		struct PathTracePush
		{
			glm::uvec4 SizeFrameBounces;
			glm::uvec4 Counts;
			glm::vec4 SkyRadiance;
			glm::vec4 GroundRadiance;
		};

	}

	uint32_t SceneRenderer::GetPathTraceSampleCount() const
	{
		return m_PathTraceState ? m_PathTraceState->Frame.load() : 0u;
	}

	bool SceneRenderer::EnsurePathTracer()
	{
		if (m_PathTracePass)
			return true;
		if (m_PathTracerUnavailable || !RHIDevice::SupportsRayQuery() || !ShaderCompiler::IsAvailable())
			return false;

		// Loaded on first use: the shader needs ray queries, so it must not exist on the raster-only path.
		auto library = Renderer::GetShaderLibrary();
		if (library->GetShaders().find("PathTrace") == library->GetShaders().end())
			library->Load("Resources/Shaders/PathTracing/PathTrace.glsl");
		if (library->GetShaders().find("PathTrace") == library->GetShaders().end())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[PathTracer] PathTrace.glsl did not compile; the path tracer is off for this session");
			m_PathTracerUnavailable = true;
			return false;
		}

		m_RayTracingScene = Ref<RayTracingScene>::Create();
		StorageBufferSpecification lightSpec;
		lightSpec.GPUOnly = false;
		lightSpec.DebugName = "PathTrace Lights";
		m_PathTraceLights = StorageBuffer::Create(256, lightSpec);
		m_PathTraceState = std::make_shared<PathTraceState>();

		ImageSpecification accumSpec;
		accumSpec.Format = ImageFormat::RGBA32F;
		accumSpec.Usage = ImageUsage::Storage;
		accumSpec.Width = std::max(1u, m_ViewportWidth);
		accumSpec.Height = std::max(1u, m_ViewportHeight);
		accumSpec.DebugName = "PathTrace-Accum";
		m_PathTraceAccumImage = Image2D::Create(accumSpec);
		m_PathTraceAccumImage->Invalidate();
		accumSpec.Format = ImageFormat::RGBA16F;
		accumSpec.DebugName = "PathTrace-Image";
		m_PathTraceImage = Image2D::Create(accumSpec);
		m_PathTraceImage->Invalidate();

		ComputePassSpecification passSpec;
		passSpec.DebugName = "PathTrace";
		passSpec.Pipeline = PipelineCompute::Create(library->Get("PathTrace"));
		m_PathTracePass = ComputePass::Create(passSpec);
		m_PathTracePass->SetInput("Camera", m_UBSCamera);
		m_PathTracePass->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
		m_PathTracePass->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
		m_PathTracePass->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
		m_PathTracePass->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
		m_PathTracePass->SetInput("RTLights", m_PathTraceLights);
		m_PathTracePass->SetInput("o_PathTraceAccum", m_PathTraceAccumImage);
		m_PathTracePass->SetInput("o_PathTraceImage", m_PathTraceImage);
		if (!m_PathTracePass->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[PathTracer] the PathTrace pass inputs do not match its shader; the path tracer is off for this session");
			m_PathTracePass = nullptr;
			m_PathTracerUnavailable = true;
			return false;
		}
		m_PathTracePass->Bake();
		LUX_CORE_INFO_TAG("Renderer", "[PathTracer] ready (ray queries on this device)");
		return true;
	}

	void SceneRenderer::PathTracePass()
	{
		ScopedCPUProfile cpuProfile(*this, "PathTracePass");
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0 || !EnsurePathTracer())
			return;

		// Targets follow the viewport.
		if (m_PathTraceImage->GetWidth() != m_ViewportWidth || m_PathTraceImage->GetHeight() != m_ViewportHeight)
		{
			m_PathTraceAccumImage->Resize(m_ViewportWidth, m_ViewportHeight);
			m_PathTraceImage->Resize(m_ViewportWidth, m_ViewportHeight);
		}

		// Snapshot everything the render thread needs; proxies and light arrays are not stable after this call.
		std::vector<const StaticMeshRenderProxy*> proxies;
		if (m_SubmittedRenderScene)
			proxies = m_SubmittedRenderScene->GetStaticMeshProxies();
		std::vector<RayTracingScene::InstanceInput> instances = RayTracingScene::GatherInstances(proxies);
		std::vector<glm::vec4> lights = PackLights(m_SceneData.SceneLightEnvironment);

		const bool uniformEnvironment = m_Options.PathTracerUniformEnvironment;
		const float environmentIntensity = m_FrameEnvironment.EnvironmentIntensity;
		const uint32_t maxBounces = std::clamp(m_Options.PathTracerMaxBounces, 1u, 16u);
		const uint32_t targetSamples = std::max(1u, m_Options.PathTracerTargetSamples);
		const uint32_t resetCounter = m_Options.PathTracerResetCounter;
		const uint32_t width = m_ViewportWidth;
		const uint32_t height = m_ViewportHeight;

		uint64_t signature = 1469598103934665603ull;
		signature = HashBytes(signature, &m_SceneData.SceneCamera.ViewMatrix, sizeof(glm::mat4));
		const glm::mat4 projection = m_SceneData.SceneCamera.Camera.GetProjectionMatrix();
		signature = HashBytes(signature, &projection, sizeof(glm::mat4));
		if (!lights.empty())
			signature = HashBytes(signature, lights.data(), lights.size() * sizeof(glm::vec4));
		signature = HashBytes(signature, &maxBounces, sizeof(maxBounces));
		signature = HashBytes(signature, &width, sizeof(width));
		signature = HashBytes(signature, &height, sizeof(height));
		signature = HashBytes(signature, &uniformEnvironment, sizeof(uniformEnvironment));
		signature = HashBytes(signature, &environmentIntensity, sizeof(environmentIntensity));

		Ref<RenderCommandBuffer> cb = m_CommandBuffer;
		Ref<RayTracingScene> scene = m_RayTracingScene;
		Ref<ComputePass> pass = m_PathTracePass;
		Ref<StorageBuffer> lightBuffer = m_PathTraceLights;
		Ref<Image2D> image = m_PathTraceImage;
		Ref<Image2D> sceneColor = GetSceneColorOutput();
		std::shared_ptr<PathTraceState> state = m_PathTraceState;

		Renderer::Submit([=, instances = std::move(instances), lights = std::move(lights)]() mutable
		{
			const bool sceneChanged = scene->RT_Update(cb, instances);
			if (sceneChanged || state->Signature != signature || state->ResetCounter != resetCounter)
			{
				state->Frame = 0;
				state->Signature = signature;
				state->ResetCounter = resetCounter;
			}
			if (!scene->IsValid())
				return; // nothing to trace: leave the raster frame in scene colour

			if (state->Frame < targetSamples)
			{
				const uint32_t lightBytes = static_cast<uint32_t>(lights.size() * sizeof(glm::vec4));
				if (lightBuffer->GetSize() < lightBytes)
					lightBuffer->Resize(lightBytes);
				if (lightBytes > 0)
					lightBuffer->RT_SetData(cb, lights.data(), lightBytes);

				PathTracePush push = {};
				push.SizeFrameBounces = glm::uvec4(width, height, state->Frame.load(), maxBounces);
				push.Counts = glm::uvec4(static_cast<uint32_t>(lights.size() / 4), uniformEnvironment ? 1u : 0u, 0u, 0u);
				push.SkyRadiance = glm::vec4(uniformEnvironment ? glm::vec3(1.0f) : glm::vec3(0.35f, 0.5f, 0.8f) * environmentIntensity, 0.0f);
				push.GroundRadiance = glm::vec4(uniformEnvironment ? glm::vec3(1.0f) : glm::vec3(0.12f, 0.1f, 0.09f) * environmentIntensity, 0.0f);

				cb->RT_BeginMarker("PathTrace");
				pass->Prepare();
				Renderer::RT_DispatchCompute(*cb, *pass, glm::uvec3((width + 7u) / 8u, (height + 7u) / 8u, 1u), Buffer(&push, sizeof(push)));
				cb->RT_EndMarker();
				state->Frame++;
			}

			// Replace the raster result in scene colour (post-processing then runs as usual).
			if (state->Frame > 0 && sceneColor)
				Renderer::RT_CopyImage(cb, image, sceneColor);
		});
	}

}
