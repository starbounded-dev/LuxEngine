// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/ResourceStateTracker.h"
#include "Lux/Renderer/RHI/RHITypes.h"

#include <cstdint>
#include <string>

namespace nri {
	struct Buffer;
	struct Descriptor;
	struct Texture;
	enum class BufferView : uint8_t;
	enum class Format : uint8_t;
	enum class TextureView : uint8_t;
}

// NRI allocates and owns GPU textures and buffers (NRI migration Phase 8), described by the Lux
// descriptions below (Phase 15).

namespace Lux {

	// What a texture is created as (NRITexture::Create).
	struct RHITextureDesc
	{
		TextureDimension Dimension = TextureDimension::Texture2D;
		nri::Format Format = {};
		uint32_t Width = 1;
		uint32_t Height = 1;
		// Slices of a 3D texture; 1 otherwise.
		uint32_t Depth = 1;
		// Layers of an array or a cube (6 per cube); 1 for a 3D texture.
		uint32_t ArraySize = 1;
		uint32_t MipLevels = 1;
		uint32_t SampleCount = 1;
		bool IsShaderResource = true;
		bool IsUAV = false;
		// A color or depth-stencil attachment, by format.
		bool IsRenderTarget = false;
		bool IsShadingRateSurface = false;
		// The state the texture is in between command buffers (ResourceStateTracker). Unknown
		// leaves it untracked.
		ResourceState RestingState = ResourceState::Unknown;
		std::string DebugName;
	};

	// Who reads or writes a buffer from the CPU, which picks its memory: device memory, HOST_UPLOAD
	// or HOST_READBACK. CPU-visible buffers are mapped and never tracked.
	enum class CPUAccess : uint8_t
	{
		None,
		Write,
		Read
	};

	// What a buffer is created as (NRIBuffer::Create).
	struct RHIBufferDesc
	{
		uint64_t Size = 0;
		uint32_t StructStride = 0;
		bool IsVertexBuffer = false;
		bool IsIndexBuffer = false;
		bool IsConstantBuffer = false;
		bool IsDrawIndirectArgs = false;
		// Shader-readable as a raw (byte-address) buffer.
		bool CanHaveRawViews = false;
		bool CanHaveUAVs = false;
		CPUAccess Access = CPUAccess::None;
		// As for textures; ignored for CPU-visible buffers.
		ResourceState RestingState = ResourceState::Unknown;
		std::string DebugName;
	};

	// A texture NRI owns. Move-only. Destroying or replacing it frees it, and its views, through
	// Renderer::SubmitResourceFree once the frames that may use it have retired.
	class NRITexture
	{
	public:
		NRITexture() = default;
		~NRITexture();
		NRITexture(NRITexture&& other) noexcept;
		NRITexture& operator=(NRITexture&& other) noexcept;
		NRITexture(const NRITexture&) = delete;
		NRITexture& operator=(const NRITexture&) = delete;

		// Any thread. Empty, and logged, when the format is UNKNOWN or NRI fails to create it.
		static NRITexture Create(const RHITextureDesc& desc);

		void Reset();

		nri::Texture* Get() const { return m_Texture; }
		// The texture as the resource state tracker sees it, resting in the desc's RestingState.
		const TrackedTexture& GetTracked() const { return m_Tracked; }
		explicit operator bool() const { return m_Texture != nullptr; }

	private:
		nri::Texture* m_Texture = nullptr;
		TrackedTexture m_Tracked;
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

	// A buffer NRI owns; same rules as NRITexture. CPU-visible buffers (CPUAccess::Write or Read) are
	// mapped.
	class NRIBuffer
	{
	public:
		NRIBuffer() = default;
		~NRIBuffer();
		NRIBuffer(NRIBuffer&& other) noexcept;
		NRIBuffer& operator=(NRIBuffer&& other) noexcept;
		NRIBuffer(const NRIBuffer&) = delete;
		NRIBuffer& operator=(const NRIBuffer&) = delete;

		// Any thread. Memory: HOST_UPLOAD for CPUAccess::Write, HOST_READBACK for Read, DEVICE
		// otherwise. Empty, and logged, when NRI fails to create it.
		static NRIBuffer Create(const RHIBufferDesc& desc);

		void Reset();

		// CPU-visible buffers only. The pointer addresses byte `offset`. Never waits for the GPU
		// (NVRHI's mapBuffer waited for copies that used the buffer); the caller makes sure no
		// frame in flight still reads or writes the mapped range.
		void* Map(uint64_t offset, uint64_t size) const;
		void Unmap() const;

		nri::Buffer* Get() const { return m_Buffer; }
		// The size the buffer was created with (the NRI buffer may be larger: Vulkan has no empty
		// buffers).
		uint64_t GetSize() const { return m_Size; }
		// The buffer as the resource state tracker sees it: CPU-visible buffers are untracked, the
		// others rest in the desc's RestingState.
		const TrackedBuffer& GetTracked() const { return m_Tracked; }
		explicit operator bool() const { return m_Buffer != nullptr; }

	private:
		nri::Buffer* m_Buffer = nullptr;
		uint64_t m_Size = 0;
		TrackedBuffer m_Tracked;
	};

}
