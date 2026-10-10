// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Assert.h"
#include "Lux/Core/Ref.h"
#include "Lux/Renderer/RHI/ResourceStateTracker.h"

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

	// What one descriptor set accesses, for the barrier tracker: recorded by the set's owner while it
	// writes the set, and required by RenderCommandBuffer before the draws and dispatches binding it
	// (NRI migration Phase 12; NVRHI derived the same from its binding sets).
	struct DescriptorSetUses
	{
		struct TextureUse
		{
			TrackedTexture Texture;
			TextureSubresourceRange Range;
			ResourceState State = ResourceState::ShaderResource;
		};

		struct BufferUse
		{
			TrackedBuffer Buffer;
			ResourceState State = ResourceState::ShaderResource;
		};

		std::vector<TextureUse> Textures;
		std::vector<BufferUse> Buffers;
		// A set with UnorderedAccess uses is required on every bind, not only when it changes: that
		// places the UAV barrier between two dispatches that write the same resource.
		bool HasStorageUses = false;

		// Null resources are skipped.
		void AddTexture(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState state);
		void AddBuffer(const TrackedBuffer& buffer, ResourceState state);
	};

	class DescriptorSetGroup;

	// A descriptor set as an NRI pass, draw or dispatch binds it (RenderCommandBuffer).
	struct BoundDescriptorSet
	{
		nri::DescriptorSet* Set = nullptr;
		// The group the set belongs to, whose recorded uses are required before the set is used. Null
		// for sets whose resources the caller requires itself.
		const DescriptorSetGroup* Group = nullptr;
		uint32_t Instance = 0;
	};

	// NRI descriptor sets for one descriptor-set number of one pipeline layout: a pool holding one
	// set per instance (one per frame in flight), and what each set accesses. A group is written while
	// it is built and never again once the GPU may read it; owners replace the whole group when its
	// contents change. The last reference frees the pool (and its sets) through the GPU deletion
	// queue.
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
		// Set `instance` with its uses; empty when the group is invalid.
		BoundDescriptorSet Bind(uint32_t instance) const { return instance < m_Sets.size() ? BoundDescriptorSet{ m_Sets[instance], this, instance } : BoundDescriptorSet{}; }

		// Writes `descriptors` into range `rangeIndex` of set `instance`, starting at array element
		// `baseDescriptor`. Only while the group is being built, or for a set no frame in flight reads.
		void Write(uint32_t instance, uint32_t rangeIndex, uint32_t baseDescriptor, const nri::Descriptor* const* descriptors, uint32_t descriptorCount);

		// The resources set `instance` accesses. Filled while the group is built, with the writes, and
		// like them left alone once the GPU may read the set. Valid groups only.
		DescriptorSetUses& GetUses(uint32_t instance) { LUX_CORE_ASSERT(instance < m_Uses.size()); return m_Uses[instance]; }
		const DescriptorSetUses& GetUses(uint32_t instance) const { LUX_CORE_ASSERT(instance < m_Uses.size()); return m_Uses[instance]; }

	private:
		nri::DescriptorPool* m_Pool = nullptr;
		std::vector<nri::DescriptorSet*> m_Sets;
		std::vector<DescriptorSetUses> m_Uses;
	};

}
