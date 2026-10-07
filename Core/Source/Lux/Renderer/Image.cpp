// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Image.h"

#include "Lux/Renderer/RendererAPI.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/NVRHIInterop.h"
#include "Lux/Renderer/RHI/RHIDevice.h"

#include "RenderCommandBuffer.h"

namespace Lux {

	// Live-image registry (debug/stats). Written by RT_Invalidate on the render thread and read by
	// the main thread's memory statistics, so every access holds s_ImageReferencesMutex.
	static std::map<nri::Texture*, WeakRef<Image2D>> s_ImageReferences;
	static std::mutex s_ImageReferencesMutex;

	// NVRHI samplers leave the LOD range unclamped.
	static constexpr float k_SamplerMaxLod = 1000.0f;

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

		if (m_Info.RHITexture)
		{
			std::scoped_lock lock(s_ImageReferencesMutex);
			s_ImageReferences.erase(m_Info.RHITexture);
		}

		m_Info.RHITexture = nullptr;
		m_Info.ImageHandle = nullptr;
		m_Info.Sampler = nullptr;
		m_Texture.Reset();
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

		if (m_Info.RHITexture)
		{
			std::scoped_lock lock(s_ImageReferencesMutex);
			s_ImageReferences.erase(m_Info.RHITexture);
		}

