/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
* (license header omitted for brevity - keep original)
*/
#include "lpch.h"
#include "ImGuiRenderer.h"

#include "Lux/Renderer/Shader.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Debug/Profiler.h"

#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

#include "nvrhi/utils.h"

#include <format>

namespace Lux {

	std::shared_ptr<ImGuiDrawDataSnapshot> ImGuiDrawDataSnapshot::Create(
		const ImDrawData* drawData,
		const std::shared_ptr<ImGuiTextureRegistry>& registry)
	{
		if (!drawData || !drawData->Valid || !registry)
			return nullptr;

		auto snapshot = std::shared_ptr<ImGuiDrawDataSnapshot>(new ImGuiDrawDataSnapshot());
		snapshot->m_DrawData.Valid = true;
		snapshot->m_DrawData.DisplayPos = drawData->DisplayPos;
		snapshot->m_DrawData.DisplaySize = drawData->DisplaySize;
		snapshot->m_DrawData.FramebufferScale = drawData->FramebufferScale;
		snapshot->m_DrawData.OwnerViewport = nullptr;
		snapshot->m_DrawData.Textures = nullptr;

		snapshot->m_OwnedDrawLists.reserve(drawData->CmdListsCount);
		for (const ImDrawList* drawList : drawData->CmdLists)
		{
			ImDrawList* clonedDrawList = drawList->CloneOutput();

			// CloneOutput() copies the finished Cmd/Idx/Vtx buffers but leaves the
			// transient build pointers (_VtxWritePtr/_IdxWritePtr/_VtxCurrentIdx) at
			// their IM_NEW defaults (null/0). AddDrawList() runs a draw-list integrity
			// assert that expects those pointers to sit at the end of the buffers, so
			// without this fix-up it aborts in Debug (IM_ASSERT) every frame. The
			// cloned buffers are complete, so point the write cursors at their ends.
			clonedDrawList->_VtxWritePtr = clonedDrawList->VtxBuffer.Data + clonedDrawList->VtxBuffer.Size;
			clonedDrawList->_IdxWritePtr = clonedDrawList->IdxBuffer.Data + clonedDrawList->IdxBuffer.Size;
			clonedDrawList->_VtxCurrentIdx = (unsigned int)clonedDrawList->VtxBuffer.Size;

			snapshot->m_OwnedDrawLists.push_back(clonedDrawList);
			snapshot->m_DrawData.AddDrawList(clonedDrawList);
		}

		// The registry is rebuilt every main-thread ImGui frame. Copy it with the draw
		// lists so the render thread never races the next frame's NewFrame()/Image calls.
		snapshot->m_PersistentTextures = registry->PersistentTextures;
		snapshot->m_FrameTextures = registry->FrameTextures;
		snapshot->m_FrameCounter = registry->FrameCounter;

		// ImGuiTextureInfo intentionally stores a non-owning pointer. The source UI can
		// release an icon or viewport texture before this snapshot reaches the render
		// thread, so retain the underlying NVRHI resources for the snapshot lifetime.
		snapshot->m_TextureKeepAlives.reserve(
			snapshot->m_PersistentTextures.size() + snapshot->m_FrameTextures.size());
		for (const ImGuiTextureInfo& texture : snapshot->m_PersistentTextures)
		{
			if (texture.Texture)
				snapshot->m_TextureKeepAlives.emplace_back(texture.Texture);
		}
		for (const ImGuiTextureInfo& texture : snapshot->m_FrameTextures)
		{
			if (texture.Texture)
				snapshot->m_TextureKeepAlives.emplace_back(texture.Texture);
		}
		return snapshot;
	}

	ImGuiDrawDataSnapshot::~ImGuiDrawDataSnapshot()
	{
		for (ImDrawList* drawList : m_OwnedDrawLists)
			IM_DELETE(drawList);
	}

