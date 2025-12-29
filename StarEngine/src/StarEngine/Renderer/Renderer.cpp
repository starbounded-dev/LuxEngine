#include "sepch.h"
#include "Renderer.h"

#include "Shader.h"

#include "Renderer2D.h"
#include "RendererAPI.h"
//#include "SceneRenderer.h"
#include "ShaderPack.h"

#include "StarEngine/Core/Timer.h"
#include "StarEngine/Debug/Profiler.h"
#include "StarEngine/Platform/Vulkan/VulkanContext.h"
#include "StarEngine/Platform/Vulkan/VulkanRenderCommandBuffer.h"
#include "StarEngine/Platform/Vulkan/VulkanSwapChain.h"
#include "StarEngine/Project/Project.h"

#include "StarEngine/Asset/AssetManager.h"

#include "nvrhi/nvrhi.h"
#include "nvrhi/utils.h"

#if SE_HAS_SHADER_COMPILER
#include "StarEngine/Platform/Vulkan/ShaderCompiler/VulkanShaderCompiler.h"
#endif

#include <glm/gtc/type_ptr.hpp>

#include <filesystem>
#include <format>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include "ShaderDefs.h"

namespace std {
	template<>
	struct hash<StarEngine::WeakRef<StarEngine::Shader>>
	{
		size_t operator()(const StarEngine::WeakRef<StarEngine::Shader>& shader) const noexcept
		{
			return shader->GetHash();
		}
	};
}

namespace StarEngine {
	namespace Utils {

		static const char* VulkanVendorIDToString(uint32_t vendorID)
		{
			switch (vendorID)
			{
			case 0x10DE: return "NVIDIA";
			case 0x1002: return "AMD";
			case 0x8086: return "INTEL";
			case 0x13B5: return "ARM";
			}
			return "Unknown";
		}

	}

	static std::unordered_map<size_t, Ref<Pipeline>> s_PipelineCache;

	struct ShaderDependencies
	{
		std::vector<Ref<PipelineCompute>> ComputePipelines;
		std::vector<Ref<Pipeline>> Pipelines;
		std::vector<Ref<Material>> Materials;
	};
	static std::unordered_map<size_t, ShaderDependencies> s_ShaderDependencies;
	static std::shared_mutex s_ShaderDependenciesMutex; // ShaderDependencies can be accessed (and modified) from multiple threads, hence require synchronization


	struct GlobalShaderInfo
	{
		// Macro name, set of shaders with that macro.
		std::unordered_map<std::string, std::unordered_map<size_t, WeakRef<Shader>>> ShaderGlobalMacrosMap;
		// Shaders waiting to be reloaded.
		std::unordered_set<WeakRef<Shader>> DirtyShaders;
	};
	static GlobalShaderInfo s_GlobalShaderInfo;

	struct RendererData
	{
		RendererCapabilities RenderCaps;

		Ref<ShaderLibrary> m_ShaderLibrary;

		Ref<Texture2D> WhiteTexture;
		Ref<Texture2D> BlackTexture;
		Ref<Texture2D> BRDFLutTexture;
		Ref<Texture2D> HilbertLut;
		Ref<TextureCube> BlackCubeTexture;
		Ref<Environment> EmptyEnvironment;

		std::unordered_map<std::string, std::string> GlobalShaderMacros;

		Ref<Texture2D> BRDFLut;

		Ref<VertexBuffer> QuadVertexBuffer;
		Ref<IndexBuffer> QuadIndexBuffer;
		VulkanShader::ShaderMaterialDescriptorSet QuadDescriptorSet;

		std::unordered_map<SceneRenderer*, std::vector<VulkanShader::ShaderMaterialDescriptorSet>> RendererDescriptorSet;
		VkDescriptorSet ActiveRendererDescriptorSet = nullptr;
		std::vector<VkDescriptorPool> DescriptorPools;
		VkDescriptorPool MaterialDescriptorPool;
		std::vector<uint32_t> DescriptorPoolAllocationCount;

		// UniformBufferSet -> Shader Hash -> Frame -> WriteDescriptor
		std::unordered_map<UniformBufferSet*, std::unordered_map<uint64_t, std::vector<std::vector<VkWriteDescriptorSet>>>> UniformBufferWriteDescriptorCache;
		std::unordered_map<StorageBufferSet*, std::unordered_map<uint64_t, std::vector<std::vector<VkWriteDescriptorSet>>>> StorageBufferWriteDescriptorCache;

		// Default samplers
		Ref<Sampler> SamplerClamp = nullptr;
		Ref<Sampler> SamplerPoint = nullptr;

		int32_t SelectedDrawCall = -1;
		int32_t DrawCallCount = 0;
	};

	static RendererData* s_RendererData = nullptr;

