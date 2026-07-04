#include "lpch.h"
#include "Image.h"

#include "Lux/Renderer/RendererAPI.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/Renderer.h"

#include "RenderCommandBuffer.h"

namespace Lux {

	static std::map<nvrhi::ITexture*, WeakRef<Image2D>> s_ImageReferences;

	Image2D::Image2D(const ImageSpecification& specification)
		: m_Specification(specification)
	{
		LUX_CORE_VERIFY(m_Specification.Width > 0 && m_Specification.Height > 0);
	}

	Image2D::~Image2D()
	{
		Release();
	}

	void Image2D::Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
#if INVESTIGATE
		Ref<Image2D> instance = this;
		Renderer::Submit([instance]() mutable
			{
				instance->RT_Invalidate();
			});
#endif

		RT_Invalidate();
	}

	void Image2D::Release()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_TransientAliasSource)
		{
			m_TransientAliasSource = nullptr;
			m_Info = {};
			m_GPUAllocationSize = 0;
			m_PerLayerImageViews.clear();
			m_PerMipImageViews.clear();
			return;
		}

		if (m_Info.ImageHandle)
			s_ImageReferences.erase(m_Info.ImageHandle.Get());

		m_Info.ImageHandle = nullptr;
		m_Info.Sampler = nullptr;
		m_GPUAllocationSize = 0;
		m_PerLayerImageViews.clear();
		m_PerMipImageViews.clear();
	}

	void Image2D::SetTransientAliasSource(Ref<Image2D> source)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(source);
		LUX_CORE_VERIFY(source.Raw() != this);
		LUX_CORE_VERIFY(!source->IsTransientAlias());

		const ImageSpecification& sourceSpec = source->GetSpecification();
		LUX_CORE_VERIFY(m_Specification.Format == sourceSpec.Format);
		LUX_CORE_VERIFY(m_Specification.Usage == sourceSpec.Usage);
		LUX_CORE_VERIFY(m_Specification.Dimension == sourceSpec.Dimension);
		LUX_CORE_VERIFY(m_Specification.Width == sourceSpec.Width);
		LUX_CORE_VERIFY(m_Specification.Height == sourceSpec.Height);
		LUX_CORE_VERIFY(m_Specification.Mips == sourceSpec.Mips);
		LUX_CORE_VERIFY(m_Specification.Layers == sourceSpec.Layers);

		if (m_TransientAliasSource == source)
			return;

		if (m_Info.ImageHandle)
			s_ImageReferences.erase(m_Info.ImageHandle.Get());

		m_Info = {};
		m_GPUAllocationSize = 0;
		m_PerLayerImageViews.clear();
		m_PerMipImageViews.clear();
		m_TransientAliasSource = source;
	}

	void Image2D::ClearTransientAliasSource()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_TransientAliasSource)
			return;

		m_TransientAliasSource = nullptr;
		m_Info = {};
		m_GPUAllocationSize = 0;
		m_PerLayerImageViews.clear();
		m_PerMipImageViews.clear();
	}

	int Image2D::GetClosestMipLevel(uint32_t width, uint32_t height) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (width > m_Specification.Width / 2 || height > m_Specification.Height / 2)
			return 0;

		int a = glm::log2(glm::min(m_Specification.Width, m_Specification.Height));
		int b = glm::log2(glm::min(width, height));
		return a - b;
	}

	std::pair<uint32_t, uint32_t> Image2D::GetMipLevelSize(int mipLevel) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint32_t width = m_Specification.Width;
		uint32_t height = m_Specification.Height;
		return { width >> mipLevel, height >> mipLevel };
	}

	void Image2D::RT_Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(m_Specification.Width > 0 && m_Specification.Height > 0);

		if (m_TransientAliasSource)
		{
			LUX_CORE_VERIFY(m_TransientAliasSource->IsValid());
			m_Info = {};
			m_GPUAllocationSize = 0;
			m_PerLayerImageViews.clear();
			m_PerMipImageViews.clear();
			return;
		}

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();

		Release();

		// Safety net since nvrhi has both Texture2D and Texture2DArray, but we should
		// use the latter if image has many layers since subresource resolution will
		// result in not using anything above the first layer if Texture2D is set
		if (m_Specification.Layers > 1 && m_Specification.Dimension == nvrhi::TextureDimension::Texture2D)
			m_Specification.Dimension = nvrhi::TextureDimension::Texture2DArray;

		nvrhi::TextureDesc textureDesc;
		textureDesc.debugName = m_Specification.DebugName;
		textureDesc.dimension = m_Specification.Dimension;
		textureDesc.format = Utils::NVRHIFormat(m_Specification.Format);
		textureDesc.width = m_Specification.Width;
		textureDesc.height = m_Specification.Height;
		textureDesc.mipLevels = m_Specification.Mips;
		textureDesc.arraySize = m_Specification.Layers;

		// Volume (3D) textures use the depth field and a single array slice.
		if (m_Specification.Dimension == nvrhi::TextureDimension::Texture3D)
		{
			textureDesc.depth = glm::max(m_Specification.Depth, 1u);
			textureDesc.arraySize = 1;
		}

		// NOTE(Yan): tiling?

		textureDesc.isRenderTarget = m_Specification.Usage == ImageUsage::Attachment;
		textureDesc.isUAV = m_Specification.Usage == ImageUsage::Storage;

		// Always has source/dest?
		if (m_Specification.Usage == ImageUsage::Texture)
		{
			textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
			textureDesc.keepInitialState = true;
		}
		else if (m_Specification.Usage == ImageUsage::Attachment)
		{
			textureDesc.initialState = Utils::IsDepthFormat(m_Specification.Format) ?
				nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::RenderTarget;
			textureDesc.keepInitialState = true;
		}
		else if (m_Specification.Usage == ImageUsage::Storage)
		{
			textureDesc.initialState = Utils::IsDepthFormat(m_Specification.Format) ?
				nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::UnorderedAccess;
			textureDesc.keepInitialState = true;
		}

		// UAV (STORAGE usage) disables framebuffer/delta-color compression on many
		// GPUs — a bandwidth tax on every render-target read/write. Grant it only
		// to images actually written by compute shaders: Storage-usage images, and
		// sampled textures with mip chains (Texture2D::GenerateMips is a compute
		// pass that writes each level as a storage image). Attachments never
		// qualify; every compute-written image in the engine is created with
		// Usage::Storage (verified against all shader storage-image bindings).
		const bool formatSupportsUAV = !Utils::IsDepthFormat(m_Specification.Format)
			&& m_Specification.Format != ImageFormat::SRGB
			&& m_Specification.Format != ImageFormat::SRGBA
			&& !Utils::IsBlockCompressed(m_Specification.Format);
		const bool isComputeMipTarget = m_Specification.Usage != ImageUsage::Attachment
			&& m_Specification.Usage != ImageUsage::HostRead
			&& m_Specification.Mips > 1;
		if (formatSupportsUAV && (m_Specification.Usage == ImageUsage::Storage || isComputeMipTarget))
			textureDesc.isUAV = true;

		if (textureDesc.isUAV)
		{
			LUX_CORE_VERIFY(!Utils::IsDepthFormat(m_Specification.Format));
			LUX_CORE_VERIFY(m_Specification.Format != ImageFormat::SRGB);
			LUX_CORE_VERIFY(m_Specification.Format != ImageFormat::SRGBA);
			LUX_CORE_VERIFY(!Utils::IsBlockCompressed(m_Specification.Format));
		}

		m_Info.ImageHandle = device->createTexture(textureDesc);
		m_GPUAllocationSize = Utils::GetImageMemorySize(m_Specification.Format, m_Specification.Width, m_Specification.Height, m_Specification.Mips, m_Specification.Layers);
		if (m_Specification.Dimension == nvrhi::TextureDimension::Texture3D)
			m_GPUAllocationSize *= glm::max(m_Specification.Depth, 1u);

		s_ImageReferences[m_Info.ImageHandle.Get()] = this;

		if (m_Specification.CreateSampler)
		{
			nvrhi::SamplerDesc samplerDesc;
			samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = !Utils::IsIntegerBased(m_Specification.Format);
			samplerDesc.addressU = nvrhi::SamplerAddressMode::ClampToEdge;
			samplerDesc.addressV = samplerDesc.addressW = samplerDesc.addressU;
			samplerDesc.mipBias = m_Specification.MipBias;

			m_Info.Sampler = device->createSampler(samplerDesc);

			// VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_SAMPLER, std::format("{} default sampler", m_Specification.DebugName), m_Info.Sampler);
		}

		m_Info.Dimension = textureDesc.dimension;
	}

	void Image2D::CreatePerLayerImageViews()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref<Image2D> instance = this;
		Renderer::Submit([instance]() mutable
			{
				//instance->RT_CreatePerLayerImageViews();
			});
		RT_CreatePerLayerImageViews();
	}

	void Image2D::RT_CreatePerLayerImageViews()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_ASSERT(m_Specification.Layers > 1);

		m_PerLayerImageViews.resize(m_Specification.Layers);
		for (uint32_t layer = 0; layer < m_Specification.Layers; layer++)
		{
			nvrhi::TextureSubresourceSet& tss = m_PerLayerImageViews[layer];
			tss.baseMipLevel = 0;
			tss.numMipLevels = m_Specification.Mips;
			tss.baseArraySlice = layer;
			tss.numArraySlices = 1;
		}

