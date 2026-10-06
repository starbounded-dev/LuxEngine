// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NVRHIWrappers.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"

#include <nvrhi/vulkan.h>

#include <map>
#include <mutex>
#include <unordered_map>

namespace Lux {

	namespace {

		constexpr uint64_t k_MinBufferSize = 16;

		// Views per NRI texture/buffer. Entries are added on any thread and removed by the
		// resource's deletion-queue release, before the resource itself is destroyed.
		std::mutex s_ViewMutex;
		std::unordered_map<const nri::Texture*, std::map<NRITextureViewKey, nri::Descriptor*>> s_TextureViews;
		std::unordered_map<const nri::Buffer*, std::map<nri::BufferView, nri::Descriptor*>> s_BufferViews;

		template<typename ViewMap>
		void DestroyViews(ViewMap& views, const void* resource)
		{
			std::scoped_lock lock(s_ViewMutex);
			auto it = views.find(static_cast<typename ViewMap::key_type>(resource));
			if (it == views.end())
				return;

			for (auto& [key, descriptor] : it->second)
				RHIDevice::API().DestroyDescriptor(descriptor);
			views.erase(it);
		}

		nri::TextureType ToNRITextureType(nvrhi::TextureDimension dimension)
		{
			switch (dimension)
			{
				case nvrhi::TextureDimension::Texture1D:
				case nvrhi::TextureDimension::Texture1DArray:
					return nri::TextureType::TEXTURE_1D;
				case nvrhi::TextureDimension::Texture3D:
					return nri::TextureType::TEXTURE_3D;
				default:
					// 2D, 2D arrays, cubes (6 layers each) and multisampled 2D.
					return nri::TextureType::TEXTURE_2D;
			}
		}

		// NVRHI's image usage (vulkan-texture.cpp pickImageUsage); NRI adds TRANSFER_SRC/DST itself.
		nri::TextureUsageBits ToNRITextureUsage(const nvrhi::TextureDesc& desc)
		{
			nri::TextureUsageBits usage = nri::TextureUsageBits::NONE;
			if (desc.isShaderResource)
				usage |= nri::TextureUsageBits::SHADER_RESOURCE;
			if (desc.isUAV)
				usage |= nri::TextureUsageBits::SHADER_RESOURCE_STORAGE;
			if (desc.isRenderTarget)
				usage |= nvrhi::getFormatInfo(desc.format).hasDepth ? nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT : nri::TextureUsageBits::COLOR_ATTACHMENT;
			if (desc.isShadingRateSurface)
				usage |= nri::TextureUsageBits::SHADING_RATE_ATTACHMENT;
			return usage;
		}

		// NVRHI's buffer usage (vulkan-buffer.cpp createBuffer). NRI gives a buffer STORAGE_BUFFER
		// usage when it is structured or byte-address, and adds TRANSFER_SRC/DST and device address.
		nri::BufferUsageBits ToNRIBufferUsage(const nvrhi::BufferDesc& desc)
		{
			nri::BufferUsageBits usage = nri::BufferUsageBits::NONE;
			if (desc.isVertexBuffer)
				usage |= nri::BufferUsageBits::VERTEX;
			if (desc.isIndexBuffer)
				usage |= nri::BufferUsageBits::INDEX;
			if (desc.isDrawIndirectArgs)
				usage |= nri::BufferUsageBits::ARGUMENT;
			if (desc.isConstantBuffer)
				usage |= nri::BufferUsageBits::CONSTANT;
			if (desc.canHaveTypedViews)
				usage |= nri::BufferUsageBits::SHADER_RESOURCE;
			if (desc.canHaveTypedViews && desc.canHaveUAVs)
				usage |= nri::BufferUsageBits::SHADER_RESOURCE_STORAGE;
			return usage;
		}

		nri::MemoryLocation ToNRIMemoryLocation(nvrhi::CpuAccessMode cpuAccess)
		{
			switch (cpuAccess)
			{
				case nvrhi::CpuAccessMode::Write: return nri::MemoryLocation::HOST_UPLOAD;
				case nvrhi::CpuAccessMode::Read:  return nri::MemoryLocation::HOST_READBACK;
				default:                          return nri::MemoryLocation::DEVICE;
			}
		}

	}

