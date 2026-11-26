// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Ray-traced GI, GI5 (docs/GI_PLAN.md): per-pixel indirect diffuse by ray queries, denoised by an edge-aware a-trous
// filter and added to scene colour after the raster lighting (RTGITrace.glsl, RTGIFilter.glsl). RT-only and opt-in
// (SceneRendererOptions::EnableRTGI); nothing here exists while it is off.

#include "lpch.h"
#include "Lux/Renderer/SceneRenderer.h"

#include "Lux/Renderer/ComputePass.h"
#include <bit>
#include "Lux/Renderer/PipelineCompute.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RayTracing/PathTraceLights.h"
#include "Lux/Renderer/RenderScene.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Shader.h"

#if defined(LUX_HAS_NRD) && !defined(LUX_DIST)
#include "NrdSdk.h"
#endif

namespace Lux {

	struct SceneRenderer::RTGIState
	{
		uint32_t Frame = 0; // render thread: frames since the camera, lights or scene last changed
		uint64_t Signature = 0;
		uint32_t Parity = 0; // which reservoir set the ReSTIR GI trace writes
		uint64_t ReuseSignature = 0; // everything but the camera: reservoirs are reprojected, so a moving camera keeps them
		bool ReuseValid = false;
		bool DenoiseValid = false; // the temporal history holds a previous frame of this scene
		uint32_t DenoiseParity = 0;
		uint64_t LastRenderedFrame = 0;
	};

	struct SceneRenderer::RTGINrdState : RefCounted
	{
		uint32_t Width = 0, Height = 0;
		Ref<ComputePass> Trace, Combine;
		Ref<Image2D> Radiance, Normals, Depth, Motion, Result;
		std::atomic<bool> Failed{ false };

		bool RT_Denoise(RenderCommandBuffer& cb, const glm::mat4& view, const glm::mat4& projection, bool reset);

#if defined(LUX_HAS_NRD) && !defined(LUX_DIST)
		struct Integration : RefCounted
		{
			nrd::Integration Instance;
		};
		Ref<Integration> Native;
		glm::mat4 PreviousView{ 1.0f }, PreviousProjection{ 1.0f };
		uint32_t FrameIndex = 0;

		~RTGINrdState() override
		{
			if (Native)
				Renderer::SubmitResourceFree([native = std::move(Native)] {});
		}
#endif
	};

	bool SceneRenderer::HasNRDSdk()
	{
#if defined(LUX_HAS_NRD) && !defined(LUX_DIST)
		static const bool installed = !NrdSdk::FindLibrary().empty();
		return installed && RHIDevice::SupportsRayQuery();
#else
		return false;
#endif
	}

	bool SceneRenderer::RTGINrdState::RT_Denoise(RenderCommandBuffer& cb, const glm::mat4& view, const glm::mat4& projection, bool reset)
	{
#if defined(LUX_HAS_NRD) && !defined(LUX_DIST)
		constexpr nrd::Identifier identifier = 1;
		if (Failed.load(std::memory_order_acquire))
			return false;
		if (!Native)
		{
			const nrd::DenoiserDesc denoiser{ identifier, nrd::Denoiser::RELAX_DIFFUSE };
			nrd::InstanceCreationDesc instance{};
			instance.denoisers = &denoiser;
			instance.denoisersNum = 1;
			nrd::IntegrationCreationDesc desc{};
			std::memcpy(desc.name, "Lux RTGI", sizeof("Lux RTGI"));
			desc.resourceWidth = static_cast<uint16_t>(Width);
			desc.resourceHeight = static_cast<uint16_t>(Height);
			desc.queuedFrameNum = static_cast<uint8_t>(Renderer::GetConfig().FramesInFlight);
			desc.autoWaitForIdle = false;
			auto native = Ref<Integration>::Create();
			if (native->Instance.Recreate(desc, instance, &RHIDevice::Get()) != nrd::Result::SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Renderer", "[RTGI] NRD initialization failed; using Lux denoising");
				Failed.store(true, std::memory_order_release);
				return false;
			}
			LUX_CORE_INFO_TAG("Renderer", "[RTGI] NRD RELAX diffuse active at {}x{}, {:.2f} MiB SDK working memory",
				Width, Height, native->Instance.GetTotalMemoryUsageInMb());
			Native = std::move(native);
			reset = true;
		}
		auto& native = Native->Instance;
		nrd::CommonSettings settings;
		settings.frameIndex = FrameIndex++;
		settings.accumulationMode = reset ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
		std::memcpy(settings.viewToClipMatrix, &projection[0][0], sizeof(settings.viewToClipMatrix));
		std::memcpy(settings.worldToViewMatrix, &view[0][0], sizeof(settings.worldToViewMatrix));
		std::memcpy(settings.viewToClipMatrixPrev, reset ? &projection[0][0] : &PreviousProjection[0][0], sizeof(settings.viewToClipMatrixPrev));
		std::memcpy(settings.worldToViewMatrixPrev, reset ? &view[0][0] : &PreviousView[0][0], sizeof(settings.worldToViewMatrixPrev));
		settings.resourceSize[0] = settings.resourceSizePrev[0] = settings.rectSize[0] = settings.rectSizePrev[0] = static_cast<uint16_t>(Width);
		settings.resourceSize[1] = settings.resourceSizePrev[1] = settings.rectSize[1] = settings.rectSizePrev[1] = static_cast<uint16_t>(Height);
		PreviousView = view;
		PreviousProjection = projection;
		native.NewFrame();
		if (native.SetCommonSettings(settings) != nrd::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTGI] NRD settings were rejected; using Lux denoising");
			Failed.store(true, std::memory_order_release);
			return false;
		}

		nrd::ResourceSnapshot resources;
		resources.restoreInitialState = true;
		constexpr nri::AccessLayoutStage sampled{ nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::ALL };
		const auto addInput = [&](nrd::ResourceType slot, const Ref<Image2D>& image)
		{
			cb.RT_RequireTextureState(image->GetImageInfo().Tracked, {}, ResourceState::ShaderResource);
			nrd::Resource input;
			input.nri.texture = image->GetHandle();
			input.state = sampled;
			resources.SetResource(slot, input);
		};
		addInput(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, Radiance);
		addInput(nrd::ResourceType::IN_NORMAL_ROUGHNESS, Normals);
		addInput(nrd::ResourceType::IN_VIEWZ, Depth);
		addInput(nrd::ResourceType::IN_MV, Motion);
		cb.RT_RequireTextureState(Result->GetImageInfo().Tracked, {}, ResourceState::UnorderedAccess);
		nrd::Resource output;
		output.nri.texture = Result->GetHandle();
		output.state = { nri::AccessBits::SHADER_RESOURCE_STORAGE | nri::AccessBits::CLEAR_STORAGE,
			nri::Layout::SHADER_RESOURCE_STORAGE, nri::StageBits::ALL };
		resources.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, output);
		if (auto* commands = cb.RT_BeginNRISegment())
		{
			native.Denoise(&identifier, 1, *commands, resources);
			cb.RT_EndNRISegment();
			return true;
		}
