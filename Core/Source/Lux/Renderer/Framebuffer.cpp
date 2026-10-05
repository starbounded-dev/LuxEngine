// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Framebuffer.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

namespace Lux {

	namespace
	{
		static void PopulateClearValues(const FramebufferSpecification& specification, std::vector<ClearValue>& clearValues)
		{
			clearValues.resize(specification.Attachments.Attachments.size());

			for (uint32_t attachmentIndex = 0; attachmentIndex < specification.Attachments.Attachments.size(); attachmentIndex++)
			{
				const auto& attachmentSpec = specification.Attachments.Attachments[attachmentIndex];
				if (Utils::IsDepthFormat(attachmentSpec.Format))
				{
					clearValues[attachmentIndex].DepthStencil = { specification.DepthClearValue, 0 };
					continue;
				}

				const auto& clearColor = specification.ClearColor;
				clearValues[attachmentIndex].Color = { { clearColor.r, clearColor.g, clearColor.b, clearColor.a } };
			}
		}
	}


	Framebuffer::Framebuffer(const FramebufferSpecification& specification)
		: m_Specification(specification)
	{
		if (specification.Width == 0)
		{
			m_Width = Application::Get().GetWindow().GetWidth();
			m_Height = Application::Get().GetWindow().GetHeight();
		}
		else
		{
			m_Width = (uint32_t)(specification.Width * m_Specification.Scale);
			m_Height = (uint32_t)(specification.Height * m_Specification.Scale);
		}

		if (m_Specification.SwapChainTarget)
		{
			LUX_CORE_ASSERT(!m_Specification.Attachments.Attachments.empty(), "Swapchain framebuffers require at least one attachment");
			if (m_Specification.Attachments.Attachments.empty())
			{
				LUX_CORE_ERROR("[Framebuffer] Swapchain target '{}' has no attachments; m_ClearValues cannot be populated", m_Specification.DebugName);
				return;
			}

			PopulateClearValues(m_Specification, m_ClearValues);
			return;
		}

		// Create all image objects immediately so we can start referencing them
		// elsewhere
		uint32_t attachmentIndex = 0;
		if (!m_Specification.ExistingFramebuffer)
		{
			for (auto& attachmentSpec : m_Specification.Attachments.Attachments)
			{
				if (m_Specification.ExistingImage)
				{
					if (Utils::IsDepthFormat(attachmentSpec.Format))
						m_DepthAttachmentImage = m_Specification.ExistingImage;
					else
						m_AttachmentImages.emplace_back(m_Specification.ExistingImage);
				}
				else if (m_Specification.ExistingImages.find(attachmentIndex) != m_Specification.ExistingImages.end())
				{
					if (Utils::IsDepthFormat(attachmentSpec.Format))
						m_DepthAttachmentImage = m_Specification.ExistingImages.at(attachmentIndex);
					else
						m_AttachmentImages.emplace_back(); // This will be set later
				}
				else if (Utils::IsDepthFormat(attachmentSpec.Format))
				{
					ImageSpecification spec;
					spec.Format = attachmentSpec.Format;
					spec.Usage = ImageUsage::Attachment;
					spec.Transfer = m_Specification.Transfer;
					spec.Width = (uint32_t)(m_Width * m_Specification.Scale);
					spec.Height = (uint32_t)(m_Height * m_Specification.Scale);
					spec.Samples = m_Specification.Samples;
					spec.DebugName = std::format("{0}-DepthAttachment{1}", m_Specification.DebugName.empty() ? "Unnamed FB" : m_Specification.DebugName, attachmentIndex);
					m_DepthAttachmentImage = Image2D::Create(spec);
				}
				else
				{
					ImageSpecification spec;
					spec.Format = attachmentSpec.Format;
					spec.Usage = ImageUsage::Attachment;
					spec.Transfer = m_Specification.Transfer;
					spec.Width = (uint32_t)(m_Width * m_Specification.Scale);
					spec.Height = (uint32_t)(m_Height * m_Specification.Scale);
					spec.Samples = m_Specification.Samples;
					spec.DebugName = std::format("{0}-ColorAttachment{1}", m_Specification.DebugName.empty() ? "Unnamed FB" : m_Specification.DebugName, attachmentIndex);
					m_AttachmentImages.emplace_back(Image2D::Create(spec));
				}
				attachmentIndex++;
			}
		}

		LUX_CORE_ASSERT(specification.Attachments.Attachments.size());
		Resize(m_Width, m_Height, true);
	}


	Framebuffer::~Framebuffer()
	{
		Release();
	}

