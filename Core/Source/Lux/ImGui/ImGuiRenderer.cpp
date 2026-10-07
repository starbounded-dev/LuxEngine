/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
* (license header omitted for brevity - keep original)
*/
#include "lpch.h"
#include "ImGuiRenderer.h"

#include "Lux/Renderer/Shader.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Debug/Profiler.h"

#include "Lux/Platform/Vulkan/VulkanShader.h"
#include "Lux/Renderer/RHI/NVRHIInterop.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

#include "nvrhi/utils.h"

#include <format>

namespace Lux {

	namespace {

		// Sets in the first descriptor pool of a frame slot; each further pool doubles the last.
		constexpr uint32_t k_NRIDescriptorPoolSize = 64;
		// Elements of headroom the geometry buffers grow by, so they are not reallocated every frame.
		constexpr size_t k_GeometryHeadroom = 5000;

		// `range` with AllMips / AllLayers resolved against the image's current texture.
		TextureSubresourceRange ResolveRange(const Image2D& image, TextureSubresourceRange range)
		{
			const nvrhi::TextureDesc& desc = image.GetImageInfo().ImageHandle->getDesc();
			if (range.MipCount == TextureSubresourceRange::AllMips)
				range.MipCount = desc.mipLevels - range.BaseMip;
			if (range.LayerCount == TextureSubresourceRange::AllLayers)
				range.LayerCount = desc.arraySize - range.BaseLayer;
			return range;
		}

	}

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
		// The copies' Refs keep every image alive until the snapshot is rendered, even when the UI
		// releases an icon or viewport texture before the snapshot reaches the render thread.
		snapshot->m_PersistentTextures = registry->PersistentTextures;
		snapshot->m_FrameTextures = registry->FrameTextures;
		snapshot->m_FrameCounter = registry->FrameCounter;
		return snapshot;
	}

	ImGuiDrawDataSnapshot::~ImGuiDrawDataSnapshot()
	{
		for (ImDrawList* drawList : m_OwnedDrawLists)
			IM_DELETE(drawList);
	}

	const ImGuiTextureInfo& ImGuiDrawDataSnapshot::ResolveTexture(uint64_t handle) const
	{
		// Returned by reference for any handle we can't resolve. Its Image is null, so the draw
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
		m_Shader = imguiShader.As<VulkanShader>();
		m_VertexShader = imguiShader.As<VulkanShader>()->GetHandle(ShaderStage::Vertex);
		m_PixelShader = imguiShader.As<VulkanShader>()->GetHandle(ShaderStage::Pixel);

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

		// The NRI path's sampler (the font sampler's wrap and linear filtering, which the renderer's
		// anisotropic repeat sampler does not match) and the pipeline for the main window's format,
		// made now rather than on the first frame. A platform window of another format gets its
		// pipeline when it is first drawn: its format is not known before it exists.
		SamplerSpecification samplerSpecification;
		samplerSpecification.AddressMode = TextureWrap::Repeat;
		m_NRISampler = Sampler::Create(samplerSpecification);
		m_NRISetIndex = m_Shader->GetNRISetIndex(0);
		m_NRITextureRange = m_Shader->GetNRIRangeIndex(0, 0);
		m_NRISamplerRange = m_Shader->GetNRIRangeIndex(0, 1);
		GetOrCreateNRIPipeline(Application::Get().GetWindow().GetSwapChain().GetNRIColorFormat());

		return true;
	}

	ImGuiRenderer::~ImGuiRenderer()
	{
		for (const auto& [format, pipeline] : m_NRIPipelines)
		{
			if (!pipeline)
				continue;
			Renderer::SubmitResourceFree([pipeline]()
				{
					RHIDevice::API().DestroyPipeline(pipeline);
				});
		}

		for (NRIFrameResources& frame : m_NRIFrames)
		{
			for (const NRIDescriptorPool& pool : frame.DescriptorPools)
			{
				Renderer::SubmitResourceFree([descriptorPool = pool.Pool]()
					{
						RHIDevice::API().DestroyDescriptorPool(descriptorPool);
					});
			}
		}
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
		// An engine image, uploaded through the shared upload list (Image2D::SetData), so the
		// resource state tracker models it like any other texture. An update uploads into a new
		// image rather than the one frames in flight may still sample (the upload can run on the copy
		// queue, unordered with them); the old one goes with the last snapshot that draws it. Uploads
		// are whole: ImTextureData::Updates[] rects are an optimization skipped here.
		ImageSpecification specification;
		specification.DebugName = "ImGui atlas texture";
		specification.Format = (tex->Format == ImTextureFormat_Alpha8) ? ImageFormat::RED8UN : ImageFormat::RGBA;
		specification.Usage = ImageUsage::Texture;
		specification.Transfer = true;
		specification.Width = static_cast<uint32_t>(tex->Width);
		specification.Height = static_cast<uint32_t>(tex->Height);
		specification.CreateSampler = false;

		Ref<Image2D> image = Image2D::Create(specification);
		image->Invalidate();
		image->SetData(Buffer(tex->GetPixels(), static_cast<uint64_t>(tex->GetSizeInBytes())));

		uint32_t slot = static_cast<uint32_t>(tex->TexID);
		const bool known = tex->Status == ImTextureStatus_WantUpdates && m_Registry->ImGuiOwnedTextures.contains(slot);
		if (known)
			m_Registry->PersistentTextures[slot] = { image, ResolveRange(*image, AllSubresources) };
		else
			slot = static_cast<uint32_t>(RegisterPersistentTexture(image));
		m_Registry->ImGuiOwnedTextures[slot] = image;

		tex->SetTexID((ImTextureID)slot);
		tex->SetStatus(ImTextureStatus_OK);
	}

	void ImGuiRenderer::DestroyImGuiTexture(ImTextureData* tex)
	{
		const uint32_t slot = (uint32_t)tex->TexID;
		auto it = m_Registry->ImGuiOwnedTextures.find(slot);
		if (it != m_Registry->ImGuiOwnedTextures.end())
		{
			// Release our reference. Any in-flight snapshot holds its own ref, so the image survives
			// until the render thread is done with it.
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

	ImTextureID ImGuiRenderer::RegisterPersistentTexture(const Ref<Image2D>& image, TextureSubresourceRange subresources)
	{
		LUX_CORE_ASSERT(image && image->GetImageInfo().ImageHandle, "RegisterPersistentTexture called with a null image!");

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

		m_Registry->PersistentTextures[index] = { image, ResolveRange(*image, subresources) };
		return (ImTextureID)(uintptr_t)index;
	}

	// -----------------------------------------------------------------------
	// CreateFrameTexture  (delegates to shared registry)
	// -----------------------------------------------------------------------

	ImTextureID ImGuiRenderer::CreateFrameTexture(const Ref<Image2D>& image,
		TextureSubresourceRange subresources,
		bool forceOpaque,
		bool isGrayscale)
	{
		LUX_CORE_ASSERT(image && image->GetImageInfo().ImageHandle, "CreateFrameTexture called with a null image!");

		ImGuiTextureInfo key{ image, ResolveRange(*image, subresources), forceOpaque, isGrayscale };

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

		nvrhi::ITexture* texture = texInfo.Image->GetImageInfo().ImageHandle;
		const BindingKey key{ texture, texInfo.Range };

		auto iter = m_BindingsCache.find(key);
		if (iter != m_BindingsCache.end())
			return iter->second;

		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(float) * 2),
			nvrhi::BindingSetItem::Texture_SRV(0, texture, nvrhi::Format::UNKNOWN, ToNVRHI(texInfo.Range)),
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

	bool ImGuiRenderer::ReallocateBuffer(NRIBuffer& buffer, size_t requiredSize,
		size_t reallocateSize, const bool indexBuffer, const bool hostVisible)
	{
		if (!buffer || size_t(buffer.GetHandle()->getDesc().byteSize) < requiredSize)
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
			desc.cpuAccess = hostVisible ? nvrhi::CpuAccessMode::Write : nvrhi::CpuAccessMode::None;
			desc.initialState = indexBuffer ? nvrhi::ResourceStates::IndexBuffer
				: nvrhi::ResourceStates::VertexBuffer;
			desc.keepInitialState = true;

			// Replacing the buffer hands the old one to the GPU deletion queue.
			buffer = NRIBuffer::Create(desc);
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

		m_VertexBufferData.resize(m_VertexBuffer.GetHandle()->getDesc().byteSize / sizeof(ImDrawVert));
		m_IndexBufferData.resize(m_IndexBuffer.GetHandle()->getDesc().byteSize / sizeof(ImDrawIdx));

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

		commandList->writeBuffer(m_VertexBuffer.GetHandle(), &m_VertexBufferData[0], m_VertexBuffer.GetHandle()->getDesc().byteSize);
		commandList->writeBuffer(m_IndexBuffer.GetHandle(), &m_IndexBufferData[0], m_IndexBuffer.GetHandle()->getDesc().byteSize);

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
		nvrhi::FramebufferHandle framebuffer, VkSemaphore waitSemaphore, bool clearTarget)
	{
		LUX_PROFILE_FUNC("ImGuiRenderer::Render");
		if (!snapshot)
			return false;

		nvrhi::IDevice* device = Application::GetGraphicsDevice();
		ImDrawData* drawData = snapshot->GetDrawData();

		// The NVRHI draws below rely on NVRHI's automatic barriers (setGraphicsState is not committed
		// through the tracker).
		m_RenderCommandBuffer->SetBarrierMode(RenderCommandBuffer::BarrierMode::Automatic);
		m_RenderCommandBuffer->RT_Begin();
		nvrhi::CommandListHandle commandList = m_RenderCommandBuffer->GetActive();

		if (clearTarget)
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
		vbufBinding.buffer = m_VertexBuffer.GetHandle();
		vbufBinding.slot = 0;
		vbufBinding.offset = 0;
		drawState.vertexBuffers.push_back(vbufBinding);

		drawState.indexBuffer.buffer = m_IndexBuffer.GetHandle();
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
				// ResolveTexture) yields a null image. GetBindingSet would deref it, and even a
				// cached binding set feeds a null texture into nvrhi's requireTextureState and
				// crashes. Skip the command instead of drawing garbage.
				LUX_CORE_ASSERT(texInfo.Image, "Image is null in ImGuiTextureInfo!");
				if (!texInfo.Image || !texInfo.Image->GetImageInfo().ImageHandle)
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

	bool ImGuiRenderer::RenderToSwapchain(const std::shared_ptr<ImGuiDrawDataSnapshot>& snapshot, VulkanSwapChain* swapchain, bool clearTarget)
	{
		if (Renderer::RT_NRIImGuiEnabled() && RenderWithNRI(snapshot, swapchain, clearTarget))
			return true;

		return Render(snapshot, GetOrCreatePipeline(swapchain),
			swapchain->GetCurrentFramebuffer(),
			swapchain->GetAcquiredImageSemaphore(),
			clearTarget);
	}

	float ImGuiRenderer::GetGPUTime() const
	{
		return m_RenderCommandBuffer ? m_RenderCommandBuffer->GetExecutionGPUTime() : 0.0f;
	}

	void ImGuiRenderer::BackbufferResizing()
	{
		m_PipelineCache.clear();
	}

	nri::Pipeline* ImGuiRenderer::GetOrCreateNRIPipeline(nri::Format format)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (auto it = m_NRIPipelines.find(format); it != m_NRIPipelines.end())
			return it->second;

		nri::Pipeline*& pipeline = m_NRIPipelines[format];
		nri::PipelineLayout* layout = m_Shader->GetNRIPipelineLayout();
		const std::vector<uint32_t>& vertexSPIRV = m_Shader->GetSPIRV(ShaderStage::Vertex);
		const std::vector<uint32_t>& pixelSPIRV = m_Shader->GetSPIRV(ShaderStage::Pixel);
		if (!layout || vertexSPIRV.empty() || pixelSPIRV.empty() || format == nri::Format::UNKNOWN)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[ImGuiRenderer] No NRI pipeline for format {}: {}", static_cast<int>(format),
				!layout ? "the ImGui shader has no NRI layout" : (format == nri::Format::UNKNOWN ? "unknown format" : "no SPIR-V"));
			return pipeline;
		}

		std::array<nri::ShaderDesc, 2> shaders = {};
		shaders[0].stage = nri::StageBits::VERTEX_SHADER;
		shaders[0].bytecode = vertexSPIRV.data();
		shaders[0].size = vertexSPIRV.size() * sizeof(uint32_t);
		shaders[0].entryPointName = "main";
		shaders[1].stage = nri::StageBits::FRAGMENT_SHADER;
		shaders[1].bytecode = pixelSPIRV.data();
		shaders[1].size = pixelSPIRV.size() * sizeof(uint32_t);
		shaders[1].entryPointName = "main";

		// ImDrawVert, at the locations NVRHI's input layout gives the shader's inputs.
		std::array<nri::VertexAttributeDesc, 3> attributes = {};
		attributes[0].vk.location = 0;
		attributes[0].offset = offsetof(ImDrawVert, pos);
		attributes[0].format = nri::Format::RG32_SFLOAT;
		attributes[1].vk.location = 1;
		attributes[1].offset = offsetof(ImDrawVert, uv);
		attributes[1].format = nri::Format::RG32_SFLOAT;
		attributes[2].vk.location = 2;
		attributes[2].offset = offsetof(ImDrawVert, col);
		attributes[2].format = nri::Format::RGBA8_UNORM;

		nri::VertexStreamDesc stream = {};
		stream.bindingSlot = 0;
		stream.stepRate = nri::VertexStreamStepRate::PER_VERTEX;
		stream.stride = sizeof(ImDrawVert);

		nri::VertexInputDesc vertexInput = {};
		vertexInput.attributes = attributes.data();
		vertexInput.attributeNum = static_cast<uint8_t>(attributes.size());
		vertexInput.streams = &stream;
		vertexInput.streamNum = 1;

		// Standard ImGui blending, as m_BasePSODesc.
		nri::ColorAttachmentDesc color = {};
		color.format = format;
		color.blendEnabled = true;
		color.colorBlend = { nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendOp::ADD };
		color.alphaBlend = { nri::BlendFactor::ONE, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendOp::ADD };
		color.colorWriteMask = nri::ColorWriteBits::RGBA;

		nri::GraphicsPipelineDesc pipelineDesc = {};
		pipelineDesc.pipelineLayout = layout;
		pipelineDesc.vertexInput = &vertexInput;
		pipelineDesc.inputAssembly.topology = nri::Topology::TRIANGLE_LIST;
		pipelineDesc.rasterization.fillMode = nri::FillMode::SOLID;
		pipelineDesc.rasterization.cullMode = nri::CullMode::NONE;
		pipelineDesc.outputMerger.colors = &color;
		pipelineDesc.outputMerger.colorNum = 1;
		pipelineDesc.outputMerger.depth.compareOp = nri::CompareOp::NONE;
		pipelineDesc.shaders = shaders.data();
		pipelineDesc.shaderNum = static_cast<uint32_t>(shaders.size());

		const NRIInterface& api = RHIDevice::API();
		if (api.CreateGraphicsPipeline(RHIDevice::Get(), pipelineDesc, pipeline) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[ImGuiRenderer] Failed to create the NRI pipeline for format {}", static_cast<int>(format));
			pipeline = nullptr;
			return pipeline;
		}

		api.SetDebugName(pipeline, "ImGui");
		return pipeline;
	}

	bool ImGuiRenderer::UpdateNRIGeometry(NRIFrameResources& frame, ImDrawData* drawData)
	{
		const size_t vertexBytes = static_cast<size_t>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
		const size_t indexBytes = static_cast<size_t>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);

		// Host-visible and per frame slot: the CPU writes one while the GPU may still read another.
		if (!ReallocateBuffer(frame.VertexBuffer, vertexBytes, (drawData->TotalVtxCount + k_GeometryHeadroom) * sizeof(ImDrawVert), false, true)
			|| !ReallocateBuffer(frame.IndexBuffer, indexBytes, (drawData->TotalIdxCount + k_GeometryHeadroom) * sizeof(ImDrawIdx), true, true))
			return false;

		if (vertexBytes == 0 || indexBytes == 0)
			return true;

		auto* vertices = static_cast<ImDrawVert*>(frame.VertexBuffer.Map(0, vertexBytes));
		auto* indices = static_cast<ImDrawIdx*>(frame.IndexBuffer.Map(0, indexBytes));
		for (const ImDrawList* drawList : drawData->CmdLists)
		{
			std::memcpy(vertices, drawList->VtxBuffer.Data, drawList->VtxBuffer.Size * sizeof(ImDrawVert));
			std::memcpy(indices, drawList->IdxBuffer.Data, drawList->IdxBuffer.Size * sizeof(ImDrawIdx));
			vertices += drawList->VtxBuffer.Size;
			indices += drawList->IdxBuffer.Size;
		}
		frame.IndexBuffer.Unmap();
		frame.VertexBuffer.Unmap();
		return true;
	}

	nri::DescriptorSet* ImGuiRenderer::AllocateNRIDescriptorSet(NRIFrameResources& frame)
	{
		const NRIInterface& api = RHIDevice::API();

		// Full pools are skipped rather than allocated from, so NRI never reports an exhausted pool.
		NRIDescriptorPool* pool = nullptr;
		for (NRIDescriptorPool& candidate : frame.DescriptorPools)
		{
			if (candidate.Used < candidate.Capacity)
			{
				pool = &candidate;
				break;
			}
		}

		if (!pool)
		{
			const uint32_t capacity = k_NRIDescriptorPoolSize << frame.DescriptorPools.size();
			NRIDescriptorPool& newPool = frame.DescriptorPools.emplace_back();
			newPool.Capacity = capacity;
			if (api.CreateDescriptorPool(RHIDevice::Get(), m_Shader->GetNRIPoolDesc(0, capacity), newPool.Pool) != nri::Result::SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Renderer", "[ImGuiRenderer] Failed to create an NRI descriptor pool of {} sets", capacity);
				frame.DescriptorPools.pop_back();
				return nullptr;
			}
			api.SetDebugName(newPool.Pool, "ImGui");
			pool = &newPool;
		}

		nri::DescriptorSet* set = nullptr;
		if (api.AllocateDescriptorSets(*pool->Pool, *m_Shader->GetNRIPipelineLayout(), m_NRISetIndex, &set, 1, 0) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[ImGuiRenderer] Failed to allocate an NRI descriptor set");
			return nullptr;
		}
		pool->Used++;
		return set;
	}

	nri::DescriptorSet* ImGuiRenderer::GetNRIDescriptorSet(NRIFrameResources& frame, const ImGuiTextureInfo& texInfo)
	{
		nri::Texture* texture = texInfo.Image->GetImageInfo().RHITexture;
		const NRISetKey key{ texture, texInfo.Range };
		if (auto it = frame.DescriptorSets.find(key); it != frame.DescriptorSets.end())
			return it->second;

		// A 2D view of the range's first layer: the shader samples a Texture2D.
		NRITextureViewKey viewKey;
		viewKey.Type = nri::TextureView::TEXTURE;
		viewKey.MipOffset = texInfo.Range.BaseMip;
		viewKey.MipNum = texInfo.Range.MipCount;
		viewKey.LayerOffset = texInfo.Range.BaseLayer;
		viewKey.LayerNum = 1;
		// GetNRITextureView logs its own failures.
		const nri::Descriptor* view = GetNRITextureView(texture, viewKey);
		nri::DescriptorSet* set = view ? AllocateNRIDescriptorSet(frame) : nullptr;
		if (set)
		{
			const nri::Descriptor* sampler = m_NRISampler->GetRHIDescriptor();
			std::array<nri::UpdateDescriptorRangeDesc, 2> updates = {};
			updates[0].descriptorSet = set;
			updates[0].rangeIndex = m_NRITextureRange;
			updates[0].descriptors = &view;
			updates[0].descriptorNum = 1;
			updates[1].descriptorSet = set;
			updates[1].rangeIndex = m_NRISamplerRange;
			updates[1].descriptors = &sampler;
			updates[1].descriptorNum = 1;
			RHIDevice::API().UpdateDescriptorRanges(updates.data(), static_cast<uint32_t>(updates.size()));
		}

		frame.DescriptorSets[key] = set;
		return set;
	}

	bool ImGuiRenderer::RenderWithNRI(const std::shared_ptr<ImGuiDrawDataSnapshot>& snapshot, VulkanSwapChain* swapchain, bool clearTarget)
	{
		LUX_PROFILE_FUNC("ImGuiRenderer::RenderWithNRI");
		if (!snapshot)
			return false;

		nri::Descriptor* target = swapchain->GetCurrentNRIColorAttachment();
		nri::Pipeline* pipeline = GetOrCreateNRIPipeline(swapchain->GetNRIColorFormat());
		const char* problem = !pipeline ? "no NRI pipeline for the target format"
			: !target ? "the back buffer has no NRI view"
			: !m_NRISampler->GetRHIDescriptor() ? "the sampler has no NRI descriptor"
			: (m_NRISetIndex == VulkanShader::k_NoNRISet || m_NRITextureRange == VulkanShader::k_NoNRISet || m_NRISamplerRange == VulkanShader::k_NoNRISet) ? "the ImGui shader's set 0 has no NRI ranges"
			: nullptr;
		if (problem)
		{
			if (!m_ReportedNRIFallback)
				LUX_CORE_ERROR_TAG("Renderer", "[ImGuiRenderer] The UI is drawn through NVRHI: {}", problem);
			m_ReportedNRIFallback = true;
			return false;
		}

		// This slot's previous frame has retired (RT_BeginFrame waited for it), so its sets and
		// geometry are free again.
		NRIFrameResources& frame = m_NRIFrames[Renderer::RT_GetCurrentFrameIndex() % m_NRIFrames.size()];
		const NRIInterface& api = RHIDevice::API();
		for (NRIDescriptorPool& pool : frame.DescriptorPools)
		{
			api.ResetDescriptorPool(*pool.Pool);
			pool.Used = 0;
		}
		frame.DescriptorSets.clear();

		// Written through Map, so before anything is recorded: a failure (logged by the buffer's
		// creation) leaves the NVRHI path a clean command buffer.
		ImDrawData* drawData = snapshot->GetDrawData();
		if (!UpdateNRIGeometry(frame, drawData))
			return false;

		m_RenderCommandBuffer->SetBarrierMode(RenderCommandBuffer::BarrierMode::Explicit);
		m_RenderCommandBuffer->RT_Begin();

		// The states, required before rendering opens so it never has to split: the target as a
		// color attachment, every drawn image as a shader resource. RT_End returns them to rest.
		m_RenderCommandBuffer->RT_RequireTextureState(swapchain->GetCurrentBackBuffer(), AllSubresources, ResourceState::RenderTarget);
		for (const ImDrawList* drawList : drawData->CmdLists)
		{
			for (const ImDrawCmd& drawCommand : drawList->CmdBuffer)
			{
				if (drawCommand.UserCallback)
					continue;
				const ImGuiTextureInfo& texInfo = snapshot->ResolveTexture(static_cast<uint64_t>(drawCommand.GetTexID()));
				if (texInfo.Image && texInfo.Image->GetImageInfo().ImageHandle)
					m_RenderCommandBuffer->RT_RequireTextureState(texInfo.Image->GetImageInfo().ImageHandle, texInfo.Range, ResourceState::ShaderResource);
			}
		}

		drawData->ScaleClipRects(drawData->FramebufferScale);
		const float framebufferWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
		const float framebufferHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;

		struct PushConstants
		{
			glm::vec2 Scale;
			glm::vec2 Translate;
			uint32_t Flags;
		} pushConstants;
		pushConstants.Scale = { 2.0f / drawData->DisplaySize.x, 2.0f / drawData->DisplaySize.y };
		pushConstants.Translate = { -1.0f - drawData->DisplayPos.x * pushConstants.Scale.x, -1.0f - drawData->DisplayPos.y * pushConstants.Scale.y };
		pushConstants.Flags = 0;

		RenderCommandBuffer::NRIRenderPassDesc passDesc;
		passDesc.ColorAttachments[0] = target;
		passDesc.ClearColor[0] = clearTarget;
		passDesc.ClearColorValues[0] = { 1.0f, 0.0f, 1.0f, 1.0f };
		passDesc.ColorAttachmentCount = 1;
		passDesc.PipelineLayout = m_Shader->GetNRIPipelineLayout();
		passDesc.Pipeline = pipeline;
		passDesc.DrawSetIndex = m_NRISetIndex;
		passDesc.VertexStride = sizeof(ImDrawVert);
		passDesc.RootConstants = true;

		nvrhi::ViewportState viewport;
		viewport.viewports = { nvrhi::Viewport(framebufferWidth, framebufferHeight) };
		viewport.scissorRects = { nvrhi::Rect(static_cast<int>(framebufferWidth), static_cast<int>(framebufferHeight)) };

		if (m_RenderCommandBuffer->RT_BeginNRIRenderPass(passDesc, "ImGui", viewport, nvrhi::VariableRateShadingState()))
		{
			const ImVec2 clipOffset = drawData->DisplayPos;
			const ImVec2 clipScale = drawData->FramebufferScale;
			int vertexOffset = 0;
			int indexOffset = 0;
			for (const ImDrawList* drawList : drawData->CmdLists)
			{
				for (const ImDrawCmd& drawCommand : drawList->CmdBuffer)
				{
					if (drawCommand.UserCallback)
					{
						// Every draw rebinds what it uses, so there is no render state to reset.
						if (drawCommand.UserCallback != ImDrawCallback_ResetRenderState)
							drawCommand.UserCallback(drawList, &drawCommand);
						continue;
					}

					const ImGuiTextureInfo& texInfo = snapshot->ResolveTexture(static_cast<uint64_t>(drawCommand.GetTexID()));
					if (!texInfo.Image || !texInfo.Image->GetImageInfo().ImageHandle)
						continue;

					const ImVec2 clipMin(std::max((drawCommand.ClipRect.x - clipOffset.x) * clipScale.x, 0.0f),
						std::max((drawCommand.ClipRect.y - clipOffset.y) * clipScale.y, 0.0f));
					const ImVec2 clipMax(std::min((drawCommand.ClipRect.z - clipOffset.x) * clipScale.x, framebufferWidth),
						std::min((drawCommand.ClipRect.w - clipOffset.y) * clipScale.y, framebufferHeight));
					if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
						continue;

					nri::DescriptorSet* set = GetNRIDescriptorSet(frame, texInfo);
					nri::CommandBuffer* commandBuffer = set ? m_RenderCommandBuffer->RT_BeginNRIDraw() : nullptr;
					if (!commandBuffer)
						continue;

					pushConstants.Flags = (texInfo.ForceOpaque ? 1 : 0) | (texInfo.IsGrayscale ? 2 : 0);
					m_RenderCommandBuffer->RT_SetNRIVertexBuffer(frame.VertexBuffer.Get(), sizeof(ImDrawVert));
					m_RenderCommandBuffer->RT_SetNRIIndexBuffer(frame.IndexBuffer.Get(), sizeof(ImDrawIdx) == 2);
					m_RenderCommandBuffer->RT_SetNRIDescriptorSet(m_NRISetIndex, set);
					m_RenderCommandBuffer->RT_SetNRIRootConstants(&pushConstants, sizeof(pushConstants));
					m_RenderCommandBuffer->RT_SetNRIScissor(nvrhi::Rect(static_cast<int>(clipMin.x), static_cast<int>(clipMax.x), static_cast<int>(clipMin.y), static_cast<int>(clipMax.y)));

					nri::DrawIndexedDesc draw = {};
					draw.indexNum = drawCommand.ElemCount;
					draw.instanceNum = 1;
					draw.baseIndex = drawCommand.IdxOffset + indexOffset;
					draw.baseVertex = static_cast<int32_t>(drawCommand.VtxOffset) + vertexOffset;
					api.CmdDrawIndexed(*commandBuffer, draw);
				}

				vertexOffset += drawList->VtxBuffer.Size;
				indexOffset += drawList->IdxBuffer.Size;
			}

			m_RenderCommandBuffer->RT_EndNRIRenderPass();
		}

		m_RenderCommandBuffer->RT_End();
		m_RenderCommandBuffer->RT_Wait(swapchain->GetAcquiredImageSemaphore());
		m_RenderCommandBuffer->RT_Submit();
		return true;
	}

}
