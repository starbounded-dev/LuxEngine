// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "CommandBufferPool.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RHI/RHIQueueTimeline.h"

namespace Lux {

	CommandBufferPool::CommandBufferPool(GPUQueue queue, uint32_t initialCount, std::string debugName)
		: m_Queue(queue), m_DebugName(std::move(debugName))
	{
		for (uint32_t i = 0; i < initialCount; i++)
		{
			if (!AddEntry())
				break;
		}
	}

	CommandBufferPool::~CommandBufferPool()
	{
		if (m_Entries.empty())
			return;

		// The last submissions may still run; the deletion queue waits for their frame.
		Renderer::SubmitResourceFree([entries = std::move(m_Entries)]()
			{
				const NRIInterface& api = RHIDevice::API();
				for (const Entry& entry : entries)
				{
					api.DestroyCommandBuffer(entry.CommandBuffer);
					api.DestroyCommandAllocator(entry.Allocator);
				}
			});
	}

	bool CommandBufferPool::AddEntry()
	{
		const NRIInterface& api = RHIDevice::API();
		nri::Queue* queue = RHIDevice::GetQueue(m_Queue);
		Entry entry;
		if (!queue || api.CreateCommandAllocator(*queue, entry.Allocator) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "{}: NRI could not create a command allocator", m_DebugName);
			return false;
		}
		if (api.CreateCommandBuffer(*entry.Allocator, entry.CommandBuffer) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "{}: NRI could not create a command buffer", m_DebugName);
			api.DestroyCommandAllocator(entry.Allocator);
			return false;
		}
		api.SetDebugName(entry.CommandBuffer, m_DebugName.c_str());
		m_Entries.push_back(entry);
		return true;
	}

	nri::CommandBuffer* CommandBufferPool::Begin()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const uint64_t completed = RHIQueueTimeline::GetCompletedValue(m_Queue);
		size_t index = m_Entries.size();
		for (size_t i = 0; i < m_Entries.size(); i++)
		{
			const size_t candidate = (m_Next + i) % m_Entries.size();
			if (m_Entries[candidate].RetireValue <= completed)
			{
				index = candidate;
				break;
			}
		}

		if (index == m_Entries.size())
		{
			// Every command buffer is in flight.
			if (!AddEntry())
				return nullptr;
			LUX_CORE_TRACE_TAG("Renderer", "{}: {} command buffers", m_DebugName, m_Entries.size());
		}
		m_Next = (index + 1) % m_Entries.size();

		const NRIInterface& api = RHIDevice::API();
		Entry& entry = m_Entries[index];
		api.ResetCommandAllocator(*entry.Allocator);
		if (api.BeginCommandBuffer(*entry.CommandBuffer, nullptr) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "{}: NRI could not begin a command buffer", m_DebugName);
			return nullptr;
		}
		return entry.CommandBuffer;
	}

	bool CommandBufferPool::End(nri::CommandBuffer& commandBuffer)
	{
		if (RHIDevice::API().EndCommandBuffer(commandBuffer) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "{}: NRI could not end a command buffer", m_DebugName);
			return false;
		}
		return true;
	}

	void CommandBufferPool::MarkSubmitted(nri::CommandBuffer& commandBuffer, uint64_t value)
	{
		for (Entry& entry : m_Entries)
		{
			if (entry.CommandBuffer == &commandBuffer)
			{
				entry.RetireValue = value;
				return;
			}
		}
		LUX_CORE_ASSERT(false, "{}: the submitted command buffer is not from this pool", m_DebugName);
	}

}
