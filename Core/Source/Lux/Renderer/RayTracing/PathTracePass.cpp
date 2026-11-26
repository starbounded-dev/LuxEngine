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
#include "Lux/Renderer/RayTracing/PathTraceLights.h"

#include <glm/gtc/constants.hpp>

#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>

namespace Lux {

	// Render-thread accumulation state. Written only by the lambdas SceneRenderer::PathTracePass queues.
	struct SceneRenderer::PathTraceState
	{
		std::atomic<uint32_t> Frame{ 0 }; // samples already in the accumulation image (read by the editor panel)
		std::atomic<float> DiffMAE{ -1.0f };  // < 0 until the first diff has been read back
		std::atomic<float> DiffPSNR{ 0.0f };
		uint32_t DiffGroups = 0;              // partial sums the last diff dispatch wrote (0: none pending)
		uint32_t DiffPixels = 0;
		uint32_t DiffReads = 0;
		uint32_t ResetCounter = 0;
		uint32_t ReSTIRParity = 0; // which of the two reservoir sets this frame writes
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

		struct DiffPush
		{
			glm::uvec4 SizeGroups;
			glm::vec4 Params;
		};

		struct PathTracePush
		{
			glm::uvec4 SizeFrameBounces;
			glm::uvec4 Counts;
			glm::vec4 SkyRadiance;
			glm::vec4 GroundRadiance;
			glm::vec4 Medium;       // fog: density, anisotropy, radius (PathTraceCommon.glslh)
			glm::vec4 MediumAlbedo;
			glm::vec4 MediumCenter;
		};

	}

	uint32_t SceneRenderer::GetPathTraceSampleCount() const
	{
		return m_PathTraceState ? m_PathTraceState->Frame.load() : 0u;
	}

	bool SceneRenderer::GetPathTraceDiffStats(float& mae, float& psnr) const
	{
		if (!m_PathTraceState || m_PathTraceState->DiffMAE.load() < 0.0f)
			return false;
		mae = m_PathTraceState->DiffMAE.load();
		psnr = m_PathTraceState->DiffPSNR.load();
		return true;
	}

	bool SceneRenderer::EnsurePathTraceDiff()
	{
		if (m_PathTraceDiffPass)
			return true;
		auto library = Renderer::GetShaderLibrary();
		if (library->GetShaders().find("PathTraceDiff") == library->GetShaders().end())
			library->Load("Resources/Shaders/PathTracing/PathTraceDiff.glsl");
		if (library->GetShaders().find("PathTraceDiff") == library->GetShaders().end())
			return false;

		ImageSpecification spec;
		spec.Format = ImageFormat::RGBA16F;
		spec.Usage = ImageUsage::Storage;
		spec.Width = std::max(1u, m_ViewportWidth);
		spec.Height = std::max(1u, m_ViewportHeight);
		spec.DebugName = "PathTrace-Diff";
		m_PathTraceDiffImage = Image2D::Create(spec);
		m_PathTraceDiffImage->Invalidate();
		StorageBufferSpecification bufferSpec;
		bufferSpec.GPUOnly = false;
		bufferSpec.DebugName = "PathTrace Diff Partials";
		m_PathTraceDiffPartials = StorageBuffer::Create(256, bufferSpec);

		ComputePassSpecification passSpec;
		passSpec.DebugName = "PathTraceDiff";
		passSpec.Pipeline = PipelineCompute::Create(library->Get("PathTraceDiff"));
		m_PathTraceDiffPass = ComputePass::Create(passSpec);
		m_PathTraceDiffPass->SetInput("u_DiffRasterColor", GetSceneColorOutput());
		m_PathTraceDiffPass->SetInput("u_DiffReferenceColor", m_PathTraceImage);
		m_PathTraceDiffPass->SetInput("o_DiffImage", m_PathTraceDiffImage);
		m_PathTraceDiffPass->SetInput("DiffPartials", m_PathTraceDiffPartials);
		if (!m_PathTraceDiffPass->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[PathTracer] the diff pass inputs do not match its shader");
			m_PathTraceDiffPass = nullptr;
			return false;
		}
		m_PathTraceDiffPass->Bake();
		return true;
	}

