// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RayReconstruction.h"

#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Utilities/FileSystem.h"

#if defined(LUX_HAS_DLSS_RR) && !defined(LUX_DIST)
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_helpers_dlssd_vk.h>
#endif

namespace Lux {

	namespace {

		bool s_DeviceSupportsReconstruction = false;

#if defined(LUX_HAS_DLSS_RR) && !defined(LUX_DIST)
		constexpr const char* k_ProjectId = "024d5f23-1124-4193-a1d8-3157aa1e81a0";
		constexpr const char* k_EngineVersion = "2026.1";

		bool PackageHasSdk()
		{
			const auto folder = FileSystem::GetExecutablePath().parent_path();
			std::error_code ec;
			return std::filesystem::is_regular_file(folder / "nvngx_dlssd.dll", ec);
		}

		// The Vulkan device is shared across views. Each view owns a feature and parameters.
		// The last retired feature shuts NGX down after the frame fence.
		uint32_t s_NgxReferences = 0;

		void NVSDK_CONV LogNgx(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature)
		{
			LUX_CORE_INFO_TAG("Renderer", "[NGX] {}", message);
		}

		struct NativeReconstruction : RefCounted
		{
			VkDevice Device = VK_NULL_HANDLE;
			NVSDK_NGX_Parameter* Parameters = nullptr;
			NVSDK_NGX_Handle* Feature = nullptr;

			~NativeReconstruction() override
			{
				if (Feature)
					NVSDK_NGX_VULKAN_ReleaseFeature(Feature);
				if (Parameters)
					NVSDK_NGX_VULKAN_DestroyParameters(Parameters);
				if (Device && --s_NgxReferences == 0)
					NVSDK_NGX_VULKAN_Shutdown1(Device);
			}

			NVSDK_NGX_Result Initialize(VkCommandBuffer commands, uint32_t width, uint32_t height)
			{
				const auto& api = RHIDevice::API();
				auto& rhi = RHIDevice::Get();
				const auto device = static_cast<VkDevice>(api.GetDeviceNativeObject(&rhi));
				const auto instance = static_cast<VkInstance>(api.GetInstanceVK(rhi));
				const auto physical = static_cast<VkPhysicalDevice>(api.GetPhysicalDeviceVK(rhi));
				const auto getInstance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(api.GetInstanceProcAddrVK(rhi));
				const auto getDevice = reinterpret_cast<PFN_vkGetDeviceProcAddr>(api.GetDeviceProcAddrVK(rhi));
				if (s_NgxReferences == 0)
				{
					const auto path = FileSystem::GetPersistentStoragePath().wstring();
					NVSDK_NGX_FeatureCommonInfo common{};
					common.LoggingInfo.LoggingCallback = LogNgx;
					common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
					common.LoggingInfo.DisableOtherLoggingSinks = true;
					const auto result = NVSDK_NGX_VULKAN_Init_with_ProjectID(k_ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
						k_EngineVersion, path.c_str(), instance, physical, device, getInstance, getDevice, &common);
					if (!NVSDK_NGX_SUCCEED(result))
					{
						LUX_CORE_ERROR_TAG("Renderer", "[NGX] Vulkan initialization failed with 0x{:X}", static_cast<unsigned>(result));
						return result;
					}
				}
				Device = device;
				s_NgxReferences++;
				const auto result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&Parameters);
				if (!NVSDK_NGX_SUCCEED(result))
				{
					LUX_CORE_ERROR_TAG("Renderer", "[NGX] Getting capabilities failed with 0x{:X}", static_cast<unsigned>(result));
					return result;
				}

				NVSDK_NGX_DLSSD_Create_Params create{};
				create.InWidth = create.InTargetWidth = width;
				create.InHeight = create.InTargetHeight = height;
				create.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
				create.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Packed;
				create.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_Linear;
				create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
					NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
				NVSDK_NGX_Parameter_SetUI(Parameters, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA,
					NVSDK_NGX_RayReconstruction_Hint_Render_Preset_E);
				return NGX_VULKAN_CREATE_DLSSD_EXT1(device, commands, 1, 1, &Feature, Parameters, &create);
			}
		};