#endif
		return false;
	}

	namespace {

		struct RTGITracePush
		{
			glm::uvec4 SizeFrameBounces;
			glm::uvec4 Counts;
			glm::vec4 SkyRadiance;
			glm::vec4 GroundRadiance;
			glm::vec4 Medium;       // fog: density, anisotropy, radius (PathTraceCommon.glslh)
			glm::vec4 MediumAlbedo;
			glm::vec4 MediumCenter;
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

	Ref<SceneRenderer::RTGINrdState> SceneRenderer::EnsureRTGINrd(uint32_t width, uint32_t height)
	{
		if (m_RTGINrdUnavailable)
			return nullptr;
#if defined(LUX_HAS_NRD) && !defined(LUX_DIST)
		if (m_RTGINrd)
		{
			const auto existing = m_RTGINrd.As<RTGINrdState>();
			if (existing->Failed.load(std::memory_order_acquire))
			{
				m_RTGINrd = nullptr;
				m_RTGINrdUnavailable = true;
				return nullptr;
			}
			if (existing->Width == width && existing->Height == height)
				return existing;
		}
		if (HasNRDSdk() && NrdSdk::Get().Ready)
		{
			const auto& sdk = *NrdSdk::Get().GetLibraryDesc();
			if (sdk.versionMajor == NRD_VERSION_MAJOR && sdk.versionMinor == NRD_VERSION_MINOR && sdk.versionBuild == NRD_VERSION_BUILD &&
				sdk.normalEncoding == nrd::NormalEncoding::RGBA16_UNORM && sdk.roughnessEncoding == nrd::RoughnessEncoding::LINEAR)
			{
				auto library = Renderer::GetShaderLibrary();
				for (const char* name : { "RTGITraceNRD", "RTGINRDCombine" })
					if (!library->GetShaders().contains(name))
						library->Load(std::string("Resources/Shaders/PathTracing/") + name + ".glsl");
				if (library->GetShaders().contains("RTGITraceNRD") && library->GetShaders().contains("RTGINRDCombine"))
				{
					auto state = Ref<RTGINrdState>::Create();
					state->Width = width;
					state->Height = height;
					const auto createImage = [&](ImageFormat format, const char* name)
					{
						ImageSpecification spec;
						spec.Format = format;
						spec.Usage = ImageUsage::Storage;
						spec.Width = width;
						spec.Height = height;
						spec.DebugName = name;
						auto image = Image2D::Create(spec);
						image->Invalidate();
						return image;
					};
					state->Radiance = createImage(ImageFormat::RGBA16F, "NRD Diffuse Radiance and Hit Distance");
					state->Normals = createImage(ImageFormat::RGBA16F, "NRD Normal and Roughness");
					state->Depth = createImage(ImageFormat::RED32F, "NRD Linear View Depth");
					state->Motion = createImage(ImageFormat::RG16F, "NRD Motion");
					state->Result = createImage(ImageFormat::RGBA16F, "NRD Denoised Diffuse");

					ComputePassSpecification spec;
					spec.DebugName = "RTGITraceNRD";
					spec.Pipeline = PipelineCompute::Create(library->Get("RTGITraceNRD"));
					state->Trace = ComputePass::Create(spec);
					state->Trace->SetInput("Camera", m_UBSCamera);
					state->Trace->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
					state->Trace->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
					state->Trace->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
					state->Trace->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
					state->Trace->SetInput("RTLights", m_PathTraceLights);
					state->Trace->SetInput("o_RTGIAccum", m_RTGIAccum);
					state->Trace->SetInput("o_RTGIAlbedo", m_RTGIAlbedo);
					state->Trace->SetInput("o_RTGINormal", m_RTGINormal);
					state->Trace->SetInput("o_NRDRadiance", state->Radiance);
					state->Trace->SetInput("o_NRDNormalRoughness", state->Normals);
					state->Trace->SetInput("o_NRDViewDepth", state->Depth);
					state->Trace->SetInput("o_NRDMotion", state->Motion);
					PreparePathTraceEnvironment(state->Trace);

					spec.DebugName = "RTGINRDCombine";
					spec.Pipeline = PipelineCompute::Create(library->Get("RTGINRDCombine"));
					state->Combine = ComputePass::Create(spec);
					state->Combine->SetInput("u_RTGIAlbedoTex", m_RTGIAlbedo);
					state->Combine->SetInput("u_RTGISceneColor", GetSceneColorOutput());
					state->Combine->SetInput("u_NRDDenoised", state->Result);
					state->Combine->SetInput("o_RTGIOutput", m_RTGIResult);
					if (state->Trace->Validate() && state->Combine->Validate())
					{
						state->Trace->Bake();
						state->Combine->Bake();
						m_RTGINrd = state;
						return state;
					}
				}
			}
		}
#endif
		LUX_CORE_WARN_TAG("Renderer", "[RTGI] compatible NRD SDK or shaders unavailable; using Lux denoising");
		m_RTGINrdUnavailable = true;
		return nullptr;
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

	struct RTGITemporalPush
	{
		glm::uvec4 SizeValidAge;
	};

	bool SceneRenderer::EnsureRTGIDenoiser()
	{
		if (m_RTGITemporalPasses[0])
			return true;
		if (m_RTGIDenoiserUnavailable || !m_RTGITracePass)
			return false;
		auto library = Renderer::GetShaderLibrary();
		for (const char* name : { "RTGITemporal", "RTGIFilterVar" })
		{
			if (library->GetShaders().find(name) == library->GetShaders().end())
				library->Load(std::string("Resources/Shaders/PathTracing/") + name + ".glsl");
			if (library->GetShaders().find(name) == library->GetShaders().end())
			{
				LUX_CORE_ERROR_TAG("Renderer", "[RTGI] {}.glsl did not compile; the RTGI denoiser is off for this session", name);
				m_RTGIDenoiserUnavailable = true;
				return false;
			}
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
			m_TDHist0[i] = createImage(ImageFormat::RGBA16F, "RTGI-TDHist0");
			m_TDHist1[i] = createImage(ImageFormat::RGBA32F, "RTGI-TDHist1");
			m_TDNormal[i] = createImage(ImageFormat::RGBA16F, "RTGI-TDNormal");
		}
		m_TDDenoised = createImage(ImageFormat::RGBA32F, "RTGI-TDDenoised");
		m_TDVarC = createImage(ImageFormat::RGBA32F, "RTGI-TDVarC");
		m_TDVarD = createImage(ImageFormat::RGBA32F, "RTGI-TDVarD");

		bool valid = true;
		for (int i = 0; i < 2 && valid; i++)
		{
			const int prev = 1 - i;
			ComputePassSpecification spec;
			spec.DebugName = "RTGITemporal";
			spec.Pipeline = PipelineCompute::Create(library->Get("RTGITemporal"));
			Ref<ComputePass> pass = ComputePass::Create(spec);
			pass->SetInput("Camera", m_UBSCamera);
			pass->SetInput("u_RTGIAlbedoTex", m_RTGIAlbedo);
			pass->SetInput("u_RTGINormalTex", m_RTGINormal);
			pass->SetInput("u_TDNoisy", m_RTGIAccum);
			pass->SetInput("u_TDPrev0", m_TDHist0[prev]);
			pass->SetInput("u_TDPrev1", m_TDHist1[prev]);
			pass->SetInput("u_TDPrevNormal", m_TDNormal[prev]);
			pass->SetInput("o_TDHist0", m_TDHist0[i]);
			pass->SetInput("o_TDHist1", m_TDHist1[i]);
			pass->SetInput("o_TDNormal", m_TDNormal[i]);
			pass->SetInput("o_TDDenoised", m_TDDenoised);
			valid = pass->Validate();
			if (valid)
				pass->Bake();
			m_RTGITemporalPasses[i] = pass;
		}
		// Variance-guided chain: denoised -> C -> D -> (+ scene colour) -> result.
		const Ref<Image2D> inputs[3] = { m_TDDenoised, m_TDVarC, m_TDVarD };
		const Ref<Image2D> outputs[3] = { m_TDVarC, m_TDVarD, m_TDVarC };
		for (int i = 0; i < 3 && valid; i++)
		{
			ComputePassSpecification spec;
			spec.DebugName = i == 2 ? "RTGIFilterVarFinal" : "RTGIFilterVar";
			spec.Pipeline = PipelineCompute::Create(library->Get("RTGIFilterVar"));
			Ref<ComputePass> pass = ComputePass::Create(spec);
			pass->SetInput("u_RTGIInput", inputs[i]);
			pass->SetInput("u_RTGIAlbedoTex", m_RTGIAlbedo);
			pass->SetInput("u_RTGINormalTex", m_RTGINormal);
			pass->SetInput("u_RTGISceneColor", GetSceneColorOutput());
			pass->SetInput("o_RTGIVarOut", outputs[i]);
			pass->SetInput("o_RTGIVarFinal", m_RTGIResult);
			valid = pass->Validate();
			if (valid)
				pass->Bake();
			m_RTGIVarFilters[i] = pass;
		}
		if (!valid)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTGI] denoiser pass inputs do not match the shaders; the denoiser is off for this session");
			m_RTGITemporalPasses[0] = m_RTGITemporalPasses[1] = nullptr;
			m_RTGIDenoiserUnavailable = true;
			return false;
		}
		LUX_CORE_INFO_TAG("Renderer", "[RTGI] denoiser ready");
		return true;
	}

	bool SceneRenderer::EnsureRTGIUpscale()
	{
		if (m_RTGIUpscalePass)
			return true;
		if (m_RTGIUpscaleUnavailable)
			return false;
		auto library = Renderer::GetShaderLibrary();
		const char* name = "RTGIUpscale";
		if (!library->GetShaders().contains(name))
			library->Load("Resources/Shaders/PathTracing/RTGIUpscale.glsl");
		if (!library->GetShaders().contains(name))
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTGI] {} did not compile; using full-resolution RTGI", name);
			m_RTGIUpscaleUnavailable = true;
			return false;
		}

		ComputePassSpecification spec;
		spec.DebugName = name;
		spec.Pipeline = PipelineCompute::Create(library->Get(name));
		Ref<ComputePass> pass = ComputePass::Create(spec);
		pass->SetInput("Camera", m_UBSCamera);
		pass->SetInput("u_RTGIAlbedoTex", m_RTGIAlbedo);
		pass->SetInput("u_RTGINormalTex", m_RTGINormal);
		pass->SetInput("u_RTGISceneColor", GetSceneColorOutput());
		pass->SetInput("u_RTGIUpscaleInput", m_TDVarC);
		pass->SetInput("u_RTGIUpscaleDepth", m_PreDepthPass->GetDepthOutput());
		pass->SetInput("u_RTGIUpscaleNormal", GetGeometryNormalOutput());
		pass->SetInput("u_RTGIUpscaleBaseColor", GetGeometryBaseColorOutput());
		pass->SetInput("u_RTGIUpscaleMetalRough", GetGeometryMetalRoughOutput());
		pass->SetInput("o_RTGIVarFinal", m_RTGIResult);
		if (!pass->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTGI] upscaler inputs do not match the shader; using full-resolution RTGI");
			m_RTGIUpscaleUnavailable = true;
			return false;
		}
		pass->Bake();
		m_RTGIUpscalePass = pass;
		return true;
	}

	bool SceneRenderer::EnsureRTReflections()
	{
		if (m_RTReflPass)
			return true;
		if (m_RTReflUnavailable || !RHIDevice::SupportsRayQuery() || !ShaderCompiler::IsAvailable() || !m_PreDepthPass)
			return false;
		auto library = Renderer::GetShaderLibrary();
		if (library->GetShaders().find("RTReflections") == library->GetShaders().end())
			library->Load("Resources/Shaders/PathTracing/RTReflections.glsl");
		if (library->GetShaders().find("RTReflections") == library->GetShaders().end())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTReflections] RTReflections.glsl did not compile; ray-traced reflections are off for this session");
			m_RTReflUnavailable = true;
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
		ImageSpecification spec;
		spec.Format = ImageFormat::RGBA16F;
		spec.Usage = ImageUsage::Storage;
		spec.Width = std::max(1u, m_ViewportWidth);
		spec.Height = std::max(1u, m_ViewportHeight);
		spec.DebugName = "RTReflections-Result";
		m_RTReflResult = Image2D::Create(spec);
		m_RTReflResult->Invalidate();

		ComputePassSpecification passSpec;
		passSpec.DebugName = "RTReflections";
		passSpec.Pipeline = PipelineCompute::Create(library->Get("RTReflections"));
		Ref<ComputePass> pass = ComputePass::Create(passSpec);
		pass->SetInput("Camera", m_UBSCamera);
		pass->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
		pass->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
		pass->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
		pass->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
		pass->SetInput("RTLights", m_PathTraceLights);
		pass->SetInput("o_RRResult", m_RTReflResult);
		pass->SetInput("u_RRDepth", m_PreDepthPass->GetDepthOutput());
		pass->SetInput("u_RRNormal", GetGeometryNormalOutput());
		pass->SetInput("u_RRMetalRough", GetGeometryMetalRoughOutput());
		pass->SetInput("u_RRBaseColor", GetGeometryBaseColorOutput());
		pass->SetInput("u_RRSceneColor", GetSceneColorOutput());
		PreparePathTraceEnvironment(pass);
		if (!pass->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTReflections] pass inputs do not match the shader; ray-traced reflections are off for this session");
			m_RTReflUnavailable = true;
			return false;
		}
		pass->Bake();
		m_RTReflPass = pass;
		LUX_CORE_INFO_TAG("Renderer", "[RTReflections] ready");
		return true;
	}

	bool SceneRenderer::EnsureRTReflectionDenoiser()
	{
		if (m_RTReflDenoisePass)
			return true;
		if (m_RTReflDenoiseUnavailable)
			return false;

		auto library = Renderer::GetShaderLibrary();
		if (!library->GetShaders().contains("RTReflectionsDenoise"))
			library->Load("Resources/Shaders/PathTracing/RTReflectionsDenoise.glsl");
		if (!library->GetShaders().contains("RTReflectionsDenoise"))
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTReflections] denoiser shader did not compile; using unfiltered reflections");
			m_RTReflDenoiseUnavailable = true;
			return false;
		}

		ImageSpecification spec = m_RTReflResult->GetSpecification();
		spec.DebugName = "RTReflections-DenoisedResult";
		Ref<Image2D> output = Image2D::Create(spec);
		output->Invalidate();

		ComputePassSpecification passSpec;
		passSpec.DebugName = "RTReflectionsDenoise";
		passSpec.Pipeline = PipelineCompute::Create(library->Get("RTReflectionsDenoise"));
		Ref<ComputePass> pass = ComputePass::Create(passSpec);
		pass->SetInput("Camera", m_UBSCamera);
		pass->SetInput("u_RRDepth", m_PreDepthPass->GetDepthOutput());
		pass->SetInput("u_RRNormal", GetGeometryNormalOutput());
		pass->SetInput("u_RRMetalRough", GetGeometryMetalRoughOutput());
		pass->SetInput("u_RRBaseColor", GetGeometryBaseColorOutput());
		pass->SetInput("u_RRSceneColor", GetSceneColorOutput());
		pass->SetInput("u_RRCorrection", m_RTReflResult);
		pass->SetInput("o_RRResult", output);
		if (!pass->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTReflections] denoiser pass inputs do not match the shader; using unfiltered reflections");
			m_RTReflDenoiseUnavailable = true;
			return false;
		}
		pass->Bake();
		m_RTReflDenoisedResult = output;
		m_RTReflDenoisePass = pass;
		return true;
	}

	void SceneRenderer::RTReflectionsPass()
	{
		ScopedCPUProfile cpuProfile(*this, "RTReflectionsPass");
		ApplyRTTuning();
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0 || !m_SubmittedRenderScene || !EnsureRTReflections())
			return;
		if (m_RTReflResult->GetWidth() != m_ViewportWidth || m_RTReflResult->GetHeight() != m_ViewportHeight)
			m_RTReflResult->Resize(m_ViewportWidth, m_ViewportHeight);
		const bool denoise = m_Options.RTReflectionDenoiser && EnsureRTReflectionDenoiser();
		const Ref<Image2D> depth = m_PreDepthPass->GetDepthOutput();
		const Ref<Image2D> normal = GetGeometryNormalOutput();
		const Ref<Image2D> metalRough = GetGeometryMetalRoughOutput();
		const Ref<Image2D> baseColor = GetGeometryBaseColorOutput();
		const Ref<Image2D> sceneColor = GetSceneColorOutput();
		// The G-buffer and scene colour are recreated on a resize.
		m_RTReflPass->SetInput("u_RRDepth", depth);
		m_RTReflPass->SetInput("u_RRNormal", normal);
		m_RTReflPass->SetInput("u_RRMetalRough", metalRough);
		m_RTReflPass->SetInput("u_RRBaseColor", baseColor);
		m_RTReflPass->SetInput("u_RRSceneColor", sceneColor);
		if (denoise)
		{
			if (m_RTReflDenoisedResult->GetWidth() != m_ViewportWidth || m_RTReflDenoisedResult->GetHeight() != m_ViewportHeight)
				m_RTReflDenoisedResult->Resize(m_ViewportWidth, m_ViewportHeight);
			m_RTReflDenoisePass->SetInput("u_RRDepth", depth);
			m_RTReflDenoisePass->SetInput("u_RRNormal", normal);
			m_RTReflDenoisePass->SetInput("u_RRMetalRough", metalRough);
			m_RTReflDenoisePass->SetInput("u_RRBaseColor", baseColor);
			m_RTReflDenoisePass->SetInput("u_RRSceneColor", sceneColor);
			GetOrCreatePassProfile("RTReflectionsDenoise").GPUActive = true;
		}
		GetOrCreatePassProfile("RTReflectionsPass").GPUActive = true;

		std::vector<const StaticMeshRenderProxy*> proxies = m_SubmittedRenderScene->GetStaticMeshProxies();
		std::vector<RayTracingScene::InstanceInput> instances = RayTracingScene::GatherInstances(proxies);
		std::vector<glm::vec4> lights = PackLights(m_SceneData.SceneLightEnvironment, m_Options.PathTracerPowerLightSampling);
		const uint32_t width = m_ViewportWidth, height = m_ViewportHeight;
		const uint32_t rays = std::clamp(m_Options.RTReflectionRays, 1u, 16u);
		const float cutoff = std::clamp(m_Options.RTReflectionRoughnessCutoff, 0.0f, 1.0f);
		constexpr uint32_t replaceEnvironment = 1u << 7u;
		constexpr uint32_t adaptiveBudget = 1u << 8u;
		constexpr uint32_t filterCorrection = 1u << 9u;
		const uint32_t reflectionFlags = (m_Options.RTReflectionReplaceEnvironment ? replaceEnvironment : 0u)
			| (m_Options.RTReflectionAdaptiveBudget ? adaptiveBudget : 0u) | (denoise ? filterCorrection : 0u);
		const uint32_t frame = m_RTReflFrame++;

		Ref<RenderCommandBuffer> cb = m_CommandBuffer;
		Ref<RayTracingScene> scene = m_RayTracingScene;
		Ref<ComputePass> pass = m_RTReflPass;
		Ref<ComputePass> filter = denoise ? m_RTReflDenoisePass : nullptr;
		Ref<StorageBuffer> lightBuffer = m_PathTraceLights;
		Ref<Image2D> result = denoise ? m_RTReflDenoisedResult : m_RTReflResult;
		const PathTraceEnvParams envParams = PreparePathTraceEnvironment(m_RTReflPass);

		Renderer::Submit([=, instances = std::move(instances), lights = std::move(lights)]() mutable
		{
			scene->RT_Update(cb, instances);
			if (!scene->IsValid() || !sceneColor)
				return;
			const uint32_t lightBytes = static_cast<uint32_t>(lights.size() * sizeof(glm::vec4));
			if (lightBuffer->GetSize() < lightBytes)
				lightBuffer->Resize(lightBytes);
			if (lightBytes > 0)
				lightBuffer->RT_SetData(cb, lights.data(), lightBytes);

			RTGITracePush push = {};
			push.SizeFrameBounces = glm::uvec4(width, height, frame, 0u);
			push.Counts = glm::uvec4(PathTraceLightCount(lights), 0u, rays, std::bit_cast<uint32_t>(cutoff));
			ApplyPathTraceEnvironment(push, envParams);
			push.Counts.y |= reflectionFlags;
			const glm::uvec3 groups((width + 7u) / 8u, (height + 7u) / 8u, 1u);
			Renderer::RT_BeginGPUPerfMarker(cb, "RTReflectionsPass");
			pass->Prepare();
			Renderer::RT_DispatchCompute(*cb, *pass, groups, Buffer(&push, sizeof(push)));
			Renderer::RT_EndGPUPerfMarker(cb);
			if (filter)
			{
				Renderer::RT_BeginGPUPerfMarker(cb, "RTReflectionsDenoise");
				filter->Prepare();
				Renderer::RT_DispatchCompute(*cb, *filter, groups, Buffer());
				Renderer::RT_EndGPUPerfMarker(cb);
			}
			Renderer::RT_CopyImage(cb, result, sceneColor);
		});
	}

	uint32_t SceneRenderer::GetRTVisibilityFlags() const
	{
		if (m_RTShadowUnavailable || !RHIDevice::SupportsRayQuery() || !ShaderCompiler::IsAvailable())
			return 0u;
		return (m_Options.EnableRTShadows || m_Options.EnableRTTranslucentShadows ? k_RTFullDirectionalShadow : 0u)
			| (m_Options.EnableRTContactShadows ? k_RTContactShadow : 0u)
			| (m_Options.EnableRTAmbientOcclusion ? k_RTAmbientOcclusion : 0u);
	}

	bool SceneRenderer::EnsureRTShadows()
	{
		if (m_RTShadowPass)
			return true;
		if (m_RTShadowUnavailable || !RHIDevice::SupportsRayQuery() || !ShaderCompiler::IsAvailable() || !m_PreDepthPass)
			return false;
		auto library = Renderer::GetShaderLibrary();
		if (library->GetShaders().find("RTShadows") == library->GetShaders().end())
			library->Load("Resources/Shaders/PathTracing/RTShadows.glsl");
		if (library->GetShaders().find("RTShadows") == library->GetShaders().end())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTShadows] RTShadows.glsl did not compile; ray-traced shadows are off for this session");
			m_RTShadowUnavailable = true;
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
		ImageSpecification spec;
		spec.Format = ImageFormat::RGBA16F;
		spec.Usage = ImageUsage::Storage;
		spec.Width = std::max(1u, m_ViewportWidth);
		spec.Height = std::max(1u, m_ViewportHeight);
		spec.DebugName = "RTShadows-Mask";
		m_RTShadowMask = Image2D::Create(spec);
		m_RTShadowMask->Invalidate();
		m_RTShadowMaskClear = Ref<StorageImageClear>::Create(m_RTShadowMask, 0);

		ComputePassSpecification passSpec;
		passSpec.DebugName = "RTShadows";
		passSpec.Pipeline = PipelineCompute::Create(library->Get("RTShadows"));
		Ref<ComputePass> pass = ComputePass::Create(passSpec);
		pass->SetInput("Camera", m_UBSCamera);
		pass->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
		pass->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
		pass->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
		pass->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
		pass->SetInput("RTLights", m_PathTraceLights);
		pass->SetInput("o_RSMask", m_RTShadowMask);
		pass->SetInput("u_RSDepth", m_PreDepthPass->GetDepthOutput());
		pass->SetInput("u_RSNormal", GetGeometryNormalOutput());
		PreparePathTraceEnvironment(pass);
		if (!pass->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTShadows] pass inputs do not match the shader; ray-traced shadows are off for this session");
			m_RTShadowMask = nullptr;
			m_RTShadowMaskClear = nullptr;
			m_RTShadowUnavailable = true;
			return false;
		}
		pass->Bake();
		m_RTShadowPass = pass;
		LUX_CORE_INFO_TAG("Renderer", "[RTShadows] ready");
		return true;
	}

	bool SceneRenderer::EnsureRTVisibilityFilter()
	{
		if (m_RTVisibilityFilter)
			return true;
		if (m_RTVisibilityFilterUnavailable)
			return false;
		auto library = Renderer::GetShaderLibrary();
		if (!library->GetShaders().contains("RTVisibilityFilter"))
			library->Load("Resources/Shaders/PathTracing/RTVisibilityFilter.glsl");
		if (!library->GetShaders().contains("RTVisibilityFilter"))
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTShadows] visibility filter did not compile; using unfiltered visibility");
			m_RTVisibilityFilterUnavailable = true;
			return false;
		}
		ImageSpecification spec = m_RTShadowMask->GetSpecification();
		spec.DebugName = "RTShadows-FilteredMask";
		Ref<Image2D> output = Image2D::Create(spec);
		output->Invalidate();
		ComputePassSpecification passSpec;
		passSpec.DebugName = "RTVisibilityFilter";
		passSpec.Pipeline = PipelineCompute::Create(library->Get("RTVisibilityFilter"));
		Ref<ComputePass> pass = ComputePass::Create(passSpec);
		pass->SetInput("Camera", m_UBSCamera);
		pass->SetInput("u_RSDepth", m_PreDepthPass->GetDepthOutput());
		pass->SetInput("u_RSNormal", GetGeometryNormalOutput());
		pass->SetInput("u_RSRawMask", m_RTShadowMask);
		pass->SetInput("o_RSMask", output);
		if (!pass->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTShadows] visibility filter inputs do not match the shader; using unfiltered visibility");
			m_RTVisibilityFilterUnavailable = true;
			return false;
		}
		pass->Bake();
		m_RTFilteredVisibility = output;
		m_RTVisibilityFilter = pass;
		return true;
	}

	void SceneRenderer::RTShadowsPass()
	{
		ScopedCPUProfile cpuProfile(*this, "RTShadowsPass");
		ApplyRTTuning();
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0 || !m_SubmittedRenderScene || !EnsureRTShadows())
			return;
		if (m_RTShadowMask->GetWidth() != m_ViewportWidth || m_RTShadowMask->GetHeight() != m_ViewportHeight)
		{
			m_RTShadowMask->Resize(m_ViewportWidth, m_ViewportHeight);
			m_RTShadowMaskClear = Ref<StorageImageClear>::Create(m_RTShadowMask, 0);
		}
		m_RTShadowPass->SetInput("u_RSDepth", m_PreDepthPass->GetDepthOutput());
		m_RTShadowPass->SetInput("u_RSNormal", GetGeometryNormalOutput());
		const uint32_t visibilityFlags = GetRTVisibilityFlags();
		const bool denoise = (visibilityFlags & (k_RTContactShadow | k_RTAmbientOcclusion)) != 0u && EnsureRTVisibilityFilter();
		if (denoise)
		{
			if (m_RTFilteredVisibility->GetWidth() != m_ViewportWidth || m_RTFilteredVisibility->GetHeight() != m_ViewportHeight)
				m_RTFilteredVisibility->Resize(m_ViewportWidth, m_ViewportHeight);
			m_RTVisibilityFilter->SetInput("u_RSDepth", m_PreDepthPass->GetDepthOutput());
			m_RTVisibilityFilter->SetInput("u_RSNormal", GetGeometryNormalOutput());
			GetOrCreatePassProfile("RTVisibilityFilter").GPUActive = true;
		}
		GetOrCreatePassProfile("RTShadowsPass").GPUActive = true;

		std::vector<const StaticMeshRenderProxy*> proxies = m_SubmittedRenderScene->GetStaticMeshProxies();
		std::vector<RayTracingScene::InstanceInput> instances = RayTracingScene::GatherInstances(proxies);
		const DirectionalLight& sun = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
		const glm::vec3 toLight = glm::length(sun.Direction) > 1e-5f ? glm::normalize(sun.Direction) : glm::vec3(0.0f, 1.0f, 0.0f);
		const bool fullShadow = (visibilityFlags & k_RTFullDirectionalShadow) != 0u;
		const bool contactShadow = (visibilityFlags & k_RTContactShadow) != 0u;
		const bool ambientOcclusion = (visibilityFlags & k_RTAmbientOcclusion) != 0u;
		const bool translucentShadows = m_Options.EnableRTTranslucentShadows;
		const bool refractiveCaustics = m_Options.EnableRTCaustics && !m_RTCausticsUnavailable;
		const float radius = fullShadow ? glm::radians(std::clamp(m_Options.RTShadowAngularRadiusDegrees, 0.0f, 30.0f)) : 0.0f;
		const float contactDistance = contactShadow ? std::clamp(m_Options.RTContactShadowDistance, 0.01f, 10.0f) : 0.0f;
		const float shadowDistance = sun.Intensity > 0.0f ? (fullShadow ? 1e30f : contactDistance) : 0.0f;
		const float aoRadius = std::clamp(m_Options.RTAmbientOcclusionRadius, 0.01f, 25.0f);
		const uint32_t width = m_ViewportWidth, height = m_ViewportHeight;
		const uint32_t rays = fullShadow ? std::clamp(m_Options.RTShadowRays, 1u, 64u) : 1u;
		const uint32_t aoRays = ambientOcclusion ? std::clamp(m_Options.RTAmbientOcclusionRays, 1u, 16u) : 0u;
		const uint32_t frame = m_RTShadowFrame++;

		Ref<RenderCommandBuffer> cb = m_CommandBuffer;
		Ref<RayTracingScene> scene = m_RayTracingScene;
		Ref<ComputePass> pass = m_RTShadowPass;
		Ref<ComputePass> filter = denoise ? m_RTVisibilityFilter : nullptr;
		Ref<StorageImageClear> rawMaskClear = m_RTShadowMaskClear;
		const PathTraceEnvParams envParams = PreparePathTraceEnvironment(m_RTShadowPass);

		Renderer::Submit([=, instances = std::move(instances)]() mutable
		{
			scene->RT_Update(cb, instances);
			const glm::uvec3 groups((width + 7u) / 8u, (height + 7u) / 8u, 1u);
			if (scene->IsValid())
			{
				RTGITracePush push = {};
				push.SizeFrameBounces = glm::uvec4(width, height, frame, 0u);
				push.Counts = glm::uvec4(0u, 0u, rays, aoRays);
				ApplyPathTraceEnvironment(push, envParams);
				push.SkyRadiance = glm::vec4(toLight, radius);
				if (translucentShadows)
					push.Counts.y |= (1u << 8u) | (1u << 5u); // translucent visibility and glass material evaluation
				if (refractiveCaustics)
					push.Counts.y |= 1u << 9u;
				push.GroundRadiance.x = shadowDistance;
				push.GroundRadiance.y = aoRadius;
				Renderer::RT_BeginGPUPerfMarker(cb, "RTShadowsPass");
				pass->Prepare();
				Renderer::RT_DispatchCompute(*cb, *pass, groups, Buffer(&push, sizeof(push)));
				Renderer::RT_EndGPUPerfMarker(cb);
			}
			else
				rawMaskClear->RT_Record(*cb, glm::vec4(1.0f));
			if (filter)
			{
				Renderer::RT_BeginGPUPerfMarker(cb, "RTVisibilityFilter");
				filter->Prepare();
				Renderer::RT_DispatchCompute(*cb, *filter, groups, Buffer());
				Renderer::RT_EndGPUPerfMarker(cb);
			}
		});
	}

	bool SceneRenderer::EnsureRTCaustics()
	{
		if (m_RTCausticTrace)
			return true;
		if (m_RTCausticsUnavailable || !RHIDevice::SupportsRayQuery() || !ShaderCompiler::IsAvailable())
			return false;
		auto library = Renderer::GetShaderLibrary();
		for (const char* name : { "RTCausticPhotons", "RTCausticResolve" })
		{
			if (!library->GetShaders().contains(name))
				library->Load(std::string("Resources/Shaders/PathTracing/") + name + ".glsl");
			if (!library->GetShaders().contains(name))
			{
				LUX_CORE_ERROR_TAG("Renderer", "[RTCaustics] {} did not compile; caustics are unavailable", name);
				m_RTCausticsUnavailable = true;
				return false;
			}
		}
		if (!m_RayTracingScene)
			m_RayTracingScene = Ref<RayTracingScene>::Create();
		if (!m_PathTraceLights)
		{
			StorageBufferSpecification spec;
			spec.GPUOnly = false;
			spec.DebugName = "PathTrace Lights";
			m_PathTraceLights = StorageBuffer::Create(256, spec);
		}

		ImageSpecification spec;
		spec.Usage = ImageUsage::Storage;
		spec.Format = ImageFormat::RED32UI;
		spec.Width = 3u * ((m_ViewportWidth + 3u) / 4u);
		spec.Height = (m_ViewportHeight + 3u) / 4u;
		spec.DebugName = "RTCaustics-Flux";
		Ref<Image2D> flux = Image2D::Create(spec);
		flux->Invalidate();
		spec.Format = ImageFormat::RGBA16F;
		spec.Width = m_ViewportWidth;
		spec.Height = m_ViewportHeight;
		spec.DebugName = "RTCaustics-SceneColor";
		Ref<Image2D> result = Image2D::Create(spec);
		result->Invalidate();

		ComputePassSpecification photonSpec;
		photonSpec.DebugName = "RTCausticPhotons";
		photonSpec.Pipeline = PipelineCompute::Create(library->Get("RTCausticPhotons"));
		Ref<ComputePass> trace = ComputePass::Create(photonSpec);
		trace->SetInput("Camera", m_UBSCamera);
		trace->SetInput("u_RTScene", Ref<RHIAccelerationStructure>(m_RayTracingScene));
		trace->SetInput("RTVertices", m_RayTracingScene->GetVertexBuffer());
		trace->SetInput("RTIndices", m_RayTracingScene->GetIndexBuffer());
		trace->SetInput("RTInstances", m_RayTracingScene->GetInstanceBuffer());
		trace->SetInput("RTLights", m_PathTraceLights);
		trace->SetInput("u_CausticDepth", m_PreDepthPass->GetDepthOutput());
		trace->SetInput("o_CausticFlux", flux);
		PreparePathTraceEnvironment(trace);

		ComputePassSpecification resolveSpec;
		resolveSpec.DebugName = "RTCausticResolve";
		resolveSpec.Pipeline = PipelineCompute::Create(library->Get("RTCausticResolve"));
		Ref<ComputePass> resolve = ComputePass::Create(resolveSpec);
		resolve->SetInput("Camera", m_UBSCamera);
		resolve->SetInput("u_CausticDepth", m_PreDepthPass->GetDepthOutput());
		resolve->SetInput("u_CausticNormal", GetGeometryNormalOutput());
		resolve->SetInput("u_CausticBaseColor", GetGeometryBaseColorOutput());
		resolve->SetInput("u_CausticMetalRough", GetGeometryMetalRoughOutput());
		resolve->SetInput("u_CausticSceneColor", GetSceneColorOutput());
		resolve->SetInput("u_CausticFlux", flux);
		resolve->SetInput("o_CausticResult", result);
		if (!trace->Validate() || !resolve->Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RTCaustics] pass inputs do not match the shaders; caustics are unavailable");
			m_RTCausticsUnavailable = true;
			return false;
		}
		trace->Bake();
		resolve->Bake();
		m_RTCausticFlux = flux;
		m_RTCausticResult = result;
		m_RTCausticClear = Ref<StorageImageClear>::Create(flux, 0);
		m_RTCausticTrace = trace;
		m_RTCausticResolve = resolve;
		return true;
	}

	void SceneRenderer::RTCausticsPass()
	{
		ScopedCPUProfile cpuProfile(*this, "RTCausticsPass");
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0 || !m_SubmittedRenderScene)
			return;
		auto instances = RayTracingScene::GatherInstances(m_SubmittedRenderScene->GetStaticMeshProxies());
		if (std::none_of(instances.begin(), instances.end(), [](const auto& instance) { return instance.Refractive; }))
			return;
		auto lights = PackLights(m_SceneData.SceneLightEnvironment, m_Options.PathTracerPowerLightSampling);
		if (lights.empty() || !EnsureRTCaustics())
			return;
		ApplyRTTuning();

		AABB bounds(glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX));
		AABB glassBounds = bounds;
		for (const auto& instance : instances)
		{
			const AABB instanceBounds = instance.Source->GetSubmeshes()[instance.Submesh].BoundingBox.Transformed(instance.ObjectToWorld);
			bounds.Min = glm::min(bounds.Min, instanceBounds.Min);
			bounds.Max = glm::max(bounds.Max, instanceBounds.Max);
			if (instance.Refractive)
			{
				glassBounds.Min = glm::min(glassBounds.Min, instanceBounds.Min);
				glassBounds.Max = glm::max(glassBounds.Max, instanceBounds.Max);
			}
		}
		const glm::vec3 centre = glassBounds.Center();
		const float radius = 0.5f * glm::length(glassBounds.Size()) + 0.01f;
		const float directionalDistance = glm::length(glm::max(glm::abs(bounds.Min - centre), glm::abs(bounds.Max - centre))) + 0.1f;
		const uint32_t width = m_ViewportWidth, height = m_ViewportHeight;
		const uint32_t photons = std::clamp(m_Options.RTCausticPhotons, 4096u, 262144u);
		if (m_RTCausticResult->GetWidth() != width || m_RTCausticResult->GetHeight() != height)
		{
			m_RTCausticFlux->Resize(3u * ((width + 3u) / 4u), (height + 3u) / 4u);
			m_RTCausticResult->Resize(width, height);
			m_RTCausticClear = Ref<StorageImageClear>::Create(m_RTCausticFlux, 0);
		}
		const Ref<Image2D> depth = m_PreDepthPass->GetDepthOutput();
		Ref<Image2D> sceneColor = GetSceneColorOutput();
		m_RTCausticTrace->SetInput("u_CausticDepth", depth);
		m_RTCausticResolve->SetInput("u_CausticDepth", depth);
		m_RTCausticResolve->SetInput("u_CausticNormal", GetGeometryNormalOutput());
		m_RTCausticResolve->SetInput("u_CausticBaseColor", GetGeometryBaseColorOutput());
		m_RTCausticResolve->SetInput("u_CausticMetalRough", GetGeometryMetalRoughOutput());
		m_RTCausticResolve->SetInput("u_CausticSceneColor", sceneColor);
		const PathTraceEnvParams envParams = PreparePathTraceEnvironment(m_RTCausticTrace);
		GetOrCreatePassProfile("RTCausticPhotons").GPUActive = true;
		GetOrCreatePassProfile("RTCausticResolve").GPUActive = true;

		Renderer::Submit([cb = m_CommandBuffer, scene = m_RayTracingScene, clear = m_RTCausticClear,
			trace = m_RTCausticTrace, resolve = m_RTCausticResolve, result = m_RTCausticResult, sceneColor,
			lightBuffer = m_PathTraceLights, envParams, centre, radius, directionalDistance, width, height, photons,
			instances = std::move(instances), lights = std::move(lights)]() mutable
		{
			scene->RT_Update(cb, instances);
			if (!scene->IsValid() || !sceneColor)
				return;
			const uint32_t lightBytes = static_cast<uint32_t>(lights.size() * sizeof(glm::vec4));
			if (lightBuffer->GetSize() < lightBytes)
				lightBuffer->Resize(lightBytes);
			lightBuffer->RT_SetData(cb, lights.data(), lightBytes);

			RTGITracePush push = {};
			push.SizeFrameBounces = glm::uvec4(width, height, 0u, photons);
			push.Counts = glm::uvec4(PathTraceLightCount(lights), 32u, 0u, 0u);
			ApplyPathTraceEnvironment(push, envParams);
			push.SkyRadiance = glm::vec4(centre, radius);
			push.GroundRadiance.x = directionalDistance;
			Renderer::RT_BeginGPUPerfMarker(cb, "RTCausticPhotons");
			clear->RT_Record(*cb, glm::vec4(0.0f));
			trace->Prepare();
			Renderer::RT_DispatchCompute(*cb, *trace, glm::uvec3((photons + 63u) / 64u, 1u, 1u), Buffer(&push, sizeof(push)));
			Renderer::RT_EndGPUPerfMarker(cb);
			Renderer::RT_BeginGPUPerfMarker(cb, "RTCausticResolve");
			resolve->Prepare();
			const glm::uvec4 resolvePush(photons, 0u, 0u, 0u);
			Renderer::RT_DispatchCompute(*cb, *resolve, glm::uvec3((width + 7u) / 8u, (height + 7u) / 8u, 1u), Buffer(&resolvePush, sizeof(resolvePush)));
			Renderer::RT_EndGPUPerfMarker(cb);
			Renderer::RT_CopyImage(cb, result, sceneColor);
		});
	}

	void SceneRenderer::RTGIPass()
	{
		ScopedCPUProfile cpuProfile(*this, "RTGIPass");
		ApplyRTTuning();
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0 || !m_SubmittedRenderScene || !EnsureRTGI())
			return;

		const uint32_t fullWidth = m_ViewportWidth, fullHeight = m_ViewportHeight;
		const glm::mat4 projection = m_SceneData.SceneCamera.Camera.GetProjectionMatrix();
		const bool upscale = m_Options.RTGIHalfResolution && projection[3][3] != 1.0f && EnsureRTGIDenoiser() && EnsureRTGIUpscale();
		const uint32_t width = upscale ? (fullWidth + 1u) / 2u : fullWidth;
		const uint32_t height = upscale ? (fullHeight + 1u) / 2u : fullHeight;
		if (m_RTGIAccum->GetWidth() != width || m_RTGIAccum->GetHeight() != height)
		{
			for (Ref<Image2D> image : { m_RTGIAccum, m_RTGIAlbedo, m_RTGINormal, m_RTGITempA, m_RTGITempB })
				image->Resize(width, height);
		}
		if (m_RTGIResult->GetWidth() != fullWidth || m_RTGIResult->GetHeight() != fullHeight)
			m_RTGIResult->Resize(fullWidth, fullHeight);
		const bool reuse = m_Options.RTGIReuse && EnsureRTGIReSTIR();
		if (reuse && (m_RGSample[0]->GetWidth() != width || m_RGSample[0]->GetHeight() != height))
		{
			for (int i = 0; i < 2; i++)
				for (Ref<Image2D> image : { m_RGSample[i], m_RGSampleNormal[i], m_RGRadiance[i], m_RGGPos[i], m_RGGNormal[i] })
					image->Resize(width, height);
		}
		const bool useNrd = m_Options.RTGINRD && !upscale && !reuse && projection[3][3] != 1.0f;
		auto nrd = useNrd ? EnsureRTGINrd(width, height) : Ref<RTGINrdState>{};
		if (!useNrd)
			m_RTGINrd = nullptr;
		const bool denoise = upscale || ((m_Options.RTGIDenoiser || m_Options.RTGINRD) && EnsureRTGIDenoiser());
		const Ref<Image2D> sceneColor = GetSceneColorOutput();
		if (nrd)
		{
			nrd->Combine->SetInput("u_RTGISceneColor", sceneColor);
			GetOrCreatePassProfile("RTGINRD").GPUActive = true;
		}
		if (denoise)
		{
			if (m_TDDenoised->GetWidth() != width || m_TDDenoised->GetHeight() != height)
			{
				for (int i = 0; i < 2; i++)
					for (Ref<Image2D> image : { m_TDHist0[i], m_TDHist1[i], m_TDNormal[i] })
						image->Resize(width, height);
				for (Ref<Image2D> image : { m_TDDenoised, m_TDVarC, m_TDVarD })
					image->Resize(width, height);
			}
			m_RTGIVarFilters[2]->SetInput("u_RTGISceneColor", sceneColor);
		}
		if (upscale)
		{
			m_RTGIUpscalePass->SetInput("u_RTGISceneColor", sceneColor);
			m_RTGIUpscalePass->SetInput("u_RTGIUpscaleDepth", m_PreDepthPass->GetDepthOutput());
			m_RTGIUpscalePass->SetInput("u_RTGIUpscaleNormal", GetGeometryNormalOutput());
			m_RTGIUpscalePass->SetInput("u_RTGIUpscaleBaseColor", GetGeometryBaseColorOutput());
			m_RTGIUpscalePass->SetInput("u_RTGIUpscaleMetalRough", GetGeometryMetalRoughOutput());
			GetOrCreatePassProfile("RTGIUpscale").GPUActive = true;
		}
		// Scene colour may have been recreated (a resize); the final pass samples it.
		m_RTGIFilterPasses[2]->SetInput("u_RTGISceneColor", sceneColor);
		GetOrCreatePassProfile("RTGITrace").GPUActive = true;
		GetOrCreatePassProfile("RTGIFilter").GPUActive = true;

		std::vector<const StaticMeshRenderProxy*> proxies = m_SubmittedRenderScene->GetStaticMeshProxies();
		std::vector<RayTracingScene::InstanceInput> instances = RayTracingScene::GatherInstances(proxies);
		std::vector<glm::vec4> lights = PackLights(m_SceneData.SceneLightEnvironment, m_Options.PathTracerPowerLightSampling);
		const uint32_t bounces = std::clamp(m_Options.RTGIBounces, 1u, 4u);
		const float environmentIntensity = m_FrameEnvironment.EnvironmentIntensity;
		const PathTraceEnvParams envParams = PreparePathTraceEnvironment(nrd ? nrd->Trace : m_RTGITracePass);
		const uint32_t historyLength = std::clamp(m_Options.RTGIHistoryLength, 1u, 256u);
		uint64_t reuseSignature = HashBytes(1469598103934665603ull, &projection, sizeof(projection));
		reuseSignature = HashBytes(reuseSignature, &bounces, sizeof(bounces));
		reuseSignature = HashBytes(reuseSignature, &fullWidth, sizeof(fullWidth));
		reuseSignature = HashBytes(reuseSignature, &fullHeight, sizeof(fullHeight));
		reuseSignature = HashBytes(reuseSignature, &upscale, sizeof(upscale));
		reuseSignature = HashBytes(reuseSignature, &reuse, sizeof(reuse));
		reuseSignature = HashBytes(reuseSignature, &denoise, sizeof(denoise));
		reuseSignature = HashBytes(reuseSignature, &useNrd, sizeof(useNrd));
		reuseSignature = HashBytes(reuseSignature, &historyLength, sizeof(historyLength));
		reuseSignature = HashBytes(reuseSignature, &envParams.Flags, sizeof(envParams.Flags));
		reuseSignature = HashBytes(reuseSignature, &envParams.Intensity, sizeof(envParams.Intensity));
		reuseSignature = HashBytes(reuseSignature, &envParams.Rotation, sizeof(envParams.Rotation));
		reuseSignature = HashBytes(reuseSignature, &envParams.FogMedium, sizeof(envParams.FogMedium));
		reuseSignature = HashBytes(reuseSignature, &envParams.FogAlbedo, sizeof(envParams.FogAlbedo));
		reuseSignature = HashBytes(reuseSignature, &envParams.FogCenter, sizeof(envParams.FogCenter));
		if (!lights.empty())
			reuseSignature = HashBytes(reuseSignature, lights.data(), lights.size() * sizeof(glm::vec4));
		const auto view = m_SceneData.SceneCamera.ViewMatrix;
		const uint64_t signature = HashBytes(reuseSignature, &view, sizeof(view));

		Ref<RenderCommandBuffer> cb = m_CommandBuffer;
		Ref<RayTracingScene> scene = m_RayTracingScene;
		Ref<ComputePass> trace = nrd ? nrd->Trace : m_RTGITracePass;
		Ref<ComputePass> temporalPasses[2] = { denoise ? m_RTGITemporalPasses[0] : nullptr, denoise ? m_RTGITemporalPasses[1] : nullptr };
		Ref<ComputePass> varFilters[3] = { denoise ? m_RTGIVarFilters[0] : nullptr, denoise ? m_RTGIVarFilters[1] : nullptr, denoise ? m_RTGIVarFilters[2] : nullptr };
		Ref<ComputePass> reusePasses[2] = { reuse ? m_RTGIReSTIRPasses[0] : nullptr, reuse ? m_RTGIReSTIRPasses[1] : nullptr };
		Ref<ComputePass> filters[3] = { m_RTGIFilterPasses[0], m_RTGIFilterPasses[1], m_RTGIFilterPasses[2] };
		Ref<ComputePass> upscalePass = upscale ? m_RTGIUpscalePass : nullptr;
		Ref<StorageBuffer> lightBuffer = m_PathTraceLights;
		Ref<Image2D> result = m_RTGIResult;
		std::shared_ptr<RTGIState> state = m_RTGIState;

		Renderer::Submit([=, instances = std::move(instances), lights = std::move(lights)]() mutable
		{
			const bool changed = scene->RT_Update(cb, instances);
			const uint64_t frameNumber = Renderer::RT_GetFrameNumber();
			const bool interrupted = state->LastRenderedFrame + 1 != frameNumber;
			const bool resetHistory = changed || interrupted || state->ReuseSignature != reuseSignature;
			if (resetHistory)
			{
				state->ReuseValid = false;
				state->DenoiseValid = false;
				state->ReuseSignature = reuseSignature;
			}
			if (changed || interrupted || state->Signature != signature)
			{
				state->Frame = 0;
				state->Signature = signature;
			}
			if (!scene->IsValid() || !sceneColor)
				return;
			state->LastRenderedFrame = frameNumber;

			const uint32_t lightBytes = static_cast<uint32_t>(lights.size() * sizeof(glm::vec4));
			if (lightBuffer->GetSize() < lightBytes)
				lightBuffer->Resize(lightBytes);
			if (lightBytes > 0)
				lightBuffer->RT_SetData(cb, lights.data(), lightBytes);

			const glm::uvec3 groups((width + 7u) / 8u, (height + 7u) / 8u, 1u);
			RTGITracePush tracePush = {};
			tracePush.SizeFrameBounces = glm::uvec4(width, height, nrd ? static_cast<uint32_t>(frameNumber) : state->Frame, bounces);
			tracePush.Counts = glm::uvec4(PathTraceLightCount(lights), 0u, 0u, nrd || temporalPasses[0] ? 1u : historyLength);
			tracePush.SkyRadiance = glm::vec4(glm::vec3(0.35f, 0.5f, 0.8f) * environmentIntensity, 0.0f);
			tracePush.GroundRadiance = glm::vec4(glm::vec3(0.12f, 0.1f, 0.09f) * environmentIntensity, 0.0f);
			ApplyPathTraceEnvironment(tracePush, envParams);
			cb->RT_BeginMarker("RTGI");
			Renderer::RT_BeginGPUPerfMarker(cb, "RTGITrace");
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
			Renderer::RT_EndGPUPerfMarker(cb);

			if (nrd)
			{
				Renderer::RT_BeginGPUPerfMarker(cb, "RTGINRD");
				const bool denoised = nrd->RT_Denoise(*cb, view, projection, resetHistory);
				if (denoised)
				{
					nrd->Combine->Prepare();
					Renderer::RT_DispatchCompute(*cb, *nrd->Combine, groups, {});
				}
				Renderer::RT_EndGPUPerfMarker(cb);
				if (denoised)
				{
					cb->RT_EndMarker();
					state->Frame++;
					Renderer::RT_CopyImage(cb, result, sceneColor);
					return;
				}
			}

			Renderer::RT_BeginGPUPerfMarker(cb, "RTGIFilter");
			const uint32_t steps[3] = { 1u, 2u, 4u };
			if (temporalPasses[0])
			{
				// The trace wrote a raw per-frame estimate; reproject-and-blend it, then filter guided by its variance.
				RTGITemporalPush temporalPush = {};
				temporalPush.SizeValidAge = glm::uvec4(width, height, state->DenoiseValid ? 1u : 0u, historyLength);
				temporalPasses[state->DenoiseParity]->Prepare();
				Renderer::RT_DispatchCompute(*cb, *temporalPasses[state->DenoiseParity], groups, Buffer(&temporalPush, sizeof(temporalPush)));
				state->DenoiseParity ^= 1u;
				state->DenoiseValid = true;
				for (uint32_t i = 0; i < 3; i++)
				{
					RTGIFilterPush push = {};
					push.SizeModeStep = glm::uvec4(width, height, i == 2 && !upscalePass ? 1u : 0u, steps[i]);
					push.Params = glm::vec4(32.0f, 0.04f, 4.0f, 0.0f);
					varFilters[i]->Prepare();
					Renderer::RT_DispatchCompute(*cb, *varFilters[i], groups, Buffer(&push, sizeof(push)));
				}
			}
			else
			{
				for (uint32_t i = 0; i < 3; i++)
				{
					RTGIFilterPush push = {};
					push.SizeModeStep = glm::uvec4(width, height, i == 2 ? 1u : 0u, steps[i]);
					push.Params = glm::vec4(32.0f, 0.04f, 0.0f, 0.0f);
					filters[i]->Prepare();
					Renderer::RT_DispatchCompute(*cb, *filters[i], groups, Buffer(&push, sizeof(push)));
				}
			}
			Renderer::RT_EndGPUPerfMarker(cb);
			if (upscalePass)
			{
				Renderer::RT_BeginGPUPerfMarker(cb, "RTGIUpscale");
				upscalePass->Prepare();
				Renderer::RT_DispatchCompute(*cb, *upscalePass, glm::uvec3((fullWidth + 7u) / 8u, (fullHeight + 7u) / 8u, 1u), {});
				Renderer::RT_EndGPUPerfMarker(cb);
			}
			cb->RT_EndMarker();
			state->Frame++;
			Renderer::RT_CopyImage(cb, result, sceneColor);
		});
	}

}
