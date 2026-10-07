// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Renderer.h"

#include "Shader.h"

#include "Renderer2D.h"
#include "RendererAPI.h"
//#include "SceneRenderer.h"
#include "ShaderPack.h"
#include "ShaderPermutationCache.h"
#include "RenderPass.h"
#include "ComputePass.h"
#include "ShaderDefs.h"

#include "Lux/Core/Timer.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Renderer/BindlessTextureTable.h"
#include "Lux/Renderer/RHI/GPUDeletionQueue.h"
#include "Lux/Renderer/RHI/NVRHIInterop.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/StringUtils.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/MaterialAsset.h"

#include "nvrhi/nvrhi.h"
#include "nvrhi/utils.h"

#if LUX_HAS_SHADER_COMPILER
#include "Lux/Platform/Vulkan/ShaderCompiler/VulkanShaderCompiler.h"
#endif

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "IndexBuffer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"

namespace std {
	template<>
	struct hash<Lux::WeakRef<Lux::Shader>>
	{
		size_t operator()(const Lux::WeakRef<Lux::Shader>& shader) const noexcept
		{
			return shader->GetHash();
		}
	};
}

namespace Lux {

	// Cache of compute pipelines keyed by shader hash, shared across the whole
	// process. Currently only the mip generator (LinearSample / LinearSampleUInt)
	// uses it; before, GenerateMips built a fresh pipeline per texture.
	static std::unordered_map<size_t, Ref<PipelineCompute>> s_MipGenPipelineCache;
	static std::mutex s_MipGenPipelineCacheMutex;

	struct ShaderDependencies
	{
		std::vector<WeakRef<PipelineCompute>> ComputePipelines;
		std::vector<WeakRef<Pipeline>> Pipelines;
		std::vector<WeakRef<Material>> Materials;
		std::vector<WeakRef<RenderPass>> Passes;
		std::vector<WeakRef<ComputePass>> ComputePasses;
	};
	static std::unordered_map<size_t, ShaderDependencies> s_ShaderDependencies;
	static std::shared_mutex s_ShaderDependenciesMutex; // ShaderDependencies can be accessed (and modified) from multiple threads, hence require synchronization

	template<typename T>
	static void PruneDeadDependencies(std::vector<WeakRef<T>>& dependencies)
	{
		dependencies.erase(
			std::remove_if(dependencies.begin(), dependencies.end(), [](const WeakRef<T>& dependency) { return !dependency; }),
			dependencies.end());
	}


	struct GlobalShaderInfo
	{
		// Macro name, set of shaders with that macro.
		std::unordered_map<std::string, std::unordered_map<size_t, WeakRef<Shader>>> ShaderGlobalMacrosMap;
		// Shaders waiting to be reloaded.
		std::unordered_set<WeakRef<Shader>> DirtyShaders;
		ShaderPermutationCache PermutationCache;
	};
	static GlobalShaderInfo s_GlobalShaderInfo;
	static const std::filesystem::path s_ShaderPermutationCachePath = "Resources/Cache/ShaderPermutations.cache";

	struct RendererData
	{
		RendererCapabilities RenderCaps;

		Ref<ShaderLibrary> m_ShaderLibrary;

		Ref<Texture2D> WhiteTexture;
		Ref<Texture2D> BlackTexture;
		Ref<Texture2D> BRDFLutTexture;
		Ref<Texture2D> HilbertLut;
		Ref<TextureCube> BlackCubeTexture;
		Ref<Material> DefaultWhiteMaterial;
		Ref<Environment> EmptyEnvironment;
		Ref<Environment> DefaultEnvironment;

		std::unordered_map<std::string, std::string> GlobalShaderMacros;

		Ref<Texture2D> BRDFLut;

		Ref<VertexBuffer> QuadVertexBuffer;
		Ref<IndexBuffer> QuadIndexBuffer;

		// Default samplers
		Ref<Sampler> SamplerClamp = nullptr;
		Ref<Sampler> SamplerPoint = nullptr;
		Ref<Sampler> SamplerRepeat = nullptr;

		int32_t DrawCallCount = 0;
		int32_t DrawInstanceCount = 0;
	};

	static RendererData* s_RendererData = nullptr;