	const ImGuiTextureInfo& ImGuiDrawDataSnapshot::ResolveTexture(uint64_t handle) const
	{
		// Returned by reference for any handle we can't resolve. Its Texture is null, so the draw
		// loop skips the command. The asserts below still fire in Debug to pinpoint the bad handle,
		// but in Release they are compiled out — without the explicit bounds checks an out-of-range
		// or stale handle would read the vector out of bounds and hand null/garbage to nvrhi
		// (the requireTextureState crash). ImGui 1.92 makes this reachable: a draw cmd can reference
		// an ImTextureData whose TexID this legacy backend never set (GetTexID() == invalid).
		static const ImGuiTextureInfo s_InvalidTexture{};

		const uint32_t textureIndex = static_cast<uint32_t>(handle & 0xFFFFFFFFull);
		const uint32_t frameCounter = static_cast<uint32_t>(handle >> 32);

		if (textureIndex < ImGuiTextureRegistry::PersistentHandleCount)
		{
			LUX_CORE_ASSERT(frameCounter == 0, "Persistent ImGui texture handle has a frame counter");
			LUX_CORE_ASSERT(textureIndex < m_PersistentTextures.size(), "Invalid persistent ImGui texture handle");
			if (textureIndex >= m_PersistentTextures.size())
				return s_InvalidTexture;
			return m_PersistentTextures[textureIndex];
		}

		LUX_CORE_ASSERT(frameCounter == m_FrameCounter,
			"Stale ImGui texture handle: from frame {}, snapshot frame {}", frameCounter, m_FrameCounter);
		if (frameCounter != m_FrameCounter)
			return s_InvalidTexture;

		const uint32_t frameTextureIndex = textureIndex - ImGuiTextureRegistry::PersistentHandleCount;
		LUX_CORE_ASSERT(frameTextureIndex < m_FrameTextures.size(), "Invalid frame ImGui texture handle");
		if (frameTextureIndex >= m_FrameTextures.size())
			return s_InvalidTexture;
		return m_FrameTextures[frameTextureIndex];
	}

	struct VERTEX_CONSTANT_BUFFER
	{
		float mvp[4][4];
	};

	// -----------------------------------------------------------------------
	// Init
	// If sharedRegistry is null we are the main renderer - create a fresh one.
	// Per-viewport renderers receive the main renderer's registry so all
	// texture handles are valid regardless of which renderer decodes them.
	// -----------------------------------------------------------------------