		NVSDK_NGX_Resource_VK DescribeImage(RenderCommandBuffer& cb, const Ref<Image2D>& image, VkFormat format, bool writable = false)
		{
			const auto& api = RHIDevice::API();
			const auto type = writable ? nri::TextureView::STORAGE_TEXTURE : nri::TextureView::TEXTURE;
			nri::Texture* texture = image->GetHandle();
			const nri::Descriptor* descriptor = GetNRITextureView(texture, { type });
			if (!descriptor)
				return {};
			cb.RT_RequireTextureState(image->GetImageInfo().Tracked, {}, writable ? ResourceState::UnorderedAccess : ResourceState::ShaderResource);
			const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			return NVSDK_NGX_Create_ImageView_Resource_VK(
				reinterpret_cast<VkImageView>(api.GetDescriptorNativeObject(descriptor)),
				reinterpret_cast<VkImage>(api.GetTextureNativeObject(texture)),
				range, format, image->GetWidth(), image->GetHeight(), writable);
		}
#endif
	}

	std::vector<std::string> RayReconstruction::RequiredInstanceExtensions()
	{
#if defined(LUX_HAS_DLSS_RR) && !defined(LUX_DIST)
		if (PackageHasSdk())
			return { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
#endif
		return {};
	}

	void RayReconstruction::EnableDeviceExtensions(vk::PhysicalDevice physicalDevice,
		const std::vector<vk::ExtensionProperties>& available, const std::unordered_set<std::string>& instanceExtensions,
		std::unordered_set<std::string>& deviceExtensions)
	{
#if defined(LUX_HAS_DLSS_RR) && !defined(LUX_DIST)
		s_DeviceSupportsReconstruction = false;
		if (!PackageHasSdk() || physicalDevice.getProperties().vendorID != 0x10deu ||
			!instanceExtensions.contains(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
			return;
		// NGX 310.9.1's Vulkan Ray Reconstruction requirements, verified with the
		// SDK query. The query itself initializes driver support, so reserve it
		// for the developer probe and initialize NGX only on first use.
		constexpr std::array required {
			VK_NVX_BINARY_IMPORT_EXTENSION_NAME,
			VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME,
			VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
			VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		};
		for (const std::string_view name : required)
		{
			if (std::none_of(available.begin(), available.end(), [&](const vk::ExtensionProperties& extension)
				{ return std::string_view(extension.extensionName.data()) == name; }))
				return;
		}
		deviceExtensions.insert(required.begin(), required.end());
		s_DeviceSupportsReconstruction = true;
#endif
	}

	bool RayReconstruction::IsAvailable()
	{
		return s_DeviceSupportsReconstruction && RHIDevice::SupportsRayQuery();
	}

	RayReconstruction::~RayReconstruction()
	{
		if (m_Native)
			Renderer::SubmitResourceFree([native = std::move(m_Native)] {});
	}

	bool RayReconstruction::RT_Evaluate(RenderCommandBuffer& cb, const Ref<Image2D>& input, const Guides& guides,
		const Ref<Image2D>& output, glm::vec2 jitter, const glm::mat4& view, const glm::mat4& projection, bool reset)
	{
#if defined(LUX_HAS_DLSS_RR) && !defined(LUX_DIST)
		if (m_Failed)
			return false;
		auto color = DescribeImage(cb, input, VK_FORMAT_R16G16B16A16_SFLOAT);
		auto motion = DescribeImage(cb, guides.Motion, VK_FORMAT_R16G16_SFLOAT);
		auto depth = DescribeImage(cb, guides.ViewDepth, VK_FORMAT_R32_SFLOAT);
		auto normals = DescribeImage(cb, guides.Normals, VK_FORMAT_R16G16B16A16_SFLOAT);
		auto diffuse = DescribeImage(cb, guides.DiffuseAlbedo, VK_FORMAT_R16G16B16A16_SFLOAT);
		auto specular = DescribeImage(cb, guides.SpecularAlbedo, VK_FORMAT_R16G16B16A16_SFLOAT);
		auto hitDistance = DescribeImage(cb, guides.SpecularHitDistance, VK_FORMAT_R32_SFLOAT);
		auto reconstructed = DescribeImage(cb, output, VK_FORMAT_R16G16B16A16_SFLOAT, true);

		if (auto* nribuffer = cb.RT_BeginNRISegment())
		{
			const auto commands = static_cast<VkCommandBuffer>(RHIDevice::API().GetCommandBufferNativeObject(nribuffer));
			const uint32_t width = input->GetWidth(), height = input->GetHeight();
			auto result = NVSDK_NGX_Result_Success;
			if (!m_Native)
			{
				auto native = Ref<NativeReconstruction>::Create();
				result = native->Initialize(commands, width, height);
				if (NVSDK_NGX_SUCCEED(result))
				{
					LUX_CORE_INFO_TAG("Renderer", "[PathTracer] DLSS Ray Reconstruction active at {}x{}", width, height);
					m_Native = native;
					reset = true;
				}
			}

			if (m_Native)
			{
				NVSDK_NGX_VK_DLSSD_Eval_Params evaluate{};
				evaluate.pInColor = &color;
				evaluate.pInMotionVectors = &motion;
				evaluate.pInDepth = &depth;
				evaluate.pInNormals = &normals;
				evaluate.pInRoughness = &normals;
				evaluate.pInDiffuseAlbedo = &diffuse;
				evaluate.pInSpecularAlbedo = &specular;
				evaluate.pInSpecularHitDistance = &hitDistance;
				evaluate.pInOutput = &reconstructed;
				evaluate.InRenderSubrectDimensions = { width, height };
				evaluate.InJitterOffsetX = jitter.x;
				evaluate.InJitterOffsetY = jitter.y;
				evaluate.InMVScaleX = static_cast<float>(width);
				evaluate.InMVScaleY = static_cast<float>(height);
				evaluate.InReset = reset ? 1 : 0;
				evaluate.InPreExposure = 1.0f;
				evaluate.InExposureScale = 1.0f;
				auto viewForSdk = view, projectionForSdk = projection;
				evaluate.pInWorldToViewMatrix = &viewForSdk[0][0];
				evaluate.pInViewToClipMatrix = &projectionForSdk[0][0];
				const auto native = m_Native.As<NativeReconstruction>();
				result = NGX_VULKAN_EVALUATE_DLSSD_EXT(commands, native->Feature, native->Parameters, &evaluate);
			}
			cb.RT_EndNRISegment();
			if (NVSDK_NGX_SUCCEED(result))
				return true;
			LUX_CORE_ERROR_TAG("Renderer", "[PathTracer] DLSS Ray Reconstruction failed with 0x{:X}; using accumulation",
				static_cast<unsigned>(result));
			m_Failed = true;
		}
#endif
		return false;
	}

}