	void Renderer::RegisterShaderDependency(Ref<Shader> shader, PipelineCompute* computePipeline)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].ComputePipelines.push_back(computePipeline);
	}

	void Renderer::RegisterShaderDependency(Ref<Shader> shader, Pipeline* pipeline)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].Pipelines.push_back(pipeline);
	}

	void Renderer::RegisterShaderDependency(Ref<Shader> shader, Material* material)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].Materials.push_back(material);
	}

	void Renderer::RegisterShaderDependency(Ref<Shader> shader, RenderPass* renderPass)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].Passes.push_back(renderPass);
	}

	void Renderer::RegisterShaderDependency(Ref<Shader> shader, ComputePass* computePass)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].ComputePasses.push_back(computePass);
	}

	void Renderer::QueueWaitForCommandList(GPUQueue waitQueue, GPUQueue executionQueue, uint64_t instance)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// nvrhi tracks a completion timeline per queue; this inserts the wait on
		// waitQueue for executionQueue's instance without any manual semaphores.
		Application::GetGraphicsDevice()->queueWaitForCommandList(ToNVRHI(waitQueue), ToNVRHI(executionQueue), instance);
	}

	Ref<PipelineCompute> Renderer::GetOrCreateMipGenPipeline(Ref<Shader> shader)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const size_t hash = shader->GetHash();
		std::scoped_lock lock(s_MipGenPipelineCacheMutex);
		if (auto it = s_MipGenPipelineCache.find(hash); it != s_MipGenPipelineCache.end())
			return it->second;

		Ref<PipelineCompute> pipeline = PipelineCompute::Create(shader);
		s_MipGenPipelineCache[hash] = pipeline;
		return pipeline;
	}

	void Renderer::OnShaderReloaded(size_t hash)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		ShaderDependencies dependencies;
		{
			std::scoped_lock lock(s_ShaderDependenciesMutex);
			if (auto it = s_ShaderDependencies.find(hash); it != s_ShaderDependencies.end())
			{
				PruneDeadDependencies(it->second.Pipelines);
				PruneDeadDependencies(it->second.ComputePipelines);
				PruneDeadDependencies(it->second.Materials);
				PruneDeadDependencies(it->second.Passes);
				PruneDeadDependencies(it->second.ComputePasses);
				dependencies = it->second; // Copy weak refs so callbacks run outside the registry lock.
			}
		}
		for (auto& pipeline : dependencies.Pipelines)
		{
			if (pipeline)
				pipeline->Invalidate();
		}

		for (auto& computePipeline : dependencies.ComputePipelines)
		{
			if (computePipeline)
				computePipeline->CreatePipeline();
		}

		for (auto& material : dependencies.Materials)
		{
			if (material)
				material->OnShaderReloaded();
		}

		// Passes re-bake after the pipelines above are rebuilt: an in-place
		// recompile released the binding layouts their baked descriptor sets
		// were created against, so drawing with them is a use-after-free.
		for (auto& renderPass : dependencies.Passes)
		{
			if (renderPass)
				renderPass->OnShaderReloaded();
		}

		for (auto& computePass : dependencies.ComputePasses)
		{
			if (computePass)
				computePass->OnShaderReloaded();
		}
	}

	uint32_t Renderer::GetCurrentFrameIndex()
	{
		return Application::Get().GetCurrentFrameIndex();
	}

	void RendererAPI::SetAPI(RendererAPIType api)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// TODO: make sure this is called at a valid time
		LUX_CORE_VERIFY(api == RendererAPIType::Vulkan, "Vulkan is currently the only supported Renderer API");
		s_CurrentRendererAPI = api;
	}

	static RendererConfig s_Config;
	static RendererData* s_Data = nullptr;
	constexpr static uint32_t s_RenderCommandQueueCount = 2;
	static RenderCommandQueue* s_CommandQueue[s_RenderCommandQueueCount];
	static std::atomic<uint32_t> s_RenderCommandQueueSubmissionIndex = 0;
	// Render-thread frame bookkeeping (RT_BeginFrame). A frame is closed behind a graphics-queue
	// event when the next one begins, so the event also covers work queued after Present; slot
	// N % FramesInFlight holds frame N's event until frame N + FramesInFlight waits on it. An
	// event, not frame counting: NVRHI does not keep descriptor tables alive from command lists,
	// and Present() skips its pacing when an acquire fails.
	static uint64_t s_RTFrameNumber = 0;
	static nvrhi::EventQueryHandle s_FrameCompletionEvents[RendererConfig::MaxFramesInFlight];
	static GPUDeletionQueue s_ResourceFreeQueue;
	// Set at the end of Shutdown; SubmitResourceFree then releases immediately.
	static std::atomic<bool> s_ShutDown = false;

	// Work submitted from background threads (e.g. the asset worker) is parked here and replayed on the
	// main thread, since the render command queue is single-producer. See Renderer::Submit.
	static std::vector<std::function<void()>> s_BackgroundThreadSubmitQueue;
	static std::mutex s_BackgroundThreadSubmitMutex;

	static std::mutex s_ResourceUploadMutex;
	static nvrhi::CommandListHandle s_ResourceUploadCommandList;
	static bool s_ResourceUploadListOpen = false;
	// Queue type the currently-cached upload list was created for. Command lists
	// are bound to a queue at creation, so a live toggle of async transfer means
	// retiring the list and making a new one of the other type.
	static bool s_ResourceUploadListIsCopyQueue = false;
	// Setting (Renderer.AsyncTransferQueue). Effective only when the device also
	// has a dedicated transfer queue — see Renderer::UseAsyncTransferQueue.
	// Default OFF: keepInitialState resources (vertex/index buffers, textures) are
	// restored to their graphics state (VertexBuffer/ShaderResource) at upload-list
	// close, which emits VERTEX_INPUT / SHADER_READ|WRITE barriers that are illegal
	// on a transfer-only queue (validation VUID-*Barrier2-*). Uploading on the
	// graphics queue instead is spec-correct. Opt back in via the setting once the
	// copy path does proper queue-family-ownership transfers.
	static std::atomic<bool> s_AsyncTransferEnabled = false;
	// Setting (Renderer.ExplicitBarriers), written from any thread. RT_BeginFrame latches it into
	// s_RTExplicitBarriers so every command buffer in a frame uses the same mode. Atomic because the
	// few command buffers begun off the render thread (readbacks) read it too.
	static std::atomic<bool> s_ExplicitBarriersRequested = false;
	static std::atomic<bool> s_RTExplicitBarriers = false;
	// Setting (Renderer.NRICompute), latched by RT_BeginFrame like the one above. Render thread only
	// once latched.
	static std::atomic<bool> s_NRIComputeRequested = false;
	static bool s_RTNRICompute = false;
	// Shaders whose NRI dispatch fell back to NVRHI; logged once each. Render thread.
	static std::unordered_set<const VulkanShader*> s_ReportedNRIComputeFallbacks;
	// Setting (Renderer.NRIGraphics), latched by RT_BeginFrame. Atomic like s_RTExplicitBarriers:
	// command buffers begun off the render thread read it in RT_Begin.
	static std::atomic<bool> s_NRIGraphicsRequested = false;
	static std::atomic<bool> s_RTNRIGraphics = false;
	// Pipelines whose pass or draws fell back to NVRHI; logged once each. Render thread.
	static std::unordered_set<const void*> s_ReportedNRIGraphicsFallbacks;
	// Copy-queue execution instance of the most recent async upload flush (0 = none
	// yet). Consumers wait on it before reading uploaded resources.
	static std::atomic<uint64_t> s_LastUploadInstance = 0;
	// Highest upload instance each consuming queue has already inserted a wait for,
	// indexed by nvrhi::CommandQueue (Graphics=0, Compute=1, Copy=2). Avoids
	// redundant per-submit waits on an upload that's already been synchronized.
	static std::atomic<uint64_t> s_QueueWaitedUploadInstance[(size_t)GPUQueue::Count] = {};

	static nvrhi::VariableShadingRate ToNVRHIShadingRate(FragmentShadingRate rate)
	{
		switch (rate)
		{
			case FragmentShadingRate::Rate1x2: return nvrhi::VariableShadingRate::e1x2;
			case FragmentShadingRate::Rate2x1: return nvrhi::VariableShadingRate::e2x1;
			case FragmentShadingRate::Rate2x2: return nvrhi::VariableShadingRate::e2x2;
			case FragmentShadingRate::Rate2x4: return nvrhi::VariableShadingRate::e2x4;
			case FragmentShadingRate::Rate4x2: return nvrhi::VariableShadingRate::e4x2;
			case FragmentShadingRate::Rate4x4: return nvrhi::VariableShadingRate::e4x4;
			case FragmentShadingRate::Rate1x1:
			default:
				return nvrhi::VariableShadingRate::e1x1;
		}
	}

	static nvrhi::VariableRateShadingState MakeFragmentShadingRateState(FragmentShadingRate rate)
	{
		nvrhi::VariableRateShadingState state;
		if (!Renderer::SupportsVariableRateShading())
			return state;

		state.enabled = true;
		state.shadingRate = ToNVRHIShadingRate(rate);
		state.pipelinePrimitiveCombiner = nvrhi::ShadingRateCombiner::Override;
		state.imageCombiner = nvrhi::ShadingRateCombiner::Override;
		return state;
	}

	// Records a compute dispatch with NRI into the NVRHI command buffer (Renderer.NRICompute). The
	// NVRHI compute state is already committed, so the barriers are in place and only the recording
	// API differs: the same pipeline, sets and push constants, bound through NRI. False when an NRI
	// object is missing; the caller then dispatches through NVRHI, and the reason is logged (once per
	// shader for a missing object) so a partial NRI frame is visible.
	static bool RT_DispatchComputeWithNRI(RenderCommandBuffer& commandBuffer, const ComputePass& computePass, const Material* material, const glm::uvec3& workGroups, const Buffer& constants)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref<VulkanShader> shader = computePass.GetShader().As<VulkanShader>();
		nri::PipelineLayout* layout = shader->GetNRIPipelineLayout();
		nri::Pipeline* pipeline = computePass.GetPipeline()->GetNRIPipeline();
		const char* missing = !layout ? "pipeline layout" : (!pipeline ? "pipeline" : nullptr);

		// The sets NVRHI binds: the pass's, and the material's in the set it occupies.
		const uint32_t frameIndex = Renderer::RT_GetCurrentFrameIndex();
		const nvrhi::BindingSetVector& bindings = commandBuffer.GetComputeState().bindings;
		std::array<nri::SetDescriptorSetDesc, 8> sets = {};
		uint32_t setCount = 0;
		for (uint32_t set = 0; !missing && set < bindings.size() && set < sets.size(); set++)
		{
			const uint32_t setIndex = shader->GetNRISetIndex(set);
			// NVRHI binds an empty placeholder where the shader declares nothing.
			if (!bindings[set] || setIndex == VulkanShader::k_NoNRISet)
				continue;

			const bool isMaterialSet = material && bindings[set] == material->GetBindingSet(frameIndex).Get();
			nri::DescriptorSet* descriptorSet = isMaterialSet ? material->GetNRIDescriptorSet(frameIndex) : computePass.GetNRIDescriptorSet(frameIndex, set);
			if (!descriptorSet)
			{
				missing = "descriptor set";
				break;
			}

			nri::SetDescriptorSetDesc& setDesc = sets[setCount++];
			setDesc.setIndex = setIndex;
			setDesc.descriptorSet = descriptorSet;
			setDesc.bindPoint = nri::BindPoint::COMPUTE;
		}

		if (missing)
		{
			if (s_ReportedNRIComputeFallbacks.insert(shader.Raw()).second)
				LUX_CORE_ERROR_TAG("Renderer", "NRI compute: {} has no NRI {}; it is dispatched through NVRHI", shader->GetName(), missing);
			return false;
		}

		nri::CommandBuffer* nriCommandBuffer = commandBuffer.RT_BeginNRISegment();
		if (!nriCommandBuffer)
			return false;

		const NRIInterface& api = RHIDevice::API();
		api.CmdSetPipelineLayout(*nriCommandBuffer, nri::BindPoint::COMPUTE, *layout);
		api.CmdSetPipeline(*nriCommandBuffer, *pipeline);
		for (uint32_t i = 0; i < setCount; i++)
			api.CmdSetDescriptorSet(*nriCommandBuffer, sets[i]);

		if (constants && shader->HasNRIRootConstants())
		{
			nri::SetRootConstantsDesc rootConstants = {};
			rootConstants.rootConstantIndex = 0;
			rootConstants.data = constants.Data;
			rootConstants.size = static_cast<uint32_t>(constants.Size);
			rootConstants.bindPoint = nri::BindPoint::COMPUTE;
			api.CmdSetRootConstants(*nriCommandBuffer, rootConstants);
		}

		api.CmdDispatch(*nriCommandBuffer, { workGroups.x, workGroups.y, workGroups.z });
		commandBuffer.RT_EndNRISegment();
		return true;
	}

	static void ReportNRIGraphicsFallback(const void* pipeline, std::string_view passName, const char* reason)
	{
		if (s_ReportedNRIGraphicsFallbacks.insert(pipeline).second)
			LUX_CORE_ERROR_TAG("Renderer", "NRI graphics: '{}' renders through NVRHI: {}", passName, reason);
	}

	static RendererAPI* InitRendererAPI()
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::Vulkan: return nullptr;
		}
		LUX_CORE_ASSERT(false, "Unknown RendererAPI");
		return nullptr;
	}

	void Renderer::Init()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		s_Data = lnew RendererData();
		s_RendererData = lnew RendererData();
		s_GlobalShaderInfo.PermutationCache.LoadFromFile(s_ShaderPermutationCachePath);

		s_CommandQueue[0] = lnew RenderCommandQueue();
		s_CommandQueue[1] = lnew RenderCommandQueue();

		// Make sure we don't have more frames in flight than swapchain images. Since frame slots are
		// a frame number (RT_GetCurrentFrameIndex), not the back-buffer index, this is no longer
		// required for correctness; kept to preserve today's frame pacing.
		s_Config.FramesInFlight = glm::min<uint32_t>(s_Config.FramesInFlight, Application::Get().GetWindow().GetSwapChain().GetBackBufferCount());
		s_Config.FramesInFlight = glm::clamp<uint32_t>(s_Config.FramesInFlight, 1u, RendererConfig::MaxFramesInFlight);

		// Before any shader is loaded: material shaders take their bindless set layout from here.
		BindlessTextureTable::Init();

		Renderer::SetGlobalMacroInShaders("__HZ_REFLECTION_OCCLUSION_METHOD", "0");
		Renderer::SetGlobalMacroInShaders("__HZ_AO_METHOD", std::format("{}", (int)ShaderDef::GetAOMethod(true)));
		Renderer::SetGlobalMacroInShaders("__HZ_GTAO_COMPUTE_BENT_NORMALS", "0");

		// Meshlet data is only built when the GPU can consume it (VK_EXT_mesh_shader).
		MeshSource::SetBuildMeshlets(Renderer::SupportsMeshShaders());

		s_Data->m_ShaderLibrary = Ref<ShaderLibrary>::Create();

		if (!s_Config.ShaderPackPath.empty())
			Renderer::GetShaderLibrary()->LoadShaderPack(s_Config.ShaderPackPath);


		// NOTE: some shaders (compute) need to have optimization disabled because of a shaderc internal error
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/LinearSample.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/LinearSampleUInt.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/ImGui.hlsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/HZB.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/HazelPBR_Static.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/HazelPBR_Transparent.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/GBuffer_Static.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/DeferredLighting.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/GBufferDebug.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/HazelPBR_Anim.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/LuxPBR_Static.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/LuxPBR_Transparent.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/Grid.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/Wireframe.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/Wireframe_Anim.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/Skybox.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/DirShadowMap.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/DirShadowMap_Anim.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/SpotShadowMap.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/SpotShadowMap_Anim.glsl");

		//SSR
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/Pre-Integration.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/Pre-Convolution.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/SSR.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/SSR-Composite.glsl");

		// Environment compute shaders
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/EnvironmentMipFilter.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/EquirectangularToCubeMap.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/EnvironmentIrradiance.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PreethamSky.glsl");

		// Post-processing
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/Bloom.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/LuminanceHistogram.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/LuminanceAverage.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/SMAA-EdgeDetection.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/SMAA-WeightAndBlend.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/DOF.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/SceneComposite.glsl");

		// Light-culling
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PreDepth.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PreDepth_Anim.glsl");
		if (Renderer::SupportsMeshShaders())
			Renderer::GetShaderLibrary()->Load("Resources/Shaders/PreDepth_Meshlet.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/MeshCulling.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/ClusterBuild.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/ClusterLightCulling.glsl");

		// Renderer2D Shaders
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/Renderer2D.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/Renderer2D_Line.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/Renderer2D_Circle.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/Renderer2D_Text.glsl");

		// Jump Flood Shaders
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/JumpFlood_Init.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/JumpFlood_Pass.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/JumpFlood_Composite.glsl");

		// GTAO
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/GTAO.hlsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/GTAO-Denoise.glsl");

		// AO
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/AO-Composite.glsl");

		// Misc
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/SelectedGeometry.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/SelectedGeometry_Anim.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/TexturePass.glsl");

		// Compile shaders
		Application::Get().GetRenderThread().Pump();

		uint32_t whiteTextureData = 0xffffffff;
		TextureSpecification spec;
		spec.DebugName = "Renderer-WhiteTexture";
		spec.Format = ImageFormat::RGBA;
		spec.Width = 1;
		spec.Height = 1;
		s_Data->WhiteTexture = Texture2D::Create(spec, Buffer(&whiteTextureData, sizeof(uint32_t)));

		constexpr uint32_t blackTextureData = 0xff000000;
		spec.DebugName = "Renderer-BlackTexture";
		s_Data->BlackTexture = Texture2D::Create(spec, Buffer(&blackTextureData, sizeof(uint32_t)));

		{
			TextureSpecification spec;
			spec.SamplerWrap = TextureWrap::Clamp;
			s_Data->BRDFLutTexture = Texture2D::Create(spec, std::filesystem::path("Resources/Renderer/BRDF_LUT.png"));
		}

		constexpr uint32_t blackCubeTextureData[6] = { 0xff000000, 0xff000000, 0xff000000, 0xff000000, 0xff000000, 0xff000000 };
		spec.DebugName = "Renderer-BlackCubeTexture";
		s_Data->BlackCubeTexture = TextureCube::Create(spec, Buffer(blackCubeTextureData, sizeof(blackCubeTextureData)));

		s_Data->EmptyEnvironment = Ref<Environment>::Create(s_Data->BlackCubeTexture, s_Data->BlackCubeTexture);

		s_Data->DefaultWhiteMaterial = Material::Create(Renderer::GetShaderLibrary()->Get("HazelPBR_Static"), "Renderer-DefaultWhiteMaterial");
		s_Data->DefaultWhiteMaterial->Set("u_MaterialUniforms.AlbedoColor", glm::vec3(1.0f));
		s_Data->DefaultWhiteMaterial->Set("u_MaterialUniforms.Emission", 0.0f);
		s_Data->DefaultWhiteMaterial->Set("u_MaterialUniforms.UseNormalMap", false);
		s_Data->DefaultWhiteMaterial->Set("u_MaterialUniforms.Metalness", 0.0f);
		s_Data->DefaultWhiteMaterial->Set("u_MaterialUniforms.Roughness", 0.4f);
		s_Data->DefaultWhiteMaterial->Set("u_AlbedoTexture", s_Data->WhiteTexture);
		s_Data->DefaultWhiteMaterial->Set("u_NormalTexture", s_Data->WhiteTexture);
		s_Data->DefaultWhiteMaterial->Set("u_MetalnessTexture", s_Data->WhiteTexture);
		s_Data->DefaultWhiteMaterial->Set("u_RoughnessTexture", s_Data->WhiteTexture);

		// Hilbert look-up texture! It's a 64 x 64 uint16 texture
		{
			TextureSpecification spec;
			spec.Format = ImageFormat::RED16UI;
			spec.Width = 64;
			spec.Height = 64;
			spec.SamplerWrap = TextureWrap::Clamp;
			spec.SamplerFilter = TextureFilter::Nearest;

			constexpr auto HilbertIndex = [](uint32_t posX, uint32_t posY)
				{
					uint16_t index = 0u;
					for (uint16_t curLevel = 64 / 2u; curLevel > 0u; curLevel /= 2u)
					{
						const uint16_t regionX = (posX & curLevel) > 0u;
						const uint16_t regionY = (posY & curLevel) > 0u;
						index += curLevel * curLevel * ((3u * regionX) ^ regionY);
						if (regionY == 0u)
						{
							if (regionX == 1u)
							{
								posX = uint16_t((64 - 1u)) - posX;
								posY = uint16_t((64 - 1u)) - posY;
							}

							std::swap(posX, posY);
						}
					}
					return index;
				};

			uint16_t* data = new uint16_t[(size_t)(64 * 64)];
			for (int x = 0; x < 64; x++)
			{
				for (int y = 0; y < 64; y++)
				{
					const uint16_t r2index = HilbertIndex(x, y);
					LUX_CORE_ASSERT(r2index < 65536);
					data[x + 64 * y] = r2index;
				}
			}
			s_Data->HilbertLut = Texture2D::Create(spec, Buffer(data, 1));
			delete[] data;

		}

		Utils::DumpGPUInfo();

		// Create fullscreen quad
		float x = -1;
		float y = -1;
		float width = 2, height = 2;
		struct QuadVertex
		{
			glm::vec3 Position;
			glm::vec2 TexCoord;
		};

		std::array<QuadVertex, 4> quadVertexData;

		quadVertexData[0].Position = glm::vec3(x, y, 0.0f);
		quadVertexData[0].TexCoord = glm::vec2(0, 1);

		quadVertexData[1].Position = glm::vec3(x + width, y, 0.0f);
		quadVertexData[1].TexCoord = glm::vec2(1, 1);

		quadVertexData[2].Position = glm::vec3(x + width, y + height, 0.0f);
		quadVertexData[2].TexCoord = glm::vec2(1, 0);

		quadVertexData[3].Position = glm::vec3(x, y + height, 0.0f);
		quadVertexData[3].TexCoord = glm::vec2(0, 0);

		s_RendererData->QuadVertexBuffer = VertexBuffer::Create(Buffer(quadVertexData.data(), quadVertexData.size() * sizeof(QuadVertex)));

		std::array<uint32_t, 6> indices = { 0, 1, 2, 2, 3, 0, };
		s_RendererData->QuadIndexBuffer = IndexBuffer::Create(Buffer{ indices.data(), indices.size() * sizeof(uint32_t) });

		s_RendererData->BRDFLut = Renderer::GetBRDFLutTexture();
	}

	void Renderer::Shutdown()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		s_GlobalShaderInfo.PermutationCache.SaveToFile(s_ShaderPermutationCachePath);
		BindlessTextureTable::Shutdown();

		{
			std::scoped_lock lock(s_ShaderDependenciesMutex);
			s_ShaderDependencies.clear();
		}

		{
			// Release the cached mip-gen compute pipelines before device teardown
			// (their deferred frees are drained by the release queues below).
			std::scoped_lock lock(s_MipGenPipelineCacheMutex);
			s_MipGenPipelineCache.clear();
		}

		// File-scope texture caches outlive the device unless released here; the CRT would otherwise
		// destroy them at exit and call into an already-destroyed VkDevice (shutdown AV in the driver).
		MaterialAsset::ReleaseSharedResources();

		auto* deviceManager = Application::Get().GetWindow().GetDeviceManager();
		nvrhi::DeviceHandle graphicsDevice = deviceManager ? deviceManager->GetDevice() : nullptr;

		// Wait for device to become idle before cleanup.
		if (graphicsDevice)
		{
			VkDevice device = (VkDevice)graphicsDevice->getNativeObject(nvrhi::ObjectTypes::VK_Device);
			vkDeviceWaitIdle(device);
		}

		// Execute any batched uploads still pending, then release the shared list.
		FlushResourceUploads();
		s_ResourceUploadCommandList = nullptr;