	void Renderer::RegisterShaderDependency(Ref<Shader> shader, Ref<PipelineCompute> computePipeline)
	{
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].ComputePipelines.push_back(computePipeline);
	}

	void Renderer::RegisterShaderDependency(Ref<Shader> shader, Ref<Pipeline> pipeline)
	{
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].Pipelines.push_back(pipeline);
	}

	void Renderer::RegisterShaderDependency(Ref<Shader> shader, Ref<Material> material)
	{
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].Materials.push_back(material);
	}

	void Renderer::OnShaderReloaded(size_t hash)
	{
		ShaderDependencies dependencies;
		{
			std::shared_lock lock(s_ShaderDependenciesMutex);
			if (auto it = s_ShaderDependencies.find(hash); it != s_ShaderDependencies.end())
			{
				dependencies = it->second; // expensive to copy, but we need to release the lock (in particular to avoid potential deadlock if things like material->OnShaderReloaded() happen to ask for the lock)
			}
		}
		for (auto& pipeline : dependencies.Pipelines)
		{
			pipeline->Invalidate();
		}

		for (auto& computePipeline : dependencies.ComputePipelines)
		{
			computePipeline->CreatePipeline();
		}

		for (auto& material : dependencies.Materials)
		{
			material->OnShaderReloaded();
		}
	}

	uint32_t Renderer::RT_GetCurrentFrameIndex()
	{
		return Application::Get().GetWindow().GetSwapChain().GetCurrentBackBufferIndex();
	}

	uint32_t Renderer::GetCurrentFrameIndex()
	{
		return Application::Get().GetCurrentFrameIndex();
	}

	void RendererAPI::SetAPI(RendererAPIType api)
	{
		// TODO: make sure this is called at a valid time
		SE_CORE_VERIFY(api == RendererAPIType::Vulkan, "Vulkan is currently the only supported Renderer API");
		s_CurrentRendererAPI = api;
	}

	static RendererConfig s_Config;
	static RendererData* s_Data = nullptr;
	constexpr static uint32_t s_RenderCommandQueueCount = 2;
	static RenderCommandQueue* s_CommandQueue[s_RenderCommandQueueCount];
	static std::atomic<uint32_t> s_RenderCommandQueueSubmissionIndex = 0;
	static RenderCommandQueue s_ResourceFreeQueue[3];

	static RendererAPI* InitRendererAPI()
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::Vulkan: return nullptr;
		}
		SE_CORE_ASSERT(false, "Unknown RendererAPI");
		return nullptr;
	}

	void Renderer::Init()
	{
		s_Data = snew RendererData();
		s_RendererData = snew RendererData();

		s_CommandQueue[0] = snew RenderCommandQueue();
		s_CommandQueue[1] = snew RenderCommandQueue();

		// Make sure we don't have more frames in flight than swapchain images
		s_Config.FramesInFlight = glm::min<uint32_t>(s_Config.FramesInFlight, Application::Get().GetWindow().GetSwapChain().GetBackBufferCount());

		Renderer::SetGlobalMacroInShaders("__HZ_REFLECTION_OCCLUSION_METHOD", "0");
		Renderer::SetGlobalMacroInShaders("__HZ_AO_METHOD", std::format("{}", (int)ShaderDef::GetAOMethod(true)));
		Renderer::SetGlobalMacroInShaders("__HZ_GTAO_COMPUTE_BENT_NORMALS", "0");

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
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/HazelPBR_Anim.glsl");
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
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/DOF.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/EdgeDetection.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PostProcessing/SceneComposite.glsl");

		// Light-culling
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PreDepth.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/PreDepth_Anim.glsl");
		Renderer::GetShaderLibrary()->Load("Resources/Shaders/LightCulling.glsl");

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
					SE_CORE_ASSERT(r2index < 65536);
					data[x + 64 * y] = r2index;
				}
			}
			s_Data->HilbertLut = Texture2D::Create(spec, Buffer(data, 1));
			delete[] data;

		}

		// From VulkanRenderer::Init()
		const auto& config = Renderer::GetConfig();
		s_RendererData->DescriptorPools.resize(config.FramesInFlight);
		s_RendererData->DescriptorPoolAllocationCount.resize(config.FramesInFlight);

		auto& caps = s_RendererData->RenderCaps;

		// TODO(Yan):
		// auto& properties = VulkanContext::GetCurrentDevice()->GetPhysicalDevice()->GetProperties();
		// Application::Get().GetWindow().GetDeviceManager();
		// caps.Vendor = Utils::VulkanVendorIDToString(properties.vendorID);
		// caps.Device = properties.deviceName;
		// caps.Version = std::to_string(properties.driverVersion);

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
		{
			std::scoped_lock lock(s_ShaderDependenciesMutex);
			s_ShaderDependencies.clear();
		}

		// From VulkanRenderer::Init()
		VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();
		vkDeviceWaitIdle(device);


#if SE_HAS_SHADER_COMPILER
		VulkanShaderCompiler::ClearUniformBuffers();