		m_Info = {};
		m_Texture.Reset();
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
			m_Texture.Reset();
			m_GPUAllocationSize = 0;
			m_PerLayerImageViews.clear();
			m_PerMipImageViews.clear();
			return;
		}

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();

		// NOTE: deliberately no Release() here. Release() nulls m_Info.ImageHandle before the
		// replacement texture exists, and Invalidate() runs RT_Invalidate on the MAIN thread while
		// the render thread reads GetHandle() (e.g. deferred compute-pass barriers). That null
		// window is what crashed the render thread when resizing a dockspace/viewport panel.
		// Instead the new texture is built into locals below and swapped into m_Info in a single
		// store, so a concurrent reader always sees a valid handle — the old one or the new one,
		// never null. The old texture is released after the swap.

		// Safety net since nvrhi has both Texture2D and Texture2DArray, but we should
		// use the latter if image has many layers since subresource resolution will
		// result in not using anything above the first layer if Texture2D is set
		if (m_Specification.Layers > 1 && m_Specification.Dimension == TextureDimension::Texture2D)
			m_Specification.Dimension = TextureDimension::Texture2DArray;

		nvrhi::TextureDesc textureDesc;
		textureDesc.debugName = m_Specification.DebugName;
		textureDesc.dimension = ToNVRHI(m_Specification.Dimension);
		textureDesc.format = Utils::NVRHIFormat(m_Specification.Format);
		textureDesc.width = m_Specification.Width;
		textureDesc.height = m_Specification.Height;
		textureDesc.mipLevels = m_Specification.Mips;
		textureDesc.arraySize = m_Specification.Layers;
		// A multisampled image has no mip chain and must be a render target - the API
		// cannot generate mips for one, and a sampled-only MS texture is not useful.
		textureDesc.sampleCount = glm::max(1u, m_Specification.Samples);
		if (textureDesc.sampleCount > 1)
		{
			LUX_CORE_VERIFY(m_Specification.Usage == ImageUsage::Attachment,
				"Image \"{}\": multisampling is only valid for attachments", m_Specification.DebugName);
			textureDesc.mipLevels = 1;
		}

		// Volume (3D) textures use the depth field and a single array slice.
		if (m_Specification.Dimension == TextureDimension::Texture3D)
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

		// Build the new texture (and sampler) into locals while the old handle is still live and
		// readable by other threads. NRI owns the image; NVRHI gets a wrapper with this same desc.
		NRITexture newTexture = NRITexture::Create(textureDesc);
		const nvrhi::TextureHandle newHandle = newTexture.GetHandle();

		// Creation fails (empty) when the image or its memory allocation fails, which would
		// otherwise only show up much later as an access violation once the null texture reaches
		// createFramebuffer/BindingSet. Fail here, where the image and its size are still known.
		LUX_CORE_VERIFY(newHandle, "Failed to create image \"{}\" ({}x{}, {} mip(s), {} layer(s), ~{} MB) - the GPU is most likely out of memory",
			m_Specification.DebugName, textureDesc.width, textureDesc.height, textureDesc.mipLevels, textureDesc.arraySize,
			Utils::GetImageMemorySize(m_Specification.Format, m_Specification.Width, m_Specification.Height, m_Specification.Mips, m_Specification.Layers) / (1024 * 1024));

		nvrhi::SamplerHandle newSampler;
		if (m_Specification.CreateSampler)
		{
			nvrhi::SamplerDesc samplerDesc;
			samplerDesc.minFilter = samplerDesc.magFilter = samplerDesc.mipFilter = !Utils::IsIntegerBased(m_Specification.Format);
			samplerDesc.addressU = nvrhi::SamplerAddressMode::ClampToEdge;
			samplerDesc.addressV = samplerDesc.addressW = samplerDesc.addressU;
			samplerDesc.mipBias = m_Specification.MipBias;

			newSampler = device->createSampler(samplerDesc);
		}

		auto newAllocationSize = Utils::GetImageMemorySize(m_Specification.Format, m_Specification.Width, m_Specification.Height, m_Specification.Mips, m_Specification.Layers);
		if (m_Specification.Dimension == TextureDimension::Texture3D)
			newAllocationSize *= glm::max(m_Specification.Depth, 1u);

		// Swap the freshly-built resources in. Assign ImageHandle first, as a single store, so a
		// concurrent render-thread reader (GetHandle) observes either the old texture or the new
		// one — never a null handle mid-recreation.
		nri::Texture* const oldTexture = m_Info.RHITexture;
		m_Info.ImageHandle = newHandle;
		m_Info.RHITexture = newTexture.Get();
		m_Info.Sampler = newSampler;
		m_Info.Dimension = textureDesc.dimension;
		m_GPUAllocationSize = newAllocationSize;

		{
			std::scoped_lock lock(s_ImageReferencesMutex);
			if (oldTexture)
				s_ImageReferences.erase(oldTexture);
			s_ImageReferences[newTexture.Get()] = this;
		}

		// Hands the old texture to the GPU deletion queue: it is destroyed once the frames that may
		// still use it have retired.
		m_Texture = std::move(newTexture);

		// The per-layer/per-mip views wrapped the old texture; drop them so they are rebuilt
		// against the new texture on demand. Done after the handle swap so the old views (which
		// still hold their own refs to the old texture) stay valid until this point.
		m_PerLayerImageViews.clear();
		m_PerMipImageViews.clear();
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
			m_PerLayerImageViews[layer] = { 0, m_Specification.Mips, layer, 1 };
		}

	}

	TextureSubresourceRange Image2D::GetMipImageView(uint32_t mip)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		auto it = m_PerMipImageViews.find(mip);
		if (it != m_PerMipImageViews.end())
			return it->second;

		return m_PerMipImageViews[mip] = { mip, 1, 0, 1 };
	}

	void Image2D::RT_CreatePerSpecificLayerImageViews(const std::vector<uint32_t>& layerIndices)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_ASSERT(m_Specification.Layers > 1);

		if (m_PerLayerImageViews.empty())
			m_PerLayerImageViews.resize(m_Specification.Layers);

		for (uint32_t layer : layerIndices)
		{
			m_PerLayerImageViews[layer] = { 0, m_Specification.Mips, layer, 1 };
		}
	}

	void Image2D::ForEachLiveImage(const std::function<void(const Image2D&)>& fn)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// Held for the whole visit: Release() erases under this lock before an image's members go
		// away, so an image being destroyed on another thread waits here instead of being read
		// while it is freed.
		std::scoped_lock lock(s_ImageReferencesMutex);
		for (const auto& [handle, image] : s_ImageReferences)
		{
			if (image)
				fn(*image);
		}
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
		{
			m_CommandList = RenderCommandBuffer::Create(1, "Image2D");
			// Copies into an NVRHI staging texture, whose state only NVRHI's automatic barriers manage.
			m_CommandList->SetBarrierMode(RenderCommandBuffer::BarrierMode::Automatic);
		}

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

		// Row pitch and row count, not width x height x BPP: BPP is 0 for block-compressed
		// formats, which made every BC texture read back as an empty buffer.
		const uint64_t rowSize = Utils::GetImageMemoryRowPitch(m_Specification.Format, m_Specification.Width);
		const uint32_t rowCount = Utils::GetImageMemoryRowCount(m_Specification.Format, m_Specification.Height);
		buffer.Allocate(rowSize * rowCount);
		for (uint32_t mip = 0; mip < mipCount; mip++)
		{
			textureSlice.mipLevel = mip;
			size_t rowPitch;
			void* data = device->mapStagingTexture(stagingTexture, textureSlice, nvrhi::CpuAccessMode::Read, &rowPitch);
			if (!data)
			{
				// Nothing is mapped, so there is nothing to unmap.
				buffer.Release();
				return;
			}

			if (rowPitch == rowSize)
			{
				memcpy(buffer.Data, data, buffer.Size);
			}
			else
			{
				byte* dst = static_cast<byte*>(buffer.Data);
				const byte* src = static_cast<const byte*>(data);
				for (uint32_t y = 0; y < rowCount; y++)
					memcpy(dst + y * rowSize, src + y * rowPitch, rowSize);
			}

			device->unmapStagingTexture(stagingTexture);
		}
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
		m_ImageInfo.Dimension = ToNVRHI(m_Specification.Dimension);
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
			.setAllAddressModes(ToNVRHI(m_Specification.AddressMode))
			.setMinFilter(m_Specification.MinFilter)
			.setMagFilter(m_Specification.MagFilter)
			.setMipFilter(m_Specification.MipFilter);

		auto device = Application::GetGraphicsDevice();
		m_Handle = device->createSampler(desc);

		// The NRI twin of the NVRHI sampler above (nvrhi::SamplerDesc defaults: no LOD clamp, no
		// comparison, isInteger off).
		const nri::Filter filter[2] = { nri::Filter::NEAREST, nri::Filter::LINEAR };
		const nri::AddressMode addressMode = m_Specification.AddressMode == TextureWrap::Repeat ? nri::AddressMode::REPEAT : nri::AddressMode::CLAMP_TO_EDGE;
		nri::SamplerDesc samplerDesc = {};
		samplerDesc.filters = { filter[m_Specification.MinFilter], filter[m_Specification.MagFilter], filter[m_Specification.MipFilter], nri::FilterOp::AVERAGE };
		samplerDesc.anisotropy = static_cast<uint8_t>(glm::clamp(m_Specification.MaxAnisotropy, 1.0f, 16.0f));
		samplerDesc.mipBias = m_Specification.MipBias;
		samplerDesc.mipMin = 0.0f;
		samplerDesc.mipMax = k_SamplerMaxLod;
		samplerDesc.addressModes = { addressMode, addressMode, addressMode };
		if (RHIDevice::API().CreateSampler(RHIDevice::Get(), samplerDesc, m_RHIDescriptor) != nri::Result::SUCCESS)
			m_RHIDescriptor = nullptr;
	}

	Sampler::~Sampler()
	{
		if (!m_RHIDescriptor)
			return;

		Renderer::SubmitResourceFree([descriptor = m_RHIDescriptor]()
			{
				RHIDevice::API().DestroyDescriptor(descriptor);
			});
	}
}