#if LUX_HAS_SHADER_COMPILER
		VulkanShaderCompiler::ClearUniformBuffers();
#endif
		delete s_RendererData;
		// END

		delete s_Data;

		// The render thread has already stopped during application shutdown.
		// Drain any resource-free commands queued by destructors on the main thread
		// before running the per-frame release queues.
		for (uint32_t i = 0; i < s_RenderCommandQueueCount; i++)
		{
			if (s_CommandQueue[i])
				s_CommandQueue[i]->Execute();
		}

		// Everything is idle once the device is, so every pending release can run.
		if (graphicsDevice)
			graphicsDevice->waitForIdle();
		s_ResourceFreeQueue.DrainAll();
		for (nvrhi::EventQueryHandle& event : s_FrameCompletionEvents)
			event = nullptr;

		if (graphicsDevice)
		{
			graphicsDevice->waitForIdle();
			graphicsDevice->runGarbageCollection();
			graphicsDevice->waitForIdle();
			graphicsDevice->runGarbageCollection();
		}

		delete s_CommandQueue[0];
		delete s_CommandQueue[1];
		s_CommandQueue[0] = s_CommandQueue[1] = nullptr;
		s_ShutDown.store(true, std::memory_order_release);
	}

	bool Renderer::IsShutDown()
	{
		return s_ShutDown.load(std::memory_order_acquire);
	}

	Ref<ShaderLibrary> Renderer::GetShaderLibrary()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_Data->m_ShaderLibrary;
	}

	uint32_t Renderer::ReloadShaders(bool forceCompile)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!s_Data || !s_Data->m_ShaderLibrary)
			return 0;

		uint32_t reloadedCount = 0;
		for (auto& [name, shader] : s_Data->m_ShaderLibrary->GetShaders())
		{
			if (!shader)
				continue;

			shader->Reload(forceCompile);
			reloadedCount++;
		}

		return reloadedCount;
	}

	uint32_t Renderer::WarmUpShaderPipelines()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::vector<WeakRef<Pipeline>> graphicsPipelines;
		std::vector<WeakRef<PipelineCompute>> computePipelines;
		{
			std::scoped_lock lock(s_ShaderDependenciesMutex);
			for (auto& [hash, dependencies] : s_ShaderDependencies)
			{
				PruneDeadDependencies(dependencies.Pipelines);
				PruneDeadDependencies(dependencies.ComputePipelines);
				graphicsPipelines.insert(graphicsPipelines.end(), dependencies.Pipelines.begin(), dependencies.Pipelines.end());
				computePipelines.insert(computePipelines.end(), dependencies.ComputePipelines.begin(), dependencies.ComputePipelines.end());
			}
		}

		uint32_t warmedCount = 0;
		for (auto& pipeline : graphicsPipelines)
		{
			if (!pipeline)
				continue;

			pipeline->Invalidate();
			warmedCount++;
		}

		for (auto& computePipeline : computePipelines)
		{
			if (!computePipeline)
				continue;

			computePipeline->CreatePipeline();
			warmedCount++;
		}

		return warmedCount;
	}

	void Renderer::RenderThreadFunc(RenderThread* renderThread)
	{
		LUX_PROFILE_THREAD("Render Thread");

		while (renderThread->IsRunning())
		{
			WaitAndRender(renderThread);
		}
	}

	void Renderer::WaitAndRender(RenderThread* renderThread)
	{
		LUX_PROFILE_FUNC("Renderer::WaitAndRender");
		auto& performanceTimers = Application::Get().m_PerformanceTimers;

		// Wait for kick, then set render thread to busy
		{
			LUX_PROFILE_SCOPE("Wait");
			Timer waitTimer;
			renderThread->WaitAndSet(RenderThread::State::Kick, RenderThread::State::Busy);
			performanceTimers.RenderThreadWaitTime = waitTimer.ElapsedMillis();
		}

		Timer workTimer;
		s_CommandQueue[GetRenderQueueIndex()]->Execute();

		// Rendering has completed, set state to idle
		renderThread->Set(RenderThread::State::Idle);

		performanceTimers.RenderThreadWorkTime = workTimer.ElapsedMillis();
	}

	void Renderer::SwapQueues()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		s_RenderCommandQueueSubmissionIndex = (s_RenderCommandQueueSubmissionIndex + 1) % s_RenderCommandQueueCount;
	}

	void Renderer::SubmitBackgroundThreadWork(std::function<void()>&& func)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::scoped_lock lock(s_BackgroundThreadSubmitMutex);
		s_BackgroundThreadSubmitQueue.push_back(std::move(func));
	}

	void Renderer::ExecuteBackgroundThreadSubmits()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_ASSERT(Application::IsMainThread(), "ExecuteBackgroundThreadSubmits must run on the main thread");

		std::vector<std::function<void()>> pending;
		{
			std::scoped_lock lock(s_BackgroundThreadSubmitMutex);
			if (s_BackgroundThreadSubmitQueue.empty())
				return;
			pending.swap(s_BackgroundThreadSubmitQueue);
		}

		// Now on the main thread: replay through the normal lock-free submission path.
		for (auto& func : pending)
			Submit(std::move(func));
	}

	// ── Batched resource uploads ─────────────────────────────────────────────
	// One shared command list accumulates initial-data uploads from any thread;
	// FlushResourceUploads submits it once. See the declaration in Renderer.h for
	// the ordering guarantee (flush runs before every RenderCommandBuffer submit).

	bool Renderer::UseAsyncTransferQueue()
	{
		if (!s_AsyncTransferEnabled.load(std::memory_order_relaxed))
			return false;
		auto* deviceManager = Application::GetGraphicsDeviceManager();
		return deviceManager && deviceManager->IsTransferQueueAvailable();
	}

	void Renderer::SetAsyncTransferQueueEnabled(bool enabled)
	{
		s_AsyncTransferEnabled.store(enabled, std::memory_order_relaxed);
	}

	bool Renderer::IsAsyncTransferQueueEnabled()
	{
		return s_AsyncTransferEnabled.load(std::memory_order_relaxed);
	}

	void Renderer::SetExplicitBarriersEnabled(bool enabled)
	{
		s_ExplicitBarriersRequested.store(enabled, std::memory_order_relaxed);
	}

	bool Renderer::IsExplicitBarriersEnabled()
	{
		return s_ExplicitBarriersRequested.load(std::memory_order_relaxed);
	}

	bool Renderer::RT_ExplicitBarriersEnabled()
	{
		return s_RTExplicitBarriers.load(std::memory_order_relaxed);
	}

	void Renderer::SetNRIComputeEnabled(bool enabled)
	{
		s_NRIComputeRequested.store(enabled, std::memory_order_relaxed);
	}

	bool Renderer::IsNRIComputeEnabled()
	{
		return s_NRIComputeRequested.load(std::memory_order_relaxed);
	}

	void Renderer::SetNRIGraphicsEnabled(bool enabled)
	{
		s_NRIGraphicsRequested.store(enabled, std::memory_order_relaxed);
	}

	bool Renderer::IsNRIGraphicsEnabled()
	{
		return s_NRIGraphicsRequested.load(std::memory_order_relaxed);
	}

	bool Renderer::RT_NRIGraphicsEnabled()
	{
		return s_RTNRIGraphics.load(std::memory_order_relaxed);
	}

	void Renderer::RecordResourceUpload(const std::function<void(nvrhi::ICommandList*)>& record)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::scoped_lock lock(s_ResourceUploadMutex);

		const bool useCopyQueue = UseAsyncTransferQueue();

		// If the effective queue changed since the cached list was made (a live
		// toggle), retire it so we recreate on the right queue. Only when idle —
		// a list mid-batch keeps its queue until it is flushed.
		if (s_ResourceUploadCommandList && !s_ResourceUploadListOpen &&
			s_ResourceUploadListIsCopyQueue != useCopyQueue)
			s_ResourceUploadCommandList = nullptr;

		if (!s_ResourceUploadCommandList)
		{
			nvrhi::CommandListParameters clParams;
			clParams.setQueueType(useCopyQueue ? nvrhi::CommandQueue::Copy : nvrhi::CommandQueue::Graphics);
			s_ResourceUploadCommandList = Application::GetGraphicsDevice()->createCommandList(clParams);
			s_ResourceUploadListIsCopyQueue = useCopyQueue;
		}

		if (!s_ResourceUploadListOpen)
		{
			s_ResourceUploadCommandList->open();
			s_ResourceUploadListOpen = true;
		}

		record(s_ResourceUploadCommandList);
	}

	void Renderer::FlushResourceUploads()
	{
		std::scoped_lock lock(s_ResourceUploadMutex);
		if (!s_ResourceUploadListOpen)
			return;

		LUX_PROFILE_SCOPE("Renderer::FlushResourceUploads");
		s_ResourceUploadCommandList->close();
		s_ResourceUploadListOpen = false;

		auto device = Application::GetGraphicsDevice();

		// All queue submissions in the engine are serialized by the single queue
		// mutex (nvrhi command-list execution is not internally thread-safe), so we
		// take it here too even though the copy queue is a distinct GPU queue.
		RenderCommandBuffer::LockQueue();
		if (s_ResourceUploadListIsCopyQueue)
		{
			// Submit on the dedicated transfer queue and record its execution
			// instance. Consumers (graphics/compute) wait on it before reading the
			// uploaded resources — see RenderCommandBuffer::RT_Submit.
			const uint64_t instance = device->executeCommandList(s_ResourceUploadCommandList, nvrhi::CommandQueue::Copy);
			s_LastUploadInstance.store(instance, std::memory_order_release);
		}
		else
		{
			// Fallback: inline on the graphics queue. Same-queue submission order
			// guarantees the following render command list sees the uploads, so no
			// cross-queue wait is needed.
			device->executeCommandList(s_ResourceUploadCommandList);
		}
		RenderCommandBuffer::UnlockQueue();
	}

	bool Renderer::ConsumePendingUpload(GPUQueue consumingQueue, uint64_t& outInstance)
	{
		const uint64_t last = s_LastUploadInstance.load(std::memory_order_acquire);
		if (last == 0)
			return false; // no async upload has been submitted (or fallback mode)

		auto& waited = s_QueueWaitedUploadInstance[(size_t)consumingQueue];
		if (waited.load(std::memory_order_relaxed) >= last)
			return false; // this queue already waits for this upload (or a newer one)

		waited.store(last, std::memory_order_relaxed);
		outInstance = last;
		return true;
	}

	uint32_t Renderer::GetRenderQueueIndex()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return (s_RenderCommandQueueSubmissionIndex + 1) % s_RenderCommandQueueCount;
	}

	uint32_t Renderer::GetRenderQueueSubmissionIndex()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_RenderCommandQueueSubmissionIndex;
	}

	void Renderer::BeginRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<RenderPass> renderPass, bool explicitClear)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Renderer::Submit([renderCommandBuffer, renderPass, explicitClear]() mutable
			{
				renderCommandBuffer->RT_BeginMarker(renderPass->GetSpecification().DebugName);

				Ref<Pipeline> pipeline = renderPass->GetSpecification().Pipeline;
				Ref<Framebuffer> framebuffer = pipeline->GetSpecification().TargetFramebuffer;

				const FramebufferSpecification& framebufferSpec = framebuffer->GetSpecification();
				const auto& attachmentSpecs = framebufferSpec.Attachments.Attachments;

				// Per-attachment LoadOp wins over the framebuffer-wide setting, so one attachment
				// can keep its contents (e.g. a shared image another pass already wrote) while
				// the rest clear. Depth is the last attachment, so color index i is attachments[i].
				const uint32_t colorAttachmentCount = static_cast<uint32_t>(framebuffer->GetColorAttachmentCount());
				std::array<bool, nvrhi::c_MaxRenderTargets> clearColor = {};
				for (uint32_t i = 0; i < colorAttachmentCount && i < clearColor.size(); i++)
				{
					const AttachmentLoadOp loadOp = i < attachmentSpecs.size() ? attachmentSpecs[i].LoadOp : AttachmentLoadOp::Inherit;
					clearColor[i] = loadOp == AttachmentLoadOp::Clear
						|| (loadOp == AttachmentLoadOp::Inherit && (explicitClear || framebufferSpec.ClearColorOnLoad));
				}
				const bool clearDepth = framebuffer->HasDepthAttachment() && (explicitClear || framebufferSpec.ClearDepthOnLoad);

				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();
				graphicsState.pipeline = pipeline->GetHandle();
				LUX_CORE_ASSERT(graphicsState.pipeline);
				graphicsState.framebuffer = framebuffer->GetHandle();
				LUX_CORE_ASSERT(graphicsState.framebuffer);

				// The graphics state persists on the command buffer across passes.
				// Clear any vertex/index bindings left by a previous draw before we
				// commit here: pass-begin does not draw, and a stale binding whose
				// buffer handle has since been released would commit VK_NULL_HANDLE
				// (vkCmdBindVertexBuffers: pBuffers[0] is VK_NULL_HANDLE). Each draw
				// sets its own vertex/index buffers immediately before drawing.
				graphicsState.vertexBuffers = {};
				graphicsState.indexBuffer = nvrhi::IndexBufferBinding{};

				// Viewport and scissor
				const uint32_t framebufferWidth = framebuffer->GetWidth();
				const uint32_t framebufferHeight = framebuffer->GetHeight();
				float fbWidth = (float)framebufferWidth;
				float fbHeight = (float)framebufferHeight;
				graphicsState.viewport.viewports = { nvrhi::Viewport(fbWidth, fbHeight) };
				graphicsState.viewport.scissorRects = { nvrhi::Rect(static_cast<int>(framebufferWidth), static_cast<int>(framebufferHeight)) };

				graphicsState.lineWidth = 0.0f;
				if (renderPass->GetPipeline()->IsDynamicLineWidth())
					graphicsState.lineWidth = renderPass->GetPipeline()->GetSpecification().LineWidth;
				graphicsState.shadingRateState = MakeFragmentShadingRateState(FragmentShadingRate::Rate1x1);

				renderPass->Prepare();
				auto bindingSets = renderPass->GetBindingSets(Renderer::RT_GetCurrentFrameIndex());
				graphicsState.bindings = bindingSets;

				// NRI graphics records the pass itself, its clears as load ops. The graphics state
				// above still serves any draw that falls back to NVRHI.
				if (RT_BeginRenderPassWithNRI(*renderCommandBuffer, *renderPass, clearColor, clearDepth))
					return;

				const auto& clearValues = framebuffer->GetClearValues();
				for (uint32_t i = 0; i < colorAttachmentCount && i < clearColor.size(); i++)
				{
					if (!clearColor[i])
						continue;

					nvrhi::Color color = nvrhi::Color(clearValues[i].Color.float32[0], clearValues[i].Color.float32[1],
						clearValues[i].Color.float32[2], clearValues[i].Color.float32[3]);

					renderCommandBuffer->RT_RequireColorAttachmentClear(framebuffer->GetHandle(), i);
					nvrhi::utils::ClearColorAttachment(renderCommandBuffer->GetActive(), framebuffer->GetHandle(), i, color);
				}

				if (clearDepth)
				{
					const auto& depthStencil = clearValues[clearValues.size() - 1].DepthStencil;
					renderCommandBuffer->RT_RequireDepthAttachmentClear(framebuffer->GetHandle());
					nvrhi::utils::ClearDepthStencilAttachment(renderCommandBuffer->GetActive(), framebuffer->GetHandle(), depthStencil.Depth, depthStencil.Stencil);
				}

				renderCommandBuffer->RT_CommitGraphicsState();
			});
	}

	void Renderer::SetFragmentShadingRate(Ref<RenderCommandBuffer> renderCommandBuffer, FragmentShadingRate rate)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(renderCommandBuffer);

		Renderer::Submit([renderCommandBuffer, rate]() mutable
		{
			nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();
			graphicsState.shadingRateState = MakeFragmentShadingRateState(rate);

			if (renderCommandBuffer->RT_InNRIRenderPass())
				renderCommandBuffer->RT_SetNRIShadingRate(graphicsState.shadingRateState);
			else if (graphicsState.pipeline && graphicsState.framebuffer)
				renderCommandBuffer->RT_CommitGraphicsState();
		});
	}

	void Renderer::EndRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Renderer::Submit([renderCommandBuffer]() mutable
			{
				renderCommandBuffer->RT_EndNRIRenderPass();
				renderCommandBuffer->RT_EndMarker();
			});
	}

	void Renderer::SetViewport(Ref<RenderCommandBuffer> renderCommandBuffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Renderer::Submit([renderCommandBuffer, x, y, width, height]() mutable
			{
				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();
				graphicsState.viewport.viewports = { nvrhi::Viewport(
					static_cast<float>(x) ,
					static_cast<float>(x + width),
					static_cast<float>(y),
					static_cast<float>(y + height),
					0.0f, 1.0f
				) };
				if (renderCommandBuffer->RT_InNRIRenderPass())
					renderCommandBuffer->RT_SetNRIViewportState(graphicsState.viewport);
				else
					renderCommandBuffer->RT_CommitGraphicsState();
			});
	}

	void Renderer::SetScissor(Ref<RenderCommandBuffer> renderCommandBuffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Renderer::Submit([renderCommandBuffer, x, y, width, height]() mutable
			{
				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();
				graphicsState.viewport.scissorRects = { nvrhi::Rect(
					static_cast<int>(x),
					static_cast<int>(x + width),
					static_cast<int>(y),
					static_cast<int>(y + height)
				) };
				if (renderCommandBuffer->RT_InNRIRenderPass())
					renderCommandBuffer->RT_SetNRIViewportState(graphicsState.viewport);
				else
					renderCommandBuffer->RT_CommitGraphicsState();
			});
	}

	void Renderer::BeginComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_ASSERT(computePass, "ComputePass cannot be null!");

		Renderer::Submit([renderCommandBuffer, computePass]() mutable
			{
				renderCommandBuffer->RT_BeginMarker(computePass->GetSpecification().DebugName);

				Ref<PipelineCompute> pipeline = computePass->GetPipeline();

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				nvrhi::ComputeState& computeState = renderCommandBuffer->GetComputeState();
				computeState.pipeline = pipeline->GetHandle();
				LUX_CORE_ASSERT(computeState.pipeline);

				computePass->Prepare();

				auto bindingSets = computePass->GetBindingSets(Renderer::RT_GetCurrentFrameIndex());
				computeState.bindings = bindingSets;

				renderCommandBuffer->RT_CommitComputeState();
			});
	}

	void Renderer::EndComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Renderer::Submit([renderCommandBuffer, computePass]() mutable
			{
				renderCommandBuffer->RT_EndMarker();
			});
	}
	void Renderer::DispatchCompute(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass, Ref<Material> material, const glm::uvec3& workGroups, Buffer constants)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Buffer pushConstantBuffer;
		if (constants)
			pushConstantBuffer = Buffer::Copy(constants);

		Renderer::Submit([renderCommandBuffer, computePass, material, workGroups, pushConstantBuffer]() mutable
			{
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				nvrhi::ComputeState& computeState = renderCommandBuffer->GetComputeState();

				// Bind material descriptor set if exists
				if (material)
				{
					Renderer::RT_BindMaterialDescriptorSet(computeState.bindings, computePass->GetShader(), material);
				}

				renderCommandBuffer->RT_CommitComputeState();

				if (s_RTNRICompute && RT_DispatchComputeWithNRI(*renderCommandBuffer, *computePass, material.Raw(), workGroups, pushConstantBuffer))
				{
					pushConstantBuffer.Release();
					return;
				}

				if (pushConstantBuffer)
				{
					commandList->setPushConstants(pushConstantBuffer.Data, pushConstantBuffer.Size);
					pushConstantBuffer.Release();
				}

				commandList->dispatch(workGroups.x, workGroups.y, workGroups.z);
			});
	}

	void Renderer::BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& markerColor)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Renderer::Submit([renderCommandBuffer, s = label]() mutable
			{
				renderCommandBuffer->RT_BeginMarker(s);
				renderCommandBuffer->RT_BeginTimerQuery(s);
			});
	}

	void Renderer::EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Renderer::Submit([renderCommandBuffer]() mutable
			{
				renderCommandBuffer->RT_EndTimerQuery();
				renderCommandBuffer->RT_EndMarker();
			});
	}

	void Renderer::RT_BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& markerColor)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		renderCommandBuffer->RT_BeginMarker(label);
		renderCommandBuffer->RT_BeginTimerQuery(label);
	}

	void Renderer::RT_EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		renderCommandBuffer->RT_EndTimerQuery();
		renderCommandBuffer->RT_EndMarker();
	}

	void Renderer::BeginFrame()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		s_Data->DrawInstanceCount = 0;
		s_Data->DrawCallCount = 0;
	}

	void Renderer::EndFrame()
	{
		LUX_PROFILE_FUNCTION_AUTO;

	}
	/*
	void Renderer::SetSceneEnvironment(Ref<SceneRenderer> sceneRenderer, Ref<Environment> environment, Ref<Image2D> shadow, Ref<Image2D> spotShadow)
	{
		LUX_PROFILE_FUNCTION_AUTO;

	}*/

	namespace
	{
		Ref<TextureCube> CreateEnvironmentIrradianceMap(const Ref<TextureCube>& radianceMap, const char* debugName)
		{
			constexpr uint32_t irradianceMapSize = 32;

			TextureSpecification irradianceSpec;
			irradianceSpec.DebugName = debugName;
			irradianceSpec.Format = ImageFormat::RGBA32F;
			irradianceSpec.Width = irradianceMapSize;
			irradianceSpec.Height = irradianceMapSize;
			irradianceSpec.Storage = true;
			Ref<TextureCube> irradianceMap = TextureCube::Create(irradianceSpec);

			Ref<Shader> irradianceShader = Renderer::GetShaderLibrary()->Get("EnvironmentIrradiance");
			Ref<Material> irradianceMaterial = Material::Create(irradianceShader);
			irradianceMaterial->Set("o_IrradianceMap", irradianceMap);
			irradianceMaterial->Set("u_RadianceMap", radianceMap);

			ComputePassSpecification computePassSpec;
			computePassSpec.Pipeline = PipelineCompute::Create(irradianceShader);
			computePassSpec.DebugName = debugName;
			Ref<ComputePass> computePass = ComputePass::Create(computePassSpec);
			computePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			computePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			computePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());

			Ref<RenderCommandBuffer> commandBuffer = RenderCommandBuffer::Create(1, std::string(debugName) + "-Compute");
			commandBuffer->Begin();
			Renderer::BeginComputePass(commandBuffer, computePass);

			uint32_t samples = Renderer::GetConfig().IrradianceMapComputeSamples;
			Buffer pushConstantBuffer(&samples, sizeof(uint32_t));
			glm::uvec3 workGroups{ irradianceMapSize / 32, irradianceMapSize / 32, 6 };
			Renderer::DispatchCompute(commandBuffer, computePass, irradianceMaterial, workGroups, pushConstantBuffer);

			Renderer::EndComputePass(commandBuffer, computePass);
			commandBuffer->End();
			commandBuffer->Submit();

			irradianceMap->GenerateMips();
			return irradianceMap;
		}
	}
	
	std::pair<Ref<TextureCube>, Ref<TextureCube>> Renderer::CreateEnvironmentMap(const std::string& filepath)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!Renderer::GetConfig().ComputeEnvironmentMaps)
			return { Renderer::GetBlackCubeTexture(), Renderer::GetBlackCubeTexture() };

		// Load the HDR equirectangular texture
		TextureSpecification equirectSpec;
		equirectSpec.DebugName = "EnvEquirect";
		equirectSpec.FlipVertically = false;
		Ref<Texture2D> envEquirect = Texture2D::Create(equirectSpec, filepath);
		if (!envEquirect || !envEquirect->Loaded())
		{
			LUX_CORE_ERROR("Failed to load environment map: {}", filepath);
			return { Renderer::GetBlackCubeTexture(), Renderer::GetBlackCubeTexture() };
		}
		LUX_CORE_ASSERT(envEquirect->GetFormat() == ImageFormat::RGBA32F, "Environment texture is not HDR!");

		return CreateEnvironmentMap(envEquirect);
	}

	std::pair<Ref<TextureCube>, Ref<TextureCube>> Renderer::CreateEnvironmentMap(Ref<Texture2D> envEquirect)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!Renderer::GetConfig().ComputeEnvironmentMaps)
			return { Renderer::GetBlackCubeTexture(), Renderer::GetBlackCubeTexture() };

		if (!envEquirect || !envEquirect->Loaded())
		{
			LUX_CORE_ERROR("Failed to load environment map from packed texture data");
			return { Renderer::GetBlackCubeTexture(), Renderer::GetBlackCubeTexture() };
		}

		if (envEquirect->GetFormat() != ImageFormat::RGBA32F)
		{
			LUX_CORE_ERROR("Environment texture is not HDR!");
			return { Renderer::GetBlackCubeTexture(), Renderer::GetBlackCubeTexture() };
		}

		const uint32_t cubemapSize = Renderer::GetConfig().EnvironmentMapResolution;

		// Create cubemap textures
		TextureSpecification cubemapSpec;
		cubemapSpec.Format = ImageFormat::RGBA16F;
		cubemapSpec.Width = cubemapSize;
		cubemapSpec.Height = cubemapSize;
		cubemapSpec.Storage = true;

		cubemapSpec.DebugName = "EnvUnfiltered";
		Ref<TextureCube> envUnfiltered = TextureCube::Create(cubemapSpec);

		cubemapSpec.DebugName = "EnvFiltered";
		Ref<TextureCube> envFiltered = TextureCube::Create(cubemapSpec);

		// Convert equirectangular to unfiltered cubemap
		{
			Ref<Shader> equirectToCubeShader = Renderer::GetShaderLibrary()->Get("EquirectangularToCubeMap");
			Ref<Material> equirectToCubeMaterial = Material::Create(equirectToCubeShader);
			equirectToCubeMaterial->Set("o_CubeMap", envUnfiltered);
			equirectToCubeMaterial->Set("u_EquirectangularTex", envEquirect);

			ComputePassSpecification computePassSpec;
			computePassSpec.Pipeline = PipelineCompute::Create(equirectToCubeShader);
			computePassSpec.DebugName = "EquirectToCubeMap";
			Ref<ComputePass> computePass = ComputePass::Create(computePassSpec);
			computePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			computePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			computePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());

			Ref<RenderCommandBuffer> commandBuffer = RenderCommandBuffer::Create(1, "EquirectToCubeMap-Compute");
			commandBuffer->Begin();
			BeginComputePass(commandBuffer, computePass);

			glm::uvec3 workGroups{ cubemapSize / 32, cubemapSize / 32, 6 };
			DispatchCompute(commandBuffer, computePass, equirectToCubeMaterial, workGroups, Buffer());

			EndComputePass(commandBuffer, computePass);
			commandBuffer->End();
			commandBuffer->Submit();

			envUnfiltered->GenerateMips();
		}

		// Copy environment map as-is to filtered mip level 0.  This level is used for perfectly reflective materials
		{
			Ref<Shader> equirectToCubeShader = Renderer::GetShaderLibrary()->Get("EquirectangularToCubeMap");
			Ref<Material> equirectToCubeMaterial = Material::Create(equirectToCubeShader);
			equirectToCubeMaterial->Set("o_CubeMap", envFiltered);
			equirectToCubeMaterial->Set("u_EquirectangularTex", envEquirect);

			ComputePassSpecification computePassSpec;
			computePassSpec.Pipeline = PipelineCompute::Create(equirectToCubeShader);
			computePassSpec.DebugName = "EquirectToCubeMapFiltered";
			Ref<ComputePass> computePass = ComputePass::Create(computePassSpec);
			computePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			computePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			computePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());

			Ref<RenderCommandBuffer> commandBuffer = RenderCommandBuffer::Create(1, "EquirectToCubeMapFiltered-Compute");
			commandBuffer->Begin();
			BeginComputePass(commandBuffer, computePass);

			glm::uvec3 workGroups{ cubemapSize / 32, cubemapSize / 32, 6 };
			DispatchCompute(commandBuffer, computePass, equirectToCubeMaterial, workGroups, Buffer());

			EndComputePass(commandBuffer, computePass);
			commandBuffer->End();
			commandBuffer->Submit();
		}

		// Step 2b: Environment Mip Filtering - pre-filter each mip level with increasing roughness for PBR
		// This is crucial for physically-based rendering as it allows materials with different roughness values
		// to sample the appropriate pre-filtered mip level for accurate specular reflections.
		{
			Ref<Shader> mipFilterShader = Renderer::GetShaderLibrary()->Get("EnvironmentMipFilter");

			ComputePassSpecification computePassSpec;
			computePassSpec.Pipeline = PipelineCompute::Create(mipFilterShader);
			computePassSpec.DebugName = "EnvironmentMipFilter";
			Ref<ComputePass> computePass = ComputePass::Create(computePassSpec);
			computePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			computePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			computePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());

			Ref<RenderCommandBuffer> commandBuffer = RenderCommandBuffer::Create(1, "EnvironmentMipFilter-Compute");
			commandBuffer->Begin();
			BeginComputePass(commandBuffer, computePass);

			const uint32_t mipCount = envFiltered->GetMipLevelCount();
			const float deltaRoughness = 1.0f / glm::max((float)mipCount - 1.0f, 1.0f);

			// Note: mip level 0 is the unfiltered copy from the previous step
			// Start from mip 1 and apply increasing roughness
			for (uint32_t mip = 1; mip < mipCount; mip++)
			{
				uint32_t mipSize = cubemapSize >> mip;
				float roughness = mip * deltaRoughness;

				// Create image views for the specific mip level output
				ImageViewSpecification outputViewSpec;
				outputViewSpec.Image = envFiltered->GetImage();
				outputViewSpec.Mip = mip;
				outputViewSpec.MipCount = 1;
				outputViewSpec.Layer = 0;
				outputViewSpec.LayerCount = 6; // All cubemap faces
				outputViewSpec.Dimension = TextureDimension::TextureCube;
				outputViewSpec.DebugName = std::format("EnvMipFilter-Output-Mip{}", mip);
				Ref<ImageView> outputView = ImageView::Create(outputViewSpec);

				Ref<Material> mipFilterMaterial = Material::Create(mipFilterShader);
				mipFilterMaterial->Set("outputTexture", outputView);
				mipFilterMaterial->Set("inputTexture", envUnfiltered);

				uint32_t numGroups = glm::max(1u, mipSize / 32);
				glm::uvec3 workGroups{ numGroups, numGroups, 6 };
				DispatchCompute(commandBuffer, computePass, mipFilterMaterial, workGroups, Buffer(&roughness, sizeof(float)));

				// Commit barriers between mip levels to ensure writes complete before next iteration
				Renderer::Submit([commandBuffer]() mutable
					{
						nvrhi::CommandListHandle commandList = commandBuffer->GetActive();
						commandList->commitBarriers();
					});
			}

			EndComputePass(commandBuffer, computePass);
			commandBuffer->End();
			commandBuffer->Submit();
		}

		Ref<TextureCube> irradianceMap = CreateEnvironmentIrradianceMap(envFiltered, "EnvironmentIrradiance");
		if (!irradianceMap)
			return { Renderer::GetBlackCubeTexture(), Renderer::GetBlackCubeTexture() };

		return { envFiltered, irradianceMap };
	}

	Ref<TextureCube> Renderer::CreatePreethamSky(float turbidity, float azimuth, float inclination)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const uint32_t cubemapSize = Renderer::GetConfig().EnvironmentMapResolution;

		TextureSpecification cubemapSpec;
		cubemapSpec.DebugName = "PreethamSky";
		cubemapSpec.Format = ImageFormat::RGBA32F;
		cubemapSpec.Width = cubemapSize;
		cubemapSpec.Height = cubemapSize;
		cubemapSpec.Storage = true;

		Ref<TextureCube> environmentMap = TextureCube::Create(cubemapSpec);

		Ref<Shader> preethamSkyShader = Renderer::GetShaderLibrary()->Get("PreethamSky");

		Ref<Material> preethamSkyMaterial = Material::Create(preethamSkyShader);
		preethamSkyMaterial->Set("o_CubeMap", environmentMap);

		ComputePassSpecification computePassSpec;
		computePassSpec.Pipeline = PipelineCompute::Create(preethamSkyShader);
		computePassSpec.DebugName = "PreethamSky";
		Ref<ComputePass> computePass = ComputePass::Create(computePassSpec);

		Ref<RenderCommandBuffer> commandBuffer = RenderCommandBuffer::Create(1, "PreethamSky-Compute");

		commandBuffer->Begin();

		BeginComputePass(commandBuffer, computePass);

		glm::vec3 params = { turbidity, azimuth, inclination };
		Buffer pushConstantBuffer(&params, sizeof(glm::vec3));
		glm::uvec3 workGroups{ cubemapSize / 32, cubemapSize / 32, 6 };
		DispatchCompute(commandBuffer, computePass, preethamSkyMaterial, workGroups, pushConstantBuffer);

		EndComputePass(commandBuffer, computePass);

		commandBuffer->End();
		commandBuffer->Submit();

		environmentMap->GenerateMips();

		return environmentMap;
	}

	Ref<Environment> Renderer::CreatePreethamSkyEnvironment(float turbidity, float azimuth, float inclination)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!Renderer::GetConfig().ComputeEnvironmentMaps)
			return GetEmptyEnvironment();

		Ref<TextureCube> radianceMap = CreatePreethamSky(turbidity, azimuth, inclination);
		if (!radianceMap)
			return GetEmptyEnvironment();

		Ref<TextureCube> irradianceMap = CreateEnvironmentIrradianceMap(radianceMap, "PreethamSkyIrradiance");
		if (!irradianceMap)
			return GetEmptyEnvironment();

		return Ref<Environment>::Create(radianceMap, irradianceMap);
	}

	void Renderer::RenderQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, const glm::mat4& transform)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(renderCommandBuffer);
		LUX_CORE_VERIFY(pipeline);

		Renderer::Submit([renderCommandBuffer, pipeline, material, transform]() mutable
			{
				LUX_PROFILE_FUNC("VulkanRenderer::RenderQuad");

				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();

				{
					nvrhi::VertexBufferBinding vertexBufferBinding;
					vertexBufferBinding.buffer = s_RendererData->QuadVertexBuffer->GetHandle();
					vertexBufferBinding.slot = 0;
					vertexBufferBinding.offset = 0;

					graphicsState.vertexBuffers = { vertexBufferBinding };
				}

				{
					nvrhi::IndexBufferBinding indexBufferBinding;
					indexBufferBinding.buffer = s_RendererData->QuadIndexBuffer->GetHandle();
					indexBufferBinding.format = nvrhi::Format::R32_UINT;
					indexBufferBinding.offset = 0;

					graphicsState.indexBuffer = indexBufferBinding;
				}

				Renderer::RT_BindMaterialDescriptorSet(graphicsState.bindings, pipeline->GetShader(), material);

				Buffer uniformStorageBuffer = material->GetUniformStorageBuffer();

				NRIIndexedDraw draw;
				draw.MaterialInstance = material.Raw();
				draw.Vertices = s_RendererData->QuadVertexBuffer.Raw();
				draw.Indices = s_RendererData->QuadIndexBuffer.Raw();
				draw.Constants = uniformStorageBuffer.Data;
				draw.ConstantsSize = static_cast<uint32_t>(uniformStorageBuffer.Size);
				draw.IndexCount = s_RendererData->QuadIndexBuffer->GetCount();
				if (RT_DrawIndexedWithNRI(*renderCommandBuffer, draw))
					return;

				renderCommandBuffer->RT_CommitGraphicsState();
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				if (uniformStorageBuffer)
					commandList->setPushConstants(uniformStorageBuffer.Data, uniformStorageBuffer.Size);

				nvrhi::DrawArguments drawArgs{};
				drawArgs.vertexCount = s_RendererData->QuadIndexBuffer->GetCount();
				drawArgs.startIndexLocation = 0;
				drawArgs.startVertexLocation = 0;
				drawArgs.instanceCount = 1;
				commandList->drawIndexed(drawArgs);
			});
	}

	void Renderer::RenderGeometry(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, Ref<VertexBuffer> vertexBuffer, Ref<IndexBuffer> indexBuffer, const glm::mat4& transform, uint32_t indexCount /*= 0*/)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Renderer::Submit([renderCommandBuffer, pipeline, material, vertexBuffer, indexBuffer, transform, indexCount]() mutable
			{
				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();

				nvrhi::VertexBufferBinding vertexBufferBinding;
				vertexBufferBinding.buffer = vertexBuffer->GetHandle();
				vertexBufferBinding.slot = 0;
				vertexBufferBinding.offset = 0;
				graphicsState.vertexBuffers = { vertexBufferBinding };

				nvrhi::IndexBufferBinding indexBufferBinding;
				indexBufferBinding.buffer = indexBuffer->GetHandle();
				indexBufferBinding.format = nvrhi::Format::R32_UINT;
				indexBufferBinding.offset = 0;
				graphicsState.indexBuffer = indexBufferBinding;

				Renderer::RT_BindMaterialDescriptorSet(graphicsState.bindings, pipeline->GetShader(), material);

				NRIIndexedDraw draw;
				draw.MaterialInstance = material.Raw();
				draw.Vertices = vertexBuffer.Raw();
				draw.Indices = indexBuffer.Raw();
				draw.Constants = &transform;
				draw.ConstantsSize = sizeof(glm::mat4);
				draw.IndexCount = indexCount;
				if (RT_DrawIndexedWithNRI(*renderCommandBuffer, draw))
					return;

				renderCommandBuffer->RT_CommitGraphicsState();
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				commandList->setPushConstants(&transform, sizeof(glm::mat4));

				nvrhi::DrawArguments drawArgs;
				drawArgs.vertexCount = indexCount;
				drawArgs.startIndexLocation = 0;
				drawArgs.startVertexLocation = 0;
				commandList->drawIndexed(drawArgs);
			});
	}

	void Renderer::RT_BindMaterialDescriptorSet(nvrhi::BindingSetVector& bindings, Ref<Shader> pipelineShader, Ref<Material> material, uint32_t set)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!material)
		{
			if (bindings.size() > set)
				bindings[set] = nullptr;
			return;
		}

		material->Prepare();

		if (!material->IsDescriptorSetCompatible(pipelineShader, set))
		{
			if (bindings.size() > set)
				bindings[set] = nullptr;
			return;
		}

		nvrhi::BindingSetHandle bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
		if (!bindingSet)
		{
			if (bindings.size() > set)
				bindings[set] = nullptr;
			return;
		}

		if (bindings.size() <= set)
			bindings.resize(set + 1);

		bindings[set] = bindingSet;
	}

	bool Renderer::RT_BeginRenderPassWithNRI(RenderCommandBuffer& renderCommandBuffer, RenderPass& renderPass, const std::array<bool, nvrhi::c_MaxRenderTargets>& clearColor, bool clearDepth)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!s_RTNRIGraphics.load(std::memory_order_relaxed) || !renderCommandBuffer.RT_UsesExplicitBarriers())
			return false;

		Ref<Pipeline> pipeline = renderPass.GetPipeline();
		Ref<VulkanShader> shader = pipeline->GetShader().As<VulkanShader>();
		Ref<Framebuffer> framebuffer = pipeline->GetSpecification().TargetFramebuffer;
		const std::string& passName = renderPass.GetSpecification().DebugName;

		const char* problem = !pipeline->GetNRIPipeline() ? "its pipeline has no NRI twin"
			: !shader->GetNRIPipelineLayout() ? "its shader has no NRI layout"
			: !framebuffer->HasNRIAttachments() ? "its framebuffer has no NRI attachments"
			: framebuffer->HasStaleAttachments() ? "its framebuffer has stale attachments"
			: nullptr;

		// The pass's own sets, where NVRHI binds them. A set the pass leaves to its materials is null
		// here and bound by the draws.
		RenderCommandBuffer::NRIRenderPassDesc desc;
		const uint32_t frameIndex = RT_GetCurrentFrameIndex();
		const nvrhi::BindingSetVector bindings = renderPass.GetBindingSets(frameIndex);
		for (uint32_t set = 0; !problem && set < bindings.size(); set++)
		{
			const uint32_t setIndex = shader->GetNRISetIndex(set);
			if (!bindings[set] || setIndex == VulkanShader::k_NoNRISet)
				continue;

			desc.DescriptorSets[setIndex] = renderPass.GetNRIDescriptorSet(frameIndex, set);
			if (!desc.DescriptorSets[setIndex])
				problem = "one of its descriptor sets has no NRI twin";
		}

		if (problem)
		{
			ReportNRIGraphicsFallback(pipeline.Raw(), passName, problem);
			return false;
		}

		// The attachments and clears of NVRHI's framebuffer and BeginRenderPass.
		const std::vector<ClearValue>& clearValues = framebuffer->GetClearValues();
		desc.ColorAttachmentCount = static_cast<uint32_t>(framebuffer->GetColorAttachmentCount());
		for (uint32_t i = 0; i < desc.ColorAttachmentCount; i++)
		{
			const ClearColorValue& clear = clearValues[i].Color;
			desc.ColorAttachments[i] = framebuffer->GetNRIColorAttachment(i);
			desc.ClearColor[i] = clearColor[i];
			desc.ClearColorValues[i] = { clear.float32[0], clear.float32[1], clear.float32[2], clear.float32[3] };
		}
		if (framebuffer->HasDepthAttachment())
		{
			desc.DepthAttachment = framebuffer->GetNRIDepthAttachment();
			desc.ClearDepth = clearDepth;
			desc.ClearDepthValue = clearValues.back().DepthStencil.Depth;
		}

		desc.PipelineLayout = shader->GetNRIPipelineLayout();
		desc.Pipeline = pipeline->GetNRIPipeline();
		desc.LineWidth = pipeline->IsDynamicLineWidth() ? pipeline->GetSpecification().LineWidth : 0.0f;
		desc.DynamicShadingRate = pipeline->HasNRIDynamicShadingRate();
		desc.DrawSetIndex = shader->GetNRISetIndex(0);
		desc.VertexStride = pipeline->GetNRIVertexStride(0);
		desc.RootConstants = shader->HasNRIRootConstants();

		// The pass-level requirements (attachments, the pass's sets), made through the graphics state
		// as for NVRHI so the draws' change detection continues from them.
		nvrhi::GraphicsState& graphicsState = renderCommandBuffer.GetGraphicsState();
		graphicsState.framebuffer = framebuffer->GetHandle();
		graphicsState.bindings = bindings;
		graphicsState.vertexBuffers = {};
		graphicsState.indexBuffer = nvrhi::IndexBufferBinding{};
		graphicsState.indirectParams = nullptr;
		renderCommandBuffer.RT_RequireGraphicsState();

		// The full-framebuffer viewport and the 1x1 shading rate BeginRenderPass gives NVRHI.
		const uint32_t width = framebuffer->GetWidth();
		const uint32_t height = framebuffer->GetHeight();
		nvrhi::ViewportState viewport;
		viewport.viewports = { nvrhi::Viewport(static_cast<float>(width), static_cast<float>(height)) };
		viewport.scissorRects = { nvrhi::Rect(static_cast<int>(width), static_cast<int>(height)) };
		return renderCommandBuffer.RT_BeginNRIRenderPass(desc, passName, viewport, MakeFragmentShadingRateState(FragmentShadingRate::Rate1x1));
	}

	bool Renderer::RT_DrawIndexedWithNRI(RenderCommandBuffer& renderCommandBuffer, const NRIIndexedDraw& draw)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!renderCommandBuffer.RT_InNRIRenderPass())
			return false;

		const RenderCommandBuffer::NRIRenderPassDesc& pass = renderCommandBuffer.RT_GetNRIRenderPass();
		const nvrhi::GraphicsState& graphicsState = renderCommandBuffer.GetGraphicsState();
		const uint32_t frameIndex = RT_GetCurrentFrameIndex();

		// Set 0 belongs to the draw: its material's set, or else the pass's own (which NVRHI also keeps
		// bound when the draw leaves a hole there).
		const char* problem = nullptr;
		nri::DescriptorSet* drawSet = nullptr;
		if (pass.DrawSetIndex != VulkanShader::k_NoNRISet)
		{
			nvrhi::IBindingSet* binding = graphicsState.bindings.empty() ? nullptr : graphicsState.bindings[0];
			if (binding && draw.MaterialInstance && binding == draw.MaterialInstance->GetBindingSet(frameIndex).Get())
				drawSet = draw.MaterialInstance->GetNRIDescriptorSet(frameIndex);
			else
				drawSet = pass.DescriptorSets[pass.DrawSetIndex];

			if (!drawSet)
				problem = binding ? "a draw's set 0 has no NRI twin" : "a draw leaves set 0 unbound";
		}

		nri::Buffer* vertexBuffer = draw.Vertices ? draw.Vertices->GetRHIBuffer() : nullptr;
		nri::Buffer* indexBuffer = draw.Indices ? draw.Indices->GetRHIBuffer() : nullptr;
		nri::Buffer* indirectBuffer = draw.IndirectArguments ? draw.IndirectArguments->GetRHIBuffer() : nullptr;
		if (!problem && (!vertexBuffer || !indexBuffer || (draw.IndirectArguments && !indirectBuffer)))
			problem = "a draw's buffer has no NRI buffer";

		if (problem)
		{
			ReportNRIGraphicsFallback(pass.Pipeline, renderCommandBuffer.RT_GetNRIRenderPassName(), problem);
			return false;
		}

		renderCommandBuffer.RT_RequireGraphicsState();
		nri::CommandBuffer* commandBuffer = renderCommandBuffer.RT_BeginNRIDraw();
		if (!commandBuffer)
			return false;

		if (drawSet)
			renderCommandBuffer.RT_SetNRIDescriptorSet(pass.DrawSetIndex, drawSet);
		renderCommandBuffer.RT_SetNRIVertexBuffer(vertexBuffer, pass.VertexStride);
		renderCommandBuffer.RT_SetNRIIndexBuffer(indexBuffer);
		if (draw.Constants && draw.ConstantsSize && pass.RootConstants)
			renderCommandBuffer.RT_SetNRIRootConstants(draw.Constants, draw.ConstantsSize);

		const NRIInterface& api = RHIDevice::API();
		if (indirectBuffer)
		{
			api.CmdDrawIndexedIndirect(*commandBuffer, *indirectBuffer, draw.IndirectOffset, 1, sizeof(DrawIndexedIndirectCommand), nullptr, 0);
			return true;
		}

		nri::DrawIndexedDesc drawDesc = {};
		drawDesc.indexNum = draw.IndexCount;
		drawDesc.instanceNum = draw.InstanceCount;
		drawDesc.baseIndex = draw.FirstIndex;
		drawDesc.baseVertex = draw.VertexOffset;
		api.CmdDrawIndexed(*commandBuffer, drawDesc);
		return true;
	}

	bool Renderer::RT_DrawMeshTasksWithNRI(RenderCommandBuffer& renderCommandBuffer, const nvrhi::MeshletState& meshletState, nri::DescriptorSet* meshletSet, const void* constants, uint32_t constantsSize, const glm::uvec3& groups)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!renderCommandBuffer.RT_InNRIRenderPass())
			return false;

		const RenderCommandBuffer::NRIRenderPassDesc& pass = renderCommandBuffer.RT_GetNRIRenderPass();
		const NRIInterface& api = RHIDevice::API();
		const char* problem = !api.CmdDrawMeshTasks ? "NRI has no mesh shader interface"
			: pass.DrawSetIndex == VulkanShader::k_NoNRISet ? "its mesh shader has no NRI set 0"
			: !meshletSet ? "a mesh has no NRI meshlet set"
			: nullptr;
		if (problem)
		{
			ReportNRIGraphicsFallback(pass.Pipeline, renderCommandBuffer.RT_GetNRIRenderPassName(), problem);
			return false;
		}

		renderCommandBuffer.RT_RequireMeshletState(meshletState);
		nri::CommandBuffer* commandBuffer = renderCommandBuffer.RT_BeginNRIDraw();
		if (!commandBuffer)
			return false;

		renderCommandBuffer.RT_SetNRIDescriptorSet(pass.DrawSetIndex, meshletSet);
		if (constants && constantsSize && pass.RootConstants)
			renderCommandBuffer.RT_SetNRIRootConstants(constants, constantsSize);
		api.CmdDrawMeshTasks(*commandBuffer, { groups.x, groups.y, groups.z });
		return true;
	}

	void Renderer::ClearImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, const glm::vec4& clearColor, TextureSubresourceRange subresources)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Renderer::Submit([renderCommandBuffer, image, clearColor, subresources]() mutable
			{
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
				if (!commandList || !image || !image->GetHandle())
					return;
				const auto& spec = image->GetSpecification();
				const std::string markerName = "ClearImage: " + (spec.DebugName.empty() ? std::string("Image2D") : spec.DebugName);
				renderCommandBuffer->RT_BeginMarker(markerName);
				renderCommandBuffer->RT_RequireTextureState(image->GetHandle(), subresources, ResourceState::CopyDest);
				commandList->clearTextureFloat(image->GetHandle(), ToNVRHI(subresources), nvrhi::Color(clearColor.r, clearColor.g, clearColor.b, clearColor.a));
				renderCommandBuffer->RT_EndMarker();
			});
	}

	void Renderer::CopyImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> sourceImage, Ref<Image2D> destinationImage)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(renderCommandBuffer);
		LUX_CORE_VERIFY(sourceImage);
		LUX_CORE_VERIFY(destinationImage);

		Renderer::Submit([renderCommandBuffer, sourceImage, destinationImage]() mutable
		{
			nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
			if (!commandList || !sourceImage->GetHandle() || !destinationImage->GetHandle())
				return;

			const uint32_t copyWidth = std::min(sourceImage->GetWidth(), destinationImage->GetWidth());
			const uint32_t copyHeight = std::min(sourceImage->GetHeight(), destinationImage->GetHeight());
			if (copyWidth == 0 || copyHeight == 0)
				return;

			const uint32_t sourceLayerCount = glm::max(1u, sourceImage->GetSpecification().Layers);
			const uint32_t destinationLayerCount = glm::max(1u, destinationImage->GetSpecification().Layers);
			const uint32_t copyLayerCount = glm::min(sourceLayerCount, destinationLayerCount);

			for (uint32_t layer = 0; layer < copyLayerCount; layer++)
			{
				nvrhi::TextureSlice srcSlice;
				srcSlice.setSize(copyWidth, copyHeight, 1);
				srcSlice.setArraySlice(layer);

				nvrhi::TextureSlice dstSlice;
				dstSlice.setSize(copyWidth, copyHeight, 1);
				dstSlice.setArraySlice(layer);

				const TextureSubresourceRange layerRange{ 0, 1, layer, 1 };
				renderCommandBuffer->RT_RequireTextureState(sourceImage->GetHandle(), layerRange, ResourceState::CopySource);
				renderCommandBuffer->RT_RequireTextureState(destinationImage->GetHandle(), layerRange, ResourceState::CopyDest);
				commandList->copyTexture(destinationImage->GetHandle(), dstSlice, sourceImage->GetHandle(), srcSlice);
			}
		});
	}

	void Renderer::BlitImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> sourceImage, Ref<Image2D> destinationImage)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		//s_RendererAPI->BlitImage(renderCommandBuffer, sourceImage, destinationImage);
	}

	void Renderer::SubmitFullscreenQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(renderCommandBuffer);
		LUX_CORE_VERIFY(pipeline);

		Renderer::Submit([renderCommandBuffer, pipeline, material]() mutable
			{
				LUX_PROFILE_FUNC("VulkanRenderer::SubmitFullscreenQuad");

				if (material == nullptr)
				{
					return;
				}

				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();

				{
					nvrhi::VertexBufferBinding vertexBufferBinding;
					vertexBufferBinding.buffer = s_RendererData->QuadVertexBuffer->GetHandle();
					vertexBufferBinding.slot = 0;
					vertexBufferBinding.offset = 0;

					graphicsState.vertexBuffers = { vertexBufferBinding };
				}

				{
					nvrhi::IndexBufferBinding indexBufferBinding;
					indexBufferBinding.buffer = s_RendererData->QuadIndexBuffer->GetHandle();
					indexBufferBinding.format = nvrhi::Format::R32_UINT;
					indexBufferBinding.offset = 0;

					graphicsState.indexBuffer = indexBufferBinding;
				}

				Renderer::RT_BindMaterialDescriptorSet(graphicsState.bindings, pipeline->GetShader(), material);

				Buffer uniformStorageBuffer = material->GetUniformStorageBuffer();

				NRIIndexedDraw draw;
				draw.MaterialInstance = material.Raw();
				draw.Vertices = s_RendererData->QuadVertexBuffer.Raw();
				draw.Indices = s_RendererData->QuadIndexBuffer.Raw();
				draw.Constants = uniformStorageBuffer.Data;
				draw.ConstantsSize = static_cast<uint32_t>(uniformStorageBuffer.Size);
				draw.IndexCount = s_RendererData->QuadIndexBuffer->GetCount();
				if (RT_DrawIndexedWithNRI(*renderCommandBuffer, draw))
					return;

				renderCommandBuffer->RT_CommitGraphicsState();
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				if (uniformStorageBuffer)
					commandList->setPushConstants(uniformStorageBuffer.Data, uniformStorageBuffer.Size);

				nvrhi::DrawArguments drawArgs{};
				drawArgs.vertexCount = s_RendererData->QuadIndexBuffer->GetCount();
				drawArgs.startIndexLocation = 0;
				drawArgs.startVertexLocation = 0;
				drawArgs.instanceCount = 1;
				commandList->drawIndexed(drawArgs);
			});
	}

	void Renderer::SubmitFullscreenQuadWithOverrides(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, Buffer vertexShaderOverrides, Buffer fragmentShaderOverrides)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(renderCommandBuffer);
		LUX_CORE_VERIFY(pipeline);

		Buffer vertexPushConstantBuffer;
		if (vertexShaderOverrides)
			vertexPushConstantBuffer = Buffer::Copy(vertexShaderOverrides);

		Buffer fragmentPushConstantBuffer;
		if (fragmentShaderOverrides)
			fragmentPushConstantBuffer = Buffer::Copy(fragmentShaderOverrides);

		Renderer::Submit([renderCommandBuffer, pipeline, material, vertexPushConstantBuffer, fragmentPushConstantBuffer]() mutable
			{
				LUX_PROFILE_FUNC("VulkanRenderer::SubmitFullscreenQuadWithOverrides");

				if (material == nullptr)
				{
					vertexPushConstantBuffer.Release();
					fragmentPushConstantBuffer.Release();
					return;
				}

				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();

				{
					nvrhi::VertexBufferBinding vertexBufferBinding;
					vertexBufferBinding.buffer = s_RendererData->QuadVertexBuffer->GetHandle();
					vertexBufferBinding.slot = 0;
					vertexBufferBinding.offset = 0;

					graphicsState.vertexBuffers = { vertexBufferBinding };
				}

				{
					nvrhi::IndexBufferBinding indexBufferBinding;
					indexBufferBinding.buffer = s_RendererData->QuadIndexBuffer->GetHandle();
					indexBufferBinding.format = nvrhi::Format::R32_UINT;
					indexBufferBinding.offset = 0;

					graphicsState.indexBuffer = indexBufferBinding;
				}

				Renderer::RT_BindMaterialDescriptorSet(graphicsState.bindings, pipeline->GetShader(), material);

				// Build combined push constants: vertex overrides first, then fragment overrides
				Buffer combinedPushConstants;
				uint64_t offset = 0;

				if (vertexPushConstantBuffer || fragmentPushConstantBuffer)
				{
					uint64_t totalSize = vertexPushConstantBuffer.Size + fragmentPushConstantBuffer.Size;
					combinedPushConstants.Allocate(totalSize);

					if (vertexPushConstantBuffer)
					{
						combinedPushConstants.Write(vertexPushConstantBuffer.Data, vertexPushConstantBuffer.Size, offset);
						offset += vertexPushConstantBuffer.Size;
					}

					if (fragmentPushConstantBuffer)
					{
						combinedPushConstants.Write(fragmentPushConstantBuffer.Data, fragmentPushConstantBuffer.Size, offset);
						offset += fragmentPushConstantBuffer.Size;
					}
				}

				NRIIndexedDraw draw;
				draw.MaterialInstance = material.Raw();
				draw.Vertices = s_RendererData->QuadVertexBuffer.Raw();
				draw.Indices = s_RendererData->QuadIndexBuffer.Raw();
				draw.Constants = combinedPushConstants.Data;
				draw.ConstantsSize = static_cast<uint32_t>(offset);
				draw.IndexCount = s_RendererData->QuadIndexBuffer->GetCount();
				if (RT_DrawIndexedWithNRI(*renderCommandBuffer, draw))
				{
					combinedPushConstants.Release();
					vertexPushConstantBuffer.Release();
					fragmentPushConstantBuffer.Release();
					return;
				}

				renderCommandBuffer->RT_CommitGraphicsState();
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				if (combinedPushConstants)
				{
					commandList->setPushConstants(combinedPushConstants.Data, offset);
					combinedPushConstants.Release();
				}

				nvrhi::DrawArguments drawArgs{};
				drawArgs.vertexCount = s_RendererData->QuadIndexBuffer->GetCount();
				drawArgs.startIndexLocation = 0;
				drawArgs.startVertexLocation = 0;
				drawArgs.instanceCount = 1;
				commandList->drawIndexed(drawArgs);

				vertexPushConstantBuffer.Release();
				fragmentPushConstantBuffer.Release();
	});
	}

	Ref<Texture2D> Renderer::GetWhiteTexture()
	{
		return s_Data->WhiteTexture;
	}

	Ref<Texture2D> Renderer::GetBlackTexture()
	{
		return s_Data->BlackTexture;
	}

	Ref<Texture2D> Renderer::GetHilbertLut()
	{
		return s_Data->HilbertLut;
	}

	Ref<Texture2D> Renderer::GetBRDFLutTexture()
	{
		return s_Data->BRDFLutTexture;
	}
	
	Ref<TextureCube> Renderer::GetBlackCubeTexture()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_Data->BlackCubeTexture;
	}

	Ref<Material> Renderer::GetDefaultWhiteMaterial()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_Data->DefaultWhiteMaterial;
	}

	
	Ref<Environment> Renderer::GetEmptyEnvironment()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_Data->EmptyEnvironment;
	}

	Ref<Environment> Renderer::GetDefaultEnvironment()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!s_Data->DefaultEnvironment)
		{
			constexpr float defaultTurbidity = 3.0f;
			constexpr float defaultAzimuth = 0.25f;
			constexpr float defaultInclination = 1.0f;

			s_Data->DefaultEnvironment = CreatePreethamSkyEnvironment(defaultTurbidity, defaultAzimuth, defaultInclination);
			if (!s_Data->DefaultEnvironment)
				s_Data->DefaultEnvironment = GetEmptyEnvironment();
		}

		return s_Data->DefaultEnvironment ? s_Data->DefaultEnvironment : GetEmptyEnvironment();
	}

	RenderCommandQueue& Renderer::GetRenderCommandQueue()
	{
		return *s_CommandQueue[s_RenderCommandQueueSubmissionIndex];
	}

	uint32_t Renderer::RT_GetCurrentFrameIndex()
	{
		return static_cast<uint32_t>(s_RTFrameNumber % s_Config.FramesInFlight);
	}

	uint64_t Renderer::RT_GetFrameNumber()
	{
		return s_RTFrameNumber;
	}

	void Renderer::RT_EnqueueResourceFree(std::function<void()> release, size_t captureBytes)
	{
		s_ResourceFreeQueue.Enqueue(s_RTFrameNumber, std::move(release), captureBytes);
	}

	void Renderer::RT_BeginFrame()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		nvrhi::IDevice* device = Application::GetGraphicsDevice();
		const uint32_t slotCount = s_Config.FramesInFlight;

		s_RTExplicitBarriers.store(s_ExplicitBarriersRequested.load(std::memory_order_relaxed), std::memory_order_relaxed);
		s_RTNRICompute = s_NRIComputeRequested.load(std::memory_order_relaxed);
		s_RTNRIGraphics.store(s_NRIGraphicsRequested.load(std::memory_order_relaxed), std::memory_order_relaxed);

		// Close the ending frame: its event covers everything submitted while it was current. The
		// lock keeps the event in order with other threads' submissions, as in Present().
		nvrhi::EventQueryHandle& closing = s_FrameCompletionEvents[s_RTFrameNumber % slotCount];
		if (!closing)
			closing = device->createEventQuery();
		RenderCommandBuffer::LockQueue();
		device->resetEventQuery(closing);
		device->setEventQuery(closing, nvrhi::CommandQueue::Graphics);
		RenderCommandBuffer::UnlockQueue();

		s_RTFrameNumber++;
		if (s_RTFrameNumber < slotCount)
			return;

		// This frame reuses the slot of frame N - FramesInFlight. Present() keeps at most two frames
		// on the GPU, so the wait normally returns at once; it only blocks after frames that skipped
		// Present()'s pacing.
		const uint64_t retiringFrame = s_RTFrameNumber - slotCount;
		device->waitEventQuery(s_FrameCompletionEvents[retiringFrame % slotCount]);
		s_ResourceFreeQueue.RT_Retire(retiringFrame);

