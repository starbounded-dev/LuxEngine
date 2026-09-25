// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "BindlessTextureTable.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/Renderer.h"

#include <vulkan/vulkan.h>
#include <algorithm>

namespace Lux {

	namespace
	{
		// Room left in the per-stage and per-set sampled-image limits for a material shader's
		// ordinary textures (G-buffer inputs, shadow maps, environment maps, BRDF LUT...).
		constexpr uint32_t k_ReservedSampledImages = 64;

		nvrhi::BindingLayoutHandle s_Layout;
		uint32_t s_Capacity = 0;
	}

	void BindlessTextureTable::Init()
	{
		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();

		VkPhysicalDeviceProperties properties{};
		const VkPhysicalDevice physicalDevice = (VkPhysicalDevice)device->getNativeObject(nvrhi::ObjectTypes::VK_PhysicalDevice);
		vkGetPhysicalDeviceProperties(physicalDevice, &properties);
		const uint32_t stageLimit = properties.limits.maxPerStageDescriptorSampledImages;
		const uint32_t setLimit = properties.limits.maxDescriptorSetSampledImages;
		const uint32_t deviceLimit = std::min(stageLimit, setLimit);
		s_Capacity = deviceLimit > k_ReservedSampledImages ? std::min(MaxCapacity, deviceLimit - k_ReservedSampledImages) : 0;
		LUX_CORE_VERIFY(s_Capacity > 0, "The GPU's sampled-image descriptor limits ({}, {}) leave no room for bindless textures", stageLimit, setLimit);
		if (s_Capacity < MaxCapacity)
			LUX_CORE_WARN_TAG("Renderer", "Bindless textures limited to {} by the GPU (per-stage {}, per-set {})", s_Capacity, stageLimit, setLimit);
		else
			LUX_CORE_INFO_TAG("Renderer", "Bindless textures: {} slots", s_Capacity);

		nvrhi::BindlessLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::All;
		desc.firstSlot = 0;
		desc.maxCapacity = s_Capacity;
		desc.layoutType = nvrhi::BindlessLayoutDesc::LayoutType::Immutable;
		desc.registerSpaces = { nvrhi::BindingLayoutItem::Texture_SRV(0) };
		s_Layout = device->createBindlessLayout(desc);
		LUX_CORE_VERIFY(s_Layout, "Failed to create the bindless texture layout");
	}

	void BindlessTextureTable::Shutdown()
	{
		s_Layout = nullptr;
		s_Capacity = 0;
	}

	nvrhi::BindingLayoutHandle BindlessTextureTable::GetLayout()
	{
		return s_Layout;
	}

	uint32_t BindlessTextureTable::GetCapacity()
	{
		return s_Capacity;
	}

	BindlessTextureTable::BindlessTextureTable()
	{
		LUX_CORE_VERIFY(s_Layout, "BindlessTextureTable::Init must run before tables are created");
		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();

		// Unwritten slots are legal (partially bound) as long as no shader reads them; the owner
		// writes every index it hands out, and slot 0 is the fallback the shaders use.
		m_Frames.resize(Renderer::GetConfig().FramesInFlight);
		for (FrameTable& frame : m_Frames)
		{
			frame.Table = device->createDescriptorTable(s_Layout);
			LUX_CORE_VERIFY(frame.Table, "Failed to create a bindless texture table");
			device->resizeDescriptorTable(frame.Table, s_Capacity, false);
			frame.Written.resize(s_Capacity);
			frame.WrittenHandles.resize(s_Capacity, nullptr);
			frame.Queued.emplace_back(0, Renderer::GetWhiteTexture());
		}
	}

	BindlessTextureTable::~BindlessTextureTable()
	{
		// In-flight frames may still reference the tables and their textures.
		Renderer::SubmitResourceFree([frames = std::move(m_Frames)]() mutable
			{
				frames.clear();
			});
	}

	void BindlessTextureTable::SetSlot(uint32_t slot, Ref<Texture2D> texture)
	{
		if (slot >= s_Capacity)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Bindless texture slot {} is outside the table ({} slots)", slot, s_Capacity);
			return;
		}
		m_Pending.emplace_back(slot, texture ? std::move(texture) : Renderer::GetWhiteTexture());
	}

	void BindlessTextureTable::Flush()
	{
		Ref<BindlessTextureTable> instance = this;
		Renderer::Submit([instance, writes = std::move(m_Pending)]() mutable
			{
				for (FrameTable& frame : instance->m_Frames)
					frame.Queued.insert(frame.Queued.end(), writes.begin(), writes.end());
				instance->RT_WriteTable(Renderer::RT_GetCurrentFrameIndex());
			});
		m_Pending.clear();
	}

	void BindlessTextureTable::RT_WriteTable(uint32_t frameIndex)
	{
		if (m_Frames.empty())
			return;

		// This frame's previous use finished before the frame index came round again, so its
		// table is free to write; the passes recorded after this read the new contents.
		FrameTable& frame = m_Frames[frameIndex % m_Frames.size()];
		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		for (auto& [slot, texture] : frame.Queued)
		{
			nvrhi::TextureHandle handle = texture->GetHandle();
			if (!handle || frame.WrittenHandles[slot] == handle.Get())
			{
				frame.Written[slot] = texture;
				continue;
			}
			if (!device->writeDescriptorTable(frame.Table, nvrhi::BindingSetItem::Texture_SRV(slot, handle)))
			{
				LUX_CORE_ERROR_TAG("Renderer", "Failed to write bindless texture slot {}", slot);
				continue;
			}
			frame.Written[slot] = texture;
			frame.WrittenHandles[slot] = handle.Get();
		}
		frame.Queued.clear();
	}

	nvrhi::IDescriptorTable* BindlessTextureTable::RT_GetTable(uint32_t frameIndex) const
	{
		return m_Frames.empty() ? nullptr : m_Frames[frameIndex % m_Frames.size()].Table.Get();
	}

}