	NRITexture::~NRITexture()
	{
		Reset();
	}

	NRITexture::NRITexture(NRITexture&& other) noexcept
		: m_Texture(std::exchange(other.m_Texture, nullptr)), m_Handle(std::move(other.m_Handle))
	{
	}

	NRITexture& NRITexture::operator=(NRITexture&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_Texture = std::exchange(other.m_Texture, nullptr);
			m_Handle = std::move(other.m_Handle);
		}
		return *this;
	}

	NRITexture NRITexture::Create(const nvrhi::TextureDesc& desc)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const NRIInterface& api = RHIDevice::API();
		const bool volume = desc.dimension == nvrhi::TextureDimension::Texture3D;

		nri::TextureDesc nriDesc = {};
		nriDesc.type = ToNRITextureType(desc.dimension);
		nriDesc.usage = ToNRITextureUsage(desc);
		// Through the VkFormat, so NRI and NVRHI name the image's format identically.
		nriDesc.format = nri::nriConvertVKFormatToNRI(static_cast<uint32_t>(nvrhi::vulkan::convertFormat(desc.format)));
		nriDesc.width = static_cast<nri::Dim_t>(desc.width);
		nriDesc.height = static_cast<nri::Dim_t>(desc.height);
		nriDesc.depth = static_cast<nri::Dim_t>(volume ? desc.depth : 1);
		nriDesc.mipNum = static_cast<nri::Dim_t>(desc.mipLevels);
		nriDesc.layerNum = static_cast<nri::Dim_t>(volume ? 1 : desc.arraySize);
		nriDesc.sampleNum = static_cast<nri::Sample_t>(desc.sampleCount);
		// As NVRHI creates images. NRI's CONCURRENT default would also cost compression on some GPUs.
		nriDesc.sharingMode = nri::SharingMode::EXCLUSIVE;

		NRITexture result;
		if (nriDesc.format == nri::Format::UNKNOWN
			|| api.CreateCommittedTexture(RHIDevice::Get(), nri::MemoryLocation::DEVICE, 0.0f, nriDesc, result.m_Texture) != nri::Result::SUCCESS)
		{
			result.m_Texture = nullptr;
			return result;
		}
		api.SetDebugName(result.m_Texture, desc.debugName.c_str());

		nvrhi::IDevice* device = Application::GetGraphicsDevice();
		result.m_Handle = device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(api.GetTextureNativeObject(result.m_Texture)), desc);
		return result;
	}

	void NRITexture::Reset()
	{
		if (!m_Texture)
		{
			m_Handle = nullptr;
			return;
		}

		LUX_CORE_ASSERT(RHIDevice::IsInitialized(), "NRI texture released after the NRI device was destroyed");
		Renderer::SubmitResourceFree([texture = std::exchange(m_Texture, nullptr), handle = std::move(m_Handle)]() mutable
			{
				handle = nullptr;
				DestroyViews(s_TextureViews, texture);
				RHIDevice::API().DestroyTexture(texture);
			});
		m_Handle = nullptr;
	}

	NRIBuffer::~NRIBuffer()
	{
		Reset();
	}

	NRIBuffer::NRIBuffer(NRIBuffer&& other) noexcept
		: m_Buffer(std::exchange(other.m_Buffer, nullptr)), m_Handle(std::move(other.m_Handle))
	{
	}

	NRIBuffer& NRIBuffer::operator=(NRIBuffer&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_Buffer = std::exchange(other.m_Buffer, nullptr);
			m_Handle = std::move(other.m_Handle);
		}
		return *this;
	}

	NRIBuffer NRIBuffer::Create(const nvrhi::BufferDesc& desc)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_ASSERT(!desc.isVolatile, "Volatile buffers are NVRHI upload memory, not NRI-owned buffers");
		const NRIInterface& api = RHIDevice::API();

		nri::BufferDesc nriDesc = {};
		// Vulkan forbids zero-size buffers; the NVRHI desc keeps the requested size.
		nriDesc.size = std::max(desc.byteSize, k_MinBufferSize);
		nriDesc.structureStride = desc.structStride;
		// NVRHI gives raw-view and UAV buffers STORAGE_BUFFER usage; NRI does for byte-address buffers.
		nriDesc.byteAddress = desc.canHaveRawViews || desc.canHaveUAVs;
		nriDesc.usage = ToNRIBufferUsage(desc);

		NRIBuffer result;
		if (api.CreateCommittedBuffer(RHIDevice::Get(), ToNRIMemoryLocation(desc.cpuAccess), 0.0f, nriDesc, result.m_Buffer) != nri::Result::SUCCESS)
		{
			result.m_Buffer = nullptr;
			return result;
		}
		api.SetDebugName(result.m_Buffer, desc.debugName.c_str());

		nvrhi::IDevice* device = Application::GetGraphicsDevice();
		result.m_Handle = device->createHandleForNativeBuffer(nvrhi::ObjectTypes::VK_Buffer, nvrhi::Object(api.GetBufferNativeObject(result.m_Buffer)), desc);
		return result;
	}

	void NRIBuffer::Reset()
	{
		if (!m_Buffer)
		{
			m_Handle = nullptr;
			return;
		}

		LUX_CORE_ASSERT(RHIDevice::IsInitialized(), "NRI buffer released after the NRI device was destroyed");
		Renderer::SubmitResourceFree([buffer = std::exchange(m_Buffer, nullptr), handle = std::move(m_Handle)]() mutable
			{
				handle = nullptr;
				DestroyViews(s_BufferViews, buffer);
				RHIDevice::API().DestroyBuffer(buffer);
			});
		m_Handle = nullptr;
	}

	void* NRIBuffer::Map(uint64_t offset, uint64_t size) const
	{
		LUX_CORE_ASSERT(m_Buffer && m_Handle->getDesc().cpuAccess != nvrhi::CpuAccessMode::None, "Only CPU-visible buffers can be mapped");
		return RHIDevice::API().MapBuffer(*m_Buffer, offset, size);
	}

	void NRIBuffer::Unmap() const
	{
		RHIDevice::API().UnmapBuffer(*m_Buffer);
	}

	nri::Descriptor* GetNRITextureView(nri::Texture* texture, const NRITextureViewKey& key)
	{
		if (!texture)
			return nullptr;

		std::scoped_lock lock(s_ViewMutex);
		nri::Descriptor*& view = s_TextureViews[texture][key];
		if (view)
			return view;

		const NRIInterface& api = RHIDevice::API();
		const nri::TextureDesc& textureDesc = api.GetTextureDesc(*texture);
		const nri::FormatProps* formatProps = nri::nriGetFormatProps(textureDesc.format);
		const bool sampled = key.Type == nri::TextureView::TEXTURE || key.Type == nri::TextureView::TEXTURE_ARRAY
			|| key.Type == nri::TextureView::TEXTURE_CUBE || key.Type == nri::TextureView::TEXTURE_CUBE_ARRAY;

		nri::TextureViewDesc viewDesc = {};
		viewDesc.texture = texture;
		viewDesc.type = key.Type;
		viewDesc.format = textureDesc.format;
		viewDesc.mipOffset = static_cast<nri::Dim_t>(key.MipOffset);
		viewDesc.mipNum = static_cast<nri::Dim_t>(key.MipNum);
		viewDesc.layerOffset = static_cast<nri::Dim_t>(key.LayerOffset);
		viewDesc.layerNum = static_cast<nri::Dim_t>(key.LayerNum);
		if (sampled && formatProps && formatProps->isDepth)
			viewDesc.planes = nri::PlaneBits::DEPTH;

		if (api.CreateTextureView(viewDesc, view) != nri::Result::SUCCESS)
			view = nullptr;
		return view;
	}

	nri::Descriptor* GetNRIBufferView(nri::Buffer* buffer, nri::BufferView type)
	{
		if (!buffer)
			return nullptr;

		std::scoped_lock lock(s_ViewMutex);
		nri::Descriptor*& view = s_BufferViews[buffer][type];
		if (view)
			return view;

		nri::BufferViewDesc viewDesc = {};
		viewDesc.buffer = buffer;
		viewDesc.type = type;
		viewDesc.size = nri::WHOLE_SIZE;
		if (RHIDevice::API().CreateBufferView(viewDesc, view) != nri::Result::SUCCESS)
			view = nullptr;
		return view;
	}

}