#ifdef LUX_DEBUG
		// Pending releases should fall back to ~0 a few frames after activity stops.
		static Timer s_PendingLogTimer;
		static size_t s_LastLoggedPending = 0;
		const size_t pending = s_ResourceFreeQueue.GetPendingCount();
		if (pending != s_LastLoggedPending && s_PendingLogTimer.Elapsed() >= 1.0f)
		{
			LUX_CORE_TRACE_TAG("Renderer", "GPU deletion queue: {} pending ({})", pending, Utils::BytesToString(s_ResourceFreeQueue.GetPendingBytes()));
			s_LastLoggedPending = pending;
			s_PendingLogTimer.Reset();
		}
#endif
	}


	const std::unordered_map<std::string, std::string>& Renderer::GetGlobalShaderMacros()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_Data->GlobalShaderMacros;
	}

	RendererConfig& Renderer::GetConfig()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_Config;
	}

	void Renderer::SetConfig(const RendererConfig& config)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		s_Config = config;
	}

	bool Renderer::SupportsMeshShaders()
	{
		static const bool s_Supported = Application::GetGraphicsDevice()->queryFeatureSupport(nvrhi::Feature::Meshlets);
		return s_Supported;
	}

	bool Renderer::SupportsVariableRateShading()
	{
		static const bool s_Supported = Application::GetGraphicsDevice()->queryFeatureSupport(nvrhi::Feature::VariableRateShading);
		return s_Supported;
	}

	void Renderer::AcknowledgeParsedGlobalMacros(const std::unordered_set<std::string>& macros, Ref<Shader> shader)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		for (const std::string& macro : macros)
		{
			s_GlobalShaderInfo.ShaderGlobalMacrosMap[macro][shader->GetHash()] = shader;
		}
	}

	void Renderer::SetMacroInShader(Ref<Shader> shader, const std::string& name, const std::string& value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		shader->SetMacro(name, value);
		ShaderPermutationKey key;
		key.ShaderName = shader->GetName();
		key.Macros.emplace_back(name, value);
		s_GlobalShaderInfo.PermutationCache.Add(key);
		s_GlobalShaderInfo.DirtyShaders.emplace(shader.Raw());
	}

	void Renderer::SetGlobalMacroInShaders(const std::string& name, const std::string& value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (s_Data->GlobalShaderMacros.find(name) != s_Data->GlobalShaderMacros.end())
		{
			if (s_Data->GlobalShaderMacros.at(name) == value)
				return;
		}

		s_Data->GlobalShaderMacros[name] = value;

		if (s_GlobalShaderInfo.ShaderGlobalMacrosMap.find(name) == s_GlobalShaderInfo.ShaderGlobalMacrosMap.end())
		{
			// Not an error: a global macro is frequently set before the shaders that
			// consume it are registered (e.g. the AO/GTAO macros at startup). The
			// value is cached in the permutation map and applied when those shaders
			// load, so this is a trace, not a warning — it was firing every launch.
			LUX_CORE_TRACE_TAG("Renderer", "No shaders with {} macro found (set before any consumer registered)", name);
			return;
		}

		LUX_CORE_ASSERT(s_GlobalShaderInfo.ShaderGlobalMacrosMap.find(name) != s_GlobalShaderInfo.ShaderGlobalMacrosMap.end(), "Macro has not been passed from any shader!");
		for (auto& [hash, shader] : s_GlobalShaderInfo.ShaderGlobalMacrosMap.at(name))
		{
			LUX_CORE_ASSERT(shader.IsValid(), "Shader is deleted!");
			ShaderPermutationKey key;
			key.ShaderName = shader->GetName();
			key.Macros.emplace_back(name, value);
			s_GlobalShaderInfo.PermutationCache.Add(key);
			s_GlobalShaderInfo.DirtyShaders.emplace(shader);
		}
	}

	uint32_t Renderer::GetShaderPermutationCacheSize()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_GlobalShaderInfo.PermutationCache.GetPermutationCount();
	}

	bool Renderer::UpdateDirtyShaders()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// TODO(Yan): how is this going to work for dist?
		const bool updatedAnyShaders = s_GlobalShaderInfo.DirtyShaders.size();
		for (WeakRef<Shader> shader : s_GlobalShaderInfo.DirtyShaders)
		{
			LUX_CORE_ASSERT(shader.IsValid(), "Shader is deleted!");
			shader->RT_Reload(true);
		}
		s_GlobalShaderInfo.DirtyShaders.clear();

		return updatedAnyShaders;
	}

	GPUMemoryStats Renderer::GetGPUMemoryStats()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// Driver-reported device-local usage and budget (VK_EXT_memory_budget, through NRI). It is
		// process-wide, so it covers NVRHI's remaining allocations as well as NRI's.
		GPUMemoryStats result;
		if (!RHIDevice::IsInitialized())
			return result;

		nri::VideoMemoryInfo info = {};
		if (RHIDevice::API().QueryVideoMemoryInfo(RHIDevice::Get(), nri::MemoryLocation::DEVICE, info) == nri::Result::SUCCESS)
		{
			result.Used = info.usageSize;
			result.TotalAvailable = info.budgetSize;
		}
		else
		{
			result.TotalAvailable = RHIDevice::GetDesc().adapterDesc.videoMemorySize;
		}
		return result;
	}

	Ref<Sampler> Renderer::GetClampSampler()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!s_RendererData->SamplerClamp)
			s_RendererData->SamplerClamp = Sampler::Create();

		return s_RendererData->SamplerClamp;
	}

	Ref<Sampler> Renderer::GetPointSampler()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!s_RendererData->SamplerPoint)
		{
			SamplerSpecification spec;
			spec.MinFilter = spec.MagFilter = spec.MipFilter = false;
			s_RendererData->SamplerPoint = Sampler::Create(spec);
		}

		return s_RendererData->SamplerPoint;
	}

	Ref<Sampler> Renderer::GetRepeatSampler()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!s_RendererData->SamplerRepeat)
		{
			SamplerSpecification spec;
			spec.AddressMode = TextureWrap::Repeat;
			spec.MaxAnisotropy = 16.0f;
			s_RendererData->SamplerRepeat = Sampler::Create(spec);
		}

		return s_RendererData->SamplerRepeat;
	}

	int Renderer::GetDrawcallCount()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_Data->DrawCallCount;
	}

	int Renderer::GetInstanceCount()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_Data->DrawInstanceCount;
	}

}
