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

#include "Lux/Renderer/Image.h"
#include "Lux/Renderer/RenderCommandBuffer.h"

namespace nri {
	struct DescriptorPool;
	struct DescriptorSet;
	struct Pipeline;
	struct Texture;
	enum class Format : uint8_t;
}

namespace Lux
{
	class VulkanShader;
	class VulkanSwapChain;

	// --------------------------------------------------------------------
	// Texture info for ImGui rendering
	// --------------------------------------------------------------------

	struct ImGuiTextureInfo
	{
		// Keeps the image alive until the frame that draws it has been rendered: the draw snapshot
		// copies this struct.
		Ref<Image2D> Image;
		// Resolved: no AllMips / AllLayers.
		TextureSubresourceRange Range;
		bool ForceOpaque = false;
		bool IsGrayscale = false;

		bool operator==(const ImGuiTextureInfo& other) const
		{
			return Image.Raw() == other.Image.Raw()
				&& Range == other.Range
				&& ForceOpaque == other.ForceOpaque
				&& IsGrayscale == other.IsGrayscale;
		}
	};

	struct ImGuiTextureInfoHash
	{
		size_t operator()(const ImGuiTextureInfo& info) const
		{
			size_t h = std::hash<const void*>()(info.Image.Raw());
			h ^= std::hash<uint32_t>()(info.Range.BaseMip) << 1;
			h ^= std::hash<uint32_t>()(info.Range.MipCount) << 2;
			h ^= std::hash<uint32_t>()(info.Range.BaseLayer) << 3;
			h ^= std::hash<uint32_t>()(info.Range.LayerCount) << 4;
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

		// The images ImGui owns (font atlas etc.), serviced via the 1.92 texture system. Keyed by
		// persistent slot.
		std::unordered_map<uint32_t, Ref<Image2D>> ImGuiOwnedTextures;

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
		// Copies of the registry's tables; their Refs keep every drawn image alive.
		std::vector<ImGuiTextureInfo> m_PersistentTextures;
		std::vector<ImGuiTextureInfo> m_FrameTextures;
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
		~ImGuiRenderer();
		ImGuiRenderer(const ImGuiRenderer&) = delete;
		ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

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

		// Render thread. Draws the snapshot into the acquired back buffer and waits for its acquire.
		// clearTarget = false draws over whatever the target already holds, for a swapchain the
		// application has already rendered into (the runtime composites its overlay onto the game
		// frame). False, with only the acquire waited for, when the UI cannot be drawn (logged once).
		bool RenderToSwapchain(const std::shared_ptr<ImGuiDrawDataSnapshot>& snapshot, VulkanSwapChain* swapchain, bool clearTarget = true);
		float GetGPUTime() const;

		// Register a persistent texture (indices 0-63) - survives across frames.
		ImTextureID RegisterPersistentTexture(const Ref<Image2D>& image, TextureSubresourceRange subresources = AllSubresources);

		// Get a per-frame texture handle (indices 64+) - valid only for the
		// current frame.  Safe to call from any renderer that shares the registry.
		ImTextureID CreateFrameTexture(const Ref<Image2D>& image,
			TextureSubresourceRange subresources = AllSubresources,
			bool forceOpaque = false,
			bool isGrayscale = false);

		// Expose the registry so ImGuiLayer can share it with per-viewport renderers.
		std::shared_ptr<ImGuiTextureRegistry> GetRegistry() const { return m_Registry; }

	private:
		// Helpers for ProcessTextures(). Create/update uploads the full pixel buffer into an image
		// registered in the shared registry; destroy releases it and reclaims the slot.
		void CreateOrUpdateImGuiTexture(ImTextureData* tex);
		void DestroyImGuiTexture(ImTextureData* tex);

		struct SetKey
		{
			const nri::Texture* Texture = nullptr;
			TextureSubresourceRange Range;
			bool operator==(const SetKey&) const = default;
		};
		struct SetKeyHash
		{
			size_t operator()(const SetKey& key) const
			{
				return std::hash<const void*>()(key.Texture) ^ (std::hash<uint32_t>()(key.Range.BaseMip) << 1)
					^ (std::hash<uint32_t>()(key.Range.MipCount) << 2) ^ (std::hash<uint32_t>()(key.Range.BaseLayer) << 3);
			}
		};
		struct DescriptorPool
		{
			nri::DescriptorPool* Pool = nullptr;
			uint32_t Capacity = 0;
			uint32_t Used = 0;
		};
		// The resources of one frame slot: geometry written through Map, and descriptor pools reset
		// when the slot comes round again (its previous frame has retired by then). Render thread.
		struct FrameResources
		{
			NRIBuffer VertexBuffer;
			NRIBuffer IndexBuffer;
			std::vector<DescriptorPool> DescriptorPools;
			std::unordered_map<SetKey, nri::DescriptorSet*, SetKeyHash> DescriptorSets;
		};

		// Host-visible: written through Map.
		bool ReallocateBuffer(NRIBuffer& buffer, size_t requiredSize, size_t reallocateSize, bool isIndexBuffer);
		// One pipeline per target format. Null when NRI cannot make it (logged once per format).
		nri::Pipeline* GetOrCreatePipeline(nri::Format format);
		bool UpdateGeometry(FrameResources& frame, ImDrawData* drawData);
		// The set binding `texInfo`'s image and the sampler, allocated once per frame slot and image.
		// Null when it cannot be made (logged).
		nri::DescriptorSet* GetDescriptorSet(FrameResources& frame, const ImGuiTextureInfo& texInfo);
		nri::DescriptorSet* AllocateDescriptorSet(FrameResources& frame);

	private:
		// Shared across all ImGuiRenderer instances for this ImGui context.
		std::shared_ptr<ImGuiTextureRegistry> m_Registry;

		Ref<RenderCommandBuffer> m_RenderCommandBuffer;
		Ref<VulkanShader> m_Shader;
		Ref<Sampler> m_Sampler;
		uint32_t m_SetIndex = 0;
		uint32_t m_TextureRange = 0;
		uint32_t m_SamplerRange = 0;
		std::unordered_map<nri::Format, nri::Pipeline*> m_Pipelines;
		std::array<FrameResources, RendererConfig::MaxFramesInFlight> m_Frames;
		bool m_ReportedSkippedFrames = false;
	};
}
