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

namespace Lux {

	class Pipeline : public RefCounted
	{
	public:
		static Ref<Pipeline> Create(const PipelineSpecification& spec) { return Ref<Pipeline>::Create(spec); }

		PipelineSpecification& GetSpecification() { return m_Specification; }
		const PipelineSpecification& GetSpecification() const { return m_Specification; }

		nvrhi::GraphicsPipelineHandle GetHandle() { return m_Handle; }

		// Meshlet pipelines (shader has a mesh stage) get a MeshletPipeline
		// instead of a graphics pipeline; GetHandle() stays null for them.
		nvrhi::MeshletPipelineHandle GetMeshletHandle() { return m_MeshletHandle; }
		bool IsMeshletPipeline() const { return m_MeshletHandle != nullptr; }

		void Invalidate();
		void RT_Invalidate();

		Ref<Shader> GetShader() const { return m_Specification.Shader; }

		bool IsDynamicLineWidth() const;

	public:
		Pipeline(const PipelineSpecification& spec);

		virtual ~Pipeline() = default;
	private:
		nvrhi::GraphicsPipelineHandle m_Handle = nullptr;
		nvrhi::MeshletPipelineHandle m_MeshletHandle = nullptr;
		PipelineSpecification m_Specification;
	};

}
