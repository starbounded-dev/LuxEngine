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

	// --------------------------------------------------------------------
	// Texture info for ImGui rendering
	// --------------------------------------------------------------------

	struct ImGuiTextureInfo
	{
		nvrhi::ITexture* Texture = nullptr;
		nvrhi::TextureSubresourceSet Subresources = nvrhi::AllSubresources;
		bool ForceOpaque = false;
		bool IsGrayscale = false;

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

	// --------------------------------------------------------------------
	// ImGuiTextureRegistry
	//
	// Holds ALL texture handle state: persistent slots (0-63) and per-frame
	// slots (64+).  This object is created once by the main ImGuiRenderer
	// and shared (via shared_ptr) with every per-viewport ImGuiRenderer so
	// that a handle registered on one renderer is valid on all of them.
	//
	// Ownership model:
	//   - ImGuiLayer creates the main ImGuiRenderer which creates the registry.
	//   - ImGuiRenderer_CreateWindow fetches the registry from the main renderer
	//     via ImGuiLayer and passes it to the new per-viewport renderer.
	//   - ImGuiLayer::Begin() calls NewFrame() on the registry exactly once
	//     per frame, before any rendering begins.
	// --------------------------------------------------------------------

	struct ImGuiTextureRegistry
	{
		static constexpr uint32_t PersistentHandleCount = 64;

		// Persistent textures: indices 0 .. PersistentHandleCount-1
		// Never cleared, survive across frames (e.g. font atlas).
		std::vector<ImGuiTextureInfo> PersistentTextures;
		uint32_t NextPersistentIndex = 0;

		// Per-frame textures: indices PersistentHandleCount .. N
		// Cleared at the start of each new frame by NewFrame().
		std::vector<ImGuiTextureInfo> FrameTextures;
		std::unordered_map<ImGuiTextureInfo, uint64_t, ImGuiTextureInfoHash> FrameTextureMap;

		// Upper 32 bits of every per-frame handle; used to detect stale handles.
		uint32_t FrameCounter = 0;

		// Call once per frame, BEFORE any rendering (e.g. from ImGuiLayer::Begin).
		// Clears per-frame slots and advances the frame counter.
		void NewFrame()
		{
			FrameTextures.clear();
			FrameTextureMap.clear();
			FrameCounter++;
		}
	};

	// --------------------------------------------------------------------
	// ImGuiRenderer
	// --------------------------------------------------------------------

	class ImGuiRenderer
	{
	public:
		static constexpr uint32_t PersistentHandleCount = ImGuiTextureRegistry::PersistentHandleCount;

		ImGuiRenderer() = default;

		// If sharedRegistry is null a fresh registry is created (main renderer).
		// Pass the main renderer's registry to all per-viewport renderers.
		bool Init(std::shared_ptr<ImGuiTextureRegistry> sharedRegistry = nullptr);

		bool UpdateFontTexture();
		bool Render(ImGuiViewport* viewport, nvrhi::GraphicsPipelineHandle pipeline, nvrhi::FramebufferHandle framebuffer, VkSemaphore waitSemaphore = nullptr);
		bool RenderToSwapchain(ImGuiViewport* viewport, VulkanSwapChain* swapchain);
		void BackbufferResizing();
		float GetGPUTime(uint32_t frameIndex) const;

		// Register a persistent texture (indices 0-63) - survives across frames.
		ImTextureID RegisterPersistentTexture(nvrhi::ITexture* texture,
			nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources);

		// Get a per-frame texture handle (indices 64+) - valid only for the
		// current frame.  Safe to call from any renderer that shares the registry.
		ImTextureID CreateFrameTexture(nvrhi::ITexture* texture,
			nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources,
			bool forceOpaque = false,
			bool isGrayscale = false);

		// Expose the registry so ImGuiLayer can share it with per-viewport renderers.
		std::shared_ptr<ImGuiTextureRegistry> GetRegistry() const { return m_Registry; }

	private:
		bool ReallocateBuffer(nvrhi::BufferHandle& buffer, size_t requiredSize, size_t reallocateSize, bool isIndexBuffer);
		nvrhi::GraphicsPipelineHandle GetOrCreatePipeline(VulkanSwapChain* swapchain);
		nvrhi::IBindingSet* GetBindingSet(const ImGuiTextureInfo& texInfo);
		bool UpdateGeometry(ImDrawData* drawData);

	private:
		// Shared across all ImGuiRenderer instances for this ImGui context.
		std::shared_ptr<ImGuiTextureRegistry> m_Registry;

		Ref<RenderCommandBuffer> m_RenderCommandBuffer;

		nvrhi::ShaderHandle       m_VertexShader;
		nvrhi::ShaderHandle       m_PixelShader;
		nvrhi::InputLayoutHandle  m_ShaderAttribLayout;

		nvrhi::TextureHandle      m_FontTexture;
		nvrhi::SamplerHandle      m_FontSampler;

		nvrhi::BufferHandle       m_VertexBuffer;
		nvrhi::BufferHandle       m_IndexBuffer;

		nvrhi::BindingLayoutHandle   m_BindingLayout;
		nvrhi::GraphicsPipelineDesc  m_BasePSODesc;

		// Binding set cache is per-renderer (device objects are global, this is
		// just a lookup optimisation and is cheap to rebuild per viewport).
		std::unordered_map<ImGuiTextureInfo, nvrhi::BindingSetHandle, ImGuiTextureInfoHash> m_BindingsCache;

		std::vector<ImDrawVert> m_VertexBufferData;
		std::vector<ImDrawIdx>  m_IndexBufferData;

		struct SwapchainPipelineCache
		{
			std::array<nvrhi::FramebufferHandle, 3> Framebuffers;
			std::array<nvrhi::GraphicsPipelineHandle, 3> Pipelines;
		};

		std::map<VulkanSwapChain*, SwapchainPipelineCache> m_PipelineCache;
	};
}
