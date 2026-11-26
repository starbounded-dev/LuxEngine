// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Renderer/Image.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <unordered_set>
#include <vector>

namespace Lux {

	class RenderCommandBuffer;

	// Opt-in native-resolution path-tracer reconstruction. The SDK is optional in a
	// developer build and is initialized on first use, on the existing Vulkan device.
	class RayReconstruction final : public RefCounted
	{
	public:
		static std::vector<std::string> RequiredInstanceExtensions();
		static void EnableDeviceExtensions(vk::PhysicalDevice physicalDevice,
			const std::vector<vk::ExtensionProperties>& available, const std::unordered_set<std::string>& instanceExtensions,
			std::unordered_set<std::string>& deviceExtensions);
		static bool IsAvailable();

		struct Guides
		{
			Ref<Image2D> Motion, ViewDepth, Normals, DiffuseAlbedo, SpecularAlbedo, SpecularHitDistance;
		};

		~RayReconstruction() override;
		bool RT_Evaluate(RenderCommandBuffer& cb, const Ref<Image2D>& input, const Guides& guides,
			const Ref<Image2D>& output, glm::vec2 jitter, const glm::mat4& view, const glm::mat4& projection, bool reset);

	private:
		Ref<RefCounted> m_Native;
		bool m_Failed = false;
	};

}
