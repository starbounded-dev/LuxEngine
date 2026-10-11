// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cstdint>
#include <vector>

namespace nri {
	struct Buffer;
	struct CommandBuffer;
	struct Texture;
}

namespace Lux {

	// CPU-written memory that GPU copies read (NRI migration Phase 13): HOST_UPLOAD pages handed out
	// linearly. A stream keeps its pages until Retire, which recycles them through the deletion
	// queue (Renderer::SubmitResourceFree), so they come back once the GPU has completed the render
	// frame in which the release runs. Retire a stream once the submission that reads it has been
	// recorded on the render thread, or queued there. Pages of the default size go back to a shared
	// pool; larger ones are freed.
	//
	// Threading: a stream is used by one thread at a time (its owner serializes). The pool is
	// thread-safe.
	class StagingStream
	{
	public:
		struct Allocation
		{
			nri::Buffer* Buffer = nullptr;
			uint64_t Offset = 0;

			explicit operator bool() const { return Buffer != nullptr; }
		};

		// Renderer::Init creates the pool's first pages; Renderer::Shutdown frees the pool, after
		// which released pages are freed at once.
		static void Init();
		static void Shutdown();

		explicit StagingStream(const char* debugName);
		// Retires what the stream still holds.
		~StagingStream();
		StagingStream(const StagingStream&) = delete;
		StagingStream& operator=(const StagingStream&) = delete;

		// `size` bytes at a multiple of `alignment` (any non-zero value: texel blocks can be 12
		// bytes), filled by `fill(uint8_t* data)`. Empty, and logged, when NRI cannot create a page.
		template<typename FillT>
		Allocation Write(uint64_t size, uint64_t alignment, FillT&& fill)
		{
			const Allocation allocation = Reserve(size, alignment);
			if (allocation)
			{
				fill(Map(allocation, size));
				Unmap(allocation);
			}
			return allocation;
		}
		// `size` bytes copied from `data`.
		Allocation Write(const void* data, uint64_t size, uint64_t alignment);

		// Recycles every page handed out so far, as described above.
		void Retire();

	private:
		struct Page
		{
			nri::Buffer* Buffer = nullptr;
			uint64_t Size = 0;
		};

		Allocation Reserve(uint64_t size, uint64_t alignment);
		static uint8_t* Map(const Allocation& allocation, uint64_t size);
		static void Unmap(const Allocation& allocation);

		static Page AcquirePage(uint64_t minSize, const char* debugName);
		static void ReleasePages(std::vector<Page>& pages);

		const char* m_DebugName = nullptr;
		// The last page is the one being filled.
		std::vector<Page> m_Pages;
		uint64_t m_Offset = 0;
	};

	// One texture subresource as NRI copies it to or from a buffer: rows of texel blocks at NRI's
	// row alignment (DeviceDesc::memoryAlignment), depth slices packed (Vulkan addresses them by row
	// count), starting at a multiple of OffsetAlignment. Depth formats copy their depth plane only.
	struct TextureCopyLayout
	{
		uint32_t Mip = 0;
		uint32_t Layer = 0;
		// Bytes of texel blocks in one row, and rows of blocks in one slice.
		uint32_t RowSize = 0;
		uint32_t RowCount = 0;
		uint32_t SliceCount = 1;
		uint32_t RowPitch = 0;
		uint64_t OffsetAlignment = 1;
		bool DepthPlane = false;

		uint32_t GetSlicePitch() const { return RowPitch * RowCount; }
		uint64_t GetSize() const { return static_cast<uint64_t>(GetSlicePitch()) * SliceCount; }
		// The tightly packed size: RowSize per row.
		uint64_t GetPackedSize() const { return static_cast<uint64_t>(RowSize) * RowCount * SliceCount; }
	};

	// False, and logged, when `texture` has no mip `mip` or layer `layer`.
	bool GetTextureCopyLayout(nri::Texture& texture, uint32_t mip, uint32_t layer, TextureCopyLayout& layout);

	// A texture subresource staged for an upload.
	struct StagedTextureData
	{
		StagingStream::Allocation Source;
		TextureCopyLayout Layout;
	};

	// Stages mip `mip` of layer `layer` of `texture` from `data`: rows of `rowPitch` bytes (rows of
	// blocks for compressed formats), one depth slice after another. False, and logged, when the
	// subresource does not exist, `rowPitch` is shorter than a row, or staging fails.
	bool StageTextureData(StagingStream& stream, nri::Texture& texture, uint32_t layer, uint32_t mip, const void* data, uint64_t rowPitch, StagedTextureData& staged);

	// Records the copy of `staged` into `texture`, which must be in CopyDest.
	void CmdUploadStagedTexture(nri::CommandBuffer& commandBuffer, nri::Texture& texture, const StagedTextureData& staged);
	// Records the copy of the subresource `layout` describes into `buffer` at `offset` (a multiple of
	// layout.OffsetAlignment). The texture must be in CopySource.
	void CmdReadbackTexture(nri::CommandBuffer& commandBuffer, nri::Buffer& buffer, uint64_t offset, nri::Texture& texture, const TextureCopyLayout& layout);

}
