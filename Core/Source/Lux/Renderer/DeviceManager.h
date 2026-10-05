// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#if LUX_HAS_DX11 || LUX_HAS_DX12
#include <DXGI.h>
#endif

#if LUX_HAS_DX11
#include <d3d11.h>
#endif

#if LUX_HAS_DX12
#include <d3d12.h>
#endif

#if LUX_HAS_VULKAN
#include <nvrhi/vulkan.h>
#endif

#define GLFW_INCLUDE_NONE // Do not include any OpenGL headers
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif // _WIN32


#include <GLFW/glfw3native.h>
#include <nvrhi/nvrhi.h>

#include <functional>

namespace Lux
{
    struct DefaultMessageCallback : public nvrhi::IMessageCallback
    {
        static DefaultMessageCallback& GetInstance();

        void message(nvrhi::MessageSeverity severity, const char* messageText) override;
    };

    struct InstanceParameters
    {
        bool enableDebugRuntime = false;
        bool headlessDevice = false;

#if LUX_HAS_VULKAN
        std::vector<std::string> requiredVulkanInstanceExtensions;
        std::vector<std::string> requiredVulkanLayers;
        std::vector<std::string> optionalVulkanInstanceExtensions;
        std::vector<std::string> optionalVulkanLayers;
#endif
    };

    struct DeviceCreationParameters : public InstanceParameters
    {
        bool Decorated = true;
        int windowPosX = -1;            // -1 means use default placement
        int windowPosY = -1;
        uint32_t backBufferWidth = 1280;
        uint32_t backBufferHeight = 720;
        uint32_t refreshRate = 0;
        uint32_t swapChainBufferCount = 3;
        nvrhi::Format swapChainFormat = nvrhi::Format::RGBA8_UNORM;
        uint32_t swapChainSampleCount = 1;
        uint32_t maxFramesInFlight = 2;
        bool enableNvrhiValidationLayer = false;
        bool vsyncEnabled = false;
        // Only consulted when vsyncEnabled is false: picks IMMEDIATE over MAILBOX.
        bool preferImmediatePresentMode = false;
        bool enableRayTracingExtensions = false; // for vulkan
        bool enableComputeQueue = false;
        bool enableCopyQueue = false;

        // Index of the adapter (DX11, DX12) or physical device (Vk) on which to initialize the device.
        // Negative values mean automatic detection.
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

#if LUX_HAS_DX11 || LUX_HAS_DX12
        DXGI_USAGE swapChainUsage = DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT;
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
#endif

#if LUX_HAS_VULKAN
        std::vector<std::string> requiredVulkanDeviceExtensions;
        std::vector<std::string> optionalVulkanDeviceExtensions;
        std::vector<size_t> ignoredVulkanValidationMessageLocations;
        std::function<void(VkDeviceCreateInfo&)> deviceCreateInfoCallback;

        // This pointer specifies an optional structure to be put at the end of the chain for 'vkGetPhysicalDeviceFeatures2' call.
        // The structure may also be a chain, and must be alive during the device initialization process.
        // The elements of this structure will be populated before 'deviceCreateInfoCallback' is called,
        // thereby allowing applications to determine if certain features may be enabled on the device.
        void* physicalDeviceFeatures2Extensions = nullptr;
#endif
    };

    class DeviceManager
    {
    public:
        static DeviceManager* Create(nvrhi::GraphicsAPI api, GLFWwindow* windowHandle);

        bool CreateDevice(const DeviceCreationParameters& params, const char* windowTitle);
        virtual bool InitSurfaceCapabilities(uint64_t surfaceHandle) = 0;

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

    protected:
        DeviceCreationParameters m_DeviceParams;
		GLFWwindow* m_WindowHandle = nullptr;
        // current DPI scale info (updated when window moves)
        float m_DPIScaleFactorX = 1.f;
        float m_DPIScaleFactorY = 1.f;

        DeviceManager() = default;

        // device-specific methods
        virtual bool CreateInstanceInternal() = 0;
        virtual bool CreateDevice() = 0;
        virtual void DestroyDevice() = 0;


    public:
        [[nodiscard]] virtual nvrhi::IDevice *GetDevice() const = 0;
        [[nodiscard]] virtual const char *GetRendererString() const = 0;
        [[nodiscard]] virtual nvrhi::GraphicsAPI GetGraphicsAPI() const = 0;

        // True when the device was created with a dedicated transfer (copy) queue.
        // False when enableCopyQueue was off or the GPU exposed no dedicated
        // transfer family; async asset uploads fall back to the graphics queue.
        [[nodiscard]] virtual bool IsTransferQueueAvailable() const { return false; }
        // Updates the present-mode source for the swapchain. The swapchain reads
        // m_DeviceParams.vsyncEnabled when it is (re)created, so the caller must
        // recreate the swapchain afterwards for this to take effect (see Window::SetVSync).
        virtual void SetVsyncEnabled(bool enabled) { m_DeviceParams.vsyncEnabled = enabled; }
        // Number of swapchain images requested the next time the swapchain is (re)created.
        // Same contract as SetVsyncEnabled: the caller must recreate the swapchain for it
        // to take effect (see Window::SetSwapChainBufferCount).
        //
        // This is a frame-rate control, not just a memory knob. Under MAILBOX the
        // presentation engine releases images on the compositor's schedule, so the count
        // sets the ceiling at roughly (count - 1) x display refresh under DWM. FIFO is
        // always refresh-locked regardless, and the practical range is small: more images
        // buy throughput at the cost of latency and VRAM.
        [[nodiscard]] uint32_t GetSwapChainBufferCount() const { return m_DeviceParams.swapChainBufferCount; }
        virtual void SetSwapChainBufferCount(uint32_t count) { m_DeviceParams.swapChainBufferCount = count; }

        // Which uncapped present mode to pick when vsync is off. Same recreate contract as
        // the two setters above.
        //
        // MAILBOX does not tear, but a composited (windowed, DWM) surface still hands
        // images back on the compositor's schedule, which is what pins the frame rate to
        // the refresh rate while the window is focused. IMMEDIATE is the only mode the
        // spec guarantees never blocks on vblank, at the cost of tearing. Which one wins
        // in practice is driver- and compositor-dependent, so this is exposed rather than
        // hard-coded.
        [[nodiscard]] bool PrefersImmediatePresentMode() const { return m_DeviceParams.preferImmediatePresentMode; }
        virtual void SetPreferImmediatePresentMode(bool prefer) { m_DeviceParams.preferImmediatePresentMode = prefer; }

        virtual void Shutdown();
        virtual ~DeviceManager() = default;

    private:
        static DeviceManager* CreateD3D11();
        static DeviceManager* CreateD3D12();
        static DeviceManager* CreateVK(GLFWwindow* windowHandle);
    };
}
