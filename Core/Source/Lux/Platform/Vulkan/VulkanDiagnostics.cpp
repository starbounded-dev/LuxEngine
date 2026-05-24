#include "lpch.h"
#include "VulkanDiagnostics.h"

#include "Lux/Core/Application.h"
#include "Lux/Platform/Vulkan/VulkanContext.h"

#include <cstring>

namespace Lux::Utils {

	static std::vector<VulkanCheckpointData> s_CheckpointStorage(1024);
	static uint32_t s_CheckpointStorageIndex = 0;

	void SetVulkanCheckpoint(VkCommandBuffer commandBuffer, const std::string& data)
	{
		if (!commandBuffer)
			return;

		DeviceManager* deviceManager = Application::GetGraphicsDeviceManager();
		if (!deviceManager || !deviceManager->IsVulkanDeviceExtensionEnabled(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME))
			return;

		Ref<VulkanDevice> device = VulkanContext::GetCurrentDevice();
		if (!device || !device->GetVulkanDevice())
			return;

		auto vkCmdSetCheckpoint = (PFN_vkCmdSetCheckpointNV)vkGetDeviceProcAddr(device->GetVulkanDevice(), "vkCmdSetCheckpointNV");
		if (!vkCmdSetCheckpoint)
			return;

		s_CheckpointStorageIndex = (s_CheckpointStorageIndex + 1) % 1024;
		VulkanCheckpointData& checkpoint = s_CheckpointStorage[s_CheckpointStorageIndex];
		memset(checkpoint.Data, 0, sizeof(checkpoint.Data));
		std::strncpy(checkpoint.Data, data.c_str(), sizeof(checkpoint.Data) - 1);
		vkCmdSetCheckpoint(commandBuffer, &checkpoint);
	}

}
