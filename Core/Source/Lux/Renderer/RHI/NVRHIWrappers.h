// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace nri {
	struct Buffer;
	struct Descriptor;
	struct Texture;
	enum class BufferView : uint8_t;
	enum class Format : uint8_t;
	enum class TextureView : uint8_t;
}

// From NRI migration Phase 8, NRI allocates and owns GPU textures and buffers while NVRHI keeps
// rendering through non-owning handles to the same VkImage/VkBuffer (plan §2.2, I6). These two
// owners hold both halves. They go away with NVRHI (Phase 15).

namespace Lux {

	// The NRI format naming the same VkFormat as `format`, so NRI and NVRHI describe an image or
	// attachment identically. UNKNOWN when NRI has no such format.
	nri::Format ToNRIFormat(nvrhi::Format format);

	// A texture NRI owns, plus the NVRHI handle for the same image. Move-only. Destroying or
	// replacing it frees both through Renderer::SubmitResourceFree, once the frames that may use it
	// have retired: the NVRHI handle first, then the NRI texture.
	class NRITexture
	{
	public:
		NRITexture() = default;
		~NRITexture();
		NRITexture(NRITexture&& other) noexcept;
		NRITexture& operator=(NRITexture&& other) noexcept;
		NRITexture(const NRITexture&) = delete;
		NRITexture& operator=(const NRITexture&) = delete;

		// Any thread. The NRI texture is created from `desc` (same format, size, mips, layers and
		// usage) and the NVRHI handle wraps it with `desc`, so NVRHI sees exactly what it would have
		// created. Empty, and logged, when NRI has no matching format or fails to create it.
		static NRITexture Create(const nvrhi::TextureDesc& desc);

		void Reset();

		nri::Texture* Get() const { return m_Texture; }
		const nvrhi::TextureHandle& GetHandle() const { return m_Handle; }
		explicit operator bool() const { return m_Texture != nullptr; }

	private:
		nri::Texture* m_Texture = nullptr;
		nvrhi::TextureHandle m_Handle;
	};

	// Which view of a texture (an NRI descriptor). Counts of 0 mean "the rest of the texture".
	// Sampled views of depth formats see the depth plane only. Format UNKNOWN means the texture's
	// own format.
	struct NRITextureViewKey
	{
		nri::TextureView Type{};
		uint32_t MipOffset = 0;
		uint32_t MipNum = 0;
		uint32_t LayerOffset = 0;
		uint32_t LayerNum = 0;
		nri::Format Format{};

		auto operator<=>(const NRITextureViewKey&) const = default;
	};

	// Views of NRI-owned textures and buffers, created on first use and destroyed together with the
	// resource (in NRITexture/NRIBuffer's deletion-queue release). Thread-safe. Null, logged once per
	// resource and view, when NRI cannot create the view.
	nri::Descriptor* GetNRITextureView(nri::Texture* texture, const NRITextureViewKey& key);
	nri::Descriptor* GetNRIBufferView(nri::Buffer* buffer, nri::BufferView type);

	// A buffer NRI owns, plus the NVRHI handle for the same buffer; same rules as NRITexture.
	// CPU-visible buffers (CpuAccessMode::Write or Read) are mapped through NRI, never NVRHI, which
	// cannot map memory it did not allocate (plan I7).
	class NRIBuffer
	{
	public:
		NRIBuffer() = default;
		~NRIBuffer();
		NRIBuffer(NRIBuffer&& other) noexcept;
		NRIBuffer& operator=(NRIBuffer&& other) noexcept;
		NRIBuffer(const NRIBuffer&) = delete;
		NRIBuffer& operator=(const NRIBuffer&) = delete;

		// Any thread. Memory: HOST_UPLOAD for CpuAccessMode::Write, HOST_READBACK for Read, DEVICE
		// otherwise. Volatile buffers are NVRHI's own upload memory and are not supported. Empty, and
		// logged, when NRI fails to create it.
		static NRIBuffer Create(const nvrhi::BufferDesc& desc);

		void Reset();

		// CPU-visible buffers only. The pointer addresses byte `offset`. Never waits for the GPU
		// (NVRHI's mapBuffer waited for copies that used the buffer); the caller makes sure no
		// frame in flight still reads or writes the mapped range.
		void* Map(uint64_t offset, uint64_t size) const;
		void Unmap() const;

		nri::Buffer* Get() const { return m_Buffer; }
		const nvrhi::BufferHandle& GetHandle() const { return m_Handle; }
		explicit operator bool() const { return m_Buffer != nullptr; }

	private:
		nri::Buffer* m_Buffer = nullptr;
		nvrhi::BufferHandle m_Handle;
	};

}
