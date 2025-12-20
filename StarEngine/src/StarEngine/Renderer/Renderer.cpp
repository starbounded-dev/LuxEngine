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
#include "StarEngine/Project/Project.h"
#include "IndexBuffer.h"
#include "ShaderDefs.h"


#include "nvrhi/nvrhi.h"

#if SE_HAS_SHADER_COMPILER
#include "StarEngine/Platform/Vulkan/ShaderCompiler/VulkanShaderCompiler.h"
#endif

#include <filesystem>
#include <format>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include "StarEngine/Platform/Vulkan/VulkanSwapChain.h"


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

	static RendererAPI* s_RendererAPI = nullptr;

	struct ShaderDependencies
	{
		//std::vector<Ref<PipelineCompute>> ComputePipelines;
		std::vector<Ref<Pipeline>> Pipelines;
		//std::vector<Ref<Material>> Materials;
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
		//Ref<TextureCube> BlackCubeTexture;
		//Ref<Environment> EmptyEnvironment;

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
		nvrhi::SamplerHandle SamplerClamp = nullptr;
		nvrhi::SamplerHandle SamplerPoint = nullptr;

		int32_t SelectedDrawCall = -1;
		int32_t DrawCallCount = 0;
	};

	static RendererData* s_RendererData = nullptr;
	/*
	void Renderer::RegisterShaderDependency(Ref<Shader> shader, Ref<PipelineCompute> computePipeline)
	{
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].ComputePipelines.push_back(computePipeline);
	}

	void Renderer::RegisterShaderDependency(Ref<Shader> shader, Ref<Material> material)
	{
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].Materials.push_back(material);
	*/

	void Renderer::RegisterShaderDependency(Ref<Shader> shader, Ref<Pipeline> pipeline)
	{
		std::scoped_lock lock(s_ShaderDependenciesMutex);
		s_ShaderDependencies[shader->GetHash()].Pipelines.push_back(pipeline);
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
		/*
		for (auto& computePipeline : dependencies.ComputePipelines)
		{
			computePipeline->CreatePipeline();
		}

		for (auto& material : dependencies.Materials)
		{
			material->OnShaderReloaded();
		}*/
	}
	
	uint32_t Renderer::RT_GetCurrentFrameIndex()
	{
		// Swapchain owns the Render Thread frame index
		return Application::Get().GetWindow().GetSwapChain().GetCurrentBufferIndex();
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
		s_CommandQueue[0] = snew RenderCommandQueue();
		s_CommandQueue[1] = snew RenderCommandQueue();

		// Make sure we don't have more frames in flight than swapchain images
		s_Config.FramesInFlight = glm::min<uint32_t>(s_Config.FramesInFlight, Application::Get().GetWindow().GetDeviceManager()->GetBackBufferCount());

		s_RendererAPI = InitRendererAPI();

		Renderer::SetGlobalMacroInShaders("__SE_REFLECTION_OCCLUSION_METHOD", "0");
		Renderer::SetGlobalMacroInShaders("__SE_AO_METHOD", std::format("{}", (int)ShaderDef::GetAOMethod(true)));
		Renderer::SetGlobalMacroInShaders("__SE_GTAO_COMPUTE_BENT_NORMALS", "0");

		s_Data->m_ShaderLibrary = Ref<ShaderLibrary>::Create();

		if (!s_Config.ShaderPackPath.empty())
			Renderer::GetShaderLibrary()->LoadShaderPack(s_Config.ShaderPackPath);


		// NOTE: some shaders (compute) need to have optimization disabled because of a shaderc internal error
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
		spec.Format = ImageFormat::RGBA;
		spec.Width = 1;
		spec.Height = 1;
		s_Data->WhiteTexture = Texture2D::Create(spec, Buffer(&whiteTextureData, sizeof(uint32_t)));

		constexpr uint32_t blackTextureData = 0xff000000;
		s_Data->BlackTexture = Texture2D::Create(spec, Buffer(&blackTextureData, sizeof(uint32_t)));

		{
			TextureSpecification spec;
			spec.SamplerWrap = TextureWrap::Clamp;
			s_Data->BRDFLutTexture = Texture2D::Create(spec, std::filesystem::path("Resources/Renderer/BRDF_LUT.png"));
		}
		/*
		constexpr uint32_t blackCubeTextureData[6] = { 0xff000000, 0xff000000, 0xff000000, 0xff000000, 0xff000000, 0xff000000 };
		s_Data->BlackCubeTexture = TextureCube::Create(spec, Buffer(blackCubeTextureData, sizeof(blackCubeTextureData)));

		s_Data->EmptyEnvironment = Ref<Environment>::Create(s_Data->BlackCubeTexture, s_Data->BlackCubeTexture);*/
		
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
		s_RendererData = snew RendererData();
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

#if OLD
		// Create descriptor pools
		Renderer::Submit([]() mutable
			{
				// Create Descriptor Pool
				VkDescriptorPoolSize pool_sizes[] =
				{
					{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
					{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
					{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
					{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
					{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
					{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
					{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
					{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
					{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
					{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
					{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
				};
				VkDescriptorPoolCreateInfo pool_info = {};
				pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
				pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
				pool_info.maxSets = 100000;
				pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
				pool_info.pPoolSizes = pool_sizes;
				VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();
				uint32_t framesInFlight = Renderer::GetConfig().FramesInFlight;
				for (uint32_t i = 0; i < framesInFlight; i++)
				{
					VK_CHECK_RESULT(vkCreateDescriptorPool(device, &pool_info, nullptr, &s_VulkanRendererData->DescriptorPools[i]));
					s_VulkanRendererData->DescriptorPoolAllocationCount[i] = 0;
				}

				VK_CHECK_RESULT(vkCreateDescriptorPool(device, &pool_info, nullptr, &s_VulkanRendererData->MaterialDescriptorPool));
			});
#endif

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
		quadVertexData[0].TexCoord = glm::vec2(0, 0);

		quadVertexData[1].Position = glm::vec3(x + width, y, 0.0f);
		quadVertexData[1].TexCoord = glm::vec2(1, 0);

		quadVertexData[2].Position = glm::vec3(x + width, y + height, 0.0f);
		quadVertexData[2].TexCoord = glm::vec2(1, 1);

		quadVertexData[3].Position = glm::vec3(x, y + height, 0.0f);
		quadVertexData[3].TexCoord = glm::vec2(0, 1);

		s_RendererData->QuadVertexBuffer = VertexBuffer::Create(Buffer(quadVertexData.data(), quadVertexData.size()));

		std::array<uint32_t, 6> indices = { 0, 1, 2, 2, 3, 0, };
		s_RendererData->QuadIndexBuffer = IndexBuffer::Create(Buffer{ indices.data(), indices.size() });

		//s_RendererData->BRDFLut = Renderer::GetBRDFLutTexture();
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
	/*
	void Renderer::BeginRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<RenderPass> renderPass, bool explicitClear)
	{
#if TODO
		HZ_CORE_ASSERT(renderPass, "RenderPass cannot be null!");

		Renderer::Submit([renderCommandBuffer, renderPass, explicitClear]()
			{
				HZ_PROFILE_SCOPE_DYNAMIC(std::format("VulkanRenderer::BeginRenderPass ({})", renderPass->GetSpecification().DebugName).c_str());
				HZ_CORE_TRACE_TAG("Renderer", "BeginRenderPass - {}", renderPass->GetSpecification().DebugName);

				uint32_t frameIndex = Renderer::RT_GetCurrentFrameIndex();


				VkCommandBuffer commandBuffer = renderCommandBuffer.As<VulkanRenderCommandBuffer>()->GetActiveCommandBuffer();

#if DEBUG
				VkDebugUtilsLabelEXT debugLabel{};
				debugLabel.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
				memcpy(&debugLabel.color, glm::value_ptr(renderPass->GetSpecification().MarkerColor), sizeof(float) * 4);
				debugLabel.pLabelName = renderPass->GetSpecification().DebugName.c_str();
				fpCmdBeginDebugUtilsLabelEXT(commandBuffer, &debugLabel);
#endif

				auto fb = renderPass->GetSpecification().Pipeline->GetSpecification().TargetFramebuffer;
				Ref<VulkanFramebuffer> framebuffer = fb.As<VulkanFramebuffer>();
				const auto& fbSpec = framebuffer->GetSpecification();

				uint32_t width = framebuffer->GetWidth();
				uint32_t height = framebuffer->GetHeight();

				VkViewport viewport = {};
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;

				VkRenderPassBeginInfo renderPassBeginInfo = {};
				renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				renderPassBeginInfo.pNext = nullptr;
				renderPassBeginInfo.renderPass = framebuffer->GetRenderPass();
				renderPassBeginInfo.renderArea.offset.x = 0;
				renderPassBeginInfo.renderArea.offset.y = 0;
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				if (framebuffer->GetSpecification().SwapChainTarget)
				{
					VulkanSwapChain& swapChain = Application::Get().GetWindow().GetSwapChain();
					width = swapChain.GetWidth();
					height = swapChain.GetHeight();
					renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
					renderPassBeginInfo.pNext = nullptr;
					renderPassBeginInfo.renderPass = framebuffer->GetRenderPass();
					renderPassBeginInfo.renderArea.offset.x = 0;
					renderPassBeginInfo.renderArea.offset.y = 0;
					renderPassBeginInfo.renderArea.extent.width = width;
					renderPassBeginInfo.renderArea.extent.height = height;
					renderPassBeginInfo.framebuffer = swapChain.GetCurrentFramebuffer();

					viewport.x = 0.0f;
					viewport.y = (float)height;
					viewport.width = (float)width;
					viewport.height = -(float)height;
				}
				else
				{
					width = framebuffer->GetWidth();
					height = framebuffer->GetHeight();
					renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
					renderPassBeginInfo.pNext = nullptr;
					renderPassBeginInfo.renderPass = framebuffer->GetRenderPass();
					renderPassBeginInfo.renderArea.offset.x = 0;
					renderPassBeginInfo.renderArea.offset.y = 0;
					renderPassBeginInfo.renderArea.extent.width = width;
					renderPassBeginInfo.renderArea.extent.height = height;
					renderPassBeginInfo.framebuffer = framebuffer->GetVulkanFramebuffer();

					viewport.x = 0.0f;
					viewport.y = 0.0f;
					viewport.width = (float)width;
					viewport.height = (float)height;
				}

				// TODO: Does our framebuffer have a depth attachment?
				const auto& clearValues = framebuffer->GetVulkanClearValues();
				renderPassBeginInfo.clearValueCount = (uint32_t)clearValues.size();
				renderPassBeginInfo.pClearValues = clearValues.data();

				vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				if (explicitClear)
				{
					const uint32_t colorAttachmentCount = (uint32_t)framebuffer->GetColorAttachmentCount();
					const uint32_t totalAttachmentCount = colorAttachmentCount + (framebuffer->HasDepthAttachment() ? 1 : 0);
					HZ_CORE_ASSERT(clearValues.size() == totalAttachmentCount);

					std::vector<VkClearAttachment> attachments(totalAttachmentCount);
					std::vector<VkClearRect> clearRects(totalAttachmentCount);
					for (uint32_t i = 0; i < colorAttachmentCount; i++)
					{
						attachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						attachments[i].colorAttachment = i;
						attachments[i].clearValue = clearValues[i];

						clearRects[i].rect.offset = { (int32_t)0, (int32_t)0 };
						clearRects[i].rect.extent = { width, height };
						clearRects[i].baseArrayLayer = 0;
						clearRects[i].layerCount = 1;
					}

					if (framebuffer->HasDepthAttachment())
					{
						attachments[colorAttachmentCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
						attachments[colorAttachmentCount].clearValue = clearValues[colorAttachmentCount];
						clearRects[colorAttachmentCount].rect.offset = { (int32_t)0, (int32_t)0 };
						clearRects[colorAttachmentCount].rect.extent = { width, height };
						clearRects[colorAttachmentCount].baseArrayLayer = 0;
						clearRects[colorAttachmentCount].layerCount = 1;
					}

					vkCmdClearAttachments(commandBuffer, totalAttachmentCount, attachments.data(), totalAttachmentCount, clearRects.data());

				}

				// Update dynamic viewport state
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

				// Update dynamic scissor state
				VkRect2D scissor = {};
				scissor.extent.width = width;
				scissor.extent.height = height;
				scissor.offset.x = 0;
				scissor.offset.y = 0;
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

				// TODO: automatic layout transitions for input resources

				// Bind Vulkan Pipeline
				Ref<VulkanPipeline> vulkanPipeline = renderPass->GetSpecification().Pipeline.As<VulkanPipeline>();
				VkPipeline vPipeline = vulkanPipeline->GetVulkanPipeline();
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vPipeline);

				if (vulkanPipeline->IsDynamicLineWidth())
					vkCmdSetLineWidth(commandBuffer, vulkanPipeline->GetSpecification().LineWidth);

				// Bind input descriptors (starting from set 1, set 0 is for per-draw)
				Ref<VulkanRenderPass> vulkanRenderPass = renderPass.As<VulkanRenderPass>();
				vulkanRenderPass->Prepare();
				if (vulkanRenderPass->HasDescriptorSets())
				{
					const auto& descriptorSets = vulkanRenderPass->GetDescriptorSets(frameIndex);
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipelineLayout(), vulkanRenderPass->GetFirstSetIndex(), (uint32_t)descriptorSets.size(), descriptorSets.data(), 0, nullptr);
				}
			});
#endif
	}

	void Renderer::EndRenderPass(Ref<RenderCommandBuffer> renderCommandBuffer)
	{
#if TODO
		Renderer::Submit([renderCommandBuffer]()
			{
				HZ_PROFILE_FUNC("VulkanRenderer::EndRenderPass");

				uint32_t frameIndex = Renderer::RT_GetCurrentFrameIndex();
				VkCommandBuffer commandBuffer = renderCommandBuffer.As<VulkanRenderCommandBuffer>()->GetActiveCommandBuffer();

				vkCmdEndRenderPass(commandBuffer);
				fpCmdEndDebugUtilsLabelEXT(commandBuffer);
			});
#endif
	}*/
	/*
	void Renderer::BeginComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass)
	{
		SE_CORE_ASSERT(computePass, "ComputePass cannot be null!");

		//s_RendererAPI->BeginComputePass(renderCommandBuffer, computePass);
	}

	void Renderer::EndComputePass(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass)
	{
		//s_RendererAPI->EndComputePass(renderCommandBuffer, computePass);
	}*/
	/*
	void Renderer::DispatchCompute(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass, Ref<Material> material, const glm::uvec3& workGroups, Buffer constants)
	{
		//s_RendererAPI->DispatchCompute(renderCommandBuffer, computePass, material, workGroups, constants);
	}*/
	/*
	void Renderer::InsertGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& color)
	{
		//s_RendererAPI->InsertGPUPerfMarker(renderCommandBuffer, label, color);
	}*/

	void Renderer::BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& markerColor)
	{
		//s_RendererAPI->BeginGPUPerfMarker(renderCommandBuffer, label, markerColor);
	}

	void Renderer::EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer)
	{
		//s_RendererAPI->EndGPUPerfMarker(renderCommandBuffer);
	}
	/*
	void Renderer::RT_InsertGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& color)
	{
		//s_RendererAPI->RT_InsertGPUPerfMarker(renderCommandBuffer, label, color);
	}*/

	void Renderer::RT_BeginGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer, const std::string& label, const glm::vec4& markerColor)
	{
		//s_RendererAPI->RT_BeginGPUPerfMarker(renderCommandBuffer, label, markerColor);
	}

	void Renderer::RT_EndGPUPerfMarker(Ref<RenderCommandBuffer> renderCommandBuffer)
	{
		//s_RendererAPI->RT_EndGPUPerfMarker(renderCommandBuffer);
	}

	void Renderer::BeginFrame()
	{
		//s_RendererAPI->BeginFrame();
	}

	void Renderer::EndFrame()
	{
		//s_RendererAPI->EndFrame();
	}
	/*
	void Renderer::SetSceneEnvironment(Ref<SceneRenderer> sceneRenderer, Ref<Environment> environment, Ref<Image2D> shadow, Ref<Image2D> spotShadow)
	{
		//s_RendererAPI->SetSceneEnvironment(sceneRenderer, environment, shadow, spotShadow);
	}

	std::pair<Ref<TextureCube>, Ref<TextureCube>> Renderer::CreateEnvironmentMap(const std::string& filepath)
	{
		return { nullptr, nullptr }; // return s_RendererAPI->CreateEnvironmentMap(filepath);
	}

	void Renderer::LightCulling(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<ComputePass> computePass, Ref<Material> material, const glm::uvec3& workGroups)
	{
		//s_RendererAPI->LightCulling(renderCommandBuffer, computePass, material, workGroups);
	}

	Ref<TextureCube> Renderer::CreatePreethamSky(float turbidity, float azimuth, float inclination)
	{
		return nullptr; // return s_RendererAPI->CreatePreethamSky(turbidity, azimuth, inclination);
	}*/
#if 0
	void Renderer::RenderStaticMesh(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<RenderPass> renderPass, Ref<StaticMesh> mesh, Ref<MeshSource> meshSource, uint32_t submeshIndex, Ref<MaterialTable> materialTable, Ref<VertexBuffer> transformBuffer, uint32_t transformOffset, uint32_t instanceCount)
	{
		HZ_CORE_VERIFY(mesh);
		HZ_CORE_VERIFY(meshSource);
		HZ_CORE_VERIFY(materialTable);

		Renderer::Submit([renderCommandBuffer, renderPass, mesh, meshSource, submeshIndex, materialTable = Ref<MaterialTable>::Create(materialTable), transformBuffer, transformOffset, instanceCount]() mutable
			{
				HZ_PROFILE_FUNC("VulkanRenderer::RenderMesh");
				HZ_SCOPE_PERF("VulkanRenderer::RenderMesh");

				if (s_RendererData->SelectedDrawCall != -1 && s_RendererData->DrawCallCount > s_RendererData->SelectedDrawCall)
					return;

				uint32_t rtWidth = renderPass->GetTargetFramebuffer()->GetWidth();
				uint32_t rtHeight = renderPass->GetTargetFramebuffer()->GetHeight();

				uint32_t frameIndex = Renderer::RT_GetCurrentFrameIndex();
				VkCommandBuffer commandBuffer = renderCommandBuffer.As<VulkanRenderCommandBuffer>()->GetActiveCommandBuffer();
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetHandle();

				nvrhi::GraphicsState graphicsState = nvrhi::GraphicsState();
				graphicsState.pipeline = renderPass->GetPipeline()->GetHandle();
				graphicsState.framebuffer = renderPass->GetTargetFramebuffer()->GetHandle();
				graphicsState.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(rtWidth, rtHeight)));

				// Geometry Vertex Buffer
				nvrhi::VertexBufferBinding vbb = nvrhi::VertexBufferBinding()
					.setBuffer(meshSource->GetVertexBuffer()->GetHandle())
					.setSlot(0)
					.setOffset(0);
				vbb.buffer = meshSource->GetVertexBuffer()->GetHandle();
				graphicsState.addVertexBuffer(vbb);

				// Transform Vertex Buffer
				nvrhi::VertexBufferBinding transformvbb = nvrhi::VertexBufferBinding()
					.setBuffer(transformBuffer->GetHandle())
					.setSlot(1)
					.setOffset(transformOffset);
				vbb.buffer = meshSource->GetVertexBuffer()->GetHandle();
				graphicsState.addVertexBuffer(transformvbb);

				// Index Buffer
				nvrhi::IndexBufferBinding ibb = nvrhi::IndexBufferBinding()
					.setBuffer(meshSource->GetIndexBuffer()->GetHandle())
					.setFormat(nvrhi::Format::R32_UINT)
					.setOffset(0);
				graphicsState.setIndexBuffer(ibb);

				// TODO: graphicsState.addBindingSet();

				commandList->setGraphicsState(graphicsState);

				const auto& submeshes = meshSource->GetSubmeshes();
				const Submesh& submesh = submeshes[submeshIndex];
				Ref<MaterialTable> meshMaterialTable = mesh->GetMaterials();
				uint32_t materialCount = meshMaterialTable->GetMaterialCount();

#if TODO
				// NOTE(Yan): probably should not involve Asset Manager at this stage
				AssetHandle materialHandle = materialTable->HasMaterial(submesh.MaterialIndex) ? materialTable->GetMaterial(submesh.MaterialIndex) : meshMaterialTable->GetMaterial(submesh.MaterialIndex);
				Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(materialHandle);
				Ref<VulkanMaterial> vulkanMaterial = material->GetMaterial().As<VulkanMaterial>();

				if (s_Data->SelectedDrawCall != -1 && s_Data->DrawCallCount > s_Data->SelectedDrawCall)
					return;

				VkPipelineLayout layout = vulkanPipeline->GetVulkanPipelineLayout();
				VkDescriptorSet descriptorSet = vulkanMaterial->GetDescriptorSet(frameIndex);
				if (descriptorSet)
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptorSet, 0, nullptr);

				Buffer uniformStorageBuffer = vulkanMaterial->GetUniformStorageBuffer();
				commandList->setPushConstants(uniformStorageBuffer.Data, uniformStorageBuffer.Size);
				//vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, (uint32_t)uniformStorageBuffer.Size, uniformStorageBuffer.Data);
#endif

				nvrhi::DrawArguments drawArgs = nvrhi::DrawArguments();
				drawArgs.vertexCount = submesh.IndexCount;
				drawArgs.instanceCount = instanceCount;
				drawArgs.startIndexLocation = submesh.BaseIndex;
				drawArgs.startVertexLocation = submesh.BaseVertex;
				commandList->drawIndexed(drawArgs);

				// vkCmdDrawIndexed(commandBuffer, submesh.IndexCount, instanceCount, submesh.BaseIndex, submesh.BaseVertex, 0);
				s_Data->DrawCallCount++;
			});
	}

	void Renderer::RenderSubmesh(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<UniformBufferSet> uniformBufferSet, Ref<StorageBufferSet> storageBufferSet, Ref<Mesh> mesh, uint32_t submeshIndex, Ref<MaterialTable> materialTable, const glm::mat4& transform)
	{
		s_RendererAPI->RenderSubmesh(renderCommandBuffer, pipeline, uniformBufferSet, storageBufferSet, mesh, submeshIndex, materialTable, transform);
	}


	void Renderer::RenderSubmeshInstanced(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Mesh> mesh, Ref<MeshSource> meshSource, uint32_t submeshIndex, Ref<MaterialTable> materialTable, Ref<VertexBuffer> transformBuffer, uint32_t transformOffset, uint32_t boneTransformsOffset, uint32_t boneTransformsStride, uint32_t instanceCount)
	{
		//s_RendererAPI->RenderSubmeshInstanced(renderCommandBuffer, pipeline, mesh, meshSource, submeshIndex, materialTable, transformBuffer, transformOffset, boneTransformsOffset, boneTransformsStride, instanceCount);
	}

	void Renderer::RenderMeshWithMaterial(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Mesh> mesh, Ref<MeshSource> meshSource, uint32_t submeshIndex, Ref<VertexBuffer> transformBuffer, uint32_t transformOffset, uint32_t boneTransformsOffset, uint32_t boneTransformsStride, uint32_t instanceCount, Ref<Material> material, Buffer additionalUniforms)
	{
		//s_RendererAPI->RenderMeshWithMaterial(renderCommandBuffer, pipeline, mesh, meshSource, submeshIndex, material, transformBuffer, transformOffset, boneTransformsOffset, boneTransformsStride, instanceCount, additionalUniforms);
	}

	void Renderer::RenderStaticMeshWithMaterial(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<StaticMesh> mesh, Ref<MeshSource> meshSource, uint32_t submeshIndex, Ref<VertexBuffer> transformBuffer, uint32_t transformOffset, uint32_t instanceCount, Ref<Material> material, Buffer additionalUniforms)
	{
		//s_RendererAPI->RenderStaticMeshWithMaterial(renderCommandBuffer, pipeline, mesh, meshSource, submeshIndex, material, transformBuffer, transformOffset, instanceCount, additionalUniforms);
	}

	void Renderer::RenderQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, const glm::mat4& transform)
	{
		//s_RendererAPI->RenderQuad(renderCommandBuffer, pipeline, material, transform);
	}

	void Renderer::RenderGeometry(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, Ref<VertexBuffer> vertexBuffer, Ref<IndexBuffer> indexBuffer, const glm::mat4& transform, uint32_t indexCount /*= 0*/)
	{
		//s_RendererAPI->RenderGeometry(renderCommandBuffer, pipeline, material, vertexBuffer, indexBuffer, transform, indexCount);
	}

	void Renderer::SubmitQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Material> material, const glm::mat4& transform)
	{
		HZ_CORE_ASSERT(false, "Not Implemented");
		/*bool depthTest = true;
		if (material)
		{
				material->Bind();
				depthTest = material->GetFlag(MaterialFlag::DepthTest);
				cullFace = !material->GetFlag(MaterialFlag::TwoSided);

				auto shader = material->GetShader();
				shader->SetUniformBuffer("Transform", &transform, sizeof(glm::mat4));
		}

		s_Data->m_FullscreenQuadVertexBuffer->Bind();
		s_Data->m_FullscreenQuadPipeline->Bind();
		s_Data->m_FullscreenQuadIndexBuffer->Bind();
		Renderer::DrawIndexed(6, PrimitiveType::Triangles, depthTest);*/
	}

	void Renderer::ClearImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, const ImageClearValue& clearValue, ImageSubresourceRange subresourceRange)
	{
		//s_RendererAPI->ClearImage(renderCommandBuffer, image, clearValue, subresourceRange);
	}

	void Renderer::CopyImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> sourceImage, Ref<Image2D> destinationImage)
	{
		//s_RendererAPI->CopyImage(renderCommandBuffer, sourceImage, destinationImage);
	}

	void Renderer::BlitImage(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> sourceImage, Ref<Image2D> destinationImage)
	{
		//s_RendererAPI->BlitImage(renderCommandBuffer, sourceImage, destinationImage);
	}

	void Renderer::SubmitFullscreenQuad(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material)
	{
		//s_RendererAPI->SubmitFullscreenQuad(renderCommandBuffer, pipeline, material);
	}

	void Renderer::SubmitFullscreenQuadWithOverrides(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Pipeline> pipeline, Ref<Material> material, Buffer vertexShaderOverrides, Buffer fragmentShaderOverrides)
	{
		//s_RendererAPI->SubmitFullscreenQuadWithOverrides(renderCommandBuffer, pipeline, material, vertexShaderOverrides, fragmentShaderOverrides);
	}
#endif
#if 0
	void Renderer::SubmitFullscreenQuad(Ref<Material> material)
	{
		// Retrieve pipeline from cache
		auto& shader = material->GetShader();
		auto hash = shader->GetHash();
		if (s_PipelineCache.find(hash) == s_PipelineCache.end())
		{
			// Create pipeline
			PipelineSpecification spec = s_Data->m_FullscreenQuadPipelineSpec;
			spec.Shader = shader;
			spec.DebugName = "Renderer-FullscreenQuad-" + shader->GetName();
			s_PipelineCache[hash] = Pipeline::Create(spec);
		}

		auto& pipeline = s_PipelineCache[hash];

		bool depthTest = true;
		bool cullFace = true;
		if (material)
		{
			// material->Bind();
			depthTest = material->GetFlag(MaterialFlag::DepthTest);
			cullFace = !material->GetFlag(MaterialFlag::TwoSided);
		}

		s_Data->FullscreenQuadVertexBuffer->Bind();
		pipeline->Bind();
		s_Data->FullscreenQuadIndexBuffer->Bind();
		Renderer::DrawIndexed(6, PrimitiveType::Triangles, depthTest);
	}
#endif
	
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
	}/*

	Ref<TextureCube> Renderer::GetBlackCubeTexture()
	{
		return s_Data->BlackCubeTexture;
	}


	Ref<Environment> Renderer::GetEmptyEnvironment()
	{
		return s_Data->EmptyEnvironment;
	}*/

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
	/*
	nvrhi::SamplerHandle Renderer::GetClampSampler()
	{
		if (s_RendererData->SamplerClamp)
			return s_RendererData->SamplerClamp;

		nvrhi::SamplerDesc samplerDesc;
		samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = true;
		samplerDesc.addressU = nvrhi::SamplerAddressMode::ClampToEdge;
		samplerDesc.addressV = samplerDesc.addressW = samplerDesc.addressU;

		s_RendererData->SamplerPoint = Application::GetGraphicsDevice()->createSampler(samplerDesc);
	}

	nvrhi::SamplerHandle Renderer::GetPointSampler()
	{
		if (s_RendererData->SamplerPoint)
			return s_RendererData->SamplerPoint;

		nvrhi::SamplerDesc samplerDesc;
		samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = false;
		samplerDesc.addressU = nvrhi::SamplerAddressMode::ClampToEdge;
		samplerDesc.addressV = samplerDesc.addressW = samplerDesc.addressU;

		s_RendererData->SamplerPoint = Application::GetGraphicsDevice()->createSampler(samplerDesc);
	}*/

}
