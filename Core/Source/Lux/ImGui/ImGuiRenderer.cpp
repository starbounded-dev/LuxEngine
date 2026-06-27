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
		const uint32_t textureIndex = static_cast<uint32_t>(handle & 0xFFFFFFFFull);
		const uint32_t frameCounter = static_cast<uint32_t>(handle >> 32);

		if (textureIndex < ImGuiTextureRegistry::PersistentHandleCount)
		{
			LUX_CORE_ASSERT(frameCounter == 0, "Persistent ImGui texture handle has a frame counter");
			LUX_CORE_ASSERT(textureIndex < m_PersistentTextures.size(), "Invalid persistent ImGui texture handle");
			return m_PersistentTextures[textureIndex];
		}

		LUX_CORE_ASSERT(frameCounter == m_FrameCounter,
			"Stale ImGui texture handle: from frame {}, snapshot frame {}", frameCounter, m_FrameCounter);
		const uint32_t frameTextureIndex = textureIndex - ImGuiTextureRegistry::PersistentHandleCount;
		LUX_CORE_ASSERT(frameTextureIndex < m_FrameTextures.size(), "Invalid frame ImGui texture handle");
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
	// Only creates and registers the font texture if it hasn't been done yet.
	// Because the registry is shared, the font persistent slot (index 0) is
	// populated once and visible to all renderer instances.
	// -----------------------------------------------------------------------

	bool ImGuiRenderer::UpdateFontTexture()
	{
		nvrhi::IDevice* device = Application::GetGraphicsDevice();

		ImGuiIO& io = ImGui::GetIO();
		io.BackendRendererName = "LuxImGuiRenderer";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
		io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

		if (m_FontTexture)
			return true;

		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		if (!pixels)
			return false;

		nvrhi::TextureDesc textureDesc;
		textureDesc.width = width;
		textureDesc.height = height;
		textureDesc.format = nvrhi::Format::RGBA8_UNORM;
		textureDesc.debugName = "ImGui font texture";

		m_FontTexture = device->createTexture(textureDesc);
		if (!m_FontTexture)
			return false;

		m_RenderCommandBuffer->RT_Begin();
		nvrhi::CommandListHandle commandList = m_RenderCommandBuffer->GetActive();

		commandList->beginTrackingTextureState(m_FontTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
		commandList->writeTexture(m_FontTexture, 0, 0, pixels, width * 4);
		commandList->setPermanentTextureState(m_FontTexture, nvrhi::ResourceStates::ShaderResource);
		commandList->commitBarriers();

		m_RenderCommandBuffer->RT_End();
		m_RenderCommandBuffer->RT_Submit();

		// Registers into the shared registry - index 0, visible to ALL renderers.
		io.Fonts->TexID = RegisterPersistentTexture(m_FontTexture.Get(), nvrhi::AllSubresources);

		return true;
	}

	// -----------------------------------------------------------------------
	// RegisterPersistentTexture  (delegates to shared registry)
	// -----------------------------------------------------------------------

	ImTextureID ImGuiRenderer::RegisterPersistentTexture(nvrhi::ITexture* texture,
		nvrhi::TextureSubresourceSet subresources)
	{
		LUX_CORE_ASSERT(m_Registry->NextPersistentIndex < PersistentHandleCount,
			"Too many persistent textures!");
		LUX_CORE_ASSERT(texture, "RegisterPersistentTexture called with null texture!");

		nvrhi::TextureSubresourceSet resolved = subresources;
		const nvrhi::TextureDesc& texDesc = texture->getDesc();

		if (resolved.numMipLevels == nvrhi::TextureSubresourceSet::AllMipLevels)
			resolved.numMipLevels = texDesc.mipLevels - resolved.baseMipLevel;
		if (resolved.numArraySlices == nvrhi::TextureSubresourceSet::AllArraySlices)
			resolved.numArraySlices = texDesc.arraySize - resolved.baseArraySlice;

		uint32_t index = m_Registry->NextPersistentIndex++;

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

				LUX_CORE_ASSERT(texInfo.Texture, "Texture is null in ImGuiTextureInfo!");
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

	float ImGuiRenderer::GetGPUTime(uint32_t frameIndex) const
	{
		return m_RenderCommandBuffer ? m_RenderCommandBuffer->GetExecutionGPUTime(frameIndex) : 0.0f;
	}

	void ImGuiRenderer::BackbufferResizing()
	{
		m_PipelineCache.clear();
	}

}
