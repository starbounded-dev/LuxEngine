// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"
#include "Lux/Renderer/RHI/DescriptorSetGroup.h"

#include <glm/glm.hpp>

#include <string>

namespace nri {
	struct Descriptor;
}

namespace Lux {

	class Image2D;
	class RenderCommandBuffer;

	// One mip of a storage image (every layer), ready to be cleared by Renderer::ClearImage. NRI
	// clears storage through a view held by a bound descriptor set (CmdClearStorage, after D3D12's
	// UAV clears), so the view and a set holding it are built here, once (NRI migration Phase 13).
	// A clear keeps the texture the image had when it was built, so commands recorded before a
	// resize still clear what they were recorded for: build a new clear when the image is resized,
	// recreated or re-aliased, as for its other views.
	//
	// Threading: created on any thread once the image is invalidated; recorded on the render thread.
	class StorageImageClear : public RefCounted
	{
	public:
		// Renderer::Init and Renderer::Shutdown: the pipeline layout every clear binds (one set
		// holding one storage texture).
		static void Init();
		static void Shutdown();

		// Invalid (and logged) when the image is not a storage image or NRI cannot build the set.
		StorageImageClear(const Ref<Image2D>& image, uint32_t mip);

		bool IsValid() const { return m_View && m_Set && m_Set->IsValid(); }
		const std::string& GetDebugName() const { return m_DebugName; }

		// Render thread, between RT_Begin and RT_End: clears the mip to `value` (written as a
		// shader would store it). Skipped, logged once, when the clear is invalid.
		void RT_Record(RenderCommandBuffer& commandBuffer, const glm::vec4& value) const;

	private:
		std::string m_DebugName;
		uint32_t m_Mip = 0;
		uint32_t m_LayerCount = 1;
		TrackedTexture m_Texture;
		// Owned by the texture's view cache (GetNRITextureView), released with the texture.
		nri::Descriptor* m_View = nullptr;
		Ref<DescriptorSetGroup> m_Set;
		// Render thread.
		mutable bool m_ReportedInvalid = false;
	};

}
