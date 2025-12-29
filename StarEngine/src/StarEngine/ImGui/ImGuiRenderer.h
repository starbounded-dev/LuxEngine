#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <stdint.h>

#include "nvrhi/nvrhi.h"

#include <imgui.h>

#include "StarEngine/Renderer/RenderCommandBuffer.h"

namespace StarEngine
{
	class VulkanSwapChain;

	class ImGuiRenderer
	{
	public:
		ImGuiRenderer() = default;

		bool Init();
		bool UpdateFontTexture();
		bool Render(ImGuiViewport* viewport, nvrhi::GraphicsPipelineHandle pipeline, nvrhi::FramebufferHandle framebuffer, vk::Semaphore waitSemaphore = nullptr);
		bool RenderToSwapchain(ImGuiViewport* viewport, VulkanSwapChain* swapchain);
		void BackbufferResizing();
	private:
		bool ReallocateBuffer(nvrhi::BufferHandle& buffer, size_t requiredSize, size_t reallocateSize, bool isIndexBuffer);

		nvrhi::GraphicsPipelineHandle GetOrCreatePipeline(VulkanSwapChain* swapchain);

		nvrhi::IBindingSet* GetBindingSet(nvrhi::ITexture* texture);
		void PruneBindingsCache();
		bool UpdateGeometry(ImDrawData* drawData);
	private:
		Ref<RenderCommandBuffer> m_RenderCommandBuffer;

		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;
		nvrhi::InputLayoutHandle m_ShaderAttribLayout;

		nvrhi::TextureHandle m_FontTexture;
		nvrhi::SamplerHandle m_FontSampler;

		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::GraphicsPipelineDesc m_BasePSODesc;

		struct CachedBindingSet
		{
			nvrhi::BindingSetHandle Binding;
			uint64_t LastUsedFrame = 0;
		};

		std::unordered_map<nvrhi::ITexture*, CachedBindingSet> m_BindingsCache;
		uint64_t m_BindingsCacheFrameCounter = 0;

		std::vector<ImDrawVert> m_VertexBufferData;
		std::vector<ImDrawIdx> m_IndexBufferData;


		struct SwapchainPipelineCache
		{
			std::array<nvrhi::FramebufferHandle, 3> Framebuffers;
			std::array<nvrhi::GraphicsPipelineHandle, 3> Pipelines;
		};

		std::map<VulkanSwapChain*, SwapchainPipelineCache> m_PipelineCache;

	};
}