#if OLD
		VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();

		VkImageAspectFlags aspectMask = Utils::IsDepthFormat(m_Specification.Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		if (m_Specification.Format == ImageFormat::DEPTH24STENCIL8)
			aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

		const VkFormat vulkanFormat = Utils::VulkanImageFormat(m_Specification.Format);

		m_PerLayerImageViews.resize(m_Specification.Layers);
		for (uint32_t layer = 0; layer < m_Specification.Layers; layer++)
		{
			VkImageViewCreateInfo imageViewCreateInfo = {};
			imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewCreateInfo.format = vulkanFormat;
			imageViewCreateInfo.flags = 0;
			imageViewCreateInfo.subresourceRange = {};
			imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
			imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
			imageViewCreateInfo.subresourceRange.levelCount = m_Specification.Mips;
			imageViewCreateInfo.subresourceRange.baseArrayLayer = layer;
			imageViewCreateInfo.subresourceRange.layerCount = 1;
			imageViewCreateInfo.image = m_Info.Image;
			VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &m_PerLayerImageViews[layer]));
			VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, std::format("{} image view layer: {}", m_Specification.DebugName, layer), m_PerLayerImageViews[layer]);
		}
#endif
	}

	nvrhi::TextureSubresourceSet Image2D::GetMipImageView(uint32_t mip)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		auto it = m_PerMipImageViews.find(mip);
		if (it != m_PerMipImageViews.end())
			return it->second;

		nvrhi::TextureSubresourceSet& tss = m_PerMipImageViews[mip];
		tss.baseMipLevel = mip;
		tss.numMipLevels = 1;
		tss.baseArraySlice = 0;
		tss.numArraySlices = 1;
		return tss;
	}

	void Image2D::RT_CreatePerSpecificLayerImageViews(const std::vector<uint32_t>& layerIndices)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_ASSERT(m_Specification.Layers > 1);

		if (m_PerLayerImageViews.empty())
			m_PerLayerImageViews.resize(m_Specification.Layers);

		for (uint32_t layer : layerIndices)
		{
			nvrhi::TextureSubresourceSet& tss = m_PerLayerImageViews[layer];
			tss.baseMipLevel = 0;
			tss.numMipLevels = m_Specification.Mips;
			tss.baseArraySlice = layer;
			tss.numArraySlices = 1;
		}
	}

	const std::map<nvrhi::ITexture*, WeakRef<Image2D>>& Image2D::GetImageRefs()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return s_ImageReferences;
	}

	void Image2D::SetData(Buffer buffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_TransientAliasSource)
		{
			m_TransientAliasSource->SetData(buffer);
			return;
		}

		LUX_CORE_VERIFY(m_Specification.Transfer, "Image must be created with ImageSpecification::Transfer enabled!");

		if (buffer)
		{
			// Shared upload batch — one vkQueueSubmit per texture causes load
			// hitches; see Renderer::RecordResourceUpload.
			Renderer::RecordResourceUpload([&](nvrhi::ICommandList* uploadList)
			{
				uploadList->writeTexture(m_Info.ImageHandle, 0, 0, buffer.Data, Utils::GetImageMemoryRowPitch(m_Specification.Format, m_Specification.Width));
			});

			m_Info.State = nvrhi::ResourceStates::ShaderResource;
		}
	}

	void Image2D::CopyToHostBuffer(Buffer& buffer) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_TransientAliasSource)
		{
			m_TransientAliasSource->CopyToHostBuffer(buffer);
			return;
		}

		nvrhi::IDevice* device = Application::Get().GetWindow().GetDeviceManager()->GetDevice();

		if (!m_CommandList)
			m_CommandList = RenderCommandBuffer::Create(1, "Image2D");

		auto stagingDesc = nvrhi::TextureDesc()
			.setFormat(Utils::NVRHIFormat(m_Specification.Format))
			.setDimension(nvrhi::TextureDimension::Texture2D)
			.setWidth(m_Specification.Width)
			.setHeight(m_Specification.Height)
			.setDepth(1)
			.setMipLevels(m_Specification.Mips)
			.setArraySize(m_Specification.Layers);

		nvrhi::StagingTextureHandle stagingTexture = device->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);

		nvrhi::TextureSlice textureSlice;

		uint32_t mipCount = 1; // TODO(Yan): not sure what the orig code was doing...
		// mipCount = m_Specification.Mips;

		m_CommandList->RT_Begin();

		//commandList->beginTrackingTextureState(m_Info.ImageHandle, nvrhi::AllSubresources, m_Info.State);
		//commandList->setTextureState(m_Info.ImageHandle, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource);
		for (uint32_t mip = 0; mip < mipCount; mip++)
		{
			textureSlice.mipLevel = mip;
			m_CommandList->GetActive()->copyTexture(stagingTexture, textureSlice, m_Info.ImageHandle, textureSlice);
		}
		//commandList->setTextureState(m_Info.ImageHandle, nvrhi::AllSubresources, m_Info.State);

		m_CommandList->RT_End();
		m_CommandList->RT_Submit();

		uint64_t bufferSize = m_Specification.Width * m_Specification.Height * Utils::GetImageFormatBPP(m_Specification.Format);
		buffer.Allocate(bufferSize);
		for (uint32_t mip = 0; mip < mipCount; mip++)
		{
			textureSlice.mipLevel = mip;
			size_t rowPitch;
			void* data = device->mapStagingTexture(stagingTexture, textureSlice, nvrhi::CpuAccessMode::Read, &rowPitch);
			if (!data)
			{
				device->unmapStagingTexture(stagingTexture);
				buffer.Release();
				return;
			}

			const uint64_t rowSize = m_Specification.Width * Utils::GetImageFormatBPP(m_Specification.Format);
			if (rowPitch == rowSize)
			{
				memcpy(buffer.Data, data, buffer.Size);
			}
			else
			{
				byte* dst = static_cast<byte*>(buffer.Data);
				const byte* src = static_cast<const byte*>(data);
				for (uint32_t y = 0; y < m_Specification.Height; y++)
					memcpy(dst + y * rowSize, src + y * rowPitch, rowSize);
			}

			device->unmapStagingTexture(stagingTexture);
		}

