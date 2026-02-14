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
#include "lpch.h"
#include "ImGuiRenderer.h"

#include "Lux/Renderer/Shader.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Debug/Profiler.h"

#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

#include "nvrhi/utils.h"

#include <format>

namespace Lux {

	struct VERTEX_CONSTANT_BUFFER
	{
		float        mvp[4][4];
	};

	bool ImGuiRenderer::UpdateFontTexture()
	{
		nvrhi::IDevice* device = Application::GetGraphicsDevice();

		ImGuiIO& io = ImGui::GetIO();

		io.BackendRendererName = "HazelImGuiRenderer";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
		io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;  // We can create multi-viewports on the Renderer side (optional)
		// If the font texture exists and is bound to ImGui, we're done.
		// Note: ImGui_Renderer will reset io.Fonts->TexID when new fonts are added.
		if (m_FontTexture && io.Fonts->TexID)
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

		if (m_FontTexture == nullptr)
			return false;

		m_RenderCommandBuffer->RT_Begin();
		nvrhi::CommandListHandle commandList = m_RenderCommandBuffer->GetActive();

		commandList->beginTrackingTextureState(m_FontTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);

		commandList->writeTexture(m_FontTexture, 0, 0, pixels, width * 4);

		commandList->setPermanentTextureState(m_FontTexture, nvrhi::ResourceStates::ShaderResource);
		commandList->commitBarriers();

		m_RenderCommandBuffer->RT_End();
		m_RenderCommandBuffer->RT_Submit();

		io.Fonts->TexID = RegisterPersistentTexture(m_FontTexture.Get(), nvrhi::AllSubresources);

		return true;
	}

