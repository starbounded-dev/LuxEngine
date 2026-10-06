// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev



#pragma once

#include "RenderCommandQueue.h"
#include "RenderCommandBuffer.h"
#include "Pipeline.h"
#include "PipelineCompute.h"
#include "Mesh.h"
#include "UniformBufferSet.h"
#include "StorageBufferSet.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/RenderThread.h"

#include "RendererCapabilities.h"
#include "RendererConfig.h"

#include "GPUStats.h"

#include <functional>
#include "Material.h"
#include "SceneEnvironment.h"
#include "Texture.h"

#include "nvrhi/nvrhi.h"
#include "Lux/Scene/Scene.h"

namespace Lux {

	class ShaderLibrary;
	class RenderPass;
	class ComputePass;

	// Unified draw command for mesh rendering - consolidates parameters for all mesh draw variants
	struct MeshDrawCommand
	{
		Ref<MeshSource> MeshSource;

		// For MaterialTable path. Can be null for a mesh draw with material override
		Ref<MaterialTable> MaterialTable;

		uint32_t SubmeshIndex = 0;
		uint32_t ObjectIndexBase = 0;
		uint32_t InstanceCount = 1;

		//for rigged meshes
		uint32_t BoneTransformsOffset = 0;
		uint32_t BoneTransformsStride = 0;

		bool IsRigged = false;
	};

	enum class FragmentShadingRate : uint8_t
	{
		Rate1x1 = 0,
		Rate1x2,
		Rate2x1,
		Rate2x2,
		Rate2x4,
		Rate4x2,
		Rate4x4
	};
	
	class Renderer
	{
	public:
		typedef void(*RenderCommandFn)(void*);

		static void Init();
		static void Shutdown();

		static RendererCapabilities& GetCapabilities() { static RendererCapabilities caps; return caps; }

		static Ref<ShaderLibrary> GetShaderLibrary();

		template<typename FuncT>
		static void Submit(FuncT&& func)
		{
			// Fast path: the application/main thread is the sole owner of the submission command queue,
			// so it can fill it lock-free. This is the overwhelmingly common case.
			if (Application::IsMainThread())
			{
				auto renderCmd = [](void* ptr) {
					auto pFunc = (FuncT*)ptr;
					(*pFunc)();

					// NOTE: Instead of destroying we could try and enforce all items to be trivally destructible
					// however some items like uniforms which contain std::strings still exist for now
					// static_assert(std::is_trivially_destructible_v<FuncT>, "FuncT must be trivially destructible");
					pFunc->~FuncT();
					};
				auto storageBuffer = GetRenderCommandQueue().Allocate(renderCmd, sizeof(func));
				new (storageBuffer) FuncT(std::forward<FuncT>(func));
				return;
			}

			// Already on the render thread (e.g. GPU work triggered from ImGui rendering, which runs as
			// a render command): run inline. Queuing here would race the application thread filling the
			// same submission queue and corrupt the command buffer. Mirrors SubmitResourceFree.
			if (RenderThread::IsCurrentThreadRT())
			{
				func();
				return;
			}

			// A background thread (e.g. the asset worker creating GPU resources for a streamed texture or
			// mesh): the render command queue is a single-producer structure, so we must not write it from
			// here. Defer to a thread-safe queue that the main thread drains and re-submits in
			// single-producer context (see ExecuteBackgroundThreadSubmits, called once per frame).
			SubmitBackgroundThreadWork(std::function<void()>(std::forward<FuncT>(func)));
		}

		// Thread-safe entry point for render work originating off the main/render threads. The work is
		// queued and replayed on the main thread via ExecuteBackgroundThreadSubmits().
		static void SubmitBackgroundThreadWork(std::function<void()>&& func);

		// Drains the background-thread submission queue and re-submits it on the main thread. MUST be
		// called from the main thread, once per frame, before the frame's render work is submitted.
		static void ExecuteBackgroundThreadSubmits();

		// ── Batched resource uploads ──────────────────────────────────────────
		// Buffer/texture constructors record their initial-data uploads into one
		// shared command list instead of creating and submitting a dedicated
		// command list per resource (a vkQueueSubmit per mesh/texture is a load
		// hitch and contends the graphics queue against the render thread). The
		// batch is flushed automatically before every RenderCommandBuffer
		// submission (see RT_Submit), so uploads always reach the GPU queue ahead
		// of any command list that could consume them. Thread-safe; the record
		// callback runs synchronously, so callers may free their CPU data on
		// return (nvrhi stages it into the command list at record time).
		static void RecordResourceUpload(const std::function<void(nvrhi::ICommandList*)>& record);
		static void FlushResourceUploads();

		// Async transfer queue: when enabled AND the device has a dedicated transfer
		// queue, the upload batch is submitted on the copy queue so streaming/asset
		// loads don't stall the graphics queue. Toggle is live (Renderer.AsyncTransferQueue).
		static void SetAsyncTransferQueueEnabled(bool enabled);
		static bool IsAsyncTransferQueueEnabled();

