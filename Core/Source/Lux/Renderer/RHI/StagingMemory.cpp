// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "StagingMemory.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"

#include <cstring>
#include <mutex>
#include <numeric>

namespace Lux {

	namespace {

		// Uploads are mostly a few KiB to a few MiB; anything larger gets a page of its own.
		constexpr uint64_t k_PageSize = 4ull * 1024 * 1024;
		constexpr uint64_t k_LargePageGranularity = 64ull * 1024;
		// Recycled pages kept for reuse (64 MiB); further ones are freed.
		constexpr size_t k_MaxFreePages = 16;
		constexpr size_t k_InitialPages = 2;

		std::mutex s_PoolMutex;
		std::vector<nri::Buffer*> s_FreePages;
		// Open from the start: the swapchain's first uploads come before Renderer::Init.
		bool s_PoolOpen = true;

		uint64_t AlignUp(uint64_t value, uint64_t alignment)
		{
			return (value + alignment - 1) / alignment * alignment;
		}

		nri::Buffer* CreatePage(uint64_t size)
		{
			LUX_PROFILE_FUNCTION_AUTO;
			nri::BufferDesc desc = {};
			desc.size = size;

			nri::Buffer* buffer = nullptr;
			const NRIInterface& api = RHIDevice::API();
			if (api.CreateCommittedBuffer(RHIDevice::Get(), nri::MemoryLocation::HOST_UPLOAD, 0.0f, desc, buffer) != nri::Result::SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Renderer", "NRI failed to create a {} byte staging page", size);
				return nullptr;
			}
			api.SetDebugName(buffer, "Staging page");
			return buffer;
		}

	}

	void StagingStream::Init()
	{
		std::scoped_lock lock(s_PoolMutex);
		for (size_t i = 0; i < k_InitialPages; i++)
		{
			if (nri::Buffer* page = CreatePage(k_PageSize))
				s_FreePages.push_back(page);
		}
	}

	void StagingStream::Shutdown()
	{
		// After the GPU is idle: pages released later are freed at once.
		std::scoped_lock lock(s_PoolMutex);
		s_PoolOpen = false;
		for (nri::Buffer* page : s_FreePages)
			RHIDevice::API().DestroyBuffer(page);
		s_FreePages.clear();
	}

	StagingStream::StagingStream(const char* debugName)
		: m_DebugName(debugName)
	{
	}

	StagingStream::~StagingStream()
	{
		Retire();
	}

	StagingStream::Allocation StagingStream::Write(const void* data, uint64_t size, uint64_t alignment)
	{
		return Write(size, alignment, [data, size](uint8_t* destination)
			{
				std::memcpy(destination, data, size);
			});
	}

	void StagingStream::Retire()
	{
		if (m_Pages.empty())
			return;

		Renderer::SubmitResourceFree([pages = std::move(m_Pages)]() mutable
			{
				ReleasePages(pages);
			});
		m_Pages.clear();
		m_Offset = 0;
	}

	StagingStream::Allocation StagingStream::Reserve(uint64_t size, uint64_t alignment)
	{
		LUX_CORE_ASSERT(size > 0 && alignment > 0);
		if (!m_Pages.empty())
		{
			const uint64_t offset = AlignUp(m_Offset, alignment);
			if (offset + size <= m_Pages.back().Size)
			{
				m_Offset = offset + size;
				return { m_Pages.back().Buffer, offset };
			}
		}

		const Page page = AcquirePage(size, m_DebugName);
		if (!page.Buffer)
			return {};

		// A page of its own goes in front, so the page being filled stays last.
		if (page.Size > k_PageSize && !m_Pages.empty())
		{
			m_Pages.insert(m_Pages.begin(), page);
			return { page.Buffer, 0 };
		}

		m_Pages.push_back(page);
		m_Offset = size;
		return { page.Buffer, 0 };
	}

	uint8_t* StagingStream::Map(const Allocation& allocation, uint64_t size)
	{
		return static_cast<uint8_t*>(RHIDevice::API().MapBuffer(*allocation.Buffer, allocation.Offset, size));
	}

	void StagingStream::Unmap(const Allocation& allocation)
	{
		// Flushes the written range of non-coherent memory.
		RHIDevice::API().UnmapBuffer(*allocation.Buffer);
	}

	StagingStream::Page StagingStream::AcquirePage(uint64_t minSize, const char* debugName)
	{
		if (minSize <= k_PageSize)
		{
			{
				std::scoped_lock lock(s_PoolMutex);
				if (!s_FreePages.empty())
				{
					nri::Buffer* page = s_FreePages.back();
					s_FreePages.pop_back();
					return { page, k_PageSize };
				}
			}
			return { CreatePage(k_PageSize), k_PageSize };
		}

		const uint64_t size = AlignUp(minSize, k_LargePageGranularity);
		LUX_CORE_TRACE_TAG("Renderer", "{}: {} byte staging page for one upload", debugName, size);
		return { CreatePage(size), size };
	}

	void StagingStream::ReleasePages(std::vector<Page>& pages)
	{
		std::scoped_lock lock(s_PoolMutex);
		for (const Page& page : pages)
		{
			if (!page.Buffer)
				continue;
			if (s_PoolOpen && page.Size == k_PageSize && s_FreePages.size() < k_MaxFreePages)
				s_FreePages.push_back(page.Buffer);
			else
				RHIDevice::API().DestroyBuffer(page.Buffer);
		}
		pages.clear();
	}