	uint32_t Framebuffer::GetWidth() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_Specification.SwapChainTarget)
			return Application::Get().GetWindow().GetSwapChain().GetWidth();

		return m_Width;
	}

	uint32_t Framebuffer::GetHeight() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_Specification.SwapChainTarget)
			return Application::Get().GetWindow().GetSwapChain().GetHeight();

		return m_Height;
	}

	nvrhi::FramebufferHandle Framebuffer::GetHandle() const
	{
		if (m_Specification.SwapChainTarget)
			return Application::Get().GetWindow().GetSwapChain().GetCurrentFramebuffer();

		return m_Handle;
	}

	void Framebuffer::Release()
	{
		LUX_PROFILE_FUNCTION_AUTO;
	}

	void Framebuffer::Resize(uint32_t width, uint32_t height, bool forceRecreate)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!forceRecreate && (m_Width == width && m_Height == height))
			return;

		// RT_Invalidate destroys and recreates the nvrhi images and framebuffer immediately, on
		// whichever thread called Resize - despite the RT_ prefix, this is the main thread. If the
		// render thread is still draining its queue it can be issuing commands against the very
		// resources being freed, which surfaces either as heap corruption when the old framebuffer
		// handle is finally released, or as a near-null dereference inside the driver's
		// vkCmdPipelineBarrier2 when commitBarriers touches a destroyed image. Dragging a dock
		// splitter resizes every frame, so the window is wide open.
		//
		// Drain the render thread first, the same way swap chain recreation does (see Window.cpp).
		// This only waits for State::Idle - it does not advance the frame or swap queues - so it is
		// safe mid-frame, and it is a no-op under the single-threaded policy or when the render
		// thread is already idle. Skipped when called *from* the render thread, where waiting on
		// ourselves would deadlock and the work is already correctly ordered.
		if (!RenderThread::IsCurrentThreadRT())
			Application::Get().GetRenderThread().BlockUntilRenderComplete();

		m_Width = (uint32_t)(width * m_Specification.Scale);
		m_Height = (uint32_t)(height * m_Specification.Scale);
		if (m_Specification.SwapChainTarget)
			PopulateClearValues(m_Specification, m_ClearValues);
		else
			RT_Invalidate();

		for (auto& callback : m_ResizeCallbacks)
			callback(this);
	}

	bool Framebuffer::HasStaleAttachments() const
	{
		if (!m_Handle)
			return false;

		for (size_t i = 0; i < m_AttachmentImages.size() && i < m_FramebufferDesc.colorAttachments.size(); i++)
		{
			const Ref<Image2D>& image = m_AttachmentImages[i];
			if (image && image->GetHandle().Get() != m_FramebufferDesc.colorAttachments[i].texture)
				return true;
		}

		if (m_DepthAttachmentImage && m_DepthAttachmentImage->GetHandle().Get() != m_FramebufferDesc.depthAttachment.texture)
			return true;

		return false;
	}

	void Framebuffer::AddResizeCallback(const std::function<void(Ref<Framebuffer>)>& func)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_ResizeCallbacks.push_back(func);
	}

	void Framebuffer::Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref< Framebuffer> instance = this;
		Renderer::Submit([instance]() mutable
			{
				//instance->RT_Invalidate();
			});
		RT_Invalidate();
	}

	void Framebuffer::RT_Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// LUX_CORE_TRACE("Framebuffer::RT_Invalidate ({})", m_Specification.DebugName);

		Release();

		std::vector<nvrhi::FramebufferAttachment> attachmentDescriptions;

		m_ClearValues.resize(m_Specification.Attachments.Attachments.size());

		bool createImages = m_AttachmentImages.empty();

		if (m_Specification.ExistingFramebuffer)
			m_AttachmentImages.clear();

		// TODO(Yan): what about load/store ops?
		nvrhi::FramebufferDesc framebufferDesc;

		uint32_t attachmentIndex = 0;
		uint32_t nvrhiAttachmentIndex = 0;
		for (const auto& attachmentSpec : m_Specification.Attachments.Attachments)
		{
			if (Utils::IsDepthFormat(attachmentSpec.Format))
			{
				if (m_Specification.ExistingImage)
				{
					m_DepthAttachmentImage = m_Specification.ExistingImage;
				}
				else if (m_Specification.ExistingFramebuffer)
				{
					Ref<Framebuffer> existingFramebuffer = m_Specification.ExistingFramebuffer.As<Framebuffer>();
					m_DepthAttachmentImage = existingFramebuffer->GetDepthImage();
				}
				else if (m_Specification.ExistingImages.find(attachmentIndex) != m_Specification.ExistingImages.end())
				{
					Ref<Image2D> existingImage = m_Specification.ExistingImages.at(attachmentIndex);
					LUX_CORE_ASSERT(Utils::IsDepthFormat(existingImage->GetSpecification().Format), "Trying to attach non-depth image as depth attachment");
					m_DepthAttachmentImage = existingImage;
				}
				else
				{
					Ref<Image2D> depthAttachmentImage = m_DepthAttachmentImage;
					auto& spec = depthAttachmentImage->GetSpecification();
					spec.Width = (uint32_t)(m_Width * m_Specification.Scale);
					spec.Height = (uint32_t)(m_Height * m_Specification.Scale);
					depthAttachmentImage->RT_Invalidate(); // Create immediately
				}

				LUX_CORE_VERIFY(m_DepthAttachmentImage->GetHandle(), "Framebuffer \"{}\" depth attachment {} has no valid image", m_Specification.DebugName, attachmentIndex);

				nvrhi::FramebufferAttachment& depthAttachment = framebufferDesc.depthAttachment;
				depthAttachment.texture = m_DepthAttachmentImage->GetHandle();
				depthAttachment.format = Utils::NVRHIFormat(attachmentSpec.Format);
				if (m_Specification.ExistingImageLayer != -1)
				{
					depthAttachment.subresources.baseArraySlice = m_Specification.ExistingImageLayer;
					depthAttachment.subresources.numArraySlices = 1;
				}

				m_ClearValues[attachmentIndex].DepthStencil = { m_Specification.DepthClearValue, 0 };

			}
			else
			{
				//HZ_CORE_ASSERT(!m_Specification.ExistingImage, "Not supported for color attachments");

				Ref<Image2D> colorAttachmentImage;
				if (m_Specification.ExistingFramebuffer)
				{
					Ref<Framebuffer> existingFramebuffer = m_Specification.ExistingFramebuffer.As<Framebuffer>();
					Ref<Image2D> existingImage = existingFramebuffer->GetImage(attachmentIndex);
					colorAttachmentImage = m_AttachmentImages.emplace_back(existingImage).As<Image2D>();
				}
				else if (m_Specification.ExistingImages.find(attachmentIndex) != m_Specification.ExistingImages.end())
				{
					Ref<Image2D> existingImage = m_Specification.ExistingImages[attachmentIndex];
					LUX_CORE_ASSERT(!Utils::IsDepthFormat(existingImage->GetSpecification().Format), "Trying to attach depth image as color attachment");
					colorAttachmentImage = existingImage.As<Image2D>();
					m_AttachmentImages[attachmentIndex] = existingImage;
				}
				else
				{
					if (createImages)
					{
						ImageSpecification spec;
						spec.Format = attachmentSpec.Format;
						spec.Usage = ImageUsage::Attachment;
						spec.Transfer = m_Specification.Transfer;
						spec.Width = (uint32_t)(m_Width * m_Specification.Scale);
						spec.Height = (uint32_t)(m_Height * m_Specification.Scale);
						colorAttachmentImage = m_AttachmentImages.emplace_back(Image2D::Create(spec)).As<Image2D>();
						LUX_CORE_VERIFY(false);

					}
					else
					{
						colorAttachmentImage = m_AttachmentImages[attachmentIndex];
						ImageSpecification& spec = colorAttachmentImage->GetSpecification();
						spec.Width = (uint32_t)(m_Width * m_Specification.Scale);
						spec.Height = (uint32_t)(m_Height * m_Specification.Scale);
						colorAttachmentImage->RT_Invalidate(); // Create immediately
						//if (colorAttachmentImage->GetSpecification().Layers == 1)
						//else if (attachmentIndex == 0 && m_Specification.ExistingImageLayers[0] == 0)// Only invalidate the first layer from only the first framebuffer
						//{
						//	colorAttachmentImage->RT_Invalidate(); // Create immediately
						//	colorAttachmentImage->RT_CreatePerSpecificLayerImageViews(m_Specification.ExistingImageLayers);
						//}
						//else if (attachmentIndex == 0)
						//{
						//	colorAttachmentImage->RT_CreatePerSpecificLayerImageViews(m_Specification.ExistingImageLayers);
						//}
					}

				}

				LUX_CORE_VERIFY(colorAttachmentImage->GetHandle(), "Framebuffer \"{}\" color attachment {} has no valid image", m_Specification.DebugName, attachmentIndex);

				nvrhi::FramebufferAttachment& colorAttachment = framebufferDesc.colorAttachments.emplace_back();
				colorAttachment.texture = colorAttachmentImage->GetHandle();
				colorAttachment.format = Utils::NVRHIFormat(attachmentSpec.Format);
				if (m_Specification.ExistingImageLayer != -1)
				{
					colorAttachment.subresources.baseArraySlice = m_Specification.ExistingImageLayer;
					colorAttachment.subresources.numArraySlices = 1;
				}

				const auto& clearColor = m_Specification.ClearColor;
				m_ClearValues[attachmentIndex].Color = { {clearColor.r, clearColor.g, clearColor.b, clearColor.a } };
			}

			attachmentIndex++;
		}


		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		m_Handle = device->createFramebuffer(framebufferDesc);
		m_FramebufferDesc = framebufferDesc;
	}

}
