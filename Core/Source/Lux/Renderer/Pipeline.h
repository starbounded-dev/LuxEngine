// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"

#include "Lux/Renderer/VertexBuffer.h"
#include "Lux/Renderer/Shader.h"
#include "Lux/Renderer/UniformBuffer.h"
#include "Lux/Renderer/IndexBuffer.h"
#include "Lux/Renderer/Framebuffer.h"
#include "Lux/Renderer/Material.h"

#include "PipelineSpecification.h"

#include "nvrhi/nvrhi.h"

#include <array>

namespace nri {
	struct Pipeline;
}

namespace Lux {

	class Pipeline : public RefCounted
	{
	public:
		static Ref<Pipeline> Create(const PipelineSpecification& spec) { return Ref<Pipeline>::Create(spec); }

		PipelineSpecification& GetSpecification() { return m_Specification; }
		const PipelineSpecification& GetSpecification() const { return m_Specification; }

		// The NRI pipeline: built from the specification over the shader's NRI layout, for the target
		// framebuffer's formats, and recreated by Invalidate. A shader with a mesh stage makes a
		// mesh-shader pipeline. Null if NRI cannot express or create it (logged).
		nri::Pipeline* GetNRIPipeline() const { return m_NRIPipeline; }
		bool IsMeshletPipeline() const { return m_IsMeshletPipeline && m_NRIPipeline; }
		// Whether the NRI pipeline takes its shading rate dynamically, and the stride of vertex buffer
		// `slot` (NRI vertex strides are dynamic, set with the buffers).
		bool HasNRIDynamicShadingRate() const { return m_NRIDynamicShadingRate; }
		uint32_t GetNRIVertexStride(uint32_t slot) const { return slot < m_NRIVertexStrides.size() ? m_NRIVertexStrides[slot] : 0; }

		void Invalidate();
		void RT_Invalidate();

		Ref<Shader> GetShader() const { return m_Specification.Shader; }

		// Line pipelines take their width when a pass opens (the NRI fork's LUX-1).
		bool IsDynamicLineWidth() const;

	public:
		Pipeline(const PipelineSpecification& spec);

		virtual ~Pipeline();
	private:
		void ReleaseNRIPipeline();
	private:
		nri::Pipeline* m_NRIPipeline = nullptr;
		bool m_IsMeshletPipeline = false;
		bool m_NRIDynamicShadingRate = false;
		std::array<uint32_t, nvrhi::c_MaxVertexAttributes> m_NRIVertexStrides = {};
		PipelineSpecification m_Specification;
	};

}
