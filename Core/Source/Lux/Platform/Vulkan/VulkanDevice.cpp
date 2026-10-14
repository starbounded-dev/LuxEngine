/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

/*
License for glfw

Copyright (c) 2002-2006 Marcus Geelnard

Copyright (c) 2006-2019 Camilla Lowy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
*/
#include "lpch.h"
#include "VulkanDevice.h"

#include "Lux/Platform/Vulkan/Debug/Aftermath.h"

#include <GLFW/glfw3.h>

#include <cstring>

// Define the Vulkan dynamic dispatcher - this needs to occur in exactly one cpp file in the program.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Lux {

	static std::vector<const char*> stringSetToVector(const std::unordered_set<std::string>& set)
	{
		std::vector<const char*> ret;
		for (const auto& s : set)
		{
			ret.push_back(s.c_str());
		}

		return ret;
	}

	bool VulkanDevice::createInstance()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_DeviceParams.headlessDevice)
		{
			if (!glfwVulkanSupported())
			{
				LUX_CORE_ERROR("GLFW reports that Vulkan is not supported. Perhaps missing a call to glfwInit()?");
				return false;
			}

			// add any extensions required by GLFW
			uint32_t glfwExtCount;
			const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwExtCount);
			LUX_CORE_VERIFY(glfwExt);

			for (uint32_t i = 0; i < glfwExtCount; i++)
			{
				enabledExtensions.instance.insert(std::string(glfwExt[i]));
			}

			// The NRI swapchain (RHISwapChain) queries surfaces through get_surface_capabilities2, and
			// on X11 creates an Xlib surface, where GLFW asks only for the XCB one.
			enabledExtensions.instance.insert(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
			if (glfwGetPlatform() == GLFW_PLATFORM_X11)
				enabledExtensions.instance.insert("VK_KHR_xlib_surface");
		}

		// add instance extensions requested by the user
		for (const std::string& name : m_DeviceParams.requiredVulkanInstanceExtensions)
		{
			enabledExtensions.instance.insert(name);
		}
		for (const std::string& name : m_DeviceParams.optionalVulkanInstanceExtensions)
		{
			optionalExtensions.instance.insert(name);
		}

		// add layers requested by the user
		for (const std::string& name : m_DeviceParams.requiredVulkanLayers)
		{
			enabledExtensions.layers.insert(name);
		}
		for (const std::string& name : m_DeviceParams.optionalVulkanLayers)
		{
			optionalExtensions.layers.insert(name);
		}

		std::unordered_set<std::string> requiredExtensions = enabledExtensions.instance;

		// figure out which optional extensions are supported
		for (const auto& instanceExt : vk::enumerateInstanceExtensionProperties())
		{
			const std::string name = instanceExt.extensionName;
			if (optionalExtensions.instance.find(name) != optionalExtensions.instance.end())
			{
				enabledExtensions.instance.insert(name);
			}

			requiredExtensions.erase(name);
		}

		if (!requiredExtensions.empty())
		{
			std::stringstream ss;
			ss << "Cannot create a Vulkan instance because the following required extension(s) are not supported:";
			for (const auto& ext : requiredExtensions)
				ss << std::endl << "  - " << ext;

			LUX_CORE_ERROR("{}", ss.str().c_str());
			return false;
		}

		LUX_CORE_INFO("Enabled Vulkan instance extensions:");
		for (const auto& ext : enabledExtensions.instance)
		{
			LUX_CORE_INFO("    {}", ext.c_str());
		}

		std::unordered_set<std::string> requiredLayers = enabledExtensions.layers;

		for (const auto& layer : vk::enumerateInstanceLayerProperties())
		{
			const std::string name = layer.layerName;
			if (optionalExtensions.layers.find(name) != optionalExtensions.layers.end())
			{
				enabledExtensions.layers.insert(name);
			}

			requiredLayers.erase(name);
		}

		if (!requiredLayers.empty())
		{
			std::stringstream ss;
			ss << "Cannot create a Vulkan instance because the following required layer(s) are not supported:";
			for (const auto& ext : requiredLayers)
				ss << std::endl << "  - " << ext;

			LUX_CORE_ERROR("{}", ss.str().c_str());
			return false;
		}

		LUX_CORE_INFO("Enabled Vulkan layers:");
		for (const auto& layer : enabledExtensions.layers)
		{
			LUX_CORE_INFO("    {}", layer.c_str());
		}

		auto instanceExtVec = stringSetToVector(enabledExtensions.instance);
		auto layerVec = stringSetToVector(enabledExtensions.layers);

		auto applicationInfo = vk::ApplicationInfo();

		// Query the Vulkan API version supported on the system to make sure we use at least 1.3 when that's present.
		vk::Result res = vk::enumerateInstanceVersion(&applicationInfo.apiVersion);

		if (res != vk::Result::eSuccess)
		{
			LUX_CORE_ERROR("Call to vkEnumerateInstanceVersion failed, error code = {}", vk::to_string(res));
			return false;
		}

		const uint32_t minimumVulkanVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);

		// Check if the Vulkan API version is sufficient.
		if (applicationInfo.apiVersion < minimumVulkanVersion)
		{
			LUX_CORE_ERROR("The Vulkan API version supported on the system ({}.{}.{}) is too low, at least {}.{}.{} is required.",
				VK_API_VERSION_MAJOR(applicationInfo.apiVersion), VK_API_VERSION_MINOR(applicationInfo.apiVersion), VK_API_VERSION_PATCH(applicationInfo.apiVersion),
				VK_API_VERSION_MAJOR(minimumVulkanVersion), VK_API_VERSION_MINOR(minimumVulkanVersion), VK_API_VERSION_PATCH(minimumVulkanVersion));
			return false;
		}

		// Spec says: A non-zero variant indicates the API is a variant of the Vulkan API and applications will typically need to be modified to run against it.
		if (VK_API_VERSION_VARIANT(applicationInfo.apiVersion) != 0)
		{
			LUX_CORE_ERROR("The Vulkan API supported on the system uses an unexpected variant: {}.", VK_API_VERSION_VARIANT(applicationInfo.apiVersion));
			return false;
		}

		m_InstanceAPIVersion = applicationInfo.apiVersion;

		// Create the vulkan instance
		vk::InstanceCreateInfo info = vk::InstanceCreateInfo()
			.setEnabledLayerCount(uint32_t(layerVec.size()))
			.setPpEnabledLayerNames(layerVec.data())
			.setEnabledExtensionCount(uint32_t(instanceExtVec.size()))
			.setPpEnabledExtensionNames(instanceExtVec.data())
			.setPApplicationInfo(&applicationInfo);

		res = vk::createInstance(&info, nullptr, &m_VulkanInstance);
		if (res != vk::Result::eSuccess)
		{
			LUX_CORE_ERROR("Failed to create a Vulkan instance, error code = {}", vk::to_string(res));
			return false;
		}

		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_VulkanInstance);

		return true;
	}

	void VulkanDevice::installDebugCallback()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		auto info = vk::DebugReportCallbackCreateInfoEXT()
			.setFlags(vk::DebugReportFlagBitsEXT::eError |
					  vk::DebugReportFlagBitsEXT::eWarning |
					//   vk::DebugReportFlagBitsEXT::eInformation |
					  vk::DebugReportFlagBitsEXT::ePerformanceWarning)
			.setPfnCallback(vulkanDebugCallback)
			.setPUserData(this);

		vk::Result res = m_VulkanInstance.createDebugReportCallbackEXT(&info, nullptr, &m_DebugReportCallback);
		LUX_CORE_VERIFY(res == vk::Result::eSuccess);
	}

	bool VulkanDevice::pickPhysicalDevice()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		auto devices = m_VulkanInstance.enumeratePhysicalDevices();

		int firstDevice = 0;
		int lastDevice = int(devices.size()) - 1;
		if (m_DeviceParams.adapterIndex >= 0)
		{
			if (m_DeviceParams.adapterIndex > lastDevice)
			{
				LUX_CORE_ERROR("The specified Vulkan physical device {} does not exist.", m_DeviceParams.adapterIndex);
				return false;
			}
			firstDevice = m_DeviceParams.adapterIndex;
			lastDevice = m_DeviceParams.adapterIndex;
		}

		// Start building an error message in case we cannot find a device.
		std::stringstream errorStream;
		errorStream << "Cannot find a Vulkan device that supports all the required extensions and properties.";

		// build a list of GPUs
		std::vector<vk::PhysicalDevice> discreteGPUs;
		std::vector<vk::PhysicalDevice> otherGPUs;
		for (int deviceIndex = firstDevice; deviceIndex <= lastDevice; ++deviceIndex)
		{
			vk::PhysicalDevice const& dev = devices[deviceIndex];
			vk::PhysicalDeviceProperties prop = dev.getProperties();

			errorStream << std::endl << prop.deviceName.data() << ":";

			// check that all required device extensions are present
			std::unordered_set<std::string> requiredExtensions = enabledExtensions.device;
			auto deviceExtensions = dev.enumerateDeviceExtensionProperties();
			for (const auto& ext : deviceExtensions)
			{
				requiredExtensions.erase(std::string(ext.extensionName.data()));
			}

			bool deviceIsGood = true;

			if (!requiredExtensions.empty())
			{
				// device is missing one or more required extensions
				for (const auto& ext : requiredExtensions)
				{
					errorStream << std::endl << "  - missing " << ext;
				}
				deviceIsGood = false;
			}

			auto deviceFeatures = dev.getFeatures();
			if (!deviceFeatures.samplerAnisotropy)
			{
				// device is a toaster oven
				errorStream << std::endl << "  - does not support samplerAnisotropy";
				deviceIsGood = false;
			}
			if (!deviceFeatures.textureCompressionBC)
			{
				errorStream << std::endl << "  - does not support textureCompressionBC";
				deviceIsGood = false;
			}

			if (!FindQueueFamilies(dev))
			{
				// device doesn't have all the queue families we need
				errorStream << std::endl << "  - does not support the necessary queue types";
				deviceIsGood = false;
			}
		

			if (!deviceIsGood)
				continue;

			if (prop.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
			{
				discreteGPUs.push_back(dev);
			}
			else
			{
				otherGPUs.push_back(dev);
			}
		}

		// pick the first discrete GPU if it exists, otherwise the first integrated GPU
		if (!discreteGPUs.empty())
		{
			m_VulkanPhysicalDevice = discreteGPUs[0];
			return true;
		}

		if (!otherGPUs.empty())
		{
			m_VulkanPhysicalDevice = otherGPUs[0];
			return true;
		}

		LUX_CORE_ERROR("{}", errorStream.str().c_str());

		return false;
	}

	bool VulkanDevice::FindQueueFamilies(vk::PhysicalDevice physicalDevice)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		auto props = physicalDevice.getQueueFamilyProperties();

		for (int i = 0; i < int(props.size()); i++)
		{
			const auto& queueFamily = props[i];

			// The swapchain presents from the graphics queue (RHISwapChain), so with a window that
			// family must be able to present.
			if (m_QueueFamilyIndices.Graphics == -1)
			{
				if (queueFamily.queueCount > 0 &&
					(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) &&
					(m_DeviceParams.headlessDevice || glfwGetPhysicalDevicePresentationSupport(m_VulkanInstance, physicalDevice, i)))
				{
					m_QueueFamilyIndices.Graphics = i;
				}
			}

			if (m_QueueFamilyIndices.Compute == -1)
			{
				if (queueFamily.queueCount > 0 &&
					(queueFamily.queueFlags & vk::QueueFlagBits::eCompute) &&
					!(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
				{
					m_QueueFamilyIndices.Compute = i;
				}
			}

			if (m_QueueFamilyIndices.Transfer == -1)
			{
				if (queueFamily.queueCount > 0 &&
					(queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) &&
					!(queueFamily.queueFlags & vk::QueueFlagBits::eCompute) &&
					!(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
				{
					m_QueueFamilyIndices.Transfer = i;
				}
			}
		}

		if (m_QueueFamilyIndices.Graphics == -1 ||
			(m_QueueFamilyIndices.Compute == -1 && m_DeviceParams.enableComputeQueue))
		{
			return false;
		}

		// A dedicated transfer family is OPTIONAL: many GPUs expose one, but device
		// selection must not fail when they don't. When absent, async asset uploads
		// fall back to the graphics queue (see Renderer::FlushResourceUploads).

		return true;
	}

	bool VulkanDevice::createDevice()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// figure out which optional extensions are supported
		auto deviceExtensions = m_VulkanPhysicalDevice.enumerateDeviceExtensionProperties();
		for (const auto& ext : deviceExtensions)
		{
			const std::string name = ext.extensionName;
			if (optionalExtensions.device.find(name) != optionalExtensions.device.end())
			{
				if (name == VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME && m_DeviceParams.headlessDevice)
					continue;

				enabledExtensions.device.insert(name);
			}

			if (m_DeviceParams.enableRayTracingExtensions && m_RayTracingExtensions.find(name) != m_RayTracingExtensions.end())
			{
				enabledExtensions.device.insert(name);
			}
		}

		if (!m_DeviceParams.headlessDevice)
		{
			enabledExtensions.device.insert(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}

		// Aftermath crash dumps must be enabled before the device exists. Its extensions then reach
		// NRI through enabledExtensions like any other.
		const bool aftermathSupported = std::all_of(std::begin(Aftermath::k_DeviceExtensions), std::end(Aftermath::k_DeviceExtensions), [&](const char* name)
		{
			return std::any_of(deviceExtensions.begin(), deviceExtensions.end(), [&](const vk::ExtensionProperties& ext) { return std::string_view(ext.extensionName.data()) == name; });
		});
		if (LUX_HAS_AFTERMATH && !aftermathSupported)
			LUX_CORE_INFO_TAG("Renderer", "Aftermath unavailable (extensions not supported)");
		const bool aftermathEnabled = aftermathSupported && Aftermath::Initialize();
		if (aftermathEnabled)
			enabledExtensions.device.insert(std::begin(Aftermath::k_DeviceExtensions), std::end(Aftermath::k_DeviceExtensions));

		// NRI treats VK_KHR_ray_tracing_pipeline and VK_KHR_ray_tracing_maintenance1 as a pair: with
		// the pipeline extension it loads vkCmdTraceRaysIndirect2KHR, and it cannot wrap the device
		// when that is missing. Lux uses no ray-tracing pipelines, so a device without maintenance1
		// goes without the pipeline extension rather than failing to start.
		if (enabledExtensions.device.count(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) && !enabledExtensions.device.count(VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME))
		{
			enabledExtensions.device.erase(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
			LUX_CORE_INFO_TAG("Renderer", "Ray-tracing pipelines off: the device has no {}, which NRI needs with them", VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME);
		}

		const vk::PhysicalDeviceProperties physicalDeviceProperties = m_VulkanPhysicalDevice.getProperties();
		m_RendererString = std::string(physicalDeviceProperties.deviceName.data());
		m_DeviceAPIVersion = std::min(m_InstanceAPIVersion, physicalDeviceProperties.apiVersion);
		const bool vulkan14 = m_DeviceAPIVersion >= VK_API_VERSION_1_4;

		bool accelStructSupported = false;
		bool rayPipelineSupported = false;
		bool rayTracingMaintenance1Supported = false;
		bool rayQuerySupported = false;
		bool meshletsSupported = false;
		bool vrsSupported = false;
		bool synchronization2Supported = false;
		bool maintenance4Supported = false;
		bool maintenance5Supported = false;
		bool maintenance6Supported = false;

		LUX_CORE_INFO("Enabled Vulkan device extensions:");
		for (const auto& ext : enabledExtensions.device)
		{
			LUX_CORE_INFO("    {}", ext.c_str());

			if (ext == VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
				accelStructSupported = true;
			else if (ext == VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
				rayPipelineSupported = true;
			else if (ext == VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME)
				rayTracingMaintenance1Supported = true;
			else if (ext == VK_KHR_RAY_QUERY_EXTENSION_NAME)
				rayQuerySupported = true;
			else if (ext == VK_EXT_MESH_SHADER_EXTENSION_NAME)
				meshletsSupported = true;
			else if (ext == VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)
				vrsSupported = true;
			else if (ext == VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)
				synchronization2Supported = true;
			else if (ext == VK_KHR_MAINTENANCE_4_EXTENSION_NAME)
				maintenance4Supported = true;
			else if (ext == VK_KHR_MAINTENANCE_5_EXTENSION_NAME)
				maintenance5Supported = true;
			else if (ext == VK_KHR_MAINTENANCE_6_EXTENSION_NAME)
				maintenance6Supported = true;
		}

#define APPEND_EXTENSION(condition, desc) if (condition) { (desc).pNext = pNext; pNext = &(desc); }  // NOLINT(cppcoreguidelines-macro-usage)
		void* pNext = nullptr;

		vk::PhysicalDeviceFeatures2 physicalDeviceFeatures2;
		// Core feature support. RHIDevice wraps this device in NRI, and NRI reads feature support
		// from the physical device rather than from what was enabled, so the core features it uses
		// on its own (sync2, maintenance5/6, push descriptors) are enabled here whenever supported.
		auto supported11 = vk::PhysicalDeviceVulkan11Features();
		auto supported12 = vk::PhysicalDeviceVulkan12Features();
		auto supported13 = vk::PhysicalDeviceVulkan13Features();
		auto supported14 = vk::PhysicalDeviceVulkan14Features();
		// Determine support for maintenance4/5/6 when they are extensions
		auto maintenance4Features = vk::PhysicalDeviceMaintenance4Features();
		auto maintenance5Features = vk::PhysicalDeviceMaintenance5Features();
		auto maintenance6Features = vk::PhysicalDeviceMaintenance6Features();
		// NRI's pipeline-statistics query pools count task and mesh invocations when the device can.
		auto supportedMeshShader = vk::PhysicalDeviceMeshShaderFeaturesEXT();
		auto supportedRayTracingMaintenance1 = vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR();

		// Put the user-provided extension structure at the end of the chain
		pNext = m_DeviceParams.physicalDeviceFeatures2Extensions;
		APPEND_EXTENSION(true, supported11);
		APPEND_EXTENSION(true, supported12);
		APPEND_EXTENSION(m_DeviceAPIVersion >= VK_API_VERSION_1_3, supported13);
		APPEND_EXTENSION(vulkan14, supported14);
		APPEND_EXTENSION(maintenance4Supported, maintenance4Features);
		APPEND_EXTENSION(!vulkan14 && maintenance5Supported, maintenance5Features);
		APPEND_EXTENSION(!vulkan14 && maintenance6Supported, maintenance6Features);
		APPEND_EXTENSION(meshletsSupported, supportedMeshShader);
		APPEND_EXTENSION(rayTracingMaintenance1Supported, supportedRayTracingMaintenance1);

		physicalDeviceFeatures2.pNext = pNext;
		m_VulkanPhysicalDevice.getFeatures2(&physicalDeviceFeatures2);

		std::unordered_set<int> uniqueQueueFamilies = {
			m_QueueFamilyIndices.Graphics };

		if (m_DeviceParams.enableComputeQueue)
			uniqueQueueFamilies.insert(m_QueueFamilyIndices.Compute);

		// The transfer family is optional — only request it when the GPU actually
		// exposed a dedicated one (FindQueueFamilies leaves it at -1 otherwise).
		if (m_DeviceParams.enableCopyQueue && m_QueueFamilyIndices.Transfer != -1)
			uniqueQueueFamilies.insert(m_QueueFamilyIndices.Transfer);

		float priority = 1.f;
		std::vector<vk::DeviceQueueCreateInfo> queueDesc;
		queueDesc.reserve(uniqueQueueFamilies.size());
		for (int queueFamily : uniqueQueueFamilies)
		{
			queueDesc.push_back(vk::DeviceQueueCreateInfo()
									.setQueueFamilyIndex(queueFamily)
									.setQueueCount(1)
									.setPQueuePriorities(&priority));
		}

		auto accelStructFeatures = vk::PhysicalDeviceAccelerationStructureFeaturesKHR()
			.setAccelerationStructure(true);
		auto rayPipelineFeatures = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR()
			.setRayTracingPipeline(true)
			.setRayTraversalPrimitiveCulling(true);
		auto rayQueryFeatures = vk::PhysicalDeviceRayQueryFeaturesKHR()
			.setRayQuery(true);
		auto rayTracingMaintenance1Features = vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR()
			.setRayTracingMaintenance1(supportedRayTracingMaintenance1.rayTracingMaintenance1)
			.setRayTracingPipelineTraceRaysIndirect2(rayPipelineSupported && supportedRayTracingMaintenance1.rayTracingPipelineTraceRaysIndirect2);
		// Mesh shaders are VK_EXT_mesh_shader (NRI's CmdDrawMeshTasks is vkCmdDrawMeshTasksEXT).
		auto meshletFeatures = vk::PhysicalDeviceMeshShaderFeaturesEXT()
			.setTaskShader(true)
			.setMeshShader(true)
			.setMeshShaderQueries(supportedMeshShader.meshShaderQueries);
		auto vrsFeatures = vk::PhysicalDeviceFragmentShadingRateFeaturesKHR()
			.setPipelineFragmentShadingRate(true)
			.setPrimitiveFragmentShadingRate(true)
			.setAttachmentFragmentShadingRate(true);
		auto vulkan13features = vk::PhysicalDeviceVulkan13Features()
			.setSynchronization2(synchronization2Supported || supported13.synchronization2)
			.setMaintenance4(maintenance4Features.maintenance4)
			.setDynamicRendering(true);
		auto vulkan14features = vk::PhysicalDeviceVulkan14Features()
			.setMaintenance5(supported14.maintenance5)
			.setMaintenance6(supported14.maintenance6)
			.setPushDescriptor(supported14.pushDescriptor);
		auto vulkan11features = vk::PhysicalDeviceVulkan11Features()
			.setShaderDrawParameters(supported11.shaderDrawParameters);

		pNext = nullptr;
		APPEND_EXTENSION(accelStructSupported, accelStructFeatures)
			APPEND_EXTENSION(rayPipelineSupported, rayPipelineFeatures)
			APPEND_EXTENSION(rayTracingMaintenance1Supported, rayTracingMaintenance1Features)
			APPEND_EXTENSION(rayQuerySupported, rayQueryFeatures)
			APPEND_EXTENSION(meshletsSupported, meshletFeatures)
			APPEND_EXTENSION(vrsSupported, vrsFeatures)
			APPEND_EXTENSION(physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_3, vulkan13features)
			APPEND_EXTENSION(physicalDeviceProperties.apiVersion < VK_API_VERSION_1_3 && maintenance4Supported, maintenance4Features)
			APPEND_EXTENSION(vulkan14, vulkan14features)
			APPEND_EXTENSION(!vulkan14 && maintenance5Supported, maintenance5Features)
			APPEND_EXTENSION(!vulkan14 && maintenance6Supported, maintenance6Features)
			APPEND_EXTENSION(true, vulkan11features);
#undef APPEND_EXTENSION

		auto deviceFeatures = vk::PhysicalDeviceFeatures()
			.setShaderImageGatherExtended(true)
			.setSamplerAnisotropy(true)
			.setTessellationShader(true)
			.setTextureCompressionBC(true)
			.setGeometryShader(true)
			.setImageCubeArray(true)
			.setDualSrcBlend(true)
			.setIndependentBlend(true)
			.setFillModeNonSolid(true)
			.setWideLines(true)
			.setPipelineStatisticsQuery(true);

		auto vulkan12features = vk::PhysicalDeviceVulkan12Features()
			.setDescriptorIndexing(true)
			.setRuntimeDescriptorArray(true)
			.setDescriptorBindingPartiallyBound(true)
			.setDescriptorBindingVariableDescriptorCount(true)
			.setTimelineSemaphore(true)
			.setShaderSampledImageArrayNonUniformIndexing(true)
			.setBufferDeviceAddress(supported12.bufferDeviceAddress)
			.setSamplerFilterMinmax(supported12.samplerFilterMinmax)
			.setHostQueryReset(supported12.hostQueryReset)
			.setDescriptorBindingUpdateUnusedWhilePending(supported12.descriptorBindingUpdateUnusedWhilePending)
			.setDescriptorBindingSampledImageUpdateAfterBind(supported12.descriptorBindingSampledImageUpdateAfterBind)
			.setDescriptorBindingStorageImageUpdateAfterBind(supported12.descriptorBindingStorageImageUpdateAfterBind)
			.setDescriptorBindingStorageBufferUpdateAfterBind(supported12.descriptorBindingStorageBufferUpdateAfterBind)
			.setDescriptorBindingUniformBufferUpdateAfterBind(supported12.descriptorBindingUniformBufferUpdateAfterBind)
			.setDescriptorBindingUniformTexelBufferUpdateAfterBind(supported12.descriptorBindingUniformTexelBufferUpdateAfterBind)
			.setDescriptorBindingStorageTexelBufferUpdateAfterBind(supported12.descriptorBindingStorageTexelBufferUpdateAfterBind)
			.setPNext(pNext);

		auto layerVec = stringSetToVector(enabledExtensions.layers);
		auto extVec = stringSetToVector(enabledExtensions.device);

		auto deviceDesc = vk::DeviceCreateInfo()
			.setPQueueCreateInfos(queueDesc.data())
			.setQueueCreateInfoCount(uint32_t(queueDesc.size()))
			.setPEnabledFeatures(&deviceFeatures)
			.setEnabledExtensionCount(uint32_t(extVec.size()))
			.setPpEnabledExtensionNames(extVec.data())
			.setEnabledLayerCount(uint32_t(layerVec.size()))
			.setPpEnabledLayerNames(layerVec.data())
			.setPNext(&vulkan12features);

		auto aftermathInfo = vk::DeviceDiagnosticsConfigCreateInfoNV()
			.setFlags(vk::DeviceDiagnosticsConfigFlagsNV(Aftermath::GetDeviceDiagnosticsFlags()))
			.setPNext(&vulkan12features);
		if (aftermathEnabled)
			deviceDesc.setPNext(&aftermathInfo);

		if (m_DeviceParams.deviceCreateInfoCallback)
			m_DeviceParams.deviceCreateInfoCallback(deviceDesc);

		const vk::Result res = m_VulkanPhysicalDevice.createDevice(&deviceDesc, nullptr, &m_VulkanDevice);
		if (res != vk::Result::eSuccess)
		{
			LUX_CORE_ERROR("Failed to create a Vulkan physical device, error code = {}", vk::to_string(res));
			return false;
		}

		m_VulkanDevice.getQueue(m_QueueFamilyIndices.Graphics, 0, &m_GraphicsQueue);
		if (m_DeviceParams.enableComputeQueue)
			m_VulkanDevice.getQueue(m_QueueFamilyIndices.Compute, 0, &m_ComputeQueue);
		m_TransferQueueAvailable = m_DeviceParams.enableCopyQueue && m_QueueFamilyIndices.Transfer != -1;
		if (m_TransferQueueAvailable)
			m_VulkanDevice.getQueue(m_QueueFamilyIndices.Transfer, 0, &m_TransferQueue);

		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_VulkanDevice);
		Aftermath::OnDeviceCreated(static_cast<VkDevice>(m_VulkanDevice), VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr);

		LUX_CORE_INFO("Created Vulkan device: {}", m_RendererString.c_str());

		return true;
	}

#define CHECK(a) if (!(a)) { return false; }

	bool VulkanDevice::CreateInstanceInternal()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_DeviceParams.enableDebugRuntime)
		{
			enabledExtensions.instance.insert("VK_EXT_debug_report");
			optionalExtensions.layers.insert("VK_LAYER_KHRONOS_validation");
		}

		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
			m_dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

		return createInstance();
	}

	bool VulkanDevice::CreateDevice()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_DeviceParams.enableDebugRuntime)
		{
			installDebugCallback();
		}

		// add device extensions requested by the user
		for (const std::string& name : m_DeviceParams.requiredVulkanDeviceExtensions)
		{
			enabledExtensions.device.insert(name);
		}
		for (const std::string& name : m_DeviceParams.optionalVulkanDeviceExtensions)
		{
			optionalExtensions.device.insert(name);
		}

		CHECK(pickPhysicalDevice())
		CHECK(FindQueueFamilies(m_VulkanPhysicalDevice))
		CHECK(createDevice())

		if (m_TransferQueueAvailable)
			LUX_CORE_INFO_TAG("Renderer", "Dedicated transfer queue enabled (family {}) for async asset uploads.", m_QueueFamilyIndices.Transfer);
		else if (m_DeviceParams.enableCopyQueue)
			LUX_CORE_INFO_TAG("Renderer", "No dedicated transfer queue on this GPU; asset uploads use the graphics queue.");

#if LUX_ENABLE_PROFILING
		m_GPUProfiler.Init(m_VulkanPhysicalDevice, m_VulkanDevice, m_GraphicsQueue, uint32_t(m_QueueFamilyIndices.Graphics));
#endif

		return true;
	}

#undef CHECK

	void VulkanDevice::DestroyDevice()
	{
		LUX_PROFILE_FUNCTION_AUTO;

		m_RendererString.clear();

#if LUX_ENABLE_PROFILING
		// Must precede vkDestroyDevice: the context owns a query pool on this device.
		m_GPUProfiler.Shutdown();
#endif

		if (m_VulkanDevice)
		{
			m_VulkanDevice.destroy();
			m_VulkanDevice = nullptr;
		}	

		Aftermath::Shutdown();

		if (m_DebugReportCallback)
		{
			m_VulkanInstance.destroyDebugReportCallbackEXT(m_DebugReportCallback);
		}

		if (m_VulkanInstance)
		{
			m_VulkanInstance.destroy();
			m_VulkanInstance = nullptr;
		}
	}

	void VulkanDevice::ReportDeviceLost(std::string_view where)
	{
		// Several threads can see the loss; the first reports, the others wait for the process to end.
		static std::atomic_flag s_Reported;
		if (s_Reported.test_and_set())
		{
			while (true)
				std::this_thread::sleep_for(std::chrono::seconds(1));
		}

		LUX_CORE_FATAL_TAG("Renderer", "GPU device lost ({})", where);
		if (Aftermath::IsEnabled())
		{
			const std::filesystem::path dumpPath = Aftermath::WaitForCrashDump();
			if (dumpPath.empty())
				LUX_CORE_FATAL_TAG("Renderer", "Aftermath wrote no GPU crash dump");
			else
				LUX_CORE_FATAL_TAG("Renderer", "Aftermath GPU crash dump: {}", dumpPath.string());
		}

		Log::GetCoreLogger()->flush();
		std::abort();
	}

	bool VulkanDevice::CreateDevice(const DeviceCreationParameters& params)
	{
		m_DeviceParams = params;
		return CreateInstanceInternal() && CreateDevice();
	}

	void VulkanDevice::Shutdown()
	{
		DestroyDevice();
	}

}
