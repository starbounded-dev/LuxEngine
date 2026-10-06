// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/RHITypes.h"

#include <nvrhi/nvrhi.h>

// Conversions between the Lux RHI vocabulary and NVRHI. Renderer implementation files only; this
// header goes away with NVRHI (NRI migration Phase 15). The enum values are identical, which
// NVRHIInterop.cpp proves with static_asserts, so the casts are free.

namespace Lux {

	enum class TextureWrap;

	inline nvrhi::ShaderType ToNVRHI(ShaderStage stage) { return static_cast<nvrhi::ShaderType>(stage); }
	inline ShaderStage FromNVRHI(nvrhi::ShaderType type) { return static_cast<ShaderStage>(type); }

	inline nvrhi::TextureDimension ToNVRHI(TextureDimension dimension) { return static_cast<nvrhi::TextureDimension>(dimension); }
	inline TextureDimension FromNVRHI(nvrhi::TextureDimension dimension) { return static_cast<TextureDimension>(dimension); }

	inline nvrhi::CommandQueue ToNVRHI(GPUQueue queue) { return static_cast<nvrhi::CommandQueue>(queue); }
	inline GPUQueue FromNVRHI(nvrhi::CommandQueue queue) { return static_cast<GPUQueue>(queue); }

	inline nvrhi::ResourceStates ToNVRHI(ResourceState state) { return static_cast<nvrhi::ResourceStates>(state); }
	inline ResourceState FromNVRHI(nvrhi::ResourceStates states) { return static_cast<ResourceState>(states); }

	// "All" is ~0u on both sides, so the counts carry over unchanged.
	inline nvrhi::TextureSubresourceSet ToNVRHI(const TextureSubresourceRange& range)
	{
		return nvrhi::TextureSubresourceSet(range.BaseMip, range.MipCount, range.BaseLayer, range.LayerCount);
	}

	inline TextureSubresourceRange FromNVRHI(const nvrhi::TextureSubresourceSet& set)
	{
		return { set.baseMipLevel, set.numMipLevels, set.baseArraySlice, set.numArraySlices };
	}

	nvrhi::SamplerAddressMode ToNVRHI(TextureWrap wrap);

}
