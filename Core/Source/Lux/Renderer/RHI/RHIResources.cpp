// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RHIResources.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RHI/UploadContext.h"

#include <format>
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

#ifdef LUX_DEBUG
		// Every NRITexture/NRIBuffer from creation until NRI destroys it, described for
		// ReportLiveNRIResources.
		std::mutex s_LiveMutex;
		std::unordered_map<const void*, std::string> s_LiveResources;

		void TrackLive(const void* resource, std::string description)
		{
			std::scoped_lock lock(s_LiveMutex);
			s_LiveResources[resource] = std::move(description);
		}

		void TrackLive(const nri::Texture* texture, const RHITextureDesc& desc)
		{
			TrackLive(texture, std::format("texture \"{}\" ({}x{}x{}, {} mip(s), {} layer(s))", desc.DebugName, desc.Width, desc.Height, desc.Depth, desc.MipLevels, desc.ArraySize));
		}

		void TrackLive(const nri::Buffer* buffer, const RHIBufferDesc& desc)
		{
			TrackLive(buffer, std::format("buffer \"{}\" ({} bytes, CPU access {})", desc.DebugName, desc.Size, static_cast<int>(desc.Access)));
		}

		void UntrackLive(const void* resource)
		{
			std::scoped_lock lock(s_LiveMutex);
			s_LiveResources.erase(resource);
		}
#else
		void TrackLive(const nri::Texture*, const RHITextureDesc&) {}
		void TrackLive(const nri::Buffer*, const RHIBufferDesc&) {}
		void UntrackLive(const void*) {}
