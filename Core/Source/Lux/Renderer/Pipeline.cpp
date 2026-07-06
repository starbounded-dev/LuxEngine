#include "lpch.h"
#include "Pipeline.h"

#include "Renderer.h"

#include "Lux/Renderer/RendererAPI.h"

#include "Lux/Platform/Vulkan/VulkanShader.h"

namespace Lux {


	namespace Utils {

		static nvrhi::Format GetNVRHIFormat(ShaderDataType type)
		{
			switch (type)
			{
			case ShaderDataType::Float:    return nvrhi::Format::R32_FLOAT;
			case ShaderDataType::Float2:   return nvrhi::Format::RG32_FLOAT;
			case ShaderDataType::Float3:   return nvrhi::Format::RGB32_FLOAT;
			case ShaderDataType::Float4:   return nvrhi::Format::RGBA32_FLOAT;
			case ShaderDataType::Int:      return nvrhi::Format::R32_SINT;
			case ShaderDataType::Int2:     return nvrhi::Format::RG32_SINT;
			case ShaderDataType::Int3:     return nvrhi::Format::RGB32_SINT;
			case ShaderDataType::Int4:     return nvrhi::Format::RGBA32_SINT;
			case ShaderDataType::Bool:     return nvrhi::Format::RGBA32_FLOAT;
			}

			LUX_CORE_ASSERT(false, "Unknown format!");
			return nvrhi::Format::UNKNOWN;
		}

		static nvrhi::PrimitiveType GetNVRHIPrimitiveType(PrimitiveTopology topology)
		{
			switch (topology)
			{
			case PrimitiveTopology::Points:			return nvrhi::PrimitiveType::PointList;
			case PrimitiveTopology::Lines:			return nvrhi::PrimitiveType::LineList;
			case PrimitiveTopology::Triangles:		return nvrhi::PrimitiveType::TriangleList;
			case PrimitiveTopology::TriangleStrip:	return nvrhi::PrimitiveType::TriangleStrip;
			case PrimitiveTopology::TriangleFan:	return nvrhi::PrimitiveType::TriangleFan;
			}

			LUX_CORE_ASSERT(false, "Unknown toplogy");
			return nvrhi::PrimitiveType::PointList;
		}

		static nvrhi::ComparisonFunc GetNVRHICompareOperator(const DepthCompareOperator compareOp)
		{
			switch (compareOp)
			{
			case DepthCompareOperator::Never:			return nvrhi::ComparisonFunc::Never;
			case DepthCompareOperator::NotEqual:		return nvrhi::ComparisonFunc::NotEqual;
			case DepthCompareOperator::Less:			return nvrhi::ComparisonFunc::Less;
			case DepthCompareOperator::LessOrEqual:		return nvrhi::ComparisonFunc::LessOrEqual;
			case DepthCompareOperator::Greater:			return nvrhi::ComparisonFunc::Greater;
			case DepthCompareOperator::GreaterOrEqual:	return nvrhi::ComparisonFunc::GreaterOrEqual;
			case DepthCompareOperator::Equal:			return nvrhi::ComparisonFunc::Equal;
			case DepthCompareOperator::Always:			return nvrhi::ComparisonFunc::Always;
			}
			LUX_CORE_ASSERT(false, "Unknown Operator");
			return nvrhi::ComparisonFunc::Never;
		}

	}

	Pipeline::Pipeline(const PipelineSpecification& spec)
		: m_Specification(spec)
	{
		LUX_CORE_ASSERT(spec.Shader);
		LUX_CORE_ASSERT(spec.TargetFramebuffer);
		Invalidate();
		Renderer::RegisterShaderDependency(spec.Shader, this);
	}

	void Pipeline::Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref<Pipeline> instance = this;
		Renderer::Submit([instance]() mutable
			{
				instance->RT_Invalidate();
			});
	}

	void Pipeline::RT_Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_INFO_TAG("Renderer", "[Pipeline] Creating graphics pipeline: {}", m_Specification.Shader->GetName());

		nvrhi::IDevice* device = Application::Get().GetWindow().GetDeviceManager()->GetDevice();
		Ref<VulkanShader> vulkanShader = Ref<VulkanShader>(m_Specification.Shader);
		Ref<Framebuffer> framebuffer = m_Specification.TargetFramebuffer;

		nvrhi::GraphicsPipelineDesc pipelineDesc;

