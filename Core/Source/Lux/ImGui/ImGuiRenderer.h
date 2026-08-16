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

		// Persistent textures: indices 1 .. PersistentHandleCount-1
		// Never cleared, survive across frames (e.g. font atlas).
		std::vector<ImGuiTextureInfo> PersistentTextures;
		// Starts at 1: slot 0 is reserved because ImGui treats TexID 0 as
		// ImTextureID_Invalid (GetTexID() asserts tex_id != 0). If the font atlas —
		// the first ImGui-owned texture — were handed slot 0, SetTexID(0) would look
		// identical to "never uploaded" and trip that assert. Slot 0 stays empty and
		// resolves to the invalid texture (draw cmd skipped), matching the sentinel.
		uint32_t NextPersistentIndex = 1;

		// Persistent slots reclaimed by DestroyImGuiTexture, reused before growing NextPersistentIndex.
		// Lets ImGui's create/destroy of atlas textures recycle slots instead of exhausting the 64.
		std::vector<uint32_t> FreePersistentSlots;

		// GPU textures ImGui owns (font atlas etc.), serviced via the 1.92 texture system and kept
		// alive here — PersistentTextures stores only a non-owning pointer. Keyed by persistent slot.
		std::unordered_map<uint32_t, nvrhi::TextureHandle> ImGuiOwnedTextures;

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

	// Immutable CPU-side copy of an ImGui viewport's draw data and texture table.
	// ImGui owns the source lists only until the next NewFrame(), so render-thread
	// consumption must not retain ImGuiViewport::DrawData directly.
	class ImGuiDrawDataSnapshot
	{
	public:
		static std::shared_ptr<ImGuiDrawDataSnapshot> Create(
			const ImDrawData* drawData,
			const std::shared_ptr<ImGuiTextureRegistry>& registry);

		~ImGuiDrawDataSnapshot();

		ImDrawData* GetDrawData() { return &m_DrawData; }
		const ImGuiTextureInfo& ResolveTexture(uint64_t handle) const;

	private:
		ImGuiDrawDataSnapshot() = default;

		ImDrawData m_DrawData;
		std::vector<ImDrawList*> m_OwnedDrawLists;
		std::vector<ImGuiTextureInfo> m_PersistentTextures;
		std::vector<ImGuiTextureInfo> m_FrameTextures;
		std::vector<nvrhi::TextureHandle> m_TextureKeepAlives;
		uint32_t m_FrameCounter = 0;
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

		// Advertises this backend's capabilities (incl. RendererHasTextures). Call once per frame
		// before ImGui::NewFrame(). Despite the legacy name it no longer creates the font texture —
		// ImGui owns atlas textures now; see ProcessTextures().
		bool UpdateFontTexture();

		// Service ImGui-owned textures (create/update/destroy the font atlas etc.). Call once per
		// frame on the main thread AFTER ImGui::Render() and before snapshotting draw data.
		void ProcessTextures();

		bool Render(const std::shared_ptr<ImGuiDrawDataSnapshot>& snapshot, nvrhi::GraphicsPipelineHandle pipeline, nvrhi::FramebufferHandle framebuffer, VkSemaphore waitSemaphore = nullptr);
		bool RenderToSwapchain(const std::shared_ptr<ImGuiDrawDataSnapshot>& snapshot, VulkanSwapChain* swapchain);
		void BackbufferResizing();
		float GetGPUTime() const;

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

		// Helpers for ProcessTextures(). Create/update uploads the full pixel buffer into an nvrhi
		// texture registered in the shared registry; destroy releases it and reclaims the slot.
		void CreateOrUpdateImGuiTexture(ImTextureData* tex);
		void DestroyImGuiTexture(ImTextureData* tex);

	private:
		// Shared across all ImGuiRenderer instances for this ImGui context.
		std::shared_ptr<ImGuiTextureRegistry> m_Registry;

		Ref<RenderCommandBuffer> m_RenderCommandBuffer;

		nvrhi::ShaderHandle       m_VertexShader;
		nvrhi::ShaderHandle       m_PixelShader;
		nvrhi::InputLayoutHandle  m_ShaderAttribLayout;

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
