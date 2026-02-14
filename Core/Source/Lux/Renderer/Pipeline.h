#pragma once

#include "Lux/Core/Ref.h"

#include "Lux/Renderer/VertexBuffer.h"
#include "Lux/Renderer/Shader.h"
#include "Lux/Renderer/UniformBuffer.h"
#include "Lux/Renderer/Framebuffer.h"

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
