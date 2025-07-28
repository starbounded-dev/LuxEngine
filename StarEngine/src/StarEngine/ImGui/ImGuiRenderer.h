/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

/*
License for Dear ImGui

Copyright (c) 2014-2019 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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

		std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_BindingsCache;

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