#endif

		nri::TextureType ToNRITextureType(TextureDimension dimension)
		{
			switch (dimension)
			{
				case TextureDimension::Texture1D:
				case TextureDimension::Texture1DArray:
					return nri::TextureType::TEXTURE_1D;
				case TextureDimension::Texture3D:
					return nri::TextureType::TEXTURE_3D;
				default:
					// 2D, 2D arrays, cubes (6 layers each) and multisampled 2D.
					return nri::TextureType::TEXTURE_2D;
			}
		}

		// NRI adds TRANSFER_SRC/DST itself.
		nri::TextureUsageBits ToNRITextureUsage(const RHITextureDesc& desc)
		{
			nri::TextureUsageBits usage = nri::TextureUsageBits::NONE;
			if (desc.IsShaderResource)
				usage |= nri::TextureUsageBits::SHADER_RESOURCE;
			if (desc.IsUAV)
				usage |= nri::TextureUsageBits::SHADER_RESOURCE_STORAGE;
			if (desc.IsRenderTarget)
				usage |= nri::nriGetFormatProps(desc.Format)->isDepth ? nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT : nri::TextureUsageBits::COLOR_ATTACHMENT;
			if (desc.IsShadingRateSurface)
				usage |= nri::TextureUsageBits::SHADING_RATE_ATTACHMENT;
			return usage;
		}

		// NRI gives a buffer STORAGE_BUFFER usage when it is structured or byte-address, and adds
		// TRANSFER_SRC/DST and device address. Raw-view and UAV buffers also get the shader-resource
		// usages: NRI validation checks barrier access against them.
		nri::BufferUsageBits ToNRIBufferUsage(const RHIBufferDesc& desc)
		{
			nri::BufferUsageBits usage = nri::BufferUsageBits::NONE;
			if (desc.IsVertexBuffer)
				usage |= nri::BufferUsageBits::VERTEX;
			if (desc.IsIndexBuffer)
				usage |= nri::BufferUsageBits::INDEX;
			if (desc.IsDrawIndirectArgs)
				usage |= nri::BufferUsageBits::ARGUMENT;
			if (desc.IsConstantBuffer)
				usage |= nri::BufferUsageBits::CONSTANT;
			if (desc.CanHaveRawViews || desc.CanHaveUAVs)
				usage |= nri::BufferUsageBits::SHADER_RESOURCE;
			if (desc.CanHaveUAVs)
				usage |= nri::BufferUsageBits::SHADER_RESOURCE_STORAGE;
			return usage;
		}

		nri::MemoryLocation ToNRIMemoryLocation(CPUAccess access)
		{
			switch (access)
			{
				case CPUAccess::Write: return nri::MemoryLocation::HOST_UPLOAD;
				case CPUAccess::Read:  return nri::MemoryLocation::HOST_READBACK;
				default:               return nri::MemoryLocation::DEVICE;
			}
		}

	}

	NRITexture::~NRITexture()
	{
		Reset();
	}

	NRITexture::NRITexture(NRITexture&& other) noexcept
		: m_Texture(std::exchange(other.m_Texture, nullptr)), m_Tracked(std::exchange(other.m_Tracked, {}))
	{
	}

	NRITexture& NRITexture::operator=(NRITexture&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_Texture = std::exchange(other.m_Texture, nullptr);
			m_Tracked = std::exchange(other.m_Tracked, {});
		}
		return *this;
	}

	NRITexture NRITexture::Create(const RHITextureDesc& desc)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (desc.Format == nri::Format::UNKNOWN)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Texture \"{}\" has no format", desc.DebugName);
			return {};
		}

		const NRIInterface& api = RHIDevice::API();
		const bool volume = desc.Dimension == TextureDimension::Texture3D;

		nri::TextureDesc nriDesc = {};
		nriDesc.type = ToNRITextureType(desc.Dimension);
		nriDesc.usage = ToNRITextureUsage(desc);
		nriDesc.format = desc.Format;
		nriDesc.width = static_cast<nri::Dim_t>(desc.Width);
		nriDesc.height = static_cast<nri::Dim_t>(desc.Height);
		nriDesc.depth = static_cast<nri::Dim_t>(volume ? desc.Depth : 1);
		nriDesc.mipNum = static_cast<nri::Dim_t>(desc.MipLevels);
		nriDesc.layerNum = static_cast<nri::Dim_t>(volume ? 1 : desc.ArraySize);
		nriDesc.sampleNum = static_cast<nri::Sample_t>(desc.SampleCount);
		// Attachments stay EXCLUSIVE, as NVRHI created them: CONCURRENT costs them compression (DCC) on
		// some GPUs. NRI validation rejects EXCLUSIVE on anything else, so the rest take NRI's
		// CONCURRENT default, which also needs no queue ownership transfer on the compute/copy queues.
		constexpr nri::TextureUsageBits attachmentUsage = nri::TextureUsageBits::COLOR_ATTACHMENT | nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT | nri::TextureUsageBits::SHADING_RATE_ATTACHMENT;
		nriDesc.sharingMode = (nriDesc.usage & attachmentUsage) ? nri::SharingMode::EXCLUSIVE : nri::SharingMode::CONCURRENT;

		NRITexture result;
		if (api.CreateCommittedTexture(RHIDevice::Get(), nri::MemoryLocation::DEVICE, 0.0f, nriDesc, result.m_Texture) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "NRI failed to create texture \"{}\" ({}x{}x{}, {} mip(s), {} layer(s), {})",
				desc.DebugName, desc.Width, desc.Height, desc.Depth, desc.MipLevels, desc.ArraySize, nri::nriGetFormatProps(desc.Format)->name);
			result.m_Texture = nullptr;
			return result;
		}
		api.SetDebugName(result.m_Texture, desc.DebugName.c_str());
		TrackLive(result.m_Texture, desc);

		result.m_Tracked.Handle = result.m_Texture;
		result.m_Tracked.MipCount = std::max(1u, static_cast<uint32_t>(nriDesc.mipNum));
		result.m_Tracked.LayerCount = std::max(1u, static_cast<uint32_t>(nriDesc.layerNum));
		result.m_Tracked.RestingState = desc.RestingState;

		// The texture starts undefined; the upload batch moves it to its resting state before any
		// submission.
		Renderer::RecordResourceUpload([&](UploadContext& upload)
			{
				upload.InitializeTexture(result.m_Tracked);
			});
		return result;
	}

	void NRITexture::Reset()
	{
		if (!m_Texture)
			return;

		LUX_CORE_ASSERT(RHIDevice::IsInitialized(), "NRI texture released after the NRI device was destroyed");
		Renderer::SubmitResourceFree([texture = std::exchange(m_Texture, nullptr)]()
			{
				DestroyViews(s_TextureViews, texture);
				UntrackLive(texture);
				RHIDevice::API().DestroyTexture(texture);
			});
		m_Tracked = {};
	}

	NRIBuffer::~NRIBuffer()
	{
		Reset();
	}

	NRIBuffer::NRIBuffer(NRIBuffer&& other) noexcept
		: m_Buffer(std::exchange(other.m_Buffer, nullptr)), m_Size(std::exchange(other.m_Size, 0)), m_Tracked(std::exchange(other.m_Tracked, {}))
	{
	}

	NRIBuffer& NRIBuffer::operator=(NRIBuffer&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_Buffer = std::exchange(other.m_Buffer, nullptr);
			m_Size = std::exchange(other.m_Size, 0);
			m_Tracked = std::exchange(other.m_Tracked, {});
		}
		return *this;
	}

	NRIBuffer NRIBuffer::Create(const RHIBufferDesc& desc)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const NRIInterface& api = RHIDevice::API();

		nri::BufferDesc nriDesc = {};
		// Vulkan forbids zero-size buffers; GetSize keeps the requested size.
		nriDesc.size = std::max(desc.Size, k_MinBufferSize);
		nriDesc.structureStride = desc.StructStride;
		// Raw-view and UAV buffers are storage buffers, which NRI makes byte-address buffers.
		nriDesc.byteAddress = desc.CanHaveRawViews || desc.CanHaveUAVs;
		nriDesc.usage = ToNRIBufferUsage(desc);

		NRIBuffer result;
		if (api.CreateCommittedBuffer(RHIDevice::Get(), ToNRIMemoryLocation(desc.Access), 0.0f, nriDesc, result.m_Buffer) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "NRI failed to create buffer \"{}\" ({} bytes, CPU access {})",
				desc.DebugName, desc.Size, static_cast<int>(desc.Access));
			result.m_Buffer = nullptr;
			return result;
		}
		api.SetDebugName(result.m_Buffer, desc.DebugName.c_str());
		TrackLive(result.m_Buffer, desc);

		result.m_Size = desc.Size;
		result.m_Tracked.Handle = result.m_Buffer;
		result.m_Tracked.Untracked = desc.Access != CPUAccess::None;
		result.m_Tracked.RestingState = result.m_Tracked.Untracked ? ResourceState::Unknown : desc.RestingState;
		return result;
	}

	void NRIBuffer::Reset()
	{
		if (!m_Buffer)
			return;

		LUX_CORE_ASSERT(RHIDevice::IsInitialized(), "NRI buffer released after the NRI device was destroyed");
		Renderer::SubmitResourceFree([buffer = std::exchange(m_Buffer, nullptr)]()
			{
				DestroyViews(s_BufferViews, buffer);
				UntrackLive(buffer);
				RHIDevice::API().DestroyBuffer(buffer);
			});
		m_Size = 0;
		m_Tracked = {};
	}

	void* NRIBuffer::Map(uint64_t offset, uint64_t size) const
	{
		LUX_CORE_ASSERT(m_Buffer && m_Tracked.Untracked, "Only CPU-visible buffers can be mapped");
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
		// A failed view stays cached as null, so it is logged once rather than retried every bake.
		auto [it, inserted] = s_TextureViews[texture].try_emplace(key, nullptr);
		nri::Descriptor*& view = it->second;
		if (!inserted)
			return view;

		const NRIInterface& api = RHIDevice::API();
		const nri::TextureDesc& textureDesc = api.GetTextureDesc(*texture);
		const nri::Format format = key.Format != nri::Format::UNKNOWN ? key.Format : textureDesc.format;
		const nri::FormatProps* formatProps = nri::nriGetFormatProps(format);
		const bool sampled = key.Type == nri::TextureView::TEXTURE || key.Type == nri::TextureView::TEXTURE_ARRAY
			|| key.Type == nri::TextureView::TEXTURE_CUBE || key.Type == nri::TextureView::TEXTURE_CUBE_ARRAY;

		nri::TextureViewDesc viewDesc = {};
		viewDesc.texture = texture;
		viewDesc.type = key.Type;
		viewDesc.format = format;
		viewDesc.mipOffset = static_cast<nri::Dim_t>(key.MipOffset);
		viewDesc.mipNum = static_cast<nri::Dim_t>(key.MipNum);
		viewDesc.layerOffset = static_cast<nri::Dim_t>(key.LayerOffset);
		viewDesc.layerNum = static_cast<nri::Dim_t>(key.LayerNum);
		if (sampled && formatProps && formatProps->isDepth)
			viewDesc.planes = nri::PlaneBits::DEPTH;

		if (api.CreateTextureView(viewDesc, view) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "NRI failed to create a view (type {}, mips {}+{}, layers {}+{}) of a {}x{} texture",
				static_cast<int>(key.Type), key.MipOffset, key.MipNum, key.LayerOffset, key.LayerNum, textureDesc.width, textureDesc.height);
			view = nullptr;
		}
		return view;
	}

	nri::Descriptor* GetNRIBufferView(nri::Buffer* buffer, nri::BufferView type)
	{
		if (!buffer)
			return nullptr;

		std::scoped_lock lock(s_ViewMutex);
		// Failures stay cached as null, as for texture views.
		auto [it, inserted] = s_BufferViews[buffer].try_emplace(type, nullptr);
		nri::Descriptor*& view = it->second;
		if (!inserted)
			return view;

		nri::BufferViewDesc viewDesc = {};
		viewDesc.buffer = buffer;
		viewDesc.type = type;
		viewDesc.size = nri::WHOLE_SIZE;
		if (RHIDevice::API().CreateBufferView(viewDesc, view) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "NRI failed to create a buffer view (type {})", static_cast<int>(type));
			view = nullptr;
		}
		return view;
	}

	size_t ReportLiveNRIResources()
	{
#ifdef LUX_DEBUG
		std::scoped_lock lock(s_LiveMutex);
		for (const auto& [resource, description] : s_LiveResources)
			LUX_CORE_ERROR_TAG("Renderer", "[RHI] Leaked NRI {}", description);
		return s_LiveResources.size();
#else
		return 0;
#endif
	}

}