#endif
		delete s_RendererData;
		// END

		delete s_Data;

		// Resource release queue
		for (uint32_t i = 0; i < s_Config.FramesInFlight; i++)
		{
			auto& queue = Renderer::GetRenderResourceReleaseQueue(i);
			queue.Execute();
		}

		delete s_CommandQueue[0];
		delete s_CommandQueue[1];
	}

	Ref<ShaderLibrary> Renderer::GetShaderLibrary()
	{
		return s_Data->m_ShaderLibrary;
	}

	void Renderer::RenderThreadFunc(RenderThread* renderThread)
	{
		SE_PROFILE_THREAD("Render Thread");

		while (renderThread->IsRunning())
		{
			WaitAndRender(renderThread);
		}
	}

	void Renderer::WaitAndRender(RenderThread* renderThread)
	{
		SE_PROFILE_FUNCTION("Renderer::WaitAndRender");
		auto& performanceTimers = Application::Get().m_PerformanceTimers;

		// Wait for kick, then set render thread to busy
		{
			SE_PROFILE_SCOPE("Wait");
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
		s_RenderCommandQueueSubmissionIndex = (s_RenderCommandQueueSubmissionIndex + 1) % s_RenderCommandQueueCount;
	}

	uint32_t Renderer::GetRenderQueueIndex()
	{
		return (s_RenderCommandQueueSubmissionIndex + 1) % s_RenderCommandQueueCount;
	}

	uint32_t Renderer::GetRenderQueueSubmissionIndex()
	{
		return s_RenderCommandQueueSubmissionIndex;
	}

	void Renderer::BeginRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<RenderPass> renderPass, bool explicitClear)
	{
		Renderer::Submit([renderCommandBuffer, renderPass, explicitClear]() mutable
			{
				renderCommandBuffer->RT_BeginMarker(renderPass->GetSpecification().DebugName);

				Ref<Pipeline> pipeline = renderPass->GetSpecification().Pipeline;
				Ref<Framebuffer> framebuffer = pipeline->GetSpecification().TargetFramebuffer;

				if (explicitClear || framebuffer->GetSpecification().ClearColorOnLoad || framebuffer->GetSpecification().ClearDepthOnLoad)
				{
					const auto& clearValues = framebuffer->GetClearValues();

					if (explicitClear || framebuffer->GetSpecification().ClearColorOnLoad)
					{
						for (size_t i = 0; i < framebuffer->GetColorAttachmentCount(); i++)
						{
							nvrhi::Color color = nvrhi::Color(clearValues[i].Color.float32[0], clearValues[i].Color.float32[1],
								clearValues[i].Color.float32[2], clearValues[i].Color.float32[3]);

							nvrhi::utils::ClearColorAttachment(renderCommandBuffer->GetActive(), framebuffer->GetHandle(), i, color);
						}
					}

					if (explicitClear || framebuffer->GetSpecification().ClearDepthOnLoad)
					{
						if (framebuffer->HasDepthAttachment())
						{
							const auto& depthStencil = clearValues[clearValues.size() - 1].DepthStencil;
							nvrhi::utils::ClearDepthStencilAttachment(renderCommandBuffer->GetActive(), framebuffer->GetHandle(), depthStencil.Depth, depthStencil.Stencil);
						}
					}
				}

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();
				graphicsState.pipeline = pipeline->GetHandle();
				SE_CORE_ASSERT(graphicsState.pipeline);
				graphicsState.framebuffer = framebuffer->GetHandle();
				SE_CORE_ASSERT(graphicsState.framebuffer);

				// Viewport and scissor
				float fbWidth = (float)framebuffer->GetWidth();
				float fbHeight = (float)framebuffer->GetHeight();
				graphicsState.viewport.viewports = { nvrhi::Viewport(fbWidth, fbHeight) };
				graphicsState.viewport.scissorRects = { nvrhi::Rect(fbWidth, fbHeight) };

				graphicsState.lineWidth = 0.0f;
				if (renderPass->GetPipeline()->IsDynamicLineWidth())
					graphicsState.lineWidth = renderPass->GetPipeline()->GetSpecification().LineWidth;

				renderPass->Prepare();
				auto bindingSets = renderPass->GetBindingSets(Renderer::RT_GetCurrentFrameIndex());
				graphicsState.bindings = bindingSets;

				renderCommandBuffer->RT_CommitGraphicsState();
			});
	}

	void Renderer::EndRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer)
	{
		Renderer::Submit([renderCommandBuffer]() mutable
			{
				renderCommandBuffer->RT_EndMarker();
			});
	}

	void Renderer::BeginComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass)
	{
		SE_CORE_ASSERT(computePass, "ComputePass cannot be null!");

		Renderer::Submit([renderCommandBuffer, computePass]() mutable
			{
				renderCommandBuffer->RT_BeginMarker(computePass->GetSpecification().DebugName);

				Ref<PipelineCompute> pipeline = computePass->GetPipeline();

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				nvrhi::ComputeState& computeState = renderCommandBuffer->GetComputeState();
				computeState.pipeline = pipeline->GetHandle();
				SE_CORE_ASSERT(computeState.pipeline);

				computePass->Prepare();

				auto bindingSets = computePass->GetBindingSets(Renderer::RT_GetCurrentFrameIndex());
				computeState.bindings = bindingSets;

				renderCommandBuffer->RT_CommitComputeState();
			});
	}

	void Renderer::EndComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass)
	{
		Renderer::Submit([renderCommandBuffer, computePass]() mutable
			{
				renderCommandBuffer->RT_EndMarker();
			});
	}

	void Renderer::DispatchCompute(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass, Ref<Material> material, const glm::uvec3& workGroups, Buffer constants)
	{
		Buffer pushConstantBuffer;
		if (constants)
			pushConstantBuffer = Buffer::Copy(constants);

		Renderer::Submit([renderCommandBuffer, computePass, material, workGroups, pushConstantBuffer]() mutable
			{
				const uint32_t frameIndex = Renderer::RT_GetCurrentFrameIndex();
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				nvrhi::ComputeState& computeState = renderCommandBuffer->GetComputeState();

				// Bind material descriptor set if exists
				if (material)
				{
					material->Prepare();
					auto bindingSet = material->GetBindingSet(frameIndex);
					if (bindingSet)
					{
						if (computeState.bindings.empty())
							computeState.bindings.resize(1);

						computeState.bindings[0] = bindingSet;
					}
				}

				renderCommandBuffer->RT_CommitComputeState();

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
		Renderer::Submit([renderCommandBuffer, s = label]() mutable
			{
				renderCommandBuffer->RT_BeginTimerQuery(s);
			});
	}

	void Renderer::EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer)
	{
		Renderer::Submit([renderCommandBuffer]() mutable
			{
				renderCommandBuffer->RT_EndTimerQuery();
			});
	}

	void Renderer::RT_BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& markerColor)
	{
		renderCommandBuffer->RT_BeginTimerQuery(label);
	}

	void Renderer::RT_EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer)
	{
		renderCommandBuffer->RT_EndTimerQuery();
	}

	void Renderer::BeginFrame()
	{

	}

	void Renderer::EndFrame()
	{

	}
	/*
	void Renderer::SetSceneEnvironment(Ref<SceneRenderer> sceneRenderer, Ref<Environment> environment, Ref<Image2D> shadow, Ref<Image2D> spotShadow)
	{

	}*/

	std::pair<Ref<TextureCube>, Ref<TextureCube>> Renderer::CreateEnvironmentMap(const std::string& filepath)
	{
		if (!Renderer::GetConfig().ComputeEnvironmentMaps)
			return { Renderer::GetBlackCubeTexture(), Renderer::GetBlackCubeTexture() };

		const uint32_t cubemapSize = Renderer::GetConfig().EnvironmentMapResolution;
		const uint32_t irradianceMapSize = 32;

		// Load the HDR equirectangular texture
		TextureSpecification equirectSpec;
		equirectSpec.DebugName = "EnvEquirect";
		Ref<Texture2D> envEquirect = Texture2D::Create(equirectSpec, filepath);
		if (!envEquirect || !envEquirect->Loaded())
		{
			SE_CORE_ERROR("Failed to load environment map: {}", filepath);
			return { Renderer::GetBlackCubeTexture(), Renderer::GetBlackCubeTexture() };
		}
		SE_CORE_ASSERT(envEquirect->GetFormat() == ImageFormat::RGBA32F, "Environment texture is not HDR!");

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
				outputViewSpec.Dimension = nvrhi::TextureDimension::TextureCube;
				outputViewSpec.DebugName = std::format("EnvMipFilter-Output-Mip{}", mip);
				Ref<ImageView> outputView = ImageView::Create(outputViewSpec);

				Ref<Material> mipFilterMaterial = Material::Create(mipFilterShader);
				mipFilterMaterial->Set("outputTexture", outputView);
				mipFilterMaterial->Set("inputTexture", envUnfiltered);

				uint32_t numGroups = glm::max(1u, mipSize / 32);
				glm::uvec3 workGroups{ numGroups, numGroups, 6 };
				DispatchCompute(commandBuffer, computePass, mipFilterMaterial, workGroups, Buffer(&roughness, sizeof(float)));

				// Commit barriers between mip levels to ensure writes complete before next iteration
				Renderer::Submit([commandBuffer]()
					{
						nvrhi::CommandListHandle commandList = commandBuffer->GetActive();
						commandList->commitBarriers();
					});
			}

			EndComputePass(commandBuffer, computePass);
			commandBuffer->End();
			commandBuffer->Submit();
		}

		// Step 3: Compute irradiance map
		cubemapSpec.Width = irradianceMapSize;
		cubemapSpec.Height = irradianceMapSize;
		cubemapSpec.DebugName = "IrradianceMap";
		Ref<TextureCube> irradianceMap = TextureCube::Create(cubemapSpec);

		{
			Ref<Shader> irradianceShader = Renderer::GetShaderLibrary()->Get("EnvironmentIrradiance");
			Ref<Material> irradianceMaterial = Material::Create(irradianceShader);
			irradianceMaterial->Set("o_IrradianceMap", irradianceMap);
			irradianceMaterial->Set("u_RadianceMap", envFiltered);

			ComputePassSpecification computePassSpec;
			computePassSpec.Pipeline = PipelineCompute::Create(irradianceShader);
			computePassSpec.DebugName = "EnvironmentIrradiance";
			Ref<ComputePass> computePass = ComputePass::Create(computePassSpec);
			computePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			computePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			computePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());

			Ref<RenderCommandBuffer> commandBuffer = RenderCommandBuffer::Create(1, "EnvironmentIrradiance-Compute");
			commandBuffer->Begin();
			BeginComputePass(commandBuffer, computePass);

			uint32_t samples = Renderer::GetConfig().IrradianceMapComputeSamples;
			Buffer pushConstantBuffer(&samples, sizeof(uint32_t));
			glm::uvec3 workGroups{ irradianceMapSize / 32, irradianceMapSize / 32, 6 };
			DispatchCompute(commandBuffer, computePass, irradianceMaterial, workGroups, pushConstantBuffer);

			EndComputePass(commandBuffer, computePass);
			commandBuffer->End();
			commandBuffer->Submit();

			irradianceMap->GenerateMips();
		}

		return { envFiltered, irradianceMap };
	}

	Ref<TextureCube> Renderer::CreatePreethamSky(float turbidity, float azimuth, float inclination)
	{
		const uint32_t cubemapSize = Renderer::GetConfig().EnvironmentMapResolution;
		const uint32_t irradianceMapSize = 32;

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
	/*
	void Renderer::RenderStaticMesh(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<RenderPass> renderPass, Ref<StaticMesh> staticMesh, Ref<MeshSource> meshSource, uint32_t submeshIndex, Ref<MaterialTable> materialTable, Ref<VertexBuffer> transformBuffer, uint32_t transformOffset, uint32_t instanceCount)
	{
		HZ_CORE_ASSERT(staticMesh);
		HZ_CORE_ASSERT(meshSource);
		HZ_CORE_ASSERT(materialTable);

		Renderer::Submit([renderCommandBuffer, renderPass, staticMesh, meshSource, submeshIndex, materialTable, transformBuffer, transformOffset, instanceCount]() mutable
			{
				HZ_PROFILE_FUNC("Renderer::RenderStaticMeshWithMaterial");
				HZ_SCOPE_PERF("Renderer::RenderStaticMeshWithMaterial");

				if (s_Data->SelectedDrawCall != -1 && s_Data->DrawCallCount > s_Data->SelectedDrawCall)
					return;

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();

				// Vertex Buffer
				{
					nvrhi::VertexBufferBinding vertexBufferBinding;

					Ref<VertexBuffer> vertexBuffer = meshSource->GetVertexBuffer();
					vertexBufferBinding.buffer = vertexBuffer->GetHandle();
					vertexBufferBinding.slot = 0;
					vertexBufferBinding.offset = 0;

					nvrhi::VertexBufferBinding transformBufferBinding;
					transformBufferBinding.buffer = transformBuffer->GetHandle();
					transformBufferBinding.slot = 1;
					transformBufferBinding.offset = transformOffset;

					graphicsState.vertexBuffers = { vertexBufferBinding, transformBufferBinding };
				}

				// Index Buffer
				{
					nvrhi::IndexBufferBinding indexBufferBinding;

					Ref<IndexBuffer> indexBuffer = meshSource->GetIndexBuffer();
					indexBufferBinding.buffer = indexBuffer->GetHandle();
					indexBufferBinding.format = nvrhi::Format::R32_UINT;
					indexBufferBinding.offset = 0;

					graphicsState.indexBuffer = indexBufferBinding;
				}

				const auto& submeshes = meshSource->GetSubmeshes();
				const Submesh& submesh = submeshes[submeshIndex];

				// Material
				{
					Ref<MaterialTable> meshMaterialTable = staticMesh->GetMaterials();
					uint32_t materialCount = meshMaterialTable->GetMaterialCount();

					// NOTE(Yan): probably should not involve Asset Manager at this stage
					AssetHandle materialHandle = materialTable->HasMaterial(submesh.MaterialIndex) ? materialTable->GetMaterial(submesh.MaterialIndex) : meshMaterialTable->GetMaterial(submesh.MaterialIndex);
					Ref<MaterialAsset> materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);
					Ref<Material> material = materialAsset->GetMaterial();

					material->Prepare();
					auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
					if (bindingSet)
						graphicsState.bindings[0] = bindingSet;

					Buffer uniformStorageBuffer = material->GetUniformStorageBuffer();
					if (uniformStorageBuffer)
						commandList->setPushConstants(uniformStorageBuffer.Data, uniformStorageBuffer.Size);
				}

				renderCommandBuffer->RT_CommitGraphicsState();

				nvrhi::DrawArguments drawArgs{};
				drawArgs.vertexCount = submesh.IndexCount;
				drawArgs.startIndexLocation = submesh.BaseIndex;
				drawArgs.startVertexLocation = submesh.BaseVertex;
				drawArgs.instanceCount = instanceCount;
				commandList->drawIndexed(drawArgs);

				s_Data->DrawCallCount++;
			});
	}

	void Renderer::RenderSubmeshInstanced(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Mesh> mesh, Ref<MeshSource> meshSource, uint32_t submeshIndex, Ref<MaterialTable> materialTable, Ref<VertexBuffer> transformBuffer, uint32_t transformOffset, uint32_t boneTransformsOffset, uint32_t boneTransformsStride, uint32_t instanceCount)
	{
		HZ_CORE_ASSERT(mesh);
		HZ_CORE_ASSERT(meshSource);
		HZ_CORE_ASSERT(materialTable);

		Renderer::Submit([renderCommandBuffer, pipeline, mesh, meshSource, submeshIndex, materialTable, transformBuffer, transformOffset, boneTransformsOffset, boneTransformsStride, instanceCount]() mutable
			{
				HZ_PROFILE_FUNC("Renderer::RenderSubmeshInstanced");
				HZ_SCOPE_PERF("Renderer::RenderSubmeshInstanced");

				if (s_Data->SelectedDrawCall != -1 && s_Data->DrawCallCount > s_Data->SelectedDrawCall)
					return;

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();

				const auto& submeshes = meshSource->GetSubmeshes();
				const Submesh& submesh = submeshes[submeshIndex];

				// Vertex Buffers
				{
					nvrhi::VertexBufferBinding vertexBufferBinding;
					vertexBufferBinding.buffer = meshSource->GetVertexBuffer()->GetHandle();
					vertexBufferBinding.slot = 0;
					vertexBufferBinding.offset = 0;

					nvrhi::VertexBufferBinding transformBufferBinding;
					transformBufferBinding.buffer = transformBuffer->GetHandle();
					transformBufferBinding.slot = 1;
					transformBufferBinding.offset = transformOffset;

					if (submesh.IsRigged)
					{
						nvrhi::VertexBufferBinding boneInfluenceBufferBinding;
						boneInfluenceBufferBinding.buffer = meshSource->GetBoneInfluenceBuffer()->GetHandle();
						boneInfluenceBufferBinding.slot = 2;
						boneInfluenceBufferBinding.offset = 0;

						graphicsState.vertexBuffers = { vertexBufferBinding, transformBufferBinding, boneInfluenceBufferBinding };
					}
					else
					{
						graphicsState.vertexBuffers = { vertexBufferBinding, transformBufferBinding };
					}
				}

				// Index Buffer
				{
					nvrhi::IndexBufferBinding indexBufferBinding;
					indexBufferBinding.buffer = meshSource->GetIndexBuffer()->GetHandle();
					indexBufferBinding.format = nvrhi::Format::R32_UINT;
					indexBufferBinding.offset = 0;

					graphicsState.indexBuffer = indexBufferBinding;
				}

				// Material
				Ref<MaterialTable> meshMaterialTable = mesh->GetMaterials();
				AssetHandle materialHandle = materialTable->HasMaterial(submesh.MaterialIndex) ? materialTable->GetMaterial(submesh.MaterialIndex) : meshMaterialTable->GetMaterial(submesh.MaterialIndex);
				Ref<MaterialAsset> materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);
				Ref<Material> material = materialAsset->GetMaterial();

				material->Prepare();
				auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
				if (bindingSet)
					graphicsState.bindings[0] = bindingSet;

				renderCommandBuffer->RT_CommitGraphicsState();

				// Push constants for animated meshes: Base index and Stride, then material uniforms
				Buffer pushConstantBuffer;
				uint64_t pushConstantOffset = 0;

				if (submesh.IsRigged)
				{
					pushConstantBuffer.Allocate(256);
					pushConstantBuffer.Write(&boneTransformsOffset, sizeof(uint32_t), 0);
					pushConstantBuffer.Write(&boneTransformsStride, sizeof(uint32_t), sizeof(uint32_t));
					pushConstantOffset = sizeof(uint32_t) * 4;  //to account for padding
				}

				Buffer uniformStorageBuffer = material->GetUniformStorageBuffer();
				if (uniformStorageBuffer)
				{
					if (!pushConstantBuffer)
					{
						pushConstantBuffer.Allocate(128);
					}
					pushConstantBuffer.Write(uniformStorageBuffer.Data, uniformStorageBuffer.Size, pushConstantOffset);
					pushConstantOffset += uniformStorageBuffer.Size;
				}

				if (pushConstantOffset > 0)
					commandList->setPushConstants(pushConstantBuffer.Data, pushConstantOffset);

				nvrhi::DrawArguments drawArgs{};
				drawArgs.vertexCount = submesh.IndexCount;
				drawArgs.startIndexLocation = submesh.BaseIndex;
				drawArgs.startVertexLocation = submesh.BaseVertex;
				drawArgs.instanceCount = instanceCount;
				commandList->drawIndexed(drawArgs);

				s_Data->DrawCallCount++;
			});
	}

	void Renderer::RenderMeshWithMaterial(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Mesh> mesh, Ref<MeshSource> meshSource, uint32_t submeshIndex, Ref<VertexBuffer> transformBuffer, uint32_t transformOffset, uint32_t boneTransformsOffset, uint32_t boneTransformsStride, uint32_t instanceCount, Ref<Material> material, Buffer additionalUniforms)
	{
		HZ_CORE_ASSERT(mesh);
		HZ_CORE_ASSERT(meshSource);
		HZ_CORE_ASSERT(material);

		bool isRigged = meshSource->IsSubmeshRigged(submeshIndex);

		Buffer pushConstantBuffer;
		uint64_t pushConstantBufferOffset = 0;

		if (isRigged || additionalUniforms.Size)
		{
			pushConstantBuffer.Allocate(128);

			if (additionalUniforms.Size)
			{
				pushConstantBuffer.Write(additionalUniforms.Data, additionalUniforms.Size, 0);
				pushConstantBufferOffset = additionalUniforms.Size;
			}

			if (isRigged)
			{
				pushConstantBuffer.Write(&boneTransformsOffset, sizeof(uint32_t), pushConstantBufferOffset);
				pushConstantBuffer.Write(&boneTransformsStride, sizeof(uint32_t), pushConstantBufferOffset + sizeof(uint32_t));
				pushConstantBufferOffset += sizeof(uint32_t) * 4 - additionalUniforms.Size; //to account for padding
			}
		}

		Buffer uniformStorageBuffer = material->GetUniformStorageBuffer();
		if (uniformStorageBuffer)
		{
			if (pushConstantBuffer)
			{
				pushConstantBuffer.Write(uniformStorageBuffer.Data, uniformStorageBuffer.Size, pushConstantBufferOffset);
				pushConstantBufferOffset += uniformStorageBuffer.Size;
			}
			else
			{
				pushConstantBuffer = uniformStorageBuffer;
				pushConstantBufferOffset = pushConstantBuffer.Size;
			}
		}

		Renderer::Submit([renderCommandBuffer, pipeline, mesh, meshSource, submeshIndex, transformBuffer, transformOffset, instanceCount, material, pushConstantBuffer, pushConstantBufferOffset, isRigged]() mutable
			{
				HZ_PROFILE_FUNC("Renderer::RenderMeshWithMaterial");
				HZ_SCOPE_PERF("Renderer::RenderMeshWithMaterial");

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();

				const auto& submeshes = meshSource->GetSubmeshes();
				const Submesh& submesh = submeshes[submeshIndex];

				// Vertex Buffers
				{
					nvrhi::VertexBufferBinding vertexBufferBinding;
					vertexBufferBinding.buffer = meshSource->GetVertexBuffer()->GetHandle();
					vertexBufferBinding.slot = 0;
					vertexBufferBinding.offset = 0;

					nvrhi::VertexBufferBinding transformBufferBinding;
					transformBufferBinding.buffer = transformBuffer->GetHandle();
					transformBufferBinding.slot = 1;
					transformBufferBinding.offset = transformOffset;

					if (isRigged)
					{
						nvrhi::VertexBufferBinding boneInfluenceBufferBinding;
						boneInfluenceBufferBinding.buffer = meshSource->GetBoneInfluenceBuffer()->GetHandle();
						boneInfluenceBufferBinding.slot = 2;
						boneInfluenceBufferBinding.offset = 0;

						graphicsState.vertexBuffers = { vertexBufferBinding, transformBufferBinding, boneInfluenceBufferBinding };
					}
					else
					{
						graphicsState.vertexBuffers = { vertexBufferBinding, transformBufferBinding };
					}
				}

				// Index Buffer
				{
					nvrhi::IndexBufferBinding indexBufferBinding;
					indexBufferBinding.buffer = meshSource->GetIndexBuffer()->GetHandle();
					indexBufferBinding.format = nvrhi::Format::R32_UINT;
					indexBufferBinding.offset = 0;

					graphicsState.indexBuffer = indexBufferBinding;
				}

				material->Prepare();
				auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
				if (bindingSet)
					graphicsState.bindings[0] = bindingSet;

				renderCommandBuffer->RT_CommitGraphicsState();

				if (pushConstantBufferOffset > 0)
					commandList->setPushConstants(pushConstantBuffer.Data, pushConstantBufferOffset);

				nvrhi::DrawArguments drawArgs{};
				drawArgs.vertexCount = submesh.IndexCount;
				drawArgs.startIndexLocation = submesh.BaseIndex;
				drawArgs.startVertexLocation = submesh.BaseVertex;
				drawArgs.instanceCount = instanceCount;
				commandList->drawIndexed(drawArgs);
			});
	}

	void Renderer::RenderStaticMeshWithMaterial(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<StaticMesh> staticMesh, Ref<MeshSource> meshSource, uint32_t submeshIndex, Ref<VertexBuffer> transformBuffer, uint32_t transformOffset, uint32_t instanceCount, Ref<Material> material, Buffer additionalUniforms)
	{
		HZ_CORE_ASSERT(staticMesh);
		HZ_CORE_ASSERT(meshSource);
		HZ_CORE_ASSERT(material);

		Buffer pushConstantBuffer;
		uint64_t pushConstantBufferOffset = 0;
		if (additionalUniforms.Size)
		{
			pushConstantBuffer.Allocate(128);
			pushConstantBuffer.Write(additionalUniforms.Data, additionalUniforms.Size);
			pushConstantBufferOffset = additionalUniforms.Size;
		}

		Renderer::Submit([renderCommandBuffer, pipeline, staticMesh, meshSource, submeshIndex, transformBuffer, transformOffset, instanceCount, material, pushConstantBuffer, pushConstantBufferOffset]() mutable
			{
				HZ_PROFILE_FUNC("Renderer::RenderStaticMeshWithMaterial");
				HZ_SCOPE_PERF("Renderer::RenderStaticMeshWithMaterial");

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				nvrhi::GraphicsState& graphicsState = renderCommandBuffer->GetGraphicsState();

				{
					nvrhi::VertexBufferBinding vertexBufferBinding;

					Ref<VertexBuffer> vertexBuffer = meshSource->GetVertexBuffer();
					vertexBufferBinding.buffer = vertexBuffer->GetHandle();
					vertexBufferBinding.slot = 0;
					vertexBufferBinding.offset = 0;

					nvrhi::VertexBufferBinding transformBufferBinding;
					transformBufferBinding.buffer = transformBuffer->GetHandle();
					transformBufferBinding.slot = 1;
					transformBufferBinding.offset = transformOffset;

					graphicsState.vertexBuffers = { vertexBufferBinding, transformBufferBinding };
				}

				{
					nvrhi::IndexBufferBinding indexBufferBinding;

					Ref<IndexBuffer> indexBuffer = meshSource->GetIndexBuffer();
					indexBufferBinding.buffer = indexBuffer->GetHandle();
					indexBufferBinding.format = nvrhi::Format::R32_UINT;
					indexBufferBinding.offset = 0;

					graphicsState.indexBuffer = indexBufferBinding;
				}

				material->Prepare();
				auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
				if (bindingSet)
					graphicsState.bindings[0] = bindingSet;

				renderCommandBuffer->RT_CommitGraphicsState();

				Buffer uniformStorageBuffer = material->GetUniformStorageBuffer();
				if (uniformStorageBuffer)
				{
					if (pushConstantBuffer)
					{
						pushConstantBuffer.Write(uniformStorageBuffer.Data, uniformStorageBuffer.Size, pushConstantBufferOffset);
						pushConstantBufferOffset += uniformStorageBuffer.Size;
					}
					else
					{
						pushConstantBuffer = uniformStorageBuffer;
						pushConstantBufferOffset = pushConstantBuffer.Size;
					}
				}

				if (pushConstantBufferOffset > 0)
					commandList->setPushConstants(pushConstantBuffer.Data, pushConstantBufferOffset);

				const auto& submeshes = meshSource->GetSubmeshes();
				const auto& submesh = submeshes[submeshIndex];

				nvrhi::DrawArguments drawArgs{};
				drawArgs.vertexCount = submesh.IndexCount;
				drawArgs.startIndexLocation = submesh.BaseIndex;
				drawArgs.startVertexLocation = submesh.BaseVertex;
				drawArgs.instanceCount = instanceCount;
				commandList->drawIndexed(drawArgs);
			});
	}
	*/
	void Renderer::RenderQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, const glm::mat4& transform)
	{
		SE_CORE_VERIFY(renderCommandBuffer);
		SE_CORE_VERIFY(pipeline);

		Renderer::Submit([renderCommandBuffer, pipeline, material, transform]() mutable
			{
				SE_PROFILE_FUNCTION("VulkanRenderer::RenderQuad");

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

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

				material->Prepare();
				auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
				if (bindingSet)
					graphicsState.bindings[0] = bindingSet;

				renderCommandBuffer->RT_CommitGraphicsState();

				Buffer uniformStorageBuffer = material->GetUniformStorageBuffer();
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
		Renderer::Submit([renderCommandBuffer, pipeline, material, vertexBuffer, indexBuffer, transform, indexCount]() mutable
			{
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

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

				material->Prepare();
				auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
				// TODO(Yan): does 0 always exist?
				graphicsState.bindings[0] = bindingSet;

				renderCommandBuffer->RT_CommitGraphicsState();

				commandList->setPushConstants(&transform, sizeof(glm::mat4));

				nvrhi::DrawArguments drawArgs;
				drawArgs.vertexCount = indexCount;
				drawArgs.startIndexLocation = 0;
				drawArgs.startVertexLocation = 0;
				commandList->drawIndexed(drawArgs);
			});
	}

	void Renderer::ClearImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, nvrhi::Color clearColor, nvrhi::TextureSubresourceSet subresourceSet)
	{
		Renderer::Submit([renderCommandBuffer, image, clearColor, subresourceSet]() mutable
			{
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
				commandList->clearTextureFloat(image->GetHandle(), subresourceSet, clearColor);
			});
	}

	void Renderer::CopyImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> sourceImage, Ref<Image2D> destinationImage)
	{
		//s_RendererAPI->CopyImage(renderCommandBuffer, sourceImage, destinationImage);
	}

	void Renderer::BlitImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> sourceImage, Ref<Image2D> destinationImage)
	{
		SE_CORE_VERIFY(renderCommandBuffer);
		SE_CORE_VERIFY(sourceImage);
		SE_CORE_VERIFY(destinationImage);
		SE_CORE_VERIFY(sourceImage->GetHandle());
		SE_CORE_VERIFY(destinationImage->GetHandle());

		const auto& srcSpec = sourceImage->GetSpecification();
		const auto& dstSpec = destinationImage->GetSpecification();

		// Fast path: identical format + dimensions => 1:1 texture copy
		if (srcSpec.Format == dstSpec.Format && srcSpec.Width == dstSpec.Width && srcSpec.Height == dstSpec.Height)
		{
			Renderer::Submit([renderCommandBuffer, sourceImage, destinationImage]() mutable
				{
					nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

					// NVRHI copyTexture doesn't do implicit state transitions
					commandList->setTextureState(sourceImage->GetHandle(), nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource);
					commandList->setTextureState(destinationImage->GetHandle(), nvrhi::AllSubresources, nvrhi::ResourceStates::CopyDest);
					commandList->commitBarriers();

					nvrhi::TextureSlice srcSlice;
					srcSlice.setMipLevel(0).setArraySlice(0);
					nvrhi::TextureSlice dstSlice;
					dstSlice.setMipLevel(0).setArraySlice(0);

					commandList->copyTexture(destinationImage->GetHandle(), dstSlice, sourceImage->GetHandle(), srcSlice);

					// Put destination back into a readable state by default
					commandList->setTextureState(destinationImage->GetHandle(), nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
					commandList->commitBarriers();
				});

			return;
		}

		// General path: use compute shader to scale/copy (requires destination to be a storage image)
		SE_CORE_VERIFY(dstSpec.Usage == ImageUsage::Storage, "Renderer::BlitImage requires destination image Usage=Storage when scaling/copying via compute.");

		Ref<Shader> shader = Renderer::GetShaderLibrary()->Get(Utils::IsIntegerBased(dstSpec.Format) ? "LinearSampleUInt" : "LinearSample");
		SE_CORE_VERIFY(shader, "Renderer::BlitImage requires LinearSample shaders to be loaded");

		ComputePassSpecification spec;
		spec.DebugName = "Renderer::BlitImage";
		spec.Pipeline = PipelineCompute::Create(shader);
		Ref<ComputePass> computePass = ComputePass::Create(spec);

		ImageViewSpecification srcImageViewSpec;
		srcImageViewSpec.Image = sourceImage;
		srcImageViewSpec.Mip = 0;
		srcImageViewSpec.MipCount = 1;

		ImageViewSpecification dstImageViewSpec;
		dstImageViewSpec.Image = destinationImage;
		dstImageViewSpec.Mip = 0;
		dstImageViewSpec.MipCount = 1;

		Ref<ImageView> srcImageView = ImageView::Create(srcImageViewSpec);
		Ref<ImageView> dstImageView = ImageView::Create(dstImageViewSpec);

		Ref<Material> material = Material::Create(shader);
		material->Set("u_InputTexture", srcImageView);
		material->Set("o_OutputTexture", dstImageView);

		struct PushConstants
		{
			glm::vec2 TexelSize;
			int SourceMip;
		} pushConstants;

		pushConstants.TexelSize = { 1.0f / (float)dstSpec.Width, 1.0f / (float)dstSpec.Height };
		pushConstants.SourceMip = 0;

		const glm::uvec3 workGroups
		{
			glm::max(1u, (dstSpec.Width + 7u) / 8u),
			glm::max(1u, (dstSpec.Height + 7u) / 8u),
			1u
		};

		Renderer::Submit([renderCommandBuffer, sourceImage, destinationImage]() mutable
			{
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

				commandList->setTextureState(sourceImage->GetHandle(), nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
				commandList->setTextureState(destinationImage->GetHandle(), nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
				commandList->commitBarriers();
			});

		Renderer::BeginComputePass(renderCommandBuffer, computePass);
		Renderer::DispatchCompute(renderCommandBuffer, computePass, material, workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
		Renderer::EndComputePass(renderCommandBuffer, computePass);

		Renderer::Submit([renderCommandBuffer, destinationImage]() mutable
			{
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
				commandList->setTextureState(destinationImage->GetHandle(), nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
				commandList->commitBarriers();
			});
	}

	void Renderer::SubmitFullscreenQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material)
	{
		SE_CORE_VERIFY(renderCommandBuffer);
		SE_CORE_VERIFY(pipeline);

		Renderer::Submit([renderCommandBuffer, pipeline, material]() mutable
			{
				SE_PROFILE_FUNCTION("VulkanRenderer::SubmitFullscreenQuad");

				if (material == nullptr)
				{
					return;
				}

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

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

				material->Prepare();
				auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
				if (bindingSet)
					graphicsState.bindings[0] = bindingSet;

				renderCommandBuffer->RT_CommitGraphicsState();

				Buffer uniformStorageBuffer = material->GetUniformStorageBuffer();
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
		SE_CORE_VERIFY(renderCommandBuffer);
		SE_CORE_VERIFY(pipeline);

		Buffer vertexPushConstantBuffer;
		if (vertexShaderOverrides)
			vertexPushConstantBuffer = Buffer::Copy(vertexShaderOverrides);

		Buffer fragmentPushConstantBuffer;
		if (fragmentShaderOverrides)
			fragmentPushConstantBuffer = Buffer::Copy(fragmentShaderOverrides);

		Renderer::Submit([renderCommandBuffer, pipeline, material, vertexPushConstantBuffer, fragmentPushConstantBuffer]() mutable
			{
				SE_PROFILE_FUNCTION("VulkanRenderer::SubmitFullscreenQuadWithOverrides");

				if (material == nullptr)
				{
					vertexPushConstantBuffer.Release();
					fragmentPushConstantBuffer.Release();
					return;
				}

				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();

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

				material->Prepare();
				auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
				if (bindingSet)
					graphicsState.bindings[0] = bindingSet;

				renderCommandBuffer->RT_CommitGraphicsState();

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
		return s_Data->BlackCubeTexture;
	}


	Ref<Environment> Renderer::GetEmptyEnvironment()
	{
		return s_Data->EmptyEnvironment;
	}

	RenderCommandQueue& Renderer::GetRenderCommandQueue()
	{
		return *s_CommandQueue[s_RenderCommandQueueSubmissionIndex];
	}

	RenderCommandQueue& Renderer::GetRenderResourceReleaseQueue(uint32_t index)
	{
		return s_ResourceFreeQueue[index];
	}


	const std::unordered_map<std::string, std::string>& Renderer::GetGlobalShaderMacros()
	{
		return s_Data->GlobalShaderMacros;
	}

	RendererConfig& Renderer::GetConfig()
	{
		return s_Config;
	}

	void Renderer::SetConfig(const RendererConfig& config)
	{
		s_Config = config;
	}

	void Renderer::AcknowledgeParsedGlobalMacros(const std::unordered_set<std::string>& macros, Ref<Shader> shader)
	{
		for (const std::string& macro : macros)
		{
			s_GlobalShaderInfo.ShaderGlobalMacrosMap[macro][shader->GetHash()] = shader;
		}
	}

	void Renderer::SetMacroInShader(Ref<Shader> shader, const std::string& name, const std::string& value)
	{
		shader->SetMacro(name, value);
		s_GlobalShaderInfo.DirtyShaders.emplace(shader.Raw());
	}

	void Renderer::SetGlobalMacroInShaders(const std::string& name, const std::string& value)
	{
		if (s_Data->GlobalShaderMacros.find(name) != s_Data->GlobalShaderMacros.end())
		{
			if (s_Data->GlobalShaderMacros.at(name) == value)
				return;
		}

		s_Data->GlobalShaderMacros[name] = value;

		if (s_GlobalShaderInfo.ShaderGlobalMacrosMap.find(name) == s_GlobalShaderInfo.ShaderGlobalMacrosMap.end())
		{
			SE_CORE_WARN_TAG("Renderer", "No shaders with {} macro found", name);
			return;
		}

		SE_CORE_ASSERT(s_GlobalShaderInfo.ShaderGlobalMacrosMap.find(name) != s_GlobalShaderInfo.ShaderGlobalMacrosMap.end(), "Macro has not been passed from any shader!");
		for (auto& [hash, shader] : s_GlobalShaderInfo.ShaderGlobalMacrosMap.at(name))
		{
			SE_CORE_ASSERT(shader.IsValid(), "Shader is deleted!");
			s_GlobalShaderInfo.DirtyShaders.emplace(shader);
		}
	}

	bool Renderer::UpdateDirtyShaders()
	{
		// TODO(Yan): how is this going to work for dist?
		const bool updatedAnyShaders = s_GlobalShaderInfo.DirtyShaders.size();
		for (WeakRef<Shader> shader : s_GlobalShaderInfo.DirtyShaders)
		{
			SE_CORE_ASSERT(shader.IsValid(), "Shader is deleted!");
			shader->RT_Reload(true);
		}
		s_GlobalShaderInfo.DirtyShaders.clear();

		return updatedAnyShaders;
	}

	GPUMemoryStats Renderer::GetGPUMemoryStats()
	{
		return VulkanAllocator::GetStats();
	}

	Ref<Sampler> Renderer::GetClampSampler()
	{
		if (!s_RendererData->SamplerClamp)
			s_RendererData->SamplerClamp = Sampler::Create();

		return s_RendererData->SamplerClamp;
	}

	Ref<Sampler> Renderer::GetPointSampler()
	{
		if (!s_RendererData->SamplerPoint)
		{
			SamplerSpecification spec;
			spec.MinFilter = spec.MagFilter = spec.MipFilter = false;
			s_RendererData->SamplerPoint = Sampler::Create(spec);
		}

		return s_RendererData->SamplerPoint;
	}

}
