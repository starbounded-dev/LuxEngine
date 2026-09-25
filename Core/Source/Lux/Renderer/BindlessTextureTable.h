// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"
#include "Lux/Renderer/Texture.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace Lux {

	// A bindless texture array backed by NVRHI descriptor tables: shaders index one unsized
	// `texture2D u_GPUMaterialTextures[]` at descriptor set `DescriptorSet`, binding 0.
	//
	// The layout is process-wide (every pipeline that declares the set shares it). Tables are
	// per owner, because each SceneRenderer has its own texture index space, and there is one
	// table per frame in flight: NVRHI's bindless layouts are partially bound but not
	// update-after-bind, so a table may only be written while no in-flight frame reads it.
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
		static nvrhi::BindingLayoutHandle GetLayout();
		static uint32_t GetCapacity();

		BindlessTextureTable();
		~BindlessTextureTable();

		// Records that `slot` should show `texture` (white when null). Main thread.
		void SetSlot(uint32_t slot, Ref<Texture2D> texture);
		// Sends recorded writes to the render thread and brings this frame's table up to date.
		// Call after the frame's SetSlot calls and before the passes that read the table.
		void Flush();

		nvrhi::IDescriptorTable* RT_GetTable(uint32_t frameIndex) const;

	private:
		void RT_WriteTable(uint32_t frameIndex);

	private:
		// Main thread.
		std::vector<std::pair<uint32_t, Ref<Texture2D>>> m_Pending;

		// Render thread. Each table keeps its textures alive while it references them.
		struct FrameTable
		{
			nvrhi::DescriptorTableHandle Table;
			std::vector<Ref<Texture2D>> Written;
			// The image each slot's descriptor points at: a hot-reloaded texture keeps its Ref but
			// swaps its image, and the descriptor must follow.
			std::vector<nvrhi::ITexture*> WrittenHandles;
			std::vector<std::pair<uint32_t, Ref<Texture2D>>> Queued;
		};
		std::vector<FrameTable> m_Frames;
	};

}