#if OLD
		auto device = VulkanContext::GetCurrentDevice();
		auto vulkanDevice = device->GetVulkanDevice();
		VulkanAllocator allocator("Image2D");

		uint64_t bufferSize = m_Specification.Width * m_Specification.Height * Utils::GetImageFormatBPP(m_Specification.Format);

		// Create staging buffer
		VkBufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = bufferSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

#if MEM_INFO
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(vulkanDevice, m_Info.Image, &memReqs);
		HZ_CORE_WARN("MemReq = {} ({})", memReqs.size, memReqs.alignment);
		HZ_CORE_WARN("Expected size = {}", bufferSize);
#endif

		VkBuffer stagingBuffer;
		VmaAllocation stagingBufferAllocation = allocator.AllocateBuffer(bufferCreateInfo, VMA_MEMORY_USAGE_GPU_TO_CPU, stagingBuffer);

		uint32_t mipCount = 1;
		uint32_t mipWidth = m_Specification.Width, mipHeight = m_Specification.Height;

		VkCommandBuffer copyCmd = device->GetCommandBuffer(true);

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = mipCount;
		subresourceRange.layerCount = 1;

		Utils::InsertImageMemoryBarrier(copyCmd, m_Info.Image,
			VK_ACCESS_TRANSFER_READ_BIT, 0,
			m_DescriptorImageInfo.imageLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			subresourceRange);

		uint64_t mipDataOffset = 0;
		for (uint32_t mip = 0; mip < mipCount; mip++)
		{
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = mip;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = mipWidth;
			bufferCopyRegion.imageExtent.height = mipHeight;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = mipDataOffset;

			vkCmdCopyImageToBuffer(
				copyCmd,
				m_Info.Image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				stagingBuffer,
				1,
				&bufferCopyRegion);

			uint64_t mipDataSize = mipWidth * mipHeight * sizeof(float) * 4 * 6;
			mipDataOffset += mipDataSize;
			mipWidth /= 2;
			mipHeight /= 2;
		}

		Utils::InsertImageMemoryBarrier(copyCmd, m_Info.Image,
			VK_ACCESS_TRANSFER_READ_BIT, 0,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_DescriptorImageInfo.imageLayout,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			subresourceRange);

		device->FlushCommandBuffer(copyCmd);

		// Copy data from staging buffer
		uint8_t* srcData = allocator.MapMemory<uint8_t>(stagingBufferAllocation);
		buffer.Allocate(bufferSize);
		memcpy(buffer.Data, srcData, bufferSize);
		allocator.UnmapMemory(stagingBufferAllocation);

		allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);
