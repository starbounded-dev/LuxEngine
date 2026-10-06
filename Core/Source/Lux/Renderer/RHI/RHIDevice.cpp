// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RHIDevice.h"

#include "Lux/Renderer/RenderCommandBuffer.h"

namespace Lux {

	namespace {

		struct RHIDeviceData
		{
			nri::Device* Device = nullptr;
			NRIInterface API = {};
			std::array<nri::Queue*, static_cast<size_t>(GPUQueue::Count)> Queues = {};
		};

		RHIDeviceData* s_Data = nullptr;

		// Indexed by GPUQueue.
		constexpr std::array<nri::QueueType, static_cast<size_t>(GPUQueue::Count)> k_QueueTypes = {
			nri::QueueType::GRAPHICS, nri::QueueType::COMPUTE, nri::QueueType::COPY
		};

		constexpr uint64_t k_SelfTestBufferSize = 64 * 1024;
		constexpr nri::Dim_t k_SelfTestTextureSize = 4;

		void NRI_CALL OnNRIMessage(nri::Message messageType, const char* file, uint32_t line, const char* message, void* userArg)
		{
			switch (messageType)
			{
				case nri::Message::INFO:
					LUX_CORE_INFO_TAG("Renderer", "[NRI] {}", message);
					break;
				case nri::Message::WARNING:
					LUX_CORE_WARN_TAG("Renderer", "[NRI] {}", message);
					break;
				default:
					LUX_CORE_ERROR_TAG("Renderer", "[NRI] {} ({}:{})", message, file, line);
					break;
			}
		}

		// NRI calls this after reporting an error. Its default raises SIGTRAP, which ends a process
		// with no debugger attached; break like a failed assert instead (Debug only).
		void NRI_CALL OnNRIAbort(void* userArg)
		{
			LUX_CORE_ASSERT(false, "NRI reported an error (see the [NRI] line above)");
		}

		bool Succeeded(nri::Result result, const char* step)
		{
			if (result == nri::Result::SUCCESS)
				return true;

			LUX_CORE_ERROR_TAG("Renderer", "[RHI] NRI self-test failed: {} returned {}", step, static_cast<int32_t>(result));
			return false;
		}

		void LogDeviceDesc(const nri::DeviceDesc& desc)
		{
			LUX_CORE_INFO_TAG("Renderer", "[RHI] NRI v{} on {}: queues graphics {} / compute {} / copy {}",
				desc.nriVersion, desc.adapterDesc.name,
				desc.adapterDesc.queueNum[static_cast<size_t>(nri::QueueType::GRAPHICS)],
				desc.adapterDesc.queueNum[static_cast<size_t>(nri::QueueType::COMPUTE)],
				desc.adapterDesc.queueNum[static_cast<size_t>(nri::QueueType::COPY)]);
			LUX_CORE_INFO_TAG("Renderer", "[RHI]   tiers: bindless {}, resourceBinding {}, shadingRate {}",
				desc.tiers.bindless, desc.tiers.resourceBinding, desc.tiers.shadingRate);
			LUX_CORE_INFO_TAG("Renderer", "[RHI]   features: meshShader {}, enhancedBarriers {}, timestamp {}, pipelineStatistics {}, viewportOriginBottomLeft {}, rootConstantsOffset {}",
				static_cast<bool>(desc.features.meshShader), static_cast<bool>(desc.features.enhancedBarriers),
				static_cast<bool>(desc.features.timestamp), static_cast<bool>(desc.features.pipelineStatistics),
				static_cast<bool>(desc.features.viewportOriginBottomLeft), static_cast<bool>(desc.features.rootConstantsOffset));
		}

	}

	bool RHIDevice::Init(const RHIDeviceCreateInfo& createInfo)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(!s_Data, "RHIDevice is already initialized");

		std::vector<nri::QueueFamilyVKDesc> queueFamilies;
		for (size_t i = 0; i < k_QueueTypes.size(); i++)
		{
			if (createInfo.QueueFamilies[i] < 0)
				continue;

			nri::QueueFamilyVKDesc& family = queueFamilies.emplace_back();
			family.queueNum = 1;
			family.queueType = k_QueueTypes[i];
			family.familyIndex = static_cast<uint32_t>(createInfo.QueueFamilies[i]);
		}

		std::vector<const char*> instanceExtensions;
		for (const std::string& extension : createInfo.InstanceExtensions)
			instanceExtensions.push_back(extension.c_str());

		std::vector<const char*> deviceExtensions;
		for (const std::string& extension : createInfo.DeviceExtensions)
			deviceExtensions.push_back(extension.c_str());

