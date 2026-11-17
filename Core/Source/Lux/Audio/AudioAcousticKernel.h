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
		~AudioAcousticKernel() override; // members are only forward-declared here
		bool Dispatch(const Ref<Geometry>& geometry, Soundwave::Position listener,
			const Soundwave::Settings& settings, const Soundwave::MaterialTable& materials) override;
		PollResult Poll(std::vector<Soundwave::ReverbRay>& rays) override;
		float SourceGpuMilliseconds() const override;
		bool SupportsSources() const override;
		bool DispatchSources(const Ref<Geometry>& geometry, std::span<const SourceInput> sources,
			std::span<const Soundwave::FlatPortal> portals) override;
		PollResult PollSources(std::vector<SourceOutput>& results) override;
		bool SupportsVisibility() const override { return m_VisibilityShader != nullptr; }
		bool DispatchVisibility(const Ref<Geometry>& geometry, std::span<const Soundwave::FlatVisibilitySegment> segments,
			std::span<const Soundwave::FlatPortal> portals) override;
		PollResult PollVisibility(std::vector<std::uint32_t>& results) override;

	private:
		struct Sources;
		Ref<Sources> m_Sources;
		struct Readback;
		struct VisibilityReadback;
		bool Bind(const Ref<Geometry>& geometry, uint32_t rays);
		bool BindVisibility(const Ref<Geometry>& geometry, uint32_t segments, uint32_t portals);
		Ref<Shader> m_Shader;
		Ref<PipelineCompute> m_Pipeline;
		Ref<ComputePass> m_Pass;
		Ref<RenderCommandBuffer> m_Commands;
		Ref<StorageBuffer> m_Nodes, m_Order, m_Triangles, m_Request, m_Rays;
		Ref<Geometry> m_Bound;
		Ref<Readback> m_Readback;
		uint32_t m_BoundRays = 0;
		uint64_t m_Dispatch = 0;
		Ref<Shader> m_VisibilityShader;
		Ref<PipelineCompute> m_VisibilityPipeline;
		Ref<ComputePass> m_VisibilityPass;
		Ref<RenderCommandBuffer> m_VisibilityCommands;
		Ref<StorageBuffer> m_VisibilityNodes, m_VisibilityOrder, m_VisibilityTriangles;
		Ref<StorageBuffer> m_VisibilityRequest, m_VisibilitySegments, m_VisibilityPortals, m_VisibilityResults;
		Ref<Geometry> m_VisibilityBound;
		Ref<VisibilityReadback> m_VisibilityReadback;
		uint32_t m_VisibilityCapacity = 0, m_VisibilityPortalCapacity = 0;
		uint32_t m_VisibilityCount = 0;
		uint64_t m_VisibilityDispatch = 0;
	};
}
