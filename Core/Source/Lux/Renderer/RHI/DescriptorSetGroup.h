// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"

#include <cstdint>
#include <vector>

namespace nri {
	struct Descriptor;
	struct DescriptorPool;
	struct DescriptorPoolDesc;
	struct DescriptorSet;
	struct PipelineLayout;
}

namespace Lux {

	// NRI descriptor sets for one descriptor-set number of one pipeline layout: a pool holding one
	// set per instance (one per frame in flight). A group is written while it is built and never
	// again once the GPU may read it; owners replace the whole group when its contents change, as
	// NVRHI replaces binding sets. It is shared like the NVRHI binding-set handles it sits beside,
	// and the last reference frees the pool (and its sets) through the GPU deletion queue.
	class DescriptorSetGroup : public RefCounted
	{
	public:
		// Any thread. `setIndex` is the set's position in the layout (VulkanShader::GetNRISetIndex).
		// Invalid (and logged) when NRI cannot create the pool or allocate the sets.
		DescriptorSetGroup(nri::PipelineLayout& layout, uint32_t setIndex, const nri::DescriptorPoolDesc& poolDesc,
			uint32_t instanceCount, uint32_t variableDescriptorNum, const char* debugName);
		~DescriptorSetGroup();

		bool IsValid() const { return !m_Sets.empty(); }
		nri::DescriptorSet* Get(uint32_t instance) const { return instance < m_Sets.size() ? m_Sets[instance] : nullptr; }

		// Writes `descriptors` into range `rangeIndex` of set `instance`, starting at array element
		// `baseDescriptor`. Only while the group is being built, or for a set no frame in flight reads.
		void Write(uint32_t instance, uint32_t rangeIndex, uint32_t baseDescriptor, const nri::Descriptor* const* descriptors, uint32_t descriptorCount);

	private:
		nri::DescriptorPool* m_Pool = nullptr;
		std::vector<nri::DescriptorSet*> m_Sets;
	};

}