		nri::DeviceCreationVKDesc desc = {};
		desc.callbackInterface.MessageCallback = OnNRIMessage;
		desc.callbackInterface.AbortExecution = OnNRIAbort;
		desc.vkBindingOffsets = {};
		desc.vkExtensions.instanceExtensions = instanceExtensions.data();
		desc.vkExtensions.instanceExtensionNum = static_cast<uint32_t>(instanceExtensions.size());
		desc.vkExtensions.deviceExtensions = deviceExtensions.data();
		desc.vkExtensions.deviceExtensionNum = static_cast<uint32_t>(deviceExtensions.size());
		desc.vkInstance = createInfo.Instance;
		desc.vkDevice = createInfo.Device;
		desc.vkPhysicalDevice = createInfo.PhysicalDevice;
		desc.queueFamilies = queueFamilies.data();
		desc.queueFamilyNum = static_cast<uint32_t>(queueFamilies.size());
		desc.minorVersion = static_cast<uint8_t>(createInfo.MinorVersion);
		desc.enableNRIValidation = createInfo.EnableValidation;

		nri::Device* device = nullptr;
		if (nri::nriCreateDeviceFromVKDevice(desc, device) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RHI] Failed to wrap the Vulkan device in NRI");
			return false;
		}

		s_Data = lnew RHIDeviceData();
		s_Data->Device = device;

		NRIInterface& api = s_Data->API;
		bool interfacesFound = nri::nriGetInterface(*device, NRI_INTERFACE(nri::CoreInterface), static_cast<nri::CoreInterface*>(&api)) == nri::Result::SUCCESS
			&& nri::nriGetInterface(*device, NRI_INTERFACE(nri::HelperInterface), static_cast<nri::HelperInterface*>(&api)) == nri::Result::SUCCESS
			&& nri::nriGetInterface(*device, NRI_INTERFACE(nri::WrapperVKInterface), static_cast<nri::WrapperVKInterface*>(&api)) == nri::Result::SUCCESS
			&& nri::nriGetInterface(*device, NRI_INTERFACE(nri::SwapChainInterface), static_cast<nri::SwapChainInterface*>(&api)) == nri::Result::SUCCESS;
		if (interfacesFound && api.GetDeviceDesc(*device).features.meshShader)
			interfacesFound = nri::nriGetInterface(*device, NRI_INTERFACE(nri::MeshShaderInterface), static_cast<nri::MeshShaderInterface*>(&api)) == nri::Result::SUCCESS;

		if (!interfacesFound)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RHI] NRI is missing a required interface");
			Shutdown();
			return false;
		}

		for (size_t i = 0; i < k_QueueTypes.size(); i++)
		{
			if (createInfo.QueueFamilies[i] >= 0 && api.GetQueue(*device, k_QueueTypes[i], 0, s_Data->Queues[i]) != nri::Result::SUCCESS)
				s_Data->Queues[i] = nullptr;
		}

		if (!s_Data->Queues[static_cast<size_t>(GPUQueue::Graphics)])
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RHI] NRI has no graphics queue");
			Shutdown();
			return false;
		}

		LogDeviceDesc(api.GetDeviceDesc(*device));
		return true;
	}

	void RHIDevice::Shutdown()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!s_Data)
			return;

		nri::nriDestroyDevice(s_Data->Device);
		ldelete s_Data;
		s_Data = nullptr;
	}

	bool RHIDevice::IsInitialized()
	{
		return s_Data != nullptr;
	}

	nri::Device& RHIDevice::Get()
	{
		LUX_CORE_ASSERT(s_Data, "RHIDevice is not initialized");
		return *s_Data->Device;
	}

	const NRIInterface& RHIDevice::API()
	{
		LUX_CORE_ASSERT(s_Data, "RHIDevice is not initialized");
		return s_Data->API;
	}

	nri::Queue* RHIDevice::GetQueue(GPUQueue queue)
	{
		LUX_CORE_ASSERT(s_Data, "RHIDevice is not initialized");
		return s_Data->Queues[static_cast<size_t>(queue)];
	}

	const nri::DeviceDesc& RHIDevice::GetDesc()
	{
		LUX_CORE_ASSERT(s_Data, "RHIDevice is not initialized");
		return s_Data->API.GetDeviceDesc(*s_Data->Device);
	}

	bool RHIDevice::RunSelfTest()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(s_Data, "RHIDevice is not initialized");

		const NRIInterface& api = s_Data->API;
		nri::Device& device = *s_Data->Device;
		nri::Queue& queue = *s_Data->Queues[static_cast<size_t>(GPUQueue::Graphics)];

		nri::Buffer* buffer = nullptr;
		nri::Texture* texture = nullptr;
		nri::Descriptor* textureView = nullptr;
		nri::CommandAllocator* commandAllocator = nullptr;
		nri::CommandBuffer* commandBuffer = nullptr;
		nri::Fence* fence = nullptr;

		// Every step logs its own failure; whatever was created is destroyed below on every path.
		const bool passed = [&]()
		{
			nri::BufferDesc bufferDesc = {};
			bufferDesc.size = k_SelfTestBufferSize;
			bufferDesc.usage = nri::BufferUsageBits::SHADER_RESOURCE_STORAGE;
			if (!Succeeded(api.CreateCommittedBuffer(device, nri::MemoryLocation::DEVICE, 0.0f, bufferDesc, buffer), "CreateCommittedBuffer"))
				return false;

			nri::TextureDesc textureDesc = {};
			textureDesc.type = nri::TextureType::TEXTURE_2D;
			textureDesc.usage = nri::TextureUsageBits::SHADER_RESOURCE;
			textureDesc.format = nri::Format::RGBA8_UNORM;
			textureDesc.width = k_SelfTestTextureSize;
			textureDesc.height = k_SelfTestTextureSize;
			textureDesc.depth = 1;
			textureDesc.mipNum = 1;
			textureDesc.layerNum = 1;
			textureDesc.sampleNum = 1;
			if (!Succeeded(api.CreateCommittedTexture(device, nri::MemoryLocation::DEVICE, 0.0f, textureDesc, texture), "CreateCommittedTexture"))
				return false;

			nri::TextureViewDesc viewDesc = {};
			viewDesc.texture = texture;
			viewDesc.type = nri::TextureView::TEXTURE;
			viewDesc.format = nri::Format::RGBA8_UNORM;
			viewDesc.mipNum = nri::REMAINING;
			viewDesc.layerNum = nri::REMAINING;
			if (!Succeeded(api.CreateTextureView(viewDesc, textureView), "CreateTextureView"))
				return false;

			if (!Succeeded(api.CreateCommandAllocator(queue, commandAllocator), "CreateCommandAllocator")
				|| !Succeeded(api.CreateCommandBuffer(*commandAllocator, commandBuffer), "CreateCommandBuffer")
				|| !Succeeded(api.CreateFence(device, 0, fence), "CreateFence"))
				return false;

			if (!Succeeded(api.BeginCommandBuffer(*commandBuffer, nullptr), "BeginCommandBuffer"))
				return false;

			nri::TextureBarrierDesc textureBarrier = {};
			textureBarrier.texture = texture;
			textureBarrier.before = { nri::AccessBits::NONE, nri::Layout::UNDEFINED, nri::StageBits::NONE };
			textureBarrier.after = { nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::FRAGMENT_SHADER };
			textureBarrier.mipNum = nri::REMAINING;
			textureBarrier.layerNum = nri::REMAINING;

			nri::BarrierDesc barrierDesc = {};
			barrierDesc.textures = &textureBarrier;
			barrierDesc.textureNum = 1;
			api.CmdBarrier(*commandBuffer, barrierDesc);
			api.CmdZeroBuffer(*commandBuffer, *buffer, 0, nri::WHOLE_SIZE);

			if (!Succeeded(api.EndCommandBuffer(*commandBuffer), "EndCommandBuffer"))
				return false;

			nri::FenceSubmitDesc signal = {};
			signal.fence = fence;
			signal.value = 1;

			nri::QueueSubmitDesc submitDesc = {};
			submitDesc.commandBuffers = &commandBuffer;
			submitDesc.commandBufferNum = 1;
			submitDesc.signalFences = &signal;
			submitDesc.signalFenceNum = 1;

			// NVRHI submits to the same VkQueue.
			RenderCommandBuffer::LockQueue();
			const nri::Result submitResult = api.QueueSubmit(queue, submitDesc);
			RenderCommandBuffer::UnlockQueue();
			if (!Succeeded(submitResult, "QueueSubmit"))
				return false;

			api.Wait(*fence, 1);
			return true;
		}();

		if (fence)
			api.DestroyFence(fence);
		if (commandBuffer)
			api.DestroyCommandBuffer(commandBuffer);
		if (commandAllocator)
			api.DestroyCommandAllocator(commandAllocator);
		if (textureView)
			api.DestroyDescriptor(textureView);
		if (texture)
			api.DestroyTexture(texture);
		if (buffer)
			api.DestroyBuffer(buffer);

		if (passed)
			LUX_CORE_INFO_TAG("Renderer", "[RHI] NRI self-test passed");
		return passed;
	}

}
