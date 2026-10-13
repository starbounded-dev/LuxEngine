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
#include "Lux/Debug/Profiler.h"

#include <functional>
#include <string>
#include <queue>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan.hpp>

#if LUX_ENABLE_PROFILING
// Only pulled in here, not in Profiler.h: TracyVulkan.hpp needs the Vulkan headers, and
// Profiler.h reaches nearly every translation unit through the PCH.
#include <tracy/TracyVulkan.hpp>
#endif

#include <format>

struct GLFWwindow;

namespace Lux {

	struct InstanceParameters
	{
		bool enableDebugRuntime = false;
		bool headlessDevice = false;

		std::vector<std::string> requiredVulkanInstanceExtensions;
		std::vector<std::string> requiredVulkanLayers;
		std::vector<std::string> optionalVulkanInstanceExtensions;
		std::vector<std::string> optionalVulkanLayers;
	};

	struct DeviceCreationParameters : public InstanceParameters
	{
		bool Decorated = true;
		int windowPosX = -1;            // -1 means use default placement
		int windowPosY = -1;
		uint32_t refreshRate = 0;
		uint32_t swapChainBufferCount = 3;
		uint32_t maxFramesInFlight = 2;
		bool vsyncEnabled = false;
		// Only consulted when vsyncEnabled is false: picks IMMEDIATE over MAILBOX.
		bool preferImmediatePresentMode = false;
		bool enableRayTracingExtensions = false;
		bool enableComputeQueue = false;
		bool enableCopyQueue = false;

		// Index of the physical device on which to initialize the device. Negative values mean
		// automatic detection.
		int adapterIndex = -1;

		// set to true to enable DPI scale factors to be computed per monitor
		// this will keep the on-screen window size in pixels constant
		//
		// if set to false, the DPI scale factors will be constant but the system
		// may scale the contents of the window based on DPI
		//
		// note that the backbuffer size is never updated automatically; if the app
		// wishes to scale up rendering based on DPI, then it must set this to true
		// and respond to DPI scale factor changes by resizing the backbuffer explicitly
		bool enablePerMonitorDPI = false;

		std::vector<std::string> requiredVulkanDeviceExtensions;
		std::vector<std::string> optionalVulkanDeviceExtensions;
		std::vector<size_t> ignoredVulkanValidationMessageLocations;
		std::function<void(VkDeviceCreateInfo&)> deviceCreateInfoCallback;

		// This pointer specifies an optional structure to be put at the end of the chain for 'vkGetPhysicalDeviceFeatures2' call.
		// The structure may also be a chain, and must be alive during the device initialization process.
		// The elements of this structure will be populated before 'deviceCreateInfoCallback' is called,
		// thereby allowing applications to determine if certain features may be enabled on the device.
		void* physicalDeviceFeatures2Extensions = nullptr;
	};

	// Brings up the Vulkan instance, device and queues that RHIDevice wraps in NRI, and holds the
	// swapchain settings (DeviceCreationParameters) every RHISwapChain is built with.
	class VulkanDeviceManager
	{
	public:
		struct QueueFamilyIndices
		{
			int32_t Graphics = -1;
			int32_t Compute = -1;
			int32_t Transfer = -1;
		};
	public:
		explicit VulkanDeviceManager(GLFWwindow* windowHandle)
			: m_WindowHandle(windowHandle)
		{
		}

		bool CreateDevice(const DeviceCreationParameters& params);
		void Shutdown();

		const DeviceCreationParameters& GetDeviceParams() const { return m_DeviceParams; }

		// returns the screen coordinate to pixel coordinate scale factor
		void GetDPIScaleInfo(float& x, float& y) const
		{
			x = m_DPIScaleFactorX;
			y = m_DPIScaleFactorY;
		}
		void SetDPIScale(float x, float y)
		{
			m_DPIScaleFactorX = x;
			m_DPIScaleFactorY = y;
		}