#pragma region Shaders

		pipelineDesc.bindingLayouts = vulkanShader->GetAllDescriptorSetLayouts();

		const auto& shaderHandles = vulkanShader->GetHandles();
		const bool isMeshletPipeline = shaderHandles.contains(nvrhi::ShaderType::Mesh);
		if (shaderHandles.contains(nvrhi::ShaderType::Vertex))
			pipelineDesc.VS = shaderHandles.at(nvrhi::ShaderType::Vertex);
		if (shaderHandles.contains(nvrhi::ShaderType::Pixel))
			pipelineDesc.PS = shaderHandles.at(nvrhi::ShaderType::Pixel);

		pipelineDesc.primType = Utils::GetNVRHIPrimitiveType(m_Specification.Topology);
		if (Renderer::SupportsVariableRateShading())
		{
			pipelineDesc.shadingRateState.enabled = true;
			pipelineDesc.shadingRateState.shadingRate = nvrhi::VariableShadingRate::e1x1;
			pipelineDesc.shadingRateState.pipelinePrimitiveCombiner = nvrhi::ShadingRateCombiner::Override;
			pipelineDesc.shadingRateState.imageCombiner = nvrhi::ShadingRateCombiner::Override;
		}

#pragma endregion

#pragma region RasterState

		nvrhi::RasterState& rasterState = pipelineDesc.renderState.rasterState;
		rasterState.cullMode = m_Specification.BackfaceCulling ? nvrhi::RasterCullMode::Back : nvrhi::RasterCullMode::None;
		rasterState.fillMode = m_Specification.Wireframe ? nvrhi::RasterFillMode::Line : nvrhi::RasterFillMode::Fill;
		rasterState.frontCounterClockwise = true;
		rasterState.multisampleEnable = false;

#pragma endregion

#pragma region DepthStencilState

		nvrhi::DepthStencilState& depthStencilState = pipelineDesc.renderState.depthStencilState;
		depthStencilState.depthTestEnable = m_Specification.DepthTest;
		depthStencilState.depthWriteEnable = m_Specification.DepthWrite;
		depthStencilState.depthFunc = Utils::GetNVRHICompareOperator(m_Specification.DepthOperator);

		// Stencil off
		depthStencilState.stencilEnable = false;
		depthStencilState.backFaceStencil.failOp = nvrhi::StencilOp::Keep;
		depthStencilState.backFaceStencil.passOp = nvrhi::StencilOp::Keep;
		depthStencilState.backFaceStencil.stencilFunc = nvrhi::ComparisonFunc::Always;
		depthStencilState.frontFaceStencil = depthStencilState.backFaceStencil;

#pragma endregion

#pragma region InputLayout

		// Vertex input descriptor
		VertexBufferLayout& vertexLayout = m_Specification.Layout;
		VertexBufferLayout& instanceLayout = m_Specification.InstanceLayout;
		VertexBufferLayout& boneInfluenceLayout = m_Specification.BoneInfluenceLayout;

		nvrhi::static_vector<nvrhi::VertexAttributeDesc, nvrhi::c_MaxVertexAttributes> vertexAttributes;

		uint32_t bufferIndex = 0;
		for (const auto& layout : { vertexLayout, instanceLayout, boneInfluenceLayout })
		{
			for (const VertexBufferElement& element : layout)
			{
				nvrhi::VertexAttributeDesc& attributeDesc = vertexAttributes.emplace_back();
				attributeDesc.bufferIndex = bufferIndex;
				attributeDesc.name = element.Name;
				attributeDesc.format = Utils::GetNVRHIFormat(element.Type);
				attributeDesc.offset = element.Offset;
				attributeDesc.elementStride = layout.GetStride();
				attributeDesc.isInstanced = layout.IsInstanced();
			}

			if (layout.GetElementCount() > 0)
				bufferIndex++;
		}

		if (!isMeshletPipeline)
			pipelineDesc.inputLayout = device->createInputLayout(vertexAttributes.data(), vertexAttributes.size(), pipelineDesc.VS);

#pragma endregion

