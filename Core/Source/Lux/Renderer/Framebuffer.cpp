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

#if WENEEDTODEALWITHTHIS
	namespace Utils {

		inline VkAttachmentLoadOp GetVkAttachmentLoadOp(const FramebufferSpecification& specification, const FramebufferTextureSpecification& attachmentSpecification)
		{
			if (attachmentSpecification.LoadOp == AttachmentLoadOp::Inherit)
			{
				if (Utils::IsDepthFormat(attachmentSpecification.Format))
					return specification.ClearDepthOnLoad ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;

				return specification.ClearColorOnLoad ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
			}

			return attachmentSpecification.LoadOp == AttachmentLoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		}

	}
#endif

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
#if DEAL
		if (m_Handle)
		{
			VkFramebuffer framebuffer = m_Framebuffer;
			Renderer::SubmitResourceFree([framebuffer]()
				{
					const auto device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();
					vkDestroyFramebuffer(device, framebuffer, nullptr);
				});

			// Don't free the images if we don't own them
			if (!m_Specification.ExistingFramebuffer)
			{
				uint32_t attachmentIndex = 0;
				for (Ref<Image2D> image : m_AttachmentImages)
				{
					if (m_Specification.ExistingImages.find(attachmentIndex) != m_Specification.ExistingImages.end())
						continue;

					// Only destroy deinterleaved image once and prevent clearing layer views on second framebuffer invalidation
					if (image->GetSpecification().Layers == 1 || attachmentIndex == 0 && !image->GetLayerImageView(0))
						image->Release();
					attachmentIndex++;
				}

				if (m_DepthAttachmentImage)
				{
					// Do we own the depth image?
					if (m_Specification.ExistingImages.find((uint32_t)m_Specification.Attachments.Attachments.size() - 1) == m_Specification.ExistingImages.end())
						m_DepthAttachmentImage->Release();
				}
			}
		}
#endif
	}

	void Framebuffer::Resize(uint32_t width, uint32_t height, bool forceRecreate)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!forceRecreate && (m_Width == width && m_Height == height))
			return;

#if 0
		Ref<Framebuffer> instance = this;
		Renderer::Submit([instance, width, height]() mutable
			{
				instance->m_Width = (uint32_t)(width * instance->m_Specification.Scale);
				instance->m_Height = (uint32_t)(height * instance->m_Specification.Scale);
				if (!instance->m_Specification.SwapChainTarget)
				{
					instance->RT_Invalidate();
				}
				else
				{
					HZ_CORE_VERIFY(false);
#if DEAL
					VulkanSwapChain& swapChain = Application::Get().GetWindow().GetSwapChain();
					instance->m_RenderPass = swapChain.GetRenderPass();

					instance->m_ClearValues.clear();
					instance->m_ClearValues.emplace_back().Color = { 0.0f, 0.0f, 0.0f, 1.0f };
#endif
				}

			});
#endif

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

				nvrhi::FramebufferAttachment& depthAttachment = framebufferDesc.depthAttachment;
				depthAttachment.texture = m_DepthAttachmentImage->GetHandle();
				depthAttachment.format = Utils::NVRHIFormat(attachmentSpec.Format);
				if (m_Specification.ExistingImageLayer != -1)
				{
					depthAttachment.subresources.baseArraySlice = m_Specification.ExistingImageLayer;
					depthAttachment.subresources.numArraySlices = 1;
				}

				m_ClearValues[attachmentIndex].DepthStencil = { m_Specification.DepthClearValue, 0 };

#if OLD

				VkAttachmentDescription& attachmentDescription = attachmentDescriptions.emplace_back();
				attachmentDescription.flags = 0;
				attachmentDescription.format = Utils::VulkanImageFormat(attachmentSpec.Format);
				attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescription.loadOp = Utils::GetVkAttachmentLoadOp(m_Specification, attachmentSpec);
				attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // TODO: if sampling, needs to be store (otherwise DONT_CARE is fine)
				attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescription.initialLayout = attachmentDescription.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
				if (attachmentSpec.Format == ImageFormat::DEPTH24STENCIL8 || true) // Separate layouts requires a "separate layouts" flag to be enabled
				{
					attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // TODO: if not sampling
					attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; // TODO: if sampling
					depthAttachmentReference = { attachmentIndex, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
				}
				else
				{
					attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL; // TODO: if not sampling
					attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL; // TODO: if sampling
					depthAttachmentReference = { attachmentIndex, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL };
				}
#endif
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

				nvrhi::FramebufferAttachment& colorAttachment = framebufferDesc.colorAttachments.emplace_back();
				colorAttachment.texture = colorAttachmentImage->GetHandle();
				colorAttachment.format = Utils::NVRHIFormat(attachmentSpec.Format);
				if (m_Specification.ExistingImageLayer != -1)
				{
					colorAttachment.subresources.baseArraySlice = m_Specification.ExistingImageLayer;
					colorAttachment.subresources.numArraySlices = 1;
				}
#if OLD
				VkAttachmentDescription& attachmentDescription = attachmentDescriptions.emplace_back();
				attachmentDescription.flags = 0;
				attachmentDescription.format = Utils::VulkanImageFormat(attachmentSpec.Format);
				attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescription.loadOp = Utils::GetVkAttachmentLoadOp(m_Specification, attachmentSpec);
				attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // TODO: if sampling, needs to be store (otherwise DONT_CARE is fine)
				attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescription.initialLayout = attachmentDescription.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
#endif

				const auto& clearColor = m_Specification.ClearColor;
				m_ClearValues[attachmentIndex].Color = { {clearColor.r, clearColor.g, clearColor.b, clearColor.a } };
			}

			attachmentIndex++;
		}