	PathTraceEnvParams SceneRenderer::PreparePathTraceEnvironment(Ref<ComputePass> pass)
	{
		PathTraceEnvParams params;
		Ref<TextureCube> radiance;
		if (m_FrameEnvironment.Environment)
			radiance = m_FrameEnvironment.Environment->RadianceMap;
		if (radiance)
		{
			params.Flags = 2;
			params.Intensity = m_FrameEnvironment.EnvironmentIntensity;
			params.Rotation = m_FrameEnvironment.EnvironmentRotation;
		}
		pass->SetInput("u_PTEnvironment", radiance ? radiance : Renderer::GetBlackCubeTexture());
		pass->SetInput("u_PTEnvironmentSampler", Renderer::GetClampSampler());
		// The raster renderer's material rows and bindless textures, so albedo / metalness / roughness / emissive maps apply.
		pass->SetInput("GPUMaterials", m_SBSGPUMaterials);
		pass->SetInput("u_PTMaterialSampler", Renderer::GetRepeatSampler());
		pass->SetBindlessTextures(m_BindlessTextures);
		if (m_Options.PathTracerMIS)
			params.Flags |= 4; // FLAG_MIS
		if (m_Options.PathTracerLobes)
			params.Flags |= 16; // FLAG_LOBES
		if (m_Options.PathTracerGlass)
			params.Flags |= 32; // FLAG_GLASS
		if (m_Options.PathTracerSubsurface)
			params.Flags |= 64; // FLAG_SSS
		params.FogMedium = glm::vec4(std::max(m_Options.PathTracerFogDensity, 0.0f), std::clamp(m_Options.PathTracerFogAnisotropy, -0.99f, 0.99f), std::max(m_Options.PathTracerFogRadius, 0.0f), 0.0f);
		params.FogAlbedo = glm::vec4(m_Options.PathTracerFogAlbedo, 0.0f);
		params.FogCenter = glm::vec4(m_Options.PathTracerFogCenter, 0.0f);
		return params;
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

		if (!m_RayTracingScene)
			m_RayTracingScene = Ref<RayTracingScene>::Create();
		if (!m_PathTraceLights)
		{
			StorageBufferSpecification lightSpec;
			lightSpec.GPUOnly = false;
			lightSpec.DebugName = "PathTrace Lights";
			m_PathTraceLights = StorageBuffer::Create(256, lightSpec);
		}
		m_PathTraceState = std::make_shared<PathTraceState>();

		ImageSpecification accumSpec;
		accumSpec.Format = ImageFormat::RGBA32F;
		accumSpec.Usage = ImageUsage::Storage;
		accumSpec.Width = std::max(1u, m_ViewportWidth);
		accumSpec.Height = std::max(1u, m_ViewportHeight);
		accumSpec.DebugName = "PathTrace-Accum";
		m_PathTraceAccumImage = Image2D::Create(accumSpec);
		m_PathTraceAccumImage->Invalidate();
		accumSpec.DebugName = "PathTrace-Moments";
		m_PathTraceMomentsImage = Image2D::Create(accumSpec);
		m_PathTraceMomentsImage->Invalidate();
		accumSpec.Format = ImageFormat::RGBA16F;
		accumSpec.DebugName = "PathTrace-Image";
		m_PathTraceImage = Image2D::Create(accumSpec);
		m_PathTraceImage->Invalidate();

		m_PathTracePass = CreatePathTracePass(library->Get("PathTrace"));
		if (!m_PathTracePass)
		{
			m_PathTracerUnavailable = true;
			return false;
		}
		LUX_CORE_INFO_TAG("Renderer", "[PathTracer] ready (ray queries on this device)");
		return true;
	}