		// Explicit barriers (setting Renderer.ExplicitBarriers, NRI migration Phase 4): NVRHI's
		// automatic barriers are turned off and every GPU access is required through the
		// RenderCommandBuffer's ResourceStateTracker. Any thread; takes effect at the next render frame.
		static void SetExplicitBarriersEnabled(bool enabled);
		static bool IsExplicitBarriersEnabled();
		// Render thread: the value latched for the current render frame.
		static bool RT_ExplicitBarriersEnabled();
		// Effective mode: the setting is on AND a dedicated transfer queue exists.
		static bool UseAsyncTransferQueue();
		// True once Shutdown has drained the queues (the device is idle).
		static bool IsShutDown();
		// If an async upload flush happened that consumingQueue hasn't yet waited on,
		// returns true and sets outInstance to the copy-queue execution instance to
		// wait on. Records the wait so it isn't re-issued. See RenderCommandBuffer::RT_Submit.
		static bool ConsumePendingUpload(GPUQueue consumingQueue, uint64_t& outInstance);

		// Releases GPU objects once the GPU has finished every frame that could still use them (see
		// RT_BeginFrame). The release is always queued on the render thread, at the point in the
		// command stream where the object stops being used. `func` must be copyable. After Shutdown
		// the device is idle and the queues are gone, so the release runs at once.
		template<typename FuncT>
		static void SubmitResourceFree(FuncT&& func)
		{
			constexpr size_t captureBytes = sizeof(std::decay_t<FuncT>);
			if (IsShutDown())
			{
				func();
				return;
			}

			if (RenderThread::IsCurrentThreadRT())
			{
				RT_EnqueueResourceFree(std::function<void()>(std::forward<FuncT>(func)), captureBytes);
				return;
			}

			Submit([func = std::forward<FuncT>(func)]() mutable
				{
					RT_EnqueueResourceFree(std::function<void()>(std::move(func)), captureBytes);
				});
		}

		/*static void* Submit(RenderCommandFn fn, unsigned int size)
		{
			return s_Instance->m_CommandQueue.Allocate(fn, size);
		}*/

		static void WaitAndRender(RenderThread* renderThread);
		static void SwapQueues();

		static void RenderThreadFunc(RenderThread* renderThread);
		static uint32_t GetRenderQueueIndex();
		static uint32_t GetRenderQueueSubmissionIndex();

		// ~Actual~ Renderer here... TODO: remove confusion later