	bool ImGuiRenderer::Init()
	{
		auto device = Application::GetGraphicsDevice();

		m_RenderCommandBuffer = RenderCommandBuffer::Create(0, "ImGuiRenderer");

		Ref<Shader> imguiShader = Renderer::GetShaderLibrary()->Get("ImGui");
		m_VertexShader = imguiShader->GetHandle(nvrhi::ShaderType::Vertex);
		m_PixelShader = imguiShader->GetHandle(nvrhi::ShaderType::Pixel);

		// create attribute layout object
		nvrhi::VertexAttributeDesc vertexAttribLayout[] = {
			{ "POSITION", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert,pos), sizeof(ImDrawVert), false },
			{ "TEXCOORD", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert,uv),  sizeof(ImDrawVert), false },
			{ "COLOR",    nvrhi::Format::RGBA8_UNORM, 1, 0, offsetof(ImDrawVert,col), sizeof(ImDrawVert), false },
		};

		m_ShaderAttribLayout = device->createInputLayout(vertexAttribLayout, sizeof(vertexAttribLayout) / sizeof(vertexAttribLayout[0]), m_VertexShader);

		// create PSO
		{
			nvrhi::BlendState blendState;
			blendState.targets[0].setBlendEnable(true)
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
				// Push constants: Scale (vec2) + Translate (vec2) + Flags (uint) = 20 bytes
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

			if (m_FontSampler == nullptr)
				return false;
		}

		return true;
	}

	bool ImGuiRenderer::ReallocateBuffer(nvrhi::BufferHandle& buffer, size_t requiredSize, size_t reallocateSize, const bool indexBuffer)
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
			desc.initialState = indexBuffer ? nvrhi::ResourceStates::IndexBuffer : nvrhi::ResourceStates::VertexBuffer;
			desc.keepInitialState = true;

			buffer = device->createBuffer(desc);

			if (!buffer)
			{
				return false;
			}
		}

		return true;
	}

	nvrhi::GraphicsPipelineHandle ImGuiRenderer::GetOrCreatePipeline(VulkanSwapChain* swapchain)
	{
		uint32_t currentFramebufferIndex = swapchain->GetCurrentBackBufferIndex();
		auto& swapchainPipelineCache = m_PipelineCache[swapchain];
		LUX_CORE_VERIFY(currentFramebufferIndex < swapchainPipelineCache.Pipelines.max_size());

		nvrhi::FramebufferHandle targetFramebuffer = swapchain->GetCurrentFramebuffer();

		nvrhi::GraphicsPipelineHandle pipeline = swapchainPipelineCache.Pipelines[currentFramebufferIndex];
		bool invalidate = !pipeline || swapchainPipelineCache.Framebuffers[currentFramebufferIndex] != targetFramebuffer;
		if (invalidate)
		{
			nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
			pipeline = device->createGraphicsPipeline(m_BasePSODesc, targetFramebuffer);
			swapchainPipelineCache.Pipelines[currentFramebufferIndex] = pipeline;
			swapchainPipelineCache.Framebuffers[currentFramebufferIndex] = targetFramebuffer;
		}
		return pipeline;
	}

	ImTextureID ImGuiRenderer::RegisterPersistentTexture(nvrhi::ITexture* texture,
		nvrhi::TextureSubresourceSet subresources)
	{
		LUX_CORE_ASSERT(m_NextPersistentIndex < PersistentHandleCount, "Too many persistent textures!");
		LUX_CORE_ASSERT(texture, "RegisterPersistentTexture called with null texture!");

		// Resolve AllSubresources (-1 values) to actual texture dimensions
		nvrhi::TextureSubresourceSet resolvedSubresources = subresources;
		const nvrhi::TextureDesc& texDesc = texture->getDesc();

		if (resolvedSubresources.numMipLevels == nvrhi::TextureSubresourceSet::AllMipLevels)
			resolvedSubresources.numMipLevels = texDesc.mipLevels - resolvedSubresources.baseMipLevel;
		if (resolvedSubresources.numArraySlices == nvrhi::TextureSubresourceSet::AllArraySlices)
			resolvedSubresources.numArraySlices = texDesc.arraySize - resolvedSubresources.baseArraySlice;

		uint32_t index = m_NextPersistentIndex++;

		// Resize if needed
		if (m_PersistentTextures.size() <= index)
			m_PersistentTextures.resize(index + 1);

		m_PersistentTextures[index] = { texture, resolvedSubresources };
		return (ImTextureID)(uintptr_t)index;
	}

	ImTextureID ImGuiRenderer::CreateFrameTexture(nvrhi::ITexture* texture,
		nvrhi::TextureSubresourceSet subresources,
		bool forceOpaque,
		bool isGrayscale)
	{
		LUX_CORE_ASSERT(texture, "GetTextureHandle called with null texture!");

		// Resolve AllSubresources (-1 values) to actual texture dimensions for consistent caching
		nvrhi::TextureSubresourceSet resolvedSubresources = subresources;
		const nvrhi::TextureDesc& texDesc = texture->getDesc();

		if (resolvedSubresources.numMipLevels == nvrhi::TextureSubresourceSet::AllMipLevels)
			resolvedSubresources.numMipLevels = texDesc.mipLevels - resolvedSubresources.baseMipLevel;
		if (resolvedSubresources.numArraySlices == nvrhi::TextureSubresourceSet::AllArraySlices)
			resolvedSubresources.numArraySlices = texDesc.arraySize - resolvedSubresources.baseArraySlice;

		ImGuiTextureInfo key{ texture, resolvedSubresources, forceOpaque, isGrayscale };

		// Check if already registered this frame
		auto it = m_FrameTextureMap.find(key);
		if (it != m_FrameTextureMap.end())
			return (ImTextureID)it->second;  // Already has frame counter encoded

		// Register new texture for this frame (offset by PersistentHandleCount)
		uint32_t index = PersistentHandleCount + (uint32_t)m_FrameTextures.size();
		m_FrameTextures.push_back({ texture, resolvedSubresources, forceOpaque, isGrayscale });

		// Encode frame counter in upper 32 bits, index in lower 32 bits
		uint64_t handle = ((uint64_t)m_FrameCounter << 32) | index;
		m_FrameTextureMap[key] = handle;

		return (ImTextureID)handle;
	}

	nvrhi::IBindingSet* ImGuiRenderer::GetBindingSet(const ImGuiTextureInfo& texInfo)
	{
		nvrhi::IDevice* device = Application::GetGraphicsDevice();

		// Resolve AllSubresources (-1 values) to actual texture dimensions for proper caching
		nvrhi::TextureSubresourceSet resolvedSubresources = texInfo.Subresources;
		const nvrhi::TextureDesc& texDesc = texInfo.Texture->getDesc();

		if (resolvedSubresources.numMipLevels == nvrhi::TextureSubresourceSet::AllMipLevels)
			resolvedSubresources.numMipLevels = texDesc.mipLevels - resolvedSubresources.baseMipLevel;
		if (resolvedSubresources.numArraySlices == nvrhi::TextureSubresourceSet::AllArraySlices)
			resolvedSubresources.numArraySlices = texDesc.arraySize - resolvedSubresources.baseArraySlice;

		// Use texture+resolved subresources as cache key
		ImGuiTextureInfo key{ texInfo.Texture, resolvedSubresources };

		auto iter = m_BindingsCache.find(key);
		if (iter != m_BindingsCache.end())
		{
			return iter->second;
		}

		nvrhi::BindingSetDesc desc;

		desc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(float) * 2),
			nvrhi::BindingSetItem::Texture_SRV(0, texInfo.Texture, nvrhi::Format::UNKNOWN, resolvedSubresources),
			nvrhi::BindingSetItem::Sampler(1, m_FontSampler)
		};

		nvrhi::BindingSetHandle binding = device->createBindingSet(desc, m_BindingLayout);
		LUX_CORE_ASSERT(binding);

		m_BindingsCache[key] = binding;
		return binding;
	}

	bool ImGuiRenderer::UpdateGeometry(ImDrawData* drawData)
	{
		nvrhi::CommandListHandle commandList = m_RenderCommandBuffer->GetActive();

		// create/resize vertex and index buffers if needed
		if (!ReallocateBuffer(m_VertexBuffer,
			drawData->TotalVtxCount * sizeof(ImDrawVert),
			(drawData->TotalVtxCount + 5000) * sizeof(ImDrawVert),
			false))
		{
			return false;
		}

		if (!ReallocateBuffer(m_IndexBuffer,
			drawData->TotalIdxCount * sizeof(ImDrawIdx),
			(drawData->TotalIdxCount + 5000) * sizeof(ImDrawIdx),
			true))
		{
			return false;
		}

		m_VertexBufferData.resize(m_VertexBuffer->getDesc().byteSize / sizeof(ImDrawVert));
		m_IndexBufferData.resize(m_IndexBuffer->getDesc().byteSize / sizeof(ImDrawIdx));

		// copy and convert all vertices into a single contiguous buffer
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

	bool ImGuiRenderer::Render(ImGuiViewport* viewport, nvrhi::GraphicsPipelineHandle pipeline, nvrhi::FramebufferHandle framebuffer, VkSemaphore waitSemaphore)
	{
		LUX_PROFILE_FUNC("ImGuiRenderer::Render");

		nvrhi::IDevice* device = Application::GetGraphicsDevice();

		ImDrawData* drawData = viewport->DrawData;

		m_RenderCommandBuffer->RT_Begin();
		nvrhi::CommandListHandle commandList = m_RenderCommandBuffer->GetActive();

		// std::string markerName = std::format("ImGui (Viewport {})", viewport == ImGui::GetMainViewport() ? "Main" : std::to_string((uint64_t)viewport));
		// m_CommandList->beginMarker(markerName.c_str());
		nvrhi::utils::ClearColorAttachment(m_RenderCommandBuffer->GetActive(), framebuffer, 0, nvrhi::Color(1, 0, 1, 1));

		if (!UpdateGeometry(drawData))
		{
			m_RenderCommandBuffer->End();
			return false;
		}

		// handle DPI scaling
		drawData->ScaleClipRects(drawData->FramebufferScale);

		struct PushConstants
		{
			glm::vec2 Scale;
			glm::vec2 Translate;
			uint32_t Flags;  // Bit 0: ForceOpaque
		} pushConstants;

		pushConstants.Scale.x = 2.0f / drawData->DisplaySize.x;
		pushConstants.Scale.y = 2.0f / drawData->DisplaySize.y;
		pushConstants.Translate.x = -1.0f - drawData->DisplayPos.x * pushConstants.Scale.x;
		pushConstants.Translate.y = -1.0f - drawData->DisplayPos.y * pushConstants.Scale.y;
		pushConstants.Flags = 0;

		float fbWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
		float fbHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;

		// set up graphics state
		nvrhi::GraphicsState drawState;

		drawState.framebuffer = framebuffer;
		LUX_CORE_ASSERT(drawState.framebuffer);

		drawState.pipeline = pipeline;

		drawState.viewport.viewports.push_back(nvrhi::Viewport(fbWidth, fbHeight));
		drawState.viewport.scissorRects.resize(1);  // updated below

		nvrhi::VertexBufferBinding vbufBinding;
		vbufBinding.buffer = m_VertexBuffer;
		vbufBinding.slot = 0;
		vbufBinding.offset = 0;
		drawState.vertexBuffers.push_back(vbufBinding);

		drawState.indexBuffer.buffer = m_IndexBuffer;
		drawState.indexBuffer.format = (sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT);
		drawState.indexBuffer.offset = 0;

		// Will project scissor/clipping rectangles into framebuffer space
		ImVec2 clip_off = drawData->DisplayPos;         // (0,0) unless using multi-viewports
		ImVec2 clip_scale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

		// render command lists
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
				}
				else
				{
					// Resolve texture handle to texture info
					uint64_t handle = (uint64_t)pCmd->TextureId;
					uint32_t texIndex = (uint32_t)(handle & 0xFFFFFFFF);       // Lower 32 bits
					uint32_t frameCounter = (uint32_t)(handle >> 32);          // Upper 32 bits

					ImGuiTextureInfo texInfo;

					if (texIndex < PersistentHandleCount)
					{
						// Persistent texture (0-63) - frame counter should be 0
						LUX_CORE_ASSERT(frameCounter == 0, "Persistent handle should have frame counter 0");
						LUX_CORE_ASSERT(texIndex < m_PersistentTextures.size(), "Invalid persistent texture handle");
						texInfo = m_PersistentTextures[texIndex];
					}
					else
					{
						// Per-frame texture (64+) - validate frame counter matches current frame
						LUX_CORE_ASSERT(frameCounter == m_FrameCounter,
							"Stale texture handle detected! Handle from frame {} used in frame {}",
							frameCounter, m_FrameCounter);

						uint32_t frameIndex = texIndex - PersistentHandleCount;
						LUX_CORE_ASSERT(frameIndex < m_FrameTextures.size(), "Invalid frame texture handle");
						texInfo = m_FrameTextures[frameIndex];
					}

					LUX_CORE_ASSERT(texInfo.Texture, "Texture is null in ImGuiTextureInfo!");
					drawState.bindings = { GetBindingSet(texInfo) };
					LUX_CORE_ASSERT(drawState.bindings[0]);

					// Set ForceOpaque flag for debug texture visualization
					pushConstants.Flags = (texInfo.ForceOpaque ? 1 : 0) | (texInfo.IsGrayscale ? 2 : 0);

					// Project scissor/clipping rectangles into framebuffer space
					ImVec2 clipMin((pCmd->ClipRect.x - clip_off.x) * clip_scale.x, (pCmd->ClipRect.y - clip_off.y) * clip_scale.y);
					ImVec2 clipMax((pCmd->ClipRect.z - clip_off.x) * clip_scale.x, (pCmd->ClipRect.w - clip_off.y) * clip_scale.y);

					// Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
					if (clipMin.x < 0.0f)
						clipMin.x = 0.0f;
					if (clipMin.y < 0.0f)
						clipMin.y = 0.0f;
					if (clipMax.x > fbWidth)
						clipMax.x = (float)fbWidth;
					if (clipMax.y > fbHeight)
						clipMax.y = (float)fbHeight;
					if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
						continue;

					drawState.viewport.scissorRects[0] = nvrhi::Rect(clipMin.x, clipMax.x, clipMin.y, clipMax.y);

					nvrhi::DrawArguments drawArguments;
					drawArguments.vertexCount = pCmd->ElemCount;
					drawArguments.startIndexLocation = pCmd->IdxOffset + idxOffset;
					drawArguments.startVertexLocation = pCmd->VtxOffset + vtxOffset;

					m_RenderCommandBuffer->GetActive()->setGraphicsState(drawState);
					commandList->setPushConstants(&pushConstants, sizeof(PushConstants));
					commandList->drawIndexed(drawArguments);
				}

			}

			vtxOffset += cmdList->VtxBuffer.Size;
			idxOffset += cmdList->IdxBuffer.Size;
		}

		m_RenderCommandBuffer->RT_End();

		// Wait for image to be available before rendering to it
		if (waitSemaphore)
			m_RenderCommandBuffer->RT_Wait(waitSemaphore);

		m_RenderCommandBuffer->RT_Submit();

		// Clear per-frame texture handles after rendering
		m_FrameTextures.clear();
		m_FrameTextureMap.clear();
		m_FrameCounter++;

		return true;
	}

	bool ImGuiRenderer::RenderToSwapchain(ImGuiViewport* viewport, VulkanSwapChain* swapchain)
	{
		return Render(viewport, GetOrCreatePipeline(swapchain), swapchain->GetCurrentFramebuffer(), swapchain->GetAcquiredImageSemaphore());
	}

	void ImGuiRenderer::BackbufferResizing()
	{
		m_PipelineCache.clear();
	}

}
