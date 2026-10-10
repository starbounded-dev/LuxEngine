// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "BindlessTextureTable.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"

#include <vulkan/vulkan.h>
#include <algorithm>

namespace Lux {

	namespace
	{
		// Room left in the per-stage and per-set sampled-image limits for a material shader's
		// ordinary textures (G-buffer inputs, shadow maps, environment maps, BRDF LUT...).
		constexpr uint32_t k_ReservedSampledImages = 64;

		uint32_t s_Capacity = 0;
		// Declares only the bindless set; tables allocate their NRI sets from it.
		nri::PipelineLayout* s_NRILayout = nullptr;
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

		const nri::DescriptorRangeDesc range = GetNRIRange();
		nri::DescriptorSetDesc setDesc = {};
		setDesc.registerSpace = DescriptorSet;
		setDesc.ranges = &range;
		setDesc.rangeNum = 1;

		nri::PipelineLayoutDesc layoutDesc = {};
		layoutDesc.rootRegisterSpace = DescriptorSet + 1;
		layoutDesc.descriptorSets = &setDesc;
		layoutDesc.descriptorSetNum = 1;
		layoutDesc.shaderStages = nri::StageBits::ALL;
		layoutDesc.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
		LUX_CORE_VERIFY(RHIDevice::API().CreatePipelineLayout(RHIDevice::Get(), layoutDesc, s_NRILayout) == nri::Result::SUCCESS,
			"Failed to create the NRI bindless texture layout");
	}

	void BindlessTextureTable::Shutdown()
	{
		s_Capacity = 0;
		if (s_NRILayout)
		{
			Renderer::SubmitResourceFree([layout = std::exchange(s_NRILayout, nullptr)]()
				{
					RHIDevice::API().DestroyPipelineLayout(layout);
				});
		}
	}

	uint32_t BindlessTextureTable::GetCapacity()
	{
		return s_Capacity;
	}

	nri::DescriptorRangeDesc BindlessTextureTable::GetNRIRange()
	{
		nri::DescriptorRangeDesc range = {};
		range.baseRegisterIndex = 0;
		range.descriptorNum = s_Capacity;
		range.descriptorType = nri::DescriptorType::TEXTURE;
		range.shaderStages = nri::StageBits::ALL;
		range.flags = nri::DescriptorRangeBits::PARTIALLY_BOUND | nri::DescriptorRangeBits::ARRAY | nri::DescriptorRangeBits::VARIABLE_SIZED_ARRAY;
		return range;
	}

	BindlessTextureTable::BindlessTextureTable()
	{
		LUX_CORE_VERIFY(s_NRILayout, "BindlessTextureTable::Init must run before tables are created");

		// Unwritten slots are legal (partially bound) as long as no shader reads them; the owner
		// writes every index it hands out, and slot 0 is the fallback the shaders use.
		m_Frames.resize(Renderer::GetConfig().FramesInFlight);
		for (FrameTable& frame : m_Frames)
		{
			frame.Written.resize(s_Capacity);
			frame.WrittenTextures.resize(s_Capacity, nullptr);
			frame.Queued.emplace_back(0, Renderer::GetWhiteTexture());
		}

		const uint32_t frameCount = static_cast<uint32_t>(m_Frames.size());
		nri::DescriptorPoolDesc poolDesc = {};
		poolDesc.descriptorSetMaxNum = frameCount;
		poolDesc.textureMaxNum = s_Capacity * frameCount;
		m_NRISets = Ref<DescriptorSetGroup>::Create(*s_NRILayout, 0, poolDesc, frameCount, s_Capacity, "Bindless textures");
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

		// This frame's previous use finished before the frame index came round again, so its set is
		// free to write; the passes recorded after this read the new contents.
		const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(m_Frames.size());
		FrameTable& frame = m_Frames[frameSlot];
		// The group logged its own failure to build; there is nothing to write into.
		if (!m_NRISets->IsValid())
		{
			frame.Queued.clear();
			return;
		}

		for (auto& [slot, texture] : frame.Queued)
		{
			const ImageInfo* imageInfo = static_cast<const ImageInfo*>(texture->GetDescriptorInfo());
			const nri::Texture* rhiTexture = imageInfo ? imageInfo->RHITexture : nullptr;
			if (!rhiTexture || frame.WrittenTextures[slot] == rhiTexture)
			{
				frame.Written[slot] = texture;
				continue;
			}

			const nri::Descriptor* view = GetNRITextureView(imageInfo->RHITexture, { nri::TextureView::TEXTURE, 0, 0, 0, 1 });
			if (!view)
			{
				LUX_CORE_ERROR_TAG("Renderer", "Bindless texture slot {} has no NRI view", slot);
				continue;
			}
			m_NRISets->Write(frameSlot, 0, slot, &view, 1);

			frame.Written[slot] = texture;
			frame.WrittenTextures[slot] = rhiTexture;
		}
		frame.Queued.clear();
	}

	BoundDescriptorSet BindlessTextureTable::RT_GetDescriptorSet(uint32_t frameIndex) const
	{
		return m_Frames.empty() || !m_NRISets ? BoundDescriptorSet{} : m_NRISets->Bind(frameIndex % static_cast<uint32_t>(m_Frames.size()));
	}

}
