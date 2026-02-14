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

#include "Lux/Renderer/RenderCommandBuffer.h"

namespace Lux
{
	class VulkanSwapChain;

	// Texture info for ImGui rendering - stores texture and subresource info
	struct ImGuiTextureInfo
	{
		nvrhi::ITexture* Texture = nullptr;
		nvrhi::TextureSubresourceSet Subresources = nvrhi::AllSubresources;
		bool ForceOpaque = false;   // Bit 0: Force alpha to 1.0 for debug visualization
		bool IsGrayscale = false;   // Bit 1: Display single-channel as grayscale (for depth maps)

		bool operator==(const ImGuiTextureInfo& other) const
		{
			return Texture == other.Texture
				&& Subresources.baseMipLevel == other.Subresources.baseMipLevel
				&& Subresources.numMipLevels == other.Subresources.numMipLevels
				&& Subresources.baseArraySlice == other.Subresources.baseArraySlice
				&& Subresources.numArraySlices == other.Subresources.numArraySlices
				&& ForceOpaque == other.ForceOpaque
				&& IsGrayscale == other.IsGrayscale;
		}
	};

	struct ImGuiTextureInfoHash
	{
		size_t operator()(const ImGuiTextureInfo& info) const
		{
			size_t h = std::hash<void*>()(info.Texture);
			h ^= std::hash<uint32_t>()(info.Subresources.baseMipLevel) << 1;
			h ^= std::hash<uint32_t>()(info.Subresources.numMipLevels) << 2;
			h ^= std::hash<uint32_t>()(info.Subresources.baseArraySlice) << 3;
			h ^= std::hash<uint32_t>()(info.Subresources.numArraySlices) << 4;
			h ^= std::hash<bool>()(info.ForceOpaque) << 5;
			h ^= std::hash<bool>()(info.IsGrayscale) << 6;
			return h;
		}
	};

	class ImGuiRenderer
	{
	public:
		static constexpr uint32_t PersistentHandleCount = 64;

		ImGuiRenderer() = default;

		bool Init();
		bool UpdateFontTexture();
		bool Render(ImGuiViewport* viewport, nvrhi::GraphicsPipelineHandle pipeline, nvrhi::FramebufferHandle framebuffer, VkSemaphore waitSemaphore = nullptr);
		bool RenderToSwapchain(ImGuiViewport* viewport, VulkanSwapChain* swapchain);
		void BackbufferResizing();

		// Register a persistent texture (indices 0-63) - survives across frames
		ImTextureID RegisterPersistentTexture(nvrhi::ITexture* texture,
			nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources);

		// Get a per-frame texture handle (indices 64+) - valid only for current frame
		// forceOpaque: Force alpha to 1.0 for debug visualization
		// isGrayscale: Display single-channel textures as grayscale (for depth maps)
		ImTextureID CreateFrameTexture(nvrhi::ITexture* texture,
			nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources,
			bool forceOpaque = false,
			bool isGrayscale = false);

	private:
		bool ReallocateBuffer(nvrhi::BufferHandle& buffer, size_t requiredSize, size_t reallocateSize, bool isIndexBuffer);

		nvrhi::GraphicsPipelineHandle GetOrCreatePipeline(VulkanSwapChain* swapchain);

		nvrhi::IBindingSet* GetBindingSet(const ImGuiTextureInfo& texInfo);
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

		// Binding set cache keyed by texture+subresources
		std::unordered_map<ImGuiTextureInfo, nvrhi::BindingSetHandle, ImGuiTextureInfoHash> m_BindingsCache;

		// Persistent textures (indices 0-63) - not cleared per frame
		std::vector<ImGuiTextureInfo> m_PersistentTextures;
		uint32_t m_NextPersistentIndex = 0;

		// Per-frame textures (indices 64+) - cleared each frame
		std::vector<ImGuiTextureInfo> m_FrameTextures;
		std::unordered_map<ImGuiTextureInfo, uint64_t, ImGuiTextureInfoHash> m_FrameTextureMap;

		// Frame counter for stale handle detection (upper 32 bits of per-frame handles)
		uint32_t m_FrameCounter = 0;

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
