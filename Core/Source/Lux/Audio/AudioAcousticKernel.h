// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "AudioAcoustics.h"

namespace Lux
{
	class ComputePass;
	class PipelineCompute;
	class RenderCommandBuffer;
	class Shader;
	class StorageBuffer;

	// Soundwave's listener reverb on the GPU (SoundwaveReverb.glsl). Main thread submits and polls;
	// the render thread records the dispatch on the graphics queue and copies the rays out once
	// RT_BeginFrame has waited for the frame that recorded it, so nothing ever waits on the GPU.
	class AudioAcousticKernel : public AudioAcoustics::ReverbBackend
	{
	public:
		// Null when the shader is not in the library (the CPU job is used instead).
		static Ref<AudioAcousticKernel> Create();
		bool Dispatch(const Ref<Geometry>& geometry, Soundwave::Position listener,
			const Soundwave::Settings& settings, const Soundwave::MaterialTable& materials) override;
		bool Poll(std::vector<Soundwave::ReverbRay>& rays) override;

	private:
		struct Readback;
		bool Bind(const Ref<Geometry>& geometry, uint32_t rays);
		Ref<Shader> m_Shader;
		Ref<PipelineCompute> m_Pipeline;
		Ref<ComputePass> m_Pass;
		Ref<RenderCommandBuffer> m_Commands;
		Ref<StorageBuffer> m_Nodes, m_Order, m_Triangles, m_Request, m_Rays;
		Ref<Geometry> m_Bound;
		Ref<Readback> m_Readback;
		uint32_t m_BoundRays = 0;
		uint64_t m_Dispatch = 0;
	};
}
