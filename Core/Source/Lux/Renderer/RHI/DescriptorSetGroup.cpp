// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "DescriptorSetGroup.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"

namespace Lux {

	DescriptorSetGroup::DescriptorSetGroup(nri::PipelineLayout& layout, uint32_t setIndex, const nri::DescriptorPoolDesc& poolDesc,
		uint32_t instanceCount, uint32_t variableDescriptorNum, const char* debugName)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const NRIInterface& api = RHIDevice::API();
		if (api.CreateDescriptorPool(RHIDevice::Get(), poolDesc, m_Pool) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Failed to create an NRI descriptor pool for {} (set index {})", debugName, setIndex);
			m_Pool = nullptr;
			return;
		}
		api.SetDebugName(m_Pool, debugName);

		std::vector<nri::DescriptorSet*> sets(instanceCount, nullptr);
		if (api.AllocateDescriptorSets(*m_Pool, layout, setIndex, sets.data(), instanceCount, variableDescriptorNum) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Failed to allocate {} NRI descriptor sets for {} (set index {})", instanceCount, debugName, setIndex);
			return;
		}
		m_Sets = std::move(sets);
		m_Uses.resize(instanceCount);
	}

	DescriptorSetGroup::~DescriptorSetGroup()
	{
		if (!m_Pool)
			return;

		// The sets belong to the pool and go with it.
		Renderer::SubmitResourceFree([pool = m_Pool]()
			{
				RHIDevice::API().DestroyDescriptorPool(pool);
			});
	}

	void DescriptorSetUses::AddTexture(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState state)
	{
		if (!texture.Handle)
			return;
		Textures.push_back({ texture, range, state });
		HasStorageUses |= state == ResourceState::UnorderedAccess;
	}

	void DescriptorSetUses::AddBuffer(const TrackedBuffer& buffer, ResourceState state)
	{
		if (!buffer.Handle)
			return;
		Buffers.push_back({ buffer, state });
		HasStorageUses |= state == ResourceState::UnorderedAccess;
	}

	void DescriptorSetGroup::Write(uint32_t instance, uint32_t rangeIndex, uint32_t baseDescriptor, const nri::Descriptor* const* descriptors, uint32_t descriptorCount)
	{
		LUX_CORE_ASSERT(instance < m_Sets.size());
		nri::UpdateDescriptorRangeDesc update = {};
		update.descriptorSet = m_Sets[instance];
		update.rangeIndex = rangeIndex;
		update.baseDescriptor = baseDescriptor;
		update.descriptors = descriptors;
		update.descriptorNum = descriptorCount;
		RHIDevice::API().UpdateDescriptorRanges(&update, 1);
	}

}