		// Render Pass API
		static void BeginRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<RenderPass> renderPass, bool explicitClear = false);
		static void EndRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer);

		// Dynamic viewport/scissor control
		static void SetViewport(Ref<RenderCommandBuffer> renderCommandBuffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
		static void SetScissor(Ref<RenderCommandBuffer> renderCommandBuffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
		static void SetFragmentShadingRate(Ref<RenderCommandBuffer> renderCommandBuffer, FragmentShadingRate rate);

		// Compute Pass API
		static void BeginComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass);
		static void EndComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass);
		static void DispatchCompute(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass, Ref<Material> material, const glm::uvec3& workGroups, Buffer constants = Buffer());

		static void BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& markerColor = {});
		static void EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer);

		static void RT_BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& markerColor = {});
		static void RT_EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer);

		static void BeginFrame();
		static void EndFrame();
		
		//static void SetSceneEnvironment(Ref<SceneRenderer> sceneRenderer, Ref<Environment> environment, Ref<Image2D> shadow, Ref<Image2D> spotShadow);
		static std::pair<Ref<TextureCube>, Ref<TextureCube>> CreateEnvironmentMap(const std::string& filepath);
		static std::pair<Ref<TextureCube>, Ref<TextureCube>> CreateEnvironmentMap(Ref<Texture2D> equirectangularTexture);
		static Ref<TextureCube> CreatePreethamSky(float turbidity, float azimuth, float inclination);
		static Ref<Environment> CreatePreethamSkyEnvironment(float turbidity, float azimuth, float inclination);

		static void RenderQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, const glm::mat4& transform);
		static void SubmitFullscreenQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material);
		static void SubmitFullscreenQuadWithOverrides(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, Buffer vertexShaderOverrides, Buffer fragmentShaderOverrides);
		static void RenderGeometry(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, Ref<VertexBuffer> vertexBuffer, Ref<IndexBuffer> indexBuffer, const glm::mat4& transform, uint32_t indexCount = 0);
		static void RT_BindMaterialDescriptorSet(nvrhi::BindingSetVector& bindings, Ref<Shader> pipelineShader, Ref<Material> material, uint32_t set = 0);
		static void ClearImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, const glm::vec4& clearColor, TextureSubresourceRange subresources);
		static void CopyImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> sourceImage, Ref<Image2D> destinationImage);
		static void BlitImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> sourceImage, Ref<Image2D> destinationImage);

		static Ref<Texture2D> GetWhiteTexture();
		static Ref<Texture2D> GetBlackTexture();
		static Ref<Texture2D> GetHilbertLut();
		static Ref<Texture2D> GetBRDFLutTexture();
		static Ref<TextureCube> GetBlackCubeTexture();
		static Ref<Material> GetDefaultWhiteMaterial();
		static Ref<Environment> GetEmptyEnvironment();
		static Ref<Environment> GetDefaultEnvironment();

		static void RegisterShaderDependency(Ref<Shader> shader, PipelineCompute* computePipeline);
		static void RegisterShaderDependency(Ref<Shader> shader, Pipeline* pipeline);
		static void RegisterShaderDependency(Ref<Shader> shader, Material* material);
		static void RegisterShaderDependency(Ref<Shader> shader, RenderPass* renderPass);
		static void RegisterShaderDependency(Ref<Shader> shader, ComputePass* computePass);
		static void OnShaderReloaded(size_t hash);

		// Returns a process-wide cached compute pipeline for the given shader,
		// creating it on first use. Used by Texture GenerateMips so the
		// LinearSample / LinearSampleUInt mip generator is built once instead of
		// once per texture (was hundreds of redundant pipeline builds at load).
		static Ref<PipelineCompute> GetOrCreateMipGenPipeline(Ref<Shader> shader);

		// Cross-queue ordering: make the next submission on waitQueue wait until
		// the given execution instance (from RenderCommandBuffer::GetLastExecutionInstance,
		// i.e. executeCommandList's return) on executionQueue has completed. Must be
		// called on the render thread, between the two queues' submits. Used to build
		// async-compute overlap (e.g. graphics waits for the compute light-cull).
		static void QueueWaitForCommandList(GPUQueue waitQueue, GPUQueue executionQueue, uint64_t instance);

		// Main thread: the slot the frame being built uses (Application's frame counter).
		static uint32_t GetCurrentFrameIndex();
		// Render thread: the per-frame resource slot, RT_GetFrameNumber() % FramesInFlight. Not the
		// swapchain image (that is VulkanSwapChain::GetCurrentBackBufferIndex), which is not
		// sequential under MAILBOX. RT_BeginFrame guarantees the GPU has finished the frame that
		// last used this slot.
		static uint32_t RT_GetCurrentFrameIndex();
		// Render thread: monotonic render frame number, advanced by RT_BeginFrame.
		static uint64_t RT_GetFrameNumber();

		static RendererConfig& GetConfig();
		static void SetConfig(const RendererConfig& config);

		// True when the device supports mesh/task shaders (VK_EXT_mesh_shader).
		static bool SupportsMeshShaders();
		// True when the active graphics device exposes variable-rate fragment shading.
		static bool SupportsVariableRateShading();

		// Render thread, once per frame before anything renders (Application's BeginFrame lambda).
		// Closes the ending frame behind a graphics-queue event, advances the frame number, waits
		// until the GPU has finished frame N - FramesInFlight (whose slot this frame reuses), and
		// runs the releases queued up to that frame.
		static void RT_BeginFrame();
		static void RT_EnqueueResourceFree(std::function<void()> release, size_t captureBytes);

		// Add known macro from shader.
		static const std::unordered_map<std::string, std::string>& GetGlobalShaderMacros();
		static void AcknowledgeParsedGlobalMacros(const std::unordered_set<std::string>& macros, Ref<Shader> shader);
		static void SetMacroInShader(Ref<Shader> shader, const std::string& name, const std::string& value = "");
		static void SetGlobalMacroInShaders(const std::string& name, const std::string& value = "");
		static uint32_t ReloadShaders(bool forceCompile = true);
		static uint32_t WarmUpShaderPipelines();
		static uint32_t GetShaderPermutationCacheSize();
		// Returns true if any shader is actually updated.
		static bool UpdateDirtyShaders();

		static GPUMemoryStats GetGPUMemoryStats();

		static Ref<Sampler> GetClampSampler();
		static Ref<Sampler> GetPointSampler();
		static Ref<Sampler> GetRepeatSampler();
		static Ref<Sampler> GetDefaultSampler() { return GetClampSampler(); }

		static int GetDrawcallCount();
		static int GetInstanceCount();
	private:
		static RenderCommandQueue& GetRenderCommandQueue();
	};

	namespace Utils {

		inline void DumpGPUInfo()
		{
			auto& caps = Renderer::GetCapabilities();
			LUX_CORE_TRACE_TAG("Renderer", "GPU Info:");
			LUX_CORE_TRACE_TAG("Renderer", "  Vendor: {0}", caps.Vendor);
			LUX_CORE_TRACE_TAG("Renderer", "  Device: {0}", caps.Device);
			LUX_CORE_TRACE_TAG("Renderer", "  Version: {0}", caps.Version);
		}

	}

}