#endif
	}

	ImageView::ImageView(const ImageViewSpecification& specification)
		: m_Specification(specification)
	{
		Invalidate();
	}

	void ImageView::Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		RT_Invalidate();
	}

	void ImageView::RT_Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_ImageInfo = m_Specification.Image->GetImageInfo();

		m_TextureSubresourceSet.baseMipLevel = m_Specification.Mip;
		m_TextureSubresourceSet.numMipLevels = m_Specification.MipCount == 0
			? m_Specification.Image->GetSpecification().Mips
			: m_Specification.MipCount;

		m_TextureSubresourceSet.baseArraySlice = m_Specification.Layer;
		m_TextureSubresourceSet.numArraySlices = m_Specification.LayerCount == 0
			? m_Specification.Image->GetSpecification().Layers
			: m_Specification.LayerCount;

		m_ImageInfo.ImageView = m_TextureSubresourceSet;
		m_ImageInfo.Dimension = m_Specification.Dimension;
	}

	Sampler::Sampler(const SamplerSpecification& specification)
		: m_Specification(specification)
	{
		Invalidate();
	}

	void Sampler::Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const auto desc = nvrhi::SamplerDesc()
			.setMipBias(m_Specification.MipBias)
			.setMaxAnisotropy(m_Specification.MaxAnisotropy)
			.setAllAddressModes(m_Specification.AddressMode)
			.setMinFilter(m_Specification.MinFilter)
			.setMagFilter(m_Specification.MagFilter)
			.setMipFilter(m_Specification.MipFilter);

		auto device = Application::GetGraphicsDevice();
		m_Handle = device->createSampler(desc);
	}
}