	Ref<ComputePass> SceneRenderer::CreatePathTracePass(const Ref<Shader>& shader)
	{
		ComputePassSpecification passSpec;
		passSpec.DebugName = shader->GetName();
		passSpec.Pipeline = PipelineCompute::Create(shader);
		if (!passSpec.Pipeline->GetNRIPipeline())
			return nullptr;

		Ref<ComputePass> pass = ComputePass::Create(passSpec);
		pass->SetInput("Camera", m_UBSCamera);
		pass->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
		pass->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
		pass->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
		pass->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
		pass->SetInput("RTLights", m_PathTraceLights);
		pass->SetInput("o_PathTraceAccum", m_PathTraceAccumImage);
		pass->SetInput("o_PathTraceImage", m_PathTraceImage);
		pass->SetInput("o_PathTraceMoments", m_PathTraceMomentsImage);
		// Keep the inactive sampled descriptor separate from the writable path-trace image.
		pass->SetInput("u_ReSTIRDirect", Renderer::GetBlackTexture());
		PreparePathTraceEnvironment(pass);
		if (!pass->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[PathTracer] the {} pass inputs do not match its shader", shader->GetName());
			return nullptr;
		}
		pass->Bake();
		return pass;
	}

	bool SceneRenderer::EnsurePathTraceSER()
	{
		if (m_PathTraceSERPass)
			return true;
		if (m_PathTraceSERUnavailable)
			return false;
		if (!RHIDevice::SupportsShaderExecutionReordering())
		{
			LUX_CORE_WARN_TAG("Renderer", "[PathTracer] shader execution reordering is unavailable on this device");
			m_PathTraceSERUnavailable = true;
			return false;
		}

		auto library = Renderer::GetShaderLibrary();
		if (!library->GetShaders().contains("PathTraceSER"))
			library->Load("Resources/Shaders/PathTracing/PathTraceSER.glsl");
		if (library->GetShaders().contains("PathTraceSER"))
			m_PathTraceSERPass = CreatePathTracePass(library->Get("PathTraceSER"));
		if (!m_PathTraceSERPass)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[PathTracer] could not initialize SER; using compute ray queries");
			m_PathTraceSERUnavailable = true;
			return false;
		}
		LUX_CORE_INFO_TAG("Renderer", "[PathTracer] shader execution reordering pipeline ready");
		return true;
	}

	bool SceneRenderer::EnsurePathTraceWavefront()
	{
		if (m_PathTraceWavefrontPass)
			return true;
		if (m_PathTraceWavefrontUnavailable || !m_PathTracePass)
			return false;
		auto library = Renderer::GetShaderLibrary();
		if (library->GetShaders().find("PathTraceWavefront") == library->GetShaders().end())
			library->Load("Resources/Shaders/PathTracing/PathTraceWavefront.glsl");
		if (library->GetShaders().find("PathTraceWavefront") == library->GetShaders().end())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[PathTracer] PathTraceWavefront.glsl did not compile; ray sorting is off for this session");
			m_PathTraceWavefrontUnavailable = true;
			return false;
		}
		StorageBufferSpecification spec;
		spec.GPUOnly = true;
		spec.DebugName = "PathTrace States";
		m_PTWStates = StorageBuffer::Create(256, spec);
		spec.DebugName = "PathTrace Keys";
		m_PTWKeys = StorageBuffer::Create(256, spec);
		spec.DebugName = "PathTrace Bins";
		m_PTWBins = StorageBuffer::Create(256 * sizeof(uint32_t), spec);
		spec.DebugName = "PathTrace Sorted";
		m_PTWSorted = StorageBuffer::Create(256, spec);

		ComputePassSpecification passSpec;
		passSpec.DebugName = "PathTraceWavefront";
		passSpec.Pipeline = PipelineCompute::Create(library->Get("PathTraceWavefront"));
		Ref<ComputePass> pass = ComputePass::Create(passSpec);
		pass->SetInput("Camera", m_UBSCamera);
		pass->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
		pass->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
		pass->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
		pass->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
		pass->SetInput("RTLights", m_PathTraceLights);
		pass->SetInput("o_PathTraceAccum", m_PathTraceAccumImage);
		pass->SetInput("o_PathTraceImage", m_PathTraceImage);
		pass->SetInput("PathStates", m_PTWStates);
		pass->SetInput("PathKeys", m_PTWKeys);
		pass->SetInput("PathBins", m_PTWBins);
		pass->SetInput("PathSorted", m_PTWSorted);
		PreparePathTraceEnvironment(pass);
		if (!pass->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[PathTracer] the wavefront pass inputs do not match its shader; ray sorting is off for this session");
			m_PathTraceWavefrontUnavailable = true;
			return false;
		}
		pass->Bake();
		m_PathTraceWavefrontPass = pass;
		LUX_CORE_INFO_TAG("Renderer", "[PathTracer] ray sorting ready");
		return true;
	}

	bool SceneRenderer::EnsureReSTIRDI()
	{
		if (m_ReSTIRDirect)
			return true;
		if (m_ReSTIRDIUnavailable || !m_PathTracePass)
			return false;
		auto library = Renderer::GetShaderLibrary();
		if (library->GetShaders().find("ReSTIRDI") == library->GetShaders().end())
			library->Load("Resources/Shaders/PathTracing/ReSTIRDI.glsl");
		if (library->GetShaders().find("ReSTIRDI") == library->GetShaders().end())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[ReSTIR] ReSTIRDI.glsl did not compile; ReSTIR DI is off for this session");
			m_ReSTIRDIUnavailable = true;
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
			m_ReSTIRResA[i] = createImage(ImageFormat::RGBA32F, "ReSTIR-ResA");
			m_ReSTIRResB[i] = createImage(ImageFormat::RGBA32F, "ReSTIR-ResB");
			m_ReSTIRGPos[i] = createImage(ImageFormat::RGBA32F, "ReSTIR-GPos");
			m_ReSTIRGNormal[i] = createImage(ImageFormat::RGBA16F, "ReSTIR-GNormal");
		}
		m_ReSTIRDirect = createImage(ImageFormat::RGBA16F, "ReSTIR-Direct");

		bool valid = true;
		for (int i = 0; i < 2 && valid; i++)
		{
			const int prev = 1 - i;
			ComputePassSpecification spec;
			spec.DebugName = "ReSTIRDI";
			spec.Pipeline = PipelineCompute::Create(library->Get("ReSTIRDI"));
			Ref<ComputePass> pass = ComputePass::Create(spec);
			pass->SetInput("Camera", m_UBSCamera);
			pass->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
			pass->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
			pass->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
			pass->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
			pass->SetInput("RTLights", m_PathTraceLights);
			pass->SetInput("o_RDResA", m_ReSTIRResA[i]);
			pass->SetInput("o_RDResB", m_ReSTIRResB[i]);
			pass->SetInput("o_RDGPos", m_ReSTIRGPos[i]);
			pass->SetInput("o_RDGNormal", m_ReSTIRGNormal[i]);
			pass->SetInput("o_RDDirect", m_ReSTIRDirect);
			pass->SetInput("u_RDPrevResA", m_ReSTIRResA[prev]);
			pass->SetInput("u_RDPrevResB", m_ReSTIRResB[prev]);
			pass->SetInput("u_RDPrevGPos", m_ReSTIRGPos[prev]);
			pass->SetInput("u_RDPrevGNormal", m_ReSTIRGNormal[prev]);
			PreparePathTraceEnvironment(pass);
			valid = pass->Validate();
			if (valid)
				pass->Bake();
			m_ReSTIRDIPasses[i] = pass;
		}
		if (!valid)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[ReSTIR] pass inputs do not match the shader; ReSTIR DI is off for this session");
			m_ReSTIRDirect = nullptr;
			m_ReSTIRDIUnavailable = true;
			return false;
		}
		LUX_CORE_INFO_TAG("Renderer", "[ReSTIR] DI ready");
		return true;
	}

	void SceneRenderer::PathTracePass()
	{
		ScopedCPUProfile cpuProfile(*this, "PathTracePass");
		ApplyRTTuning();
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0 || !EnsurePathTracer())
			return;

		// Targets follow the viewport.
		if (m_PathTraceImage->GetWidth() != m_ViewportWidth || m_PathTraceImage->GetHeight() != m_ViewportHeight)
		{
			m_PathTraceAccumImage->Resize(m_ViewportWidth, m_ViewportHeight);
			m_PathTraceMomentsImage->Resize(m_ViewportWidth, m_ViewportHeight);
			m_PathTraceImage->Resize(m_ViewportWidth, m_ViewportHeight);
			if (m_PathTraceDiffImage)
				m_PathTraceDiffImage->Resize(m_ViewportWidth, m_ViewportHeight);
		}

		Ref<ComputePass> pass = m_PathTracePass;
		Ref<ComputePass> serPass = m_Options.PathTracerShaderExecutionReordering && EnsurePathTraceSER() ? m_PathTraceSERPass : nullptr;
		const bool raySort = !serPass && m_Options.PathTracerRaySort && !m_Options.PathTracerReSTIRDI && EnsurePathTraceWavefront();
		const bool restirDI = m_Options.PathTracerReSTIRDI && EnsureReSTIRDI();
		if (restirDI)
			GetOrCreatePassProfile("ReSTIRDI").GPUActive = true;
		if (restirDI)
		{
			if (m_ReSTIRDirect->GetWidth() != m_ViewportWidth || m_ReSTIRDirect->GetHeight() != m_ViewportHeight)
			{
				m_ReSTIRDirect->Resize(m_ViewportWidth, m_ViewportHeight);
				for (int i = 0; i < 2; i++)
				{
					m_ReSTIRResA[i]->Resize(m_ViewportWidth, m_ViewportHeight);
					m_ReSTIRResB[i]->Resize(m_ViewportWidth, m_ViewportHeight);
					m_ReSTIRGPos[i]->Resize(m_ViewportWidth, m_ViewportHeight);
					m_ReSTIRGNormal[i]->Resize(m_ViewportWidth, m_ViewportHeight);
				}
			}
			pass->SetInput("u_ReSTIRDirect", m_ReSTIRDirect);
			if (serPass)
				serPass->SetInput("u_ReSTIRDirect", m_ReSTIRDirect);
		}

		const bool diff = m_Options.PathTracerDiffHeatmap && EnsurePathTraceDiff();
		if (diff)
			m_PathTraceDiffPass->SetInput("u_DiffRasterColor", GetSceneColorOutput()); // follows scene colour recreations

		// Snapshot everything the render thread needs; proxies and light arrays are not stable after this call.
		std::vector<const StaticMeshRenderProxy*> proxies;
		if (m_SubmittedRenderScene)
			proxies = m_SubmittedRenderScene->GetStaticMeshProxies();
		std::vector<RayTracingScene::InstanceInput> instances = RayTracingScene::GatherInstances(proxies);
		std::vector<glm::vec4> lights = PackLights(m_SceneData.SceneLightEnvironment, m_Options.PathTracerPowerLightSampling);

		const bool uniformEnvironment = m_Options.PathTracerUniformEnvironment;
		const float environmentIntensity = m_FrameEnvironment.EnvironmentIntensity;
		const uint32_t maxBounces = std::clamp(m_Options.PathTracerMaxBounces, 1u, 16u);
		const uint32_t targetSamples = std::max(1u, m_Options.PathTracerTargetSamples);
		const uint32_t resetCounter = m_Options.PathTracerResetCounter;
		const glm::vec4 fogMedium(std::max(m_Options.PathTracerFogDensity, 0.0f), std::clamp(m_Options.PathTracerFogAnisotropy, -0.99f, 0.99f), std::max(m_Options.PathTracerFogRadius, 0.0f), 0.0f);
		const glm::vec4 fogAlbedo(m_Options.PathTracerFogAlbedo, 0.0f);
		const glm::vec4 fogCenter(m_Options.PathTracerFogCenter, 0.0f);
		const float furnaceRadiance = m_Options.PathTracerFurnaceRadiance;
		const float adaptiveThreshold = std::max(m_Options.PathTracerAdaptiveThreshold, 0.0f);
		const uint32_t debugView = m_Options.PathTracerDebugView != 0 ? std::min(m_Options.PathTracerDebugView, 4u) : (m_Options.PathTracerSampleHeatmap ? 4u : 0u);
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
		signature = HashBytes(signature, &adaptiveThreshold, sizeof(adaptiveThreshold));
		signature = HashBytes(signature, &debugView, sizeof(debugView));
		signature = HashBytes(signature, &raySort, sizeof(raySort));
		signature = HashBytes(signature, &fogMedium, sizeof(fogMedium));
		signature = HashBytes(signature, &furnaceRadiance, sizeof(furnaceRadiance));
		signature = HashBytes(signature, &fogAlbedo, sizeof(fogAlbedo));
		signature = HashBytes(signature, &fogCenter, sizeof(fogCenter));

		Ref<RenderCommandBuffer> cb = m_CommandBuffer;
		Ref<RayTracingScene> scene = m_RayTracingScene;
		Ref<StorageBuffer> lightBuffer = m_PathTraceLights;
		Ref<Image2D> image = m_PathTraceImage;
		Ref<Image2D> sceneColor = GetSceneColorOutput();
		std::shared_ptr<PathTraceState> state = m_PathTraceState;
		PathTraceEnvParams envParams = PreparePathTraceEnvironment(pass);
		if (serPass)
			PreparePathTraceEnvironment(serPass);
		if (restirDI)
			envParams.Flags |= 8; // FLAG_RESTIR_DI
		Ref<ComputePass> wavefront = raySort ? m_PathTraceWavefrontPass : nullptr;
		Ref<StorageBuffer> wavefrontBuffers[4] = { m_PTWStates, m_PTWKeys, m_PTWBins, m_PTWSorted };
		Ref<ComputePass> restirPasses[2] = { restirDI ? m_ReSTIRDIPasses[0] : nullptr, restirDI ? m_ReSTIRDIPasses[1] : nullptr };
		signature = HashBytes(signature, &envParams.Flags, sizeof(envParams.Flags));
		Ref<ComputePass> diffPass = diff ? m_PathTraceDiffPass : nullptr;
		Ref<Image2D> diffImage = diff ? m_PathTraceDiffImage : nullptr;
		Ref<StorageBuffer> diffPartials = diff ? m_PathTraceDiffPartials : nullptr;
		const float diffScale = m_Options.PathTracerDiffScale;
		const float diffPeak = std::max(m_Options.PathTracerDiffPeak, 0.01f);

		Renderer::Submit([=, instances = std::move(instances), lights = std::move(lights)]() mutable
		{
			const bool ser = serPass && serPass->GetPipeline()->GetNRIPipeline();
			const uint64_t dispatchSignature = HashBytes(signature, &ser, sizeof(ser));
			const bool sceneChanged = scene->RT_Update(cb, instances);
			if (sceneChanged || state->Signature != dispatchSignature || state->ResetCounter != resetCounter)
			{
				state->Frame = 0;
				state->Signature = dispatchSignature;
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
				push.Counts = glm::uvec4(PathTraceLightCount(lights), uniformEnvironment ? 1u : 0u, std::bit_cast<uint32_t>(adaptiveThreshold), debugView);
				push.SkyRadiance = glm::vec4(uniformEnvironment ? glm::vec3(furnaceRadiance) : glm::vec3(0.35f, 0.5f, 0.8f) * environmentIntensity, 0.0f);
				push.GroundRadiance = glm::vec4(uniformEnvironment ? glm::vec3(furnaceRadiance) : glm::vec3(0.12f, 0.1f, 0.09f) * environmentIntensity, 0.0f);
				ApplyPathTraceEnvironment(push, envParams);
				push.Medium = fogMedium;
				push.MediumAlbedo = fogAlbedo;
				push.MediumCenter = fogCenter;

				if (restirPasses[0])
				{
					// Reservoirs: candidates + temporal / spatial reuse from the previous frame's set, one shadow ray, direct light image.
					PathTracePush restirPush = push;
					restirPush.Counts.z = state->Frame.load() > 0 ? 1u : 0u;
					restirPush.Counts.w = 4u;
					Renderer::RT_BeginGPUPerfMarker(cb, "ReSTIRDI");
					restirPasses[state->ReSTIRParity]->Prepare();
					Renderer::RT_DispatchCompute(*cb, *restirPasses[state->ReSTIRParity], glm::uvec3((width + 7u) / 8u, (height + 7u) / 8u, 1u), Buffer(&restirPush, sizeof(restirPush)));
					Renderer::RT_EndGPUPerfMarker(cb);
					state->ReSTIRParity ^= 1u;
				}

				if (wavefront)
				{
					const uint32_t pixels = width * height;
					const uint32_t bytes[4] = { pixels * 96u, pixels * 4u, 256u * 4u, pixels * 4u };
					for (int i = 0; i < 4; i++)
					{
						if (wavefrontBuffers[i]->GetSize() < bytes[i])
							wavefrontBuffers[i]->Resize(bytes[i]);
					}
					const uint32_t groupsX = (width + 7u) / 8u, groupsY = (height + 7u) / 8u;
					cb->RT_BeginMarker("PathTraceSorted");
					// clear bins, first bounce + histogram, prefix scan, scatter (finishing dead paths), the rest of the live paths in sorted order
					for (const uint32_t mode : { 1u, 0u, 3u, 4u, 5u })
					{
						PathTracePush modePush = push;
						modePush.Counts.z = groupsX * 8u;
						modePush.Counts.w = mode;
						wavefront->Prepare();
						Renderer::RT_DispatchCompute(*cb, *wavefront, glm::uvec3(groupsX, groupsY, 1u), Buffer(&modePush, sizeof(modePush)));
					}
					cb->RT_EndMarker();
				}
				else
				{
					cb->RT_BeginMarker(ser ? "PathTraceSER" : "PathTrace");
					if (ser)
					{
						serPass->Prepare();
						Renderer::RT_DispatchRays(*cb, *serPass, glm::uvec3(width, height, 1u), Buffer(&push, sizeof(push)));
					}
					else
					{
						pass->Prepare();
						Renderer::RT_DispatchCompute(*cb, *pass, glm::uvec3((width + 7u) / 8u, (height + 7u) / 8u, 1u), Buffer(&push, sizeof(push)));
					}
					cb->RT_EndMarker();
				}
				state->Frame++;
			}

			if (state->Frame.load() == 0 || !sceneColor)
				return;

			if (diffPass)
			{
				// The diff goes first: scene colour still holds the raster frame here.
				const uint32_t groupsX = (width + 7u) / 8u, groupsY = (height + 7u) / 8u;
				const uint32_t groups = groupsX * groupsY;
				if (state->DiffGroups > 0 && diffPartials->GetSize() >= state->DiffGroups * sizeof(glm::vec2) && state->DiffPixels > 0)
				{
					// Read the previous dispatch's sums (a frame or more old, so the GPU has finished with them).
					const void* mapped = diffPartials->GetBuffer().Map(0, state->DiffGroups * sizeof(glm::vec2));
					if (mapped)
					{
						double sumAbs = 0.0, sumSq = 0.0;
						const glm::vec2* partials = static_cast<const glm::vec2*>(mapped);
						for (uint32_t i = 0; i < state->DiffGroups; i++)
						{
							sumAbs += partials[i].x;
							sumSq += partials[i].y;
						}
						diffPartials->GetBuffer().Unmap();
						const double mse = sumSq / state->DiffPixels;
						state->DiffMAE = static_cast<float>(sumAbs / state->DiffPixels);
						state->DiffPSNR = mse > 1e-12 ? static_cast<float>(-10.0 * std::log10(mse)) : 99.0f;
						// Logged now and then so a headless run (the golden harness) can assert on it.
						if (++state->DiffReads % 60 == 0)
							LUX_CORE_INFO_TAG("Core", "[PathTracer] diff vs raster: MAE {:.5f}, PSNR {:.2f} dB", state->DiffMAE.load(), state->DiffPSNR.load());
					}
				}

				if (diffPartials->GetSize() < groups * sizeof(glm::vec2))
					diffPartials->Resize(groups * static_cast<uint32_t>(sizeof(glm::vec2)));
				DiffPush diffPush = {};
				diffPush.SizeGroups = glm::uvec4(width, height, groupsX, 0u);
				diffPush.Params = glm::vec4(diffScale, 1.0f / diffPeak, 0.0f, 0.0f);
				cb->RT_BeginMarker("PathTraceDiff");
				diffPass->Prepare();
				Renderer::RT_DispatchCompute(*cb, *diffPass, glm::uvec3(groupsX, groupsY, 1u), Buffer(&diffPush, sizeof(diffPush)));
				cb->RT_EndMarker();
				state->DiffGroups = groups;
				state->DiffPixels = width * height;
				Renderer::RT_CopyImage(cb, diffImage, sceneColor);
				return;
			}

			// Replace the raster result in scene colour (post-processing then runs as usual).
			Renderer::RT_CopyImage(cb, image, sceneColor);
		});
	}

}