#pragma region BlendState

		nvrhi::BlendState& blendState = pipelineDesc.renderState.blendState;
		size_t colorAttachmentCount = framebuffer->GetSpecification().SwapChainTarget ? 1 : framebuffer->GetColorAttachmentCount();
		if (framebuffer->GetSpecification().SwapChainTarget)
		{
			nvrhi::BlendState::RenderTarget& renderTarget = blendState.targets[0];
			renderTarget.blendEnable = true;
			renderTarget.colorWriteMask = nvrhi::ColorMask::All;
			renderTarget.srcBlend = nvrhi::BlendFactor::SrcAlpha;
			renderTarget.destBlend = nvrhi::BlendFactor::OneMinusSrcAlpha;
			renderTarget.blendOp = nvrhi::BlendOp::Add;
			renderTarget.blendOpAlpha = nvrhi::BlendOp::Add;
			renderTarget.srcBlendAlpha = nvrhi::BlendFactor::One;
			renderTarget.destBlendAlpha = nvrhi::BlendFactor::Zero;
		}
		else
		{
			for (size_t i = 0; i < colorAttachmentCount; i++)
			{
				if (!framebuffer->GetSpecification().Blend)
					break;

				nvrhi::BlendState::RenderTarget& renderTarget = blendState.targets[i];
				renderTarget.colorWriteMask = nvrhi::ColorMask::All;

				const auto& attachmentSpec = framebuffer->GetSpecification().Attachments.Attachments[i];
				FramebufferBlendMode blendMode = framebuffer->GetSpecification().BlendMode == FramebufferBlendMode::None
					? attachmentSpec.BlendMode
					: framebuffer->GetSpecification().BlendMode;

				renderTarget.blendEnable = attachmentSpec.Blend ? VK_TRUE : VK_FALSE;

				renderTarget.blendOp = nvrhi::BlendOp::Add;
				renderTarget.blendOpAlpha = nvrhi::BlendOp::Add;
				renderTarget.srcBlendAlpha = nvrhi::BlendFactor::One;
				renderTarget.destBlendAlpha = nvrhi::BlendFactor::Zero;

				switch (blendMode)
				{
				case FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha:
					renderTarget.srcBlend = nvrhi::BlendFactor::SrcAlpha;
					renderTarget.destBlend = nvrhi::BlendFactor::OneMinusSrcAlpha;
					renderTarget.srcBlendAlpha = nvrhi::BlendFactor::SrcAlpha;
					renderTarget.destBlendAlpha = nvrhi::BlendFactor::OneMinusSrcAlpha;
					break;
				case FramebufferBlendMode::OneZero:
					renderTarget.srcBlend = nvrhi::BlendFactor::One;
					renderTarget.destBlend = nvrhi::BlendFactor::Zero;
					break;
				case FramebufferBlendMode::Zero_SrcColor:
					renderTarget.srcBlend = nvrhi::BlendFactor::Zero;
					renderTarget.destBlend = nvrhi::BlendFactor::SrcColor;
					break;
				default:
					LUX_CORE_VERIFY(false);
				}
			}
		}

#pragma endregion

		pipelineDesc.dynamicLineWidth = IsDynamicLineWidth();

		if (isMeshletPipeline)
		{
			// Mesh-shader pipeline: same render state, no vertex input; task
			// (amplification) stage is optional.
			nvrhi::MeshletPipelineDesc meshletDesc;
			meshletDesc.bindingLayouts = pipelineDesc.bindingLayouts;
			if (shaderHandles.contains(nvrhi::ShaderType::Amplification))
				meshletDesc.AS = shaderHandles.at(nvrhi::ShaderType::Amplification);
			meshletDesc.MS = shaderHandles.at(nvrhi::ShaderType::Mesh);
			meshletDesc.PS = pipelineDesc.PS;
			meshletDesc.primType = pipelineDesc.primType;
			meshletDesc.renderState = pipelineDesc.renderState;

			m_Handle = nullptr;
			m_MeshletHandle = device->createMeshletPipeline(meshletDesc, m_Specification.TargetFramebuffer->GetHandle());
			return;
		}

		m_Handle = device->createGraphicsPipeline(pipelineDesc, m_Specification.TargetFramebuffer->GetHandle());
	}

	bool Pipeline::IsDynamicLineWidth() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_Specification.Topology == PrimitiveTopology::Lines || m_Specification.Topology == PrimitiveTopology::LineStrip || m_Specification.Wireframe;
	}

}