		// Swapchain settings, read when a swapchain is (re)created: the caller recreates it for a
		// change to take effect (Window::SetVSync and friends).
		void SetVsyncEnabled(bool enabled) { m_DeviceParams.vsyncEnabled = enabled; }
		// This is a frame-rate control, not just a memory knob. Under MAILBOX the
		// presentation engine releases images on the compositor's schedule, so the count
		// sets the ceiling at roughly (count - 1) x display refresh under DWM. FIFO is
		// always refresh-locked regardless, and the practical range is small: more images
		// buy throughput at the cost of latency and VRAM.
		[[nodiscard]] uint32_t GetSwapChainBufferCount() const { return m_DeviceParams.swapChainBufferCount; }
		void SetSwapChainBufferCount(uint32_t count) { m_DeviceParams.swapChainBufferCount = count; }
		// Which uncapped present mode to pick when vsync is off (Rendering.md § Present mode).
		[[nodiscard]] bool PrefersImmediatePresentMode() const { return m_DeviceParams.preferImmediatePresentMode; }
		void SetPreferImmediatePresentMode(bool prefer) { m_DeviceParams.preferImmediatePresentMode = prefer; }

		const char* GetRendererString() const { return m_RendererString.c_str(); }

		vk::Instance GetVulkanInstance() const { return m_VulkanInstance; }
		vk::PhysicalDevice GetVulkanPhysicalDevice() const { return m_VulkanPhysicalDevice; }
		vk::Device GetVulkanDevice() const { return m_VulkanDevice; }
		const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }
		// The version the device is used at: the lower of the instance and physical-device versions.
		uint32_t GetDeviceAPIVersion() const { return m_DeviceAPIVersion; }
		const std::unordered_set<std::string>& GetEnabledInstanceExtensions() const { return enabledExtensions.instance; }
		const std::unordered_set<std::string>& GetEnabledDeviceExtensions() const { return enabledExtensions.device; }

		[[nodiscard]] bool IsComputeQueueAvailable() const { return m_DeviceParams.enableComputeQueue && m_QueueFamilyIndices.Compute >= 0; }

		// A lost device is fatal: waits for the Aftermath GPU crash dump (when enabled), logs where it
		// went, then ends the process. Any thread; `where` names the call that saw VK_ERROR_DEVICE_LOST.
		[[noreturn]] static void ReportDeviceLost(std::string_view where);
		// True when the device was created with a dedicated transfer (copy) queue. False when
		// enableCopyQueue was off or the GPU has no dedicated transfer family; async asset uploads
		// then fall back to the graphics queue.
		[[nodiscard]] bool IsTransferQueueAvailable() const { return m_TransferQueueAvailable; }

#if LUX_ENABLE_PROFILING
		// Tracy GPU context for the graphics queue. Valid between CreateDevice and
		// DestroyDevice; null if creation failed, which callers must tolerate. Zones are
		// emitted on the render thread by RenderCommandBuffer's timer-query pair.
		[[nodiscard]] TracyVkCtx GetGPUProfilerContext() const { return m_TracyGPUContext; }
#endif
	private:
		bool CreateInstanceInternal();
		bool CreateDevice();
		void DestroyDevice();

		bool createInstance();
		void installDebugCallback();
		bool pickPhysicalDevice();
		bool FindQueueFamilies(vk::PhysicalDevice physicalDevice);
		bool createDevice();

		struct VulkanExtensionSet
		{
			std::unordered_set<std::string> instance;
			std::unordered_set<std::string> layers;
			std::unordered_set<std::string> device;
		};

		// minimal set of required extensions
		VulkanExtensionSet enabledExtensions = {
			// instance
			{
				VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
			},
			// layers
			{ },
			// device
			{
				VK_KHR_MAINTENANCE1_EXTENSION_NAME
			},
		};

