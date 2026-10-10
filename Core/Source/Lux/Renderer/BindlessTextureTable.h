// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Renderer/RHI/DescriptorSetGroup.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace nri {
	struct DescriptorRangeDesc;
	struct DescriptorSet;
	struct Texture;
}

namespace Lux {

	// A bindless texture array: shaders index one unsized `texture2D u_GPUMaterialTextures[]` at
	// descriptor set `DescriptorSet`, binding 0.
	//
	// The range is process-wide (every pipeline layout that declares the set uses it). Tables are
	// per owner, because each SceneRenderer has its own texture index space, and there is one
	// set per frame in flight: the range is partially bound but not update-after-bind, so a set may
	// only be written while no in-flight frame reads it.
	//
	// Threading: SetSlot/Flush run on the main thread. They only record, and hand the writes to
	// the render thread through Renderer::Submit, which is the only thread that touches tables.
	class BindlessTextureTable : public RefCounted
	{
	public:
		static constexpr uint32_t DescriptorSet = 4;
		// Mirrors GPU_TEXTURE_SCENE_MAX_TEXTURES in MaterialScene.glslh; the device may clamp it.
		static constexpr uint32_t MaxCapacity = 16384;

		static void Init();
		static void Shutdown();
		static uint32_t GetCapacity();
		// The bindless set's single range in NRI pipeline layouts. Every layout that declares the
		// set uses exactly this range, so their set layouts are identical (and compatible).
		static nri::DescriptorRangeDesc GetNRIRange();

		BindlessTextureTable();
		~BindlessTextureTable();

		// Records that `slot` should show `texture` (white when null). Main thread.
		void SetSlot(uint32_t slot, Ref<Texture2D> texture);
		// Sends recorded writes to the render thread and brings this frame's table up to date.
		// Call after the frame's SetSlot calls and before the passes that read the table.
		void Flush();

		// This frame's table (empty if NRI could not build it). Untracked, as in NVRHI: it records no
		// uses.
		BoundDescriptorSet RT_GetDescriptorSet(uint32_t frameIndex) const;

	private:
		void RT_WriteTable(uint32_t frameIndex);

	private:
		// Main thread.
		std::vector<std::pair<uint32_t, Ref<Texture2D>>> m_Pending;

		// Render thread. Each table keeps its textures alive while it references them.
		struct FrameTable
		{
			std::vector<Ref<Texture2D>> Written;
			// The texture each slot's descriptor points at: a hot-reloaded texture keeps its Ref but
			// swaps its image, and the descriptor must follow. Compared, never dereferenced.
			std::vector<const nri::Texture*> WrittenTextures;
			std::vector<std::pair<uint32_t, Ref<Texture2D>>> Queued;
		};
		std::vector<FrameTable> m_Frames;
		// One set per frame table.
		Ref<DescriptorSetGroup> m_NRISets;
	};

}
