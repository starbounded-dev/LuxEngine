// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/ResourceStateTracker.h"

#include <nvrhi/nvrhi.h>

// The NVRHI side of the resource state tracker. Goes away with NVRHI (NRI migration Phase 15).

namespace Lux {

	// Forwards requirements to NVRHI's setTextureState/setBufferState. NVRHI keeps its own state per
	// command list and works out the "before" state; with automatic barriers off these calls are
	// the only transitions it makes.
	class NVRHIBarrierEmitter final : public IBarrierEmitter
	{
	public:
		void SetCommandList(nvrhi::ICommandList* commandList) { m_CommandList = commandList; }

		void RequireTexture(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState state) override;
		void RequireBuffer(const TrackedBuffer& buffer, ResourceState state) override;
		void Commit() override;

		bool QueryTextureState(const TrackedTexture& texture, uint32_t mip, uint32_t layer, ResourceState& outState) const override;
		bool QueryBufferState(const TrackedBuffer& buffer, ResourceState& outState) const override;

	private:
		nvrhi::ICommandList* m_CommandList = nullptr;
	};

	// Describe NVRHI objects for the tracker. The resting state is the keepInitialState initial
	// state, which every Lux image and GPU buffer is created with; volatile and CPU-visible buffers
	// have none, because NVRHI does not track their state.
	TrackedTexture DescribeTexture(nvrhi::ITexture* texture);
	TrackedBuffer DescribeBuffer(nvrhi::IBuffer* buffer);

}