		// optional extensions
		VulkanExtensionSet optionalExtensions = {
			// instance
			{
				VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME,
				VK_EXT_DEBUG_UTILS_EXTENSION_NAME
			},
			// layers
			{ },
			// device
			{
				VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
				VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
				VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
				VK_EXT_MESH_SHADER_EXTENSION_NAME,
				VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
				VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
				VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
				VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME,
				// Core in Vulkan 1.4; NRI uses them when the device supports them (see createDevice)
				VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
				VK_KHR_MAINTENANCE_6_EXTENSION_NAME,
				VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
				// NRI's memory budget query and VMA's budget tracking need it enabled
				VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
			},
		};

		std::unordered_set<std::string> m_RayTracingExtensions = {
			VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
			VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
			VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
			VK_KHR_RAY_QUERY_EXTENSION_NAME,
			VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
			// NRI resolves vkCmdTraceRaysIndirect2KHR whenever the pipeline extension is enabled.
			VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME
		};

		std::string m_RendererString;

		vk::Instance m_VulkanInstance;
		uint32_t m_InstanceAPIVersion = 0;
		uint32_t m_DeviceAPIVersion = 0;
		vk::DebugReportCallbackEXT m_DebugReportCallback;

		vk::PhysicalDevice m_VulkanPhysicalDevice;

		QueueFamilyIndices m_QueueFamilyIndices;

		vk::Device m_VulkanDevice;
		vk::Queue m_GraphicsQueue;
		vk::Queue m_ComputeQueue;
		vk::Queue m_TransferQueue;

		DeviceCreationParameters m_DeviceParams;
		GLFWwindow* m_WindowHandle = nullptr;
		// current DPI scale info (updated when window moves)
		float m_DPIScaleFactorX = 1.f;
		float m_DPIScaleFactorY = 1.f;

		// True once the logical device was created with a dedicated transfer queue.
		bool m_TransferQueueAvailable = false;

		vk::detail::DynamicLoader m_dynamicLoader;

#if LUX_ENABLE_PROFILING
		// Owned here because this class owns the physical device, logical device, and
		// graphics queue Tracy binds to, so the context's lifetime must match theirs.
		TracyVkCtx m_TracyGPUContext = nullptr;
		// Tracy drives its init command buffer itself - recording, submitting, and
		// calling vkQueueWaitIdle several times - so it needs a pool of its own.
		vk::CommandPool m_TracyGPUCommandPool;
#endif

	private:
#if LUX_ENABLE_PROFILING
		void CreateGPUProfilerContext();
		void DestroyGPUProfilerContext();
#endif

		static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
			VkDebugReportFlagsEXT flags,
			VkDebugReportObjectTypeEXT objType,
			uint64_t obj,
			size_t location,
			int32_t code,
			const char* layerPrefix,
			const char* msg,
			void* userData)
		{
			const VulkanDeviceManager* manager = (const VulkanDeviceManager*)userData;

			if (manager)
			{
				const auto& ignored = manager->m_DeviceParams.ignoredVulkanValidationMessageLocations;
				const auto found = std::find(ignored.begin(), ignored.end(), location);
				if (found != ignored.end())
					return VK_FALSE;
			}

			const bool error = (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) != 0;
			const bool performance = (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) != 0;
			const char* severity = error ? "error" : "warning";
			const char* type = performance ? "performance validation" : "validation";
			const std::string fullMessage = std::format(
				"Vulkan {0} {1}:\n"
				"Location: 0x{2:x}\n"
				"Code: {3}\n"
				"Layer: {4}\n"
				"{5}",
				type,
				severity,
				location,
				code,
				layerPrefix ? layerPrefix : "",
				msg ? msg : "");

			if (error)
			{
				LUX_CORE_ERROR_TAG("Renderer", "{}", fullMessage);
				if (Log::GetEditorConsoleLogger())
					Log::GetEditorConsoleLogger()->error("{}", fullMessage);
			}
			else
			{
				LUX_CORE_WARN_TAG("Renderer", "{}", fullMessage);
				if (Log::GetEditorConsoleLogger())
					Log::GetEditorConsoleLogger()->warn("{}", fullMessage);
			}

			return VK_FALSE;
		}
	};

}