	bool GetTextureCopyLayout(nri::Texture& texture, uint32_t mip, uint32_t layer, TextureCopyLayout& layout)
	{
		const nri::TextureDesc& desc = RHIDevice::API().GetTextureDesc(texture);
		if (mip >= desc.mipNum || layer >= std::max<uint32_t>(1u, desc.layerNum))
		{
			LUX_CORE_ERROR_TAG("Renderer", "Texture copy of mip {} of layer {} skipped: the texture has {} mip(s) and {} layer(s)", mip, layer, desc.mipNum, desc.layerNum);
			return false;
		}

		const nri::FormatProps& format = *nri::nriGetFormatProps(desc.format);
		layout.Mip = mip;
		layout.Layer = layer;
		layout.DepthPlane = format.isDepth;
		// The depth plane of D32S8 is copied as four-byte depth texels (the NRI fork's patch).
		const uint32_t stride = desc.format == nri::Format::D32_SFLOAT_S8_UINT ? 4u : format.stride;

		const uint32_t width = std::max(1u, static_cast<uint32_t>(desc.width) >> mip);
		const uint32_t height = std::max(1u, static_cast<uint32_t>(desc.height) >> mip);
		layout.SliceCount = desc.type == nri::TextureType::TEXTURE_3D ? std::max(1u, static_cast<uint32_t>(desc.depth) >> mip) : 1u;
		layout.RowSize = (width + format.blockWidth - 1) / format.blockWidth * stride;
		layout.RowCount = (height + format.blockHeight - 1) / format.blockHeight;

		const nri::DeviceDesc& deviceDesc = RHIDevice::GetDesc();
		const uint32_t rowAlignment = std::lcm(std::max(1u, deviceDesc.memoryAlignment.uploadBufferTextureRow), stride);
		layout.RowPitch = static_cast<uint32_t>(AlignUp(layout.RowSize, rowAlignment));
		layout.OffsetAlignment = std::lcm(static_cast<uint64_t>(std::max(1u, deviceDesc.memoryAlignment.uploadBufferTextureSlice)), static_cast<uint64_t>(stride));
		return true;
	}

	bool StageTextureData(StagingStream& stream, nri::Texture& texture, uint32_t layer, uint32_t mip, const void* data, uint64_t rowPitch, StagedTextureData& staged)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		TextureCopyLayout& layout = staged.Layout;
		if (!data || !GetTextureCopyLayout(texture, mip, layer, layout))
			return false;
		if (rowPitch < layout.RowSize)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Texture upload to mip {} of layer {} skipped: a row is {} bytes, the data's rows are {}", mip, layer, layout.RowSize, rowPitch);
			return false;
		}

		const uint8_t* source = static_cast<const uint8_t*>(data);
		staged.Source = stream.Write(layout.GetSize(), layout.OffsetAlignment, [&](uint8_t* destination)
			{
				if (layout.RowPitch == rowPitch)
				{
					std::memcpy(destination, source, layout.GetSize());
					return;
				}
				for (uint32_t row = 0; row < layout.RowCount * layout.SliceCount; row++)
					std::memcpy(destination + static_cast<size_t>(row) * layout.RowPitch, source + static_cast<size_t>(row) * rowPitch, layout.RowSize);
			});
		return static_cast<bool>(staged.Source);
	}

	namespace {

		void GetCopyDescs(const TextureCopyLayout& layout, uint64_t offset, nri::TextureRegionDesc& region, nri::TextureDataLayoutDesc& dataLayout)
		{
			// Width, height and depth 0: the whole mip.
			region = {};
			region.mipOffset = static_cast<nri::Dim_t>(layout.Mip);
			region.layerOffset = static_cast<nri::Dim_t>(layout.Layer);
			region.planes = layout.DepthPlane ? nri::PlaneBits::DEPTH : nri::PlaneBits::ALL;

			dataLayout = {};
			dataLayout.offset = offset;
			dataLayout.rowPitch = layout.RowPitch;
			dataLayout.slicePitch = layout.GetSlicePitch();
		}

	}

	void CmdUploadStagedTexture(nri::CommandBuffer& commandBuffer, nri::Texture& texture, const StagedTextureData& staged)
	{
		nri::TextureRegionDesc region;
		nri::TextureDataLayoutDesc dataLayout;
		GetCopyDescs(staged.Layout, staged.Source.Offset, region, dataLayout);
		RHIDevice::API().CmdUploadBufferToTexture(commandBuffer, texture, region, *staged.Source.Buffer, dataLayout);
	}

	void CmdReadbackTexture(nri::CommandBuffer& commandBuffer, nri::Buffer& buffer, uint64_t offset, nri::Texture& texture, const TextureCopyLayout& layout)
	{
		nri::TextureRegionDesc region;
		nri::TextureDataLayoutDesc dataLayout;
		GetCopyDescs(layout, offset, region, dataLayout);
		RHIDevice::API().CmdReadbackTextureToBuffer(commandBuffer, buffer, dataLayout, texture, region);
	}

}
