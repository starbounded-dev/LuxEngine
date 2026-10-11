// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "StorageImageClear.h"

#include "Lux/Renderer/Image.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/NVRHIWrappers.h"
#include "Lux/Renderer/RHI/RHIDevice.h"

namespace Lux {

	namespace {
		// One set (set 0) with one storage texture; no shader uses it, only CmdClearStorage.
		nri::PipelineLayout* s_Layout = nullptr;
	}

	void StorageImageClear::Init()
	{
		nri::DescriptorRangeDesc range = {};
		range.baseRegisterIndex = 0;
		range.descriptorNum = 1;
		range.descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
		range.shaderStages = nri::StageBits::COMPUTE_SHADER;

		nri::DescriptorSetDesc setDesc = {};
		setDesc.registerSpace = 0;
		setDesc.ranges = &range;
		setDesc.rangeNum = 1;

		nri::PipelineLayoutDesc layoutDesc = {};
		layoutDesc.rootRegisterSpace = 1;
		layoutDesc.descriptorSets = &setDesc;
		layoutDesc.descriptorSetNum = 1;
		layoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
		layoutDesc.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
		LUX_CORE_VERIFY(RHIDevice::API().CreatePipelineLayout(RHIDevice::Get(), layoutDesc, s_Layout) == nri::Result::SUCCESS,
			"Failed to create the NRI storage clear layout");
	}

	void StorageImageClear::Shutdown()
	{
		if (!s_Layout)
			return;

		Renderer::SubmitResourceFree([layout = std::exchange(s_Layout, nullptr)]()
			{
				RHIDevice::API().DestroyPipelineLayout(layout);
			});
	}

	StorageImageClear::StorageImageClear(const Ref<Image2D>& image, uint32_t mip)
		: m_Mip(mip)
	{
		LUX_CORE_VERIFY(s_Layout, "StorageImageClear::Init must run before clears are created");
		LUX_CORE_VERIFY(image);

		const ImageSpecification& spec = image->GetSpecification();
		const ImageInfo& info = image->GetImageInfo();
		m_DebugName = spec.DebugName.empty() ? std::string("Image2D") : spec.DebugName;
		if (spec.Usage != ImageUsage::Storage || !info.Tracked.Handle || mip >= spec.Mips)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Cannot clear mip {} of '{}': it is not a mip of a valid storage image", mip, m_DebugName);
			return;
		}

		m_Texture = info.Tracked;
		m_LayerCount = glm::max(1u, spec.Layers);

		NRITextureViewKey key;
		key.Type = m_LayerCount > 1 ? nri::TextureView::STORAGE_TEXTURE_ARRAY : nri::TextureView::STORAGE_TEXTURE;
		key.MipOffset = mip;
		key.MipNum = 1;
		key.LayerNum = m_LayerCount > 1 ? 0 : 1;
		// GetNRITextureView logs its own failures.
		m_View = GetNRITextureView(static_cast<nri::Texture*>(m_Texture.Handle), key);
		if (!m_View)
			return;

		nri::DescriptorPoolDesc poolDesc = {};
		poolDesc.descriptorSetMaxNum = 1;
		poolDesc.storageTextureMaxNum = 1;
		const std::string debugName = std::format("{} clear (mip {})", m_DebugName, mip);
		m_Set = Ref<DescriptorSetGroup>::Create(*s_Layout, 0, poolDesc, 1, 0, debugName.c_str());
		if (m_Set->IsValid())
			m_Set->Write(0, 0, 0, &m_View, 1);
	}

	void StorageImageClear::RT_Record(RenderCommandBuffer& commandBuffer, const glm::vec4& value) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!IsValid())
		{
			if (!m_ReportedInvalid)
				LUX_CORE_ERROR_TAG("Renderer", "'{}' is not cleared: its clear could not be built", m_DebugName);
			m_ReportedInvalid = true;
			return;
		}

		// A storage clear writes in the storage layout, after the UAV barrier against earlier writes.
		commandBuffer.RT_RequireTextureState(m_Texture, { m_Mip, 1, 0, m_LayerCount }, ResourceState::UnorderedAccess);
		nri::CommandBuffer* nriCommandBuffer = commandBuffer.RT_BeginNRITransfer();
		if (!nriCommandBuffer)
			return;

		// The set is bound for the clear alone: draws and dispatches bind their own layout and sets.
		const NRIInterface& api = RHIDevice::API();
		api.CmdSetPipelineLayout(*nriCommandBuffer, nri::BindPoint::COMPUTE, *s_Layout);
		nri::SetDescriptorSetDesc setDesc = {};
		setDesc.setIndex = 0;
		setDesc.descriptorSet = m_Set->Get(0);
		setDesc.bindPoint = nri::BindPoint::COMPUTE;
		api.CmdSetDescriptorSet(*nriCommandBuffer, setDesc);

		nri::ClearStorageDesc clearDesc = {};
		clearDesc.descriptor = m_View;
		clearDesc.value.f = { value.r, value.g, value.b, value.a };
		clearDesc.setIndex = 0;
		clearDesc.rangeIndex = 0;
		clearDesc.descriptorIndex = 0;
		api.CmdClearStorage(*nriCommandBuffer, clearDesc);
		commandBuffer.RT_EndNRISegment();
	}

}