	bool ImGuiRenderer::Init(std::shared_ptr<ImGuiTextureRegistry> sharedRegistry)
	{
		m_Registry = sharedRegistry ? std::move(sharedRegistry)
			: std::make_shared<ImGuiTextureRegistry>();

		auto device = Application::GetGraphicsDevice();

		m_RenderCommandBuffer = RenderCommandBuffer::Create(0, "ImGuiRenderer", true);

		Ref<Shader> imguiShader = Renderer::GetShaderLibrary()->Get("ImGui");
		m_VertexShader = imguiShader->GetHandle(nvrhi::ShaderType::Vertex);
		m_PixelShader = imguiShader->GetHandle(nvrhi::ShaderType::Pixel);

		nvrhi::VertexAttributeDesc vertexAttribLayout[] = {
			{ "POSITION", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert, pos), sizeof(ImDrawVert), false },
			{ "TEXCOORD", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert, uv),  sizeof(ImDrawVert), false },
			{ "COLOR",    nvrhi::Format::RGBA8_UNORM, 1, 0, offsetof(ImDrawVert, col), sizeof(ImDrawVert), false },
		};

		m_ShaderAttribLayout = device->createInputLayout(
			vertexAttribLayout,
			sizeof(vertexAttribLayout) / sizeof(vertexAttribLayout[0]),
			m_VertexShader);

		{
			nvrhi::BlendState blendState;
			blendState.targets[0]
				.setBlendEnable(true)
				.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
				.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
				.setSrcBlendAlpha(nvrhi::BlendFactor::One)
				.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);

			auto rasterState = nvrhi::RasterState()
				.setFillSolid()
				.setCullNone()
				.setScissorEnable(true)
				.setDepthClipEnable(true);

			auto depthStencilState = nvrhi::DepthStencilState()
				.disableDepthTest()
				.enableDepthWrite()
				.disableStencil()
				.setDepthFunc(nvrhi::ComparisonFunc::Always);

			nvrhi::RenderState renderState;
			renderState.blendState = blendState;
			renderState.depthStencilState = depthStencilState;
			renderState.rasterState = rasterState;

			nvrhi::BindingLayoutDesc layoutDesc;
			layoutDesc.visibility = nvrhi::ShaderType::All;
			layoutDesc.bindings = {
				nvrhi::BindingLayoutItem::PushConstants(0, sizeof(glm::vec2) * 2 + sizeof(uint32_t)),
				nvrhi::BindingLayoutItem::Texture_SRV(0),
				nvrhi::BindingLayoutItem::Sampler(1)
			};
			m_BindingLayout = device->createBindingLayout(layoutDesc);

			m_BasePSODesc.primType = nvrhi::PrimitiveType::TriangleList;
			m_BasePSODesc.inputLayout = m_ShaderAttribLayout;
			m_BasePSODesc.VS = m_VertexShader;
			m_BasePSODesc.PS = m_PixelShader;
			m_BasePSODesc.renderState = renderState;
			m_BasePSODesc.bindingLayouts = { m_BindingLayout };
		}

		{
			const auto desc = nvrhi::SamplerDesc()
				.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
				.setAllFilters(true);

			m_FontSampler = device->createSampler(desc);
			if (!m_FontSampler)
				return false;
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// UpdateFontTexture
	// Advertises backend capabilities. Must run before ImGui::NewFrame() so ImGui knows the
	// backend services ImTextureData (RendererHasTextures). The font atlas itself is no longer
	// created here — under ImGui 1.92 the atlas is an ImGui-owned ImTextureData that is created,
	// updated and destroyed by ProcessTextures().
	// -----------------------------------------------------------------------

	bool ImGuiRenderer::UpdateFontTexture()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.BackendRendererName = "LuxImGuiRenderer";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
		io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
		io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
		return true;
	}

	// -----------------------------------------------------------------------
	// ProcessTextures
	// Services the ImGui-owned texture list (font atlas + any custom atlas). Runs once per frame
	// on the main thread from ImGuiLayer::End(), after ImGui::Render() (so statuses/pixels are
	// final) and before draw data is snapshotted (so the render thread only sees ready textures).
	// The registry is only ever touched on the main thread, so no locking is needed.
	// -----------------------------------------------------------------------

	void ImGuiRenderer::ProcessTextures()
	{
		for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
		{
			switch (tex->Status)
			{
				case ImTextureStatus_WantCreate:
				case ImTextureStatus_WantUpdates:
					CreateOrUpdateImGuiTexture(tex);
					break;
				case ImTextureStatus_WantDestroy:
					// UnusedFrames > 0 guarantees the texture isn't referenced by the draw data we
					// just snapshotted, so releasing our reference now is safe.
					if (tex->UnusedFrames > 0)
						DestroyImGuiTexture(tex);
					break;
				default:
					break;
			}
		}
	}

	void ImGuiRenderer::CreateOrUpdateImGuiTexture(ImTextureData* tex)
	{
		nvrhi::IDevice* device = Application::GetGraphicsDevice();
		const bool create = (tex->Status == ImTextureStatus_WantCreate);

		nvrhi::TextureHandle handle;
		uint32_t slot;

		if (create)
		{
			nvrhi::TextureDesc textureDesc;
			textureDesc.width = (uint32_t)tex->Width;
			textureDesc.height = (uint32_t)tex->Height;
			textureDesc.format = (tex->Format == ImTextureFormat_Alpha8)
				? nvrhi::Format::R8_UNORM : nvrhi::Format::RGBA8_UNORM;
			textureDesc.debugName = "ImGui atlas texture";
			// keepInitialState lets nvrhi assume the texture is ShaderResource entering any command
			// list (incl. the render-thread draw), so sampling it never trips "Unknown prior state".
			textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
			textureDesc.keepInitialState = true;

			handle = device->createTexture(textureDesc);
			if (!handle)
				return;

			slot = (uint32_t)RegisterPersistentTexture(handle.Get(), nvrhi::AllSubresources);
			m_Registry->ImGuiOwnedTextures[slot] = handle; // keep alive
		}
		else // WantUpdates: re-upload into the existing texture
		{
			slot = (uint32_t)tex->TexID;
			auto it = m_Registry->ImGuiOwnedTextures.find(slot);
			if (it == m_Registry->ImGuiOwnedTextures.end())
			{
				// No record of this texture (e.g. a stale TexID) — recreate from scratch.
				tex->SetStatus(ImTextureStatus_WantCreate);
				CreateOrUpdateImGuiTexture(tex);
				return;
			}
			handle = it->second;
		}

		// Upload the whole pixel buffer. ImTextureData::Updates[] rects are an optimization we skip:
		// the editor's fonts are baked at load, so full re-uploads are rare.
		m_RenderCommandBuffer->RT_Begin();
		nvrhi::CommandListHandle commandList = m_RenderCommandBuffer->GetActive();
		commandList->beginTrackingTextureState(handle, nvrhi::AllSubresources,
			create ? nvrhi::ResourceStates::Common : nvrhi::ResourceStates::ShaderResource);
		commandList->writeTexture(handle, 0, 0, tex->GetPixels(), (size_t)tex->GetPitch());
		commandList->setTextureState(handle, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
		commandList->commitBarriers();
		m_RenderCommandBuffer->RT_End();
		m_RenderCommandBuffer->RT_Submit();

		tex->SetTexID((ImTextureID)slot);
		tex->SetStatus(ImTextureStatus_OK);
	}

	void ImGuiRenderer::DestroyImGuiTexture(ImTextureData* tex)
	{
		const uint32_t slot = (uint32_t)tex->TexID;
		auto it = m_Registry->ImGuiOwnedTextures.find(slot);
		if (it != m_Registry->ImGuiOwnedTextures.end())
		{
			// Release our reference. Any in-flight snapshot holds its own ref (m_TextureKeepAlives),
			// so the GPU resource survives until the render thread is done with it.
			m_Registry->ImGuiOwnedTextures.erase(it);
			if (slot < m_Registry->PersistentTextures.size())
				m_Registry->PersistentTextures[slot] = {}; // stop resolving to a freed texture
			m_Registry->FreePersistentSlots.push_back(slot); // reclaim for reuse
		}
		tex->SetTexID(ImTextureID_Invalid);
		tex->SetStatus(ImTextureStatus_Destroyed);
	}

	// -----------------------------------------------------------------------
	// RegisterPersistentTexture  (delegates to shared registry)
	// -----------------------------------------------------------------------

	ImTextureID ImGuiRenderer::RegisterPersistentTexture(nvrhi::ITexture* texture,
		nvrhi::TextureSubresourceSet subresources)
	{
		LUX_CORE_ASSERT(texture, "RegisterPersistentTexture called with null texture!");

		nvrhi::TextureSubresourceSet resolved = subresources;
		const nvrhi::TextureDesc& texDesc = texture->getDesc();

		if (resolved.numMipLevels == nvrhi::TextureSubresourceSet::AllMipLevels)
			resolved.numMipLevels = texDesc.mipLevels - resolved.baseMipLevel;
		if (resolved.numArraySlices == nvrhi::TextureSubresourceSet::AllArraySlices)
			resolved.numArraySlices = texDesc.arraySize - resolved.baseArraySlice;

		uint32_t index;
		if (!m_Registry->FreePersistentSlots.empty())
		{
			index = m_Registry->FreePersistentSlots.back();
			m_Registry->FreePersistentSlots.pop_back();
		}
		else
		{
			LUX_CORE_ASSERT(m_Registry->NextPersistentIndex < PersistentHandleCount,
				"Too many persistent textures!");
			index = m_Registry->NextPersistentIndex++;
		}

		if (m_Registry->PersistentTextures.size() <= index)
			m_Registry->PersistentTextures.resize(index + 1);

		m_Registry->PersistentTextures[index] = { texture, resolved };
		return (ImTextureID)(uintptr_t)index;
	}

	// -----------------------------------------------------------------------
	// CreateFrameTexture  (delegates to shared registry)
	// -----------------------------------------------------------------------

	ImTextureID ImGuiRenderer::CreateFrameTexture(nvrhi::ITexture* texture,
		nvrhi::TextureSubresourceSet subresources,
		bool forceOpaque,
		bool isGrayscale)
	{
		LUX_CORE_ASSERT(texture, "CreateFrameTexture called with null texture!");

		nvrhi::TextureSubresourceSet resolved = subresources;
		const nvrhi::TextureDesc& texDesc = texture->getDesc();

		if (resolved.numMipLevels == nvrhi::TextureSubresourceSet::AllMipLevels)
			resolved.numMipLevels = texDesc.mipLevels - resolved.baseMipLevel;
		if (resolved.numArraySlices == nvrhi::TextureSubresourceSet::AllArraySlices)
			resolved.numArraySlices = texDesc.arraySize - resolved.baseArraySlice;

		ImGuiTextureInfo key{ texture, resolved, forceOpaque, isGrayscale };

		auto it = m_Registry->FrameTextureMap.find(key);
		if (it != m_Registry->FrameTextureMap.end())
			return (ImTextureID)it->second;

		uint32_t index = PersistentHandleCount + (uint32_t)m_Registry->FrameTextures.size();
		m_Registry->FrameTextures.push_back(key);

		uint64_t handle = ((uint64_t)m_Registry->FrameCounter << 32) | index;
		m_Registry->FrameTextureMap[key] = handle;

		return (ImTextureID)handle;
	}

	// -----------------------------------------------------------------------
	// GetBindingSet
	// -----------------------------------------------------------------------

	nvrhi::IBindingSet* ImGuiRenderer::GetBindingSet(const ImGuiTextureInfo& texInfo)
	{
		nvrhi::IDevice* device = Application::GetGraphicsDevice();

		nvrhi::TextureSubresourceSet resolved = texInfo.Subresources;
		const nvrhi::TextureDesc& texDesc = texInfo.Texture->getDesc();

		if (resolved.numMipLevels == nvrhi::TextureSubresourceSet::AllMipLevels)
			resolved.numMipLevels = texDesc.mipLevels - resolved.baseMipLevel;
		if (resolved.numArraySlices == nvrhi::TextureSubresourceSet::AllArraySlices)
			resolved.numArraySlices = texDesc.arraySize - resolved.baseArraySlice;

		ImGuiTextureInfo key{ texInfo.Texture, resolved };

		auto iter = m_BindingsCache.find(key);
		if (iter != m_BindingsCache.end())
			return iter->second;

		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(float) * 2),
			nvrhi::BindingSetItem::Texture_SRV(0, texInfo.Texture, nvrhi::Format::UNKNOWN, resolved),
			nvrhi::BindingSetItem::Sampler(1, m_FontSampler)
		};

		nvrhi::BindingSetHandle binding = device->createBindingSet(desc, m_BindingLayout);
		LUX_CORE_ASSERT(binding);

		m_BindingsCache[key] = binding;
		return binding;
	}

	// -----------------------------------------------------------------------
	// ReallocateBuffer
	// -----------------------------------------------------------------------

	bool ImGuiRenderer::ReallocateBuffer(nvrhi::BufferHandle& buffer, size_t requiredSize,
		size_t reallocateSize, const bool indexBuffer)
	{
		nvrhi::IDevice* device = Application::GetGraphicsDevice();

		if (buffer == nullptr || size_t(buffer->getDesc().byteSize) < requiredSize)
		{
			nvrhi::BufferDesc desc;
			desc.byteSize = uint32_t(reallocateSize);
			desc.structStride = 0;
			desc.debugName = indexBuffer ? "ImGui index buffer" : "ImGui vertex buffer";
			desc.canHaveUAVs = false;
			desc.isVertexBuffer = !indexBuffer;
			desc.isIndexBuffer = indexBuffer;
			desc.isDrawIndirectArgs = false;
			desc.isVolatile = false;
			desc.initialState = indexBuffer ? nvrhi::ResourceStates::IndexBuffer
				: nvrhi::ResourceStates::VertexBuffer;
			desc.keepInitialState = true;

			buffer = device->createBuffer(desc);
			if (!buffer)
				return false;
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// GetOrCreatePipeline
	// -----------------------------------------------------------------------

	nvrhi::GraphicsPipelineHandle ImGuiRenderer::GetOrCreatePipeline(VulkanSwapChain* swapchain)
	{
		uint32_t currentFramebufferIndex = swapchain->GetCurrentBackBufferIndex();
		auto& cache = m_PipelineCache[swapchain];
		LUX_CORE_VERIFY(currentFramebufferIndex < cache.Pipelines.max_size());

		nvrhi::FramebufferHandle targetFramebuffer = swapchain->GetCurrentFramebuffer();
		nvrhi::GraphicsPipelineHandle pipeline = cache.Pipelines[currentFramebufferIndex];

		bool invalidate = !pipeline || cache.Framebuffers[currentFramebufferIndex] != targetFramebuffer;
		if (invalidate)
		{
			nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
			pipeline = device->createGraphicsPipeline(m_BasePSODesc, targetFramebuffer);
			cache.Pipelines[currentFramebufferIndex] = pipeline;
			cache.Framebuffers[currentFramebufferIndex] = targetFramebuffer;
		}

		return pipeline;
	}

	// -----------------------------------------------------------------------
	// UpdateGeometry
	// -----------------------------------------------------------------------

	bool ImGuiRenderer::UpdateGeometry(ImDrawData* drawData)
	{
		nvrhi::CommandListHandle commandList = m_RenderCommandBuffer->GetActive();

		if (!ReallocateBuffer(m_VertexBuffer,
			drawData->TotalVtxCount * sizeof(ImDrawVert),
			(drawData->TotalVtxCount + 5000) * sizeof(ImDrawVert), false))
			return false;

		if (!ReallocateBuffer(m_IndexBuffer,
			drawData->TotalIdxCount * sizeof(ImDrawIdx),
			(drawData->TotalIdxCount + 5000) * sizeof(ImDrawIdx), true))
			return false;

		m_VertexBufferData.resize(m_VertexBuffer->getDesc().byteSize / sizeof(ImDrawVert));
		m_IndexBufferData.resize(m_IndexBuffer->getDesc().byteSize / sizeof(ImDrawIdx));

		ImDrawVert* vtxDst = &m_VertexBufferData[0];
		ImDrawIdx* idxDst = &m_IndexBufferData[0];

		for (int n = 0; n < drawData->CmdListsCount; n++)
		{
			const ImDrawList* cmdList = drawData->CmdLists[n];
			memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtxDst += cmdList->VtxBuffer.Size;
			idxDst += cmdList->IdxBuffer.Size;
		}

		commandList->writeBuffer(m_VertexBuffer, &m_VertexBufferData[0], m_VertexBuffer->getDesc().byteSize);
		commandList->writeBuffer(m_IndexBuffer, &m_IndexBufferData[0], m_IndexBuffer->getDesc().byteSize);

		return true;
	}

	// -----------------------------------------------------------------------
	// Render
	//
	// NOTE: Frame texture clearing has moved OUT of this function.
	// It is now done once per frame in ImGuiLayer::Begin() via the shared
	// registry's NewFrame(). This ensures all per-viewport renderers that
	// run after the main renderer can still resolve their texture handles.
	// -----------------------------------------------------------------------

	bool ImGuiRenderer::Render(const std::shared_ptr<ImGuiDrawDataSnapshot>& snapshot, nvrhi::GraphicsPipelineHandle pipeline,
		nvrhi::FramebufferHandle framebuffer, VkSemaphore waitSemaphore)
	{
		LUX_PROFILE_FUNC("ImGuiRenderer::Render");
		if (!snapshot)
			return false;

		nvrhi::IDevice* device = Application::GetGraphicsDevice();
		ImDrawData* drawData = snapshot->GetDrawData();

		m_RenderCommandBuffer->RT_Begin();
		nvrhi::CommandListHandle commandList = m_RenderCommandBuffer->GetActive();

		nvrhi::utils::ClearColorAttachment(commandList, framebuffer, 0, nvrhi::Color(1, 0, 1, 1));

		if (!UpdateGeometry(drawData))
		{
			m_RenderCommandBuffer->RT_End();
			return false;
		}

		drawData->ScaleClipRects(drawData->FramebufferScale);

		struct PushConstants
		{
			glm::vec2 Scale;
			glm::vec2 Translate;
			uint32_t  Flags;
		} pushConstants;

		pushConstants.Scale.x = 2.0f / drawData->DisplaySize.x;
		pushConstants.Scale.y = 2.0f / drawData->DisplaySize.y;
		pushConstants.Translate.x = -1.0f - drawData->DisplayPos.x * pushConstants.Scale.x;
		pushConstants.Translate.y = -1.0f - drawData->DisplayPos.y * pushConstants.Scale.y;
		pushConstants.Flags = 0;

		float fbWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
		float fbHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;

		nvrhi::GraphicsState drawState;
		drawState.framebuffer = framebuffer;
		LUX_CORE_ASSERT(drawState.framebuffer);
		drawState.pipeline = pipeline;
		drawState.viewport.viewports.push_back(nvrhi::Viewport(fbWidth, fbHeight));
		drawState.viewport.scissorRects.resize(1);

		nvrhi::VertexBufferBinding vbufBinding;
		vbufBinding.buffer = m_VertexBuffer;
		vbufBinding.slot = 0;
		vbufBinding.offset = 0;
		drawState.vertexBuffers.push_back(vbufBinding);

		drawState.indexBuffer.buffer = m_IndexBuffer;
		drawState.indexBuffer.format = (sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT);
		drawState.indexBuffer.offset = 0;

		ImVec2 clip_off = drawData->DisplayPos;
		ImVec2 clip_scale = drawData->FramebufferScale;

		int vtxOffset = 0;
		int idxOffset = 0;

		for (int n = 0; n < drawData->CmdListsCount; n++)
		{
			const ImDrawList* cmdList = drawData->CmdLists[n];

			for (int i = 0; i < cmdList->CmdBuffer.Size; i++)
			{
				const ImDrawCmd* pCmd = &cmdList->CmdBuffer[i];

				if (pCmd->UserCallback)
				{
					pCmd->UserCallback(cmdList, pCmd);
					continue;
				}

				// --------------------------------------------------------
				// Decode handle and look up in SHARED registry.
				// All per-viewport renderers share the same registry so a
				// handle produced by the main renderer is always resolvable.
				// --------------------------------------------------------
				const uint64_t handle = static_cast<uint64_t>(pCmd->GetTexID());
				const ImGuiTextureInfo& texInfo = snapshot->ResolveTexture(handle);

				// In Release the assert is compiled out; a handle we couldn't resolve (see
				// ResolveTexture) yields a null texture. GetBindingSet would deref it, and even a
				// cached binding set feeds a null texture into nvrhi's requireTextureState and
				// crashes. Skip the command instead of drawing garbage.
				LUX_CORE_ASSERT(texInfo.Texture, "Texture is null in ImGuiTextureInfo!");
				if (!texInfo.Texture)
					continue;

				drawState.bindings = { GetBindingSet(texInfo) };
				LUX_CORE_ASSERT(drawState.bindings[0]);

				pushConstants.Flags = (texInfo.ForceOpaque ? 1 : 0) | (texInfo.IsGrayscale ? 2 : 0);

				ImVec2 clipMin((pCmd->ClipRect.x - clip_off.x) * clip_scale.x,
					(pCmd->ClipRect.y - clip_off.y) * clip_scale.y);
				ImVec2 clipMax((pCmd->ClipRect.z - clip_off.x) * clip_scale.x,
					(pCmd->ClipRect.w - clip_off.y) * clip_scale.y);

				if (clipMin.x < 0.0f) clipMin.x = 0.0f;
				if (clipMin.y < 0.0f) clipMin.y = 0.0f;
				if (clipMax.x > fbWidth)  clipMax.x = fbWidth;
				if (clipMax.y > fbHeight) clipMax.y = fbHeight;
				if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
					continue;

				drawState.viewport.scissorRects[0] = nvrhi::Rect(clipMin.x, clipMax.x, clipMin.y, clipMax.y);

				nvrhi::DrawArguments drawArguments;
				drawArguments.vertexCount = pCmd->ElemCount;
				drawArguments.startIndexLocation = pCmd->IdxOffset + idxOffset;
				drawArguments.startVertexLocation = pCmd->VtxOffset + vtxOffset;

				commandList->setGraphicsState(drawState);
				commandList->setPushConstants(&pushConstants, sizeof(PushConstants));
				commandList->drawIndexed(drawArguments);
			}

			vtxOffset += cmdList->VtxBuffer.Size;
			idxOffset += cmdList->IdxBuffer.Size;
		}

		m_RenderCommandBuffer->RT_End();

		if (waitSemaphore)
			m_RenderCommandBuffer->RT_Wait(waitSemaphore);

		m_RenderCommandBuffer->RT_Submit();

		// Frame texture clearing and counter increment have moved to
		// ImGuiLayer::Begin() so all viewports render with valid handles.

		return true;
	}

	bool ImGuiRenderer::RenderToSwapchain(const std::shared_ptr<ImGuiDrawDataSnapshot>& snapshot, VulkanSwapChain* swapchain)
	{
		return Render(snapshot, GetOrCreatePipeline(swapchain),
			swapchain->GetCurrentFramebuffer(),
			swapchain->GetAcquiredImageSemaphore());
	}

	float ImGuiRenderer::GetGPUTime() const
	{
		return m_RenderCommandBuffer ? m_RenderCommandBuffer->GetExecutionGPUTime() : 0.0f;
	}

	void ImGuiRenderer::BackbufferResizing()
	{
		m_PipelineCache.clear();
	}

}
