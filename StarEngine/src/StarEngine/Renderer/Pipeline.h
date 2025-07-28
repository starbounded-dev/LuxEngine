#pragma once

#include "StarEngine/Core/Ref.h"

#include "StarEngine/Renderer/VertexBuffer.h"
#include "StarEngine/Renderer/Shader.h"
#include "StarEngine/Renderer/UniformBuffer.h"
#include "StarEngine/Renderer/Framebuffer.h"

#include "PipelineSpecification.h"

#include "nvrhi/nvrhi.h"

namespace StarEngine {

	class Pipeline : public RefCounted
	{
	public:
		static Ref<Pipeline> Create(const PipelineSpecification& spec) { return Ref<Pipeline>::Create(spec); }

		PipelineSpecification& GetSpecification() { return m_Specification; }
		const PipelineSpecification& GetSpecification() const { return m_Specification; }

		nvrhi::GraphicsPipelineHandle GetHandle() { return m_Handle; }

		void Invalidate();
		void RT_Invalidate();

		Ref<Shader> GetShader() const { return m_Specification.Shader; }

		bool IsDynamicLineWidth() const;

	public:
		Pipeline(const PipelineSpecification& spec);

		virtual ~Pipeline() = default;
	private:
		nvrhi::GraphicsPipelineHandle m_Handle = nullptr;
		PipelineSpecification m_Specification;
	};

}