#if OLD
		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = uint32_t(colorAttachmentReferences.size());
		subpassDescription.pColorAttachments = colorAttachmentReferences.data();
		if (m_DepthAttachmentImage)
			subpassDescription.pDepthStencilAttachment = &depthAttachmentReference;

		// TODO: do we need these?
		// Use subpass dependencies for layout transitions
		std::vector<VkSubpassDependency> dependencies;

		if (m_AttachmentImages.size())
		{
			{
				VkSubpassDependency& depedency = dependencies.emplace_back();
				depedency.srcSubpass = VK_SUBPASS_EXTERNAL;
				depedency.dstSubpass = 0;
				depedency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				depedency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				depedency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				depedency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				depedency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			}
			{
				VkSubpassDependency& depedency = dependencies.emplace_back();
				depedency.srcSubpass = 0;
				depedency.dstSubpass = VK_SUBPASS_EXTERNAL;
				depedency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				depedency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				depedency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				depedency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				depedency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			}
		}

		if (m_DepthAttachmentImage)
		{
			{
				VkSubpassDependency& depedency = dependencies.emplace_back();
				depedency.srcSubpass = VK_SUBPASS_EXTERNAL;
				depedency.dstSubpass = 0;
				depedency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				depedency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
				depedency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				depedency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				depedency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			}

			{
				VkSubpassDependency& depedency = dependencies.emplace_back();
				depedency.srcSubpass = 0;
				depedency.dstSubpass = VK_SUBPASS_EXTERNAL;
				depedency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				depedency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				depedency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				depedency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				depedency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			}
		}

		// Create the actual renderpass
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
		renderPassInfo.pAttachments = attachmentDescriptions.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_RenderPass));
		VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_RENDER_PASS, m_Specification.DebugName, m_RenderPass);

		std::vector<VkImageView> attachments(m_AttachmentImages.size());
		for (uint32_t i = 0; i < m_AttachmentImages.size(); i++)
		{
			Ref<VulkanImage2D> image = m_AttachmentImages[i].As<VulkanImage2D>();
			if (image->GetSpecification().Layers > 1)
				attachments[i] = image->GetLayerImageView(m_Specification.ExistingImageLayers[i]);
			else
				attachments[i] = image->GetImageInfo().ImageView;
			HZ_CORE_ASSERT(attachments[i]);
		}

		if (m_DepthAttachmentImage)
		{
			Ref<VulkanImage2D> image = m_DepthAttachmentImage.As<VulkanImage2D>();
			if (m_Specification.ExistingImage && image->GetSpecification().Layers > 1)
			{
				HZ_CORE_ASSERT(m_Specification.ExistingImageLayers.size() == 1, "Depth attachments do not support deinterleaving");
				attachments.emplace_back(image->GetLayerImageView(m_Specification.ExistingImageLayers[0]));
			}
			else
				attachments.emplace_back(image->GetImageInfo().ImageView);

			HZ_CORE_ASSERT(attachments.back());
		}

		VkFramebufferCreateInfo framebufferCreateInfo = {};
		framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferCreateInfo.renderPass = m_RenderPass;
		framebufferCreateInfo.attachmentCount = uint32_t(attachments.size());
		framebufferCreateInfo.pAttachments = attachments.data();
		framebufferCreateInfo.width = m_Width;
		framebufferCreateInfo.height = m_Height;
		framebufferCreateInfo.layers = 1;

		VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &m_Framebuffer));
		VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_FRAMEBUFFER, m_Specification.DebugName, m_Framebuffer);
#endif

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		m_Handle = device->createFramebuffer(framebufferDesc);
		m_FramebufferDesc = framebufferDesc;
	}

}
