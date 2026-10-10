// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Pipeline.h"

#include "Renderer.h"

#include "Lux/Renderer/RendererAPI.h"

#include "Lux/Platform/Vulkan/VulkanShader.h"
#include "Lux/Renderer/RHI/NVRHIWrappers.h"
#include "Lux/Renderer/RHI/RHIDevice.h"

#include <array>
#include <span>

namespace Lux {


	namespace Utils {

		struct NRIShaderStage
		{
			ShaderStage Stage;
			nri::StageBits StageBits;
		};

		// The stages each pipeline kind takes from its shader, in pipeline order; absent ones are skipped.
		constexpr NRIShaderStage k_VertexPipelineStages[] = {
			{ ShaderStage::Vertex, nri::StageBits::VERTEX_SHADER },
			{ ShaderStage::Pixel, nri::StageBits::FRAGMENT_SHADER },
		};
		constexpr NRIShaderStage k_MeshletPipelineStages[] = {
			{ ShaderStage::Amplification, nri::StageBits::TASK_SHADER },
			{ ShaderStage::Mesh, nri::StageBits::MESH_SHADER },
			{ ShaderStage::Pixel, nri::StageBits::FRAGMENT_SHADER },
		};

		// False for formats NRI vertex input cannot take (matrices take several locations).
		static bool TryGetNRIFormat(ShaderDataType type, nri::Format& outFormat)
		{
			switch (type)
			{
				case ShaderDataType::Float:		outFormat = nri::Format::R32_SFLOAT; return true;
				case ShaderDataType::Float2:	outFormat = nri::Format::RG32_SFLOAT; return true;
				case ShaderDataType::Float3:	outFormat = nri::Format::RGB32_SFLOAT; return true;
				case ShaderDataType::Float4:	outFormat = nri::Format::RGBA32_SFLOAT; return true;
				case ShaderDataType::Int:		outFormat = nri::Format::R32_SINT; return true;
				case ShaderDataType::Int2:		outFormat = nri::Format::RG32_SINT; return true;
				case ShaderDataType::Int3:		outFormat = nri::Format::RGB32_SINT; return true;
				case ShaderDataType::Int4:		outFormat = nri::Format::RGBA32_SINT; return true;
				// As before NRI: a bool attribute reads four floats.
				case ShaderDataType::Bool:		outFormat = nri::Format::RGBA32_SFLOAT; return true;
				default:						return false;
			}
		}

		// False for topologies NRI cannot express: it has no triangle fans (D3D12 has none).
		static bool TryGetNRITopology(PrimitiveTopology topology, nri::Topology& outTopology)
		{
			switch (topology)
			{
				case PrimitiveTopology::Points:			outTopology = nri::Topology::POINT_LIST; return true;
				case PrimitiveTopology::Lines:			outTopology = nri::Topology::LINE_LIST; return true;
				case PrimitiveTopology::LineStrip:		outTopology = nri::Topology::LINE_STRIP; return true;
				case PrimitiveTopology::Triangles:		outTopology = nri::Topology::TRIANGLE_LIST; return true;
				case PrimitiveTopology::TriangleStrip:	outTopology = nri::Topology::TRIANGLE_STRIP; return true;
				default:								return false;
			}
		}

		static nri::CompareOp GetNRICompareOp(DepthCompareOperator compareOp)
		{
			switch (compareOp)
			{
				case DepthCompareOperator::Never:			return nri::CompareOp::NEVER;
				case DepthCompareOperator::NotEqual:		return nri::CompareOp::NOT_EQUAL;
				case DepthCompareOperator::Less:			return nri::CompareOp::LESS;
				case DepthCompareOperator::LessOrEqual:		return nri::CompareOp::LESS_EQUAL;
				case DepthCompareOperator::Greater:			return nri::CompareOp::GREATER;
				case DepthCompareOperator::GreaterOrEqual:	return nri::CompareOp::GREATER_EQUAL;
				case DepthCompareOperator::Equal:			return nri::CompareOp::EQUAL;
				case DepthCompareOperator::Always:			return nri::CompareOp::ALWAYS;
				default:									break;
			}
			LUX_CORE_ASSERT(false, "Unknown depth compare operator");
			return nri::CompareOp::NEVER;
		}

		// The blend state of color attachment `index` of `framebuffer`.
		static void SetNRIBlend(nri::ColorAttachmentDesc& color, const FramebufferSpecification& framebuffer, uint32_t index)
		{
			color.colorWriteMask = nri::ColorWriteBits::RGBA;
			if (framebuffer.SwapChainTarget)
			{
				color.blendEnabled = true;
				color.colorBlend = { nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendOp::ADD };
				color.alphaBlend = { nri::BlendFactor::ONE, nri::BlendFactor::ZERO, nri::BlendOp::ADD };
				return;
			}

			// Off: write the color as it is.
			color.colorBlend = { nri::BlendFactor::ONE, nri::BlendFactor::ZERO, nri::BlendOp::ADD };
			color.alphaBlend = { nri::BlendFactor::ONE, nri::BlendFactor::ZERO, nri::BlendOp::ADD };
			if (!framebuffer.Blend)
				return;

			const FramebufferTextureSpecification& attachment = framebuffer.Attachments.Attachments[index];
			const FramebufferBlendMode blendMode = framebuffer.BlendMode == FramebufferBlendMode::None ? attachment.BlendMode : framebuffer.BlendMode;
			color.blendEnabled = attachment.Blend;
			switch (blendMode)
			{
				case FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha:
					color.colorBlend = { nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendOp::ADD };
					color.alphaBlend = { nri::BlendFactor::SRC_ALPHA, nri::BlendFactor::ONE_MINUS_SRC_ALPHA, nri::BlendOp::ADD };
					break;
				case FramebufferBlendMode::OneZero:
					color.colorBlend = { nri::BlendFactor::ONE, nri::BlendFactor::ZERO, nri::BlendOp::ADD };
					break;
				case FramebufferBlendMode::Zero_SrcColor:
					color.colorBlend = { nri::BlendFactor::ZERO, nri::BlendFactor::SRC_COLOR, nri::BlendOp::ADD };
					break;
				case FramebufferBlendMode::Additive:
					// Colour adds; alpha keeps the destination's (an additive pass layers light, it
					// does not change coverage).
					color.colorBlend = { nri::BlendFactor::ONE, nri::BlendFactor::ONE, nri::BlendOp::ADD };
					color.alphaBlend = { nri::BlendFactor::ZERO, nri::BlendFactor::ONE, nri::BlendOp::ADD };
					break;
				default:
					LUX_CORE_VERIFY(false);
			}
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

	Pipeline::~Pipeline()
	{
		ReleaseNRIPipeline();
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
		ReleaseNRIPipeline();
		m_NRIDynamicShadingRate = false;
		m_NRIVertexStrides = {};

		const std::string& name = m_Specification.DebugName.empty() ? m_Specification.Shader->GetName() : m_Specification.DebugName;
		Ref<VulkanShader> shader = m_Specification.Shader.As<VulkanShader>();
		Ref<Framebuffer> framebuffer = m_Specification.TargetFramebuffer;
		nri::PipelineLayout* layout = shader->GetNRIPipelineLayout();
		m_IsMeshletPipeline = !shader->GetSPIRV(ShaderStage::Mesh).empty();

		nri::GraphicsPipelineDesc pipelineDesc = {};
		const char* problem = !layout ? "no NRI pipeline layout"
			: !Utils::TryGetNRITopology(m_Specification.Topology, pipelineDesc.inputAssembly.topology) ? "its topology has no NRI equivalent (NRI has no triangle fans)"
			: nullptr;

		// Shaders.
		static_assert(std::size(Utils::k_MeshletPipelineStages) >= std::size(Utils::k_VertexPipelineStages));
		std::array<nri::ShaderDesc, std::size(Utils::k_MeshletPipelineStages)> shaders = {};
		uint32_t shaderCount = 0;
		const std::span<const Utils::NRIShaderStage> stages = m_IsMeshletPipeline
			? std::span<const Utils::NRIShaderStage>(Utils::k_MeshletPipelineStages)
			: std::span<const Utils::NRIShaderStage>(Utils::k_VertexPipelineStages);
		for (const Utils::NRIShaderStage& stage : stages)
		{
			const std::vector<uint32_t>& spirv = shader->GetSPIRV(stage.Stage);
			if (spirv.empty())
				continue;

			nri::ShaderDesc& shaderDesc = shaders[shaderCount++];
			shaderDesc.stage = stage.StageBits;
			shaderDesc.bytecode = spirv.data();
			shaderDesc.size = spirv.size() * sizeof(uint32_t);
			shaderDesc.entryPointName = "main";
		}
		if (!problem && shaderCount == 0)
			problem = "no SPIR-V";

		// Vertex input: the layouts' elements at locations 0..n in order, one stream (binding slot) per
		// layout that has elements.
		const std::array<const VertexBufferLayout*, 3> layouts = { &m_Specification.Layout, &m_Specification.InstanceLayout, &m_Specification.BoneInfluenceLayout };
		std::array<nri::VertexAttributeDesc, nvrhi::c_MaxVertexAttributes> attributes = {};
		std::array<nri::VertexStreamDesc, layouts.size()> streams = {};
		uint32_t attributeCount = 0;
		uint32_t streamCount = 0;
		nri::VertexInputDesc vertexInput = {};
		if (!m_IsMeshletPipeline)
		{
			for (const VertexBufferLayout* vertexLayout : layouts)
			{
				if (vertexLayout->GetElementCount() == 0)
					continue;

				for (const VertexBufferElement& element : *vertexLayout)
				{
					if (attributeCount == attributes.size())
					{
						problem = "it has more vertex attributes than NRI takes";
						break;
					}

					nri::VertexAttributeDesc& attribute = attributes[attributeCount];
					attribute.vk.location = attributeCount;
					attribute.offset = element.Offset;
					attribute.streamIndex = static_cast<uint16_t>(streamCount);
					if (!Utils::TryGetNRIFormat(element.Type, attribute.format) && !problem)
						problem = "a vertex attribute type has no NRI format";
					attributeCount++;
				}

				nri::VertexStreamDesc& stream = streams[streamCount];
				stream.bindingSlot = static_cast<uint16_t>(streamCount);
				stream.stepRate = vertexLayout->IsInstanced() ? nri::VertexStreamStepRate::PER_INSTANCE : nri::VertexStreamStepRate::PER_VERTEX;
				stream.stride = static_cast<uint16_t>(vertexLayout->GetStride());
				streamCount++;
			}

			vertexInput.attributes = attributes.data();
			vertexInput.attributeNum = static_cast<uint8_t>(attributeCount);
			vertexInput.streams = streams.data();
			vertexInput.streamNum = static_cast<uint8_t>(streamCount);
			pipelineDesc.vertexInput = &vertexInput;
		}

		// Rasterization.
		pipelineDesc.rasterization.fillMode = m_Specification.Wireframe ? nri::FillMode::WIREFRAME : nri::FillMode::SOLID;
		pipelineDesc.rasterization.cullMode = m_Specification.BackfaceCulling ? nri::CullMode::BACK : nri::CullMode::NONE;
		pipelineDesc.rasterization.frontCounterClockwise = true;
		// Mesh pipelines take no shading-rate state, as NVRHI's meshlet pipelines had none.
		pipelineDesc.rasterization.shadingRate = Renderer::SupportsVariableRateShading() && !m_IsMeshletPipeline;
		if (!problem && pipelineDesc.rasterization.shadingRate && RHIDevice::GetDesc().tiers.shadingRate == 0)
			problem = "it uses variable rate shading, which NRI reports as unsupported";

		// Multisampling, from the framebuffer.
		nri::MultisampleDesc multisample = {};
		if (framebuffer->GetSampleCount() > 1)
		{
			multisample.sampleMask = nri::ALL;
			multisample.sampleNum = static_cast<nri::Sample_t>(framebuffer->GetSampleCount());
			pipelineDesc.multisample = &multisample;
		}

		// Output merger: one blend state per color attachment of the framebuffer.
		const uint32_t colorCount = static_cast<uint32_t>(framebuffer->GetColorAttachmentCount());
		std::array<nri::ColorAttachmentDesc, nvrhi::c_MaxRenderTargets> colors = {};
		if (!problem && colorCount > colors.size())
			problem = "its framebuffer has more color attachments than NRI takes";
		for (uint32_t i = 0; !problem && i < colorCount; i++)
		{
			nri::ColorAttachmentDesc& color = colors[i];
			color.format = framebuffer->GetNRIColorFormat(i);
			Utils::SetNRIBlend(color, framebuffer->GetSpecification(), i);
			if (color.format == nri::Format::UNKNOWN)
				problem = "a color attachment format has no NRI equivalent";
		}
		pipelineDesc.outputMerger.colors = colors.data();
		pipelineDesc.outputMerger.colorNum = colorCount;

		// NRI disables the depth test with CompareOp::NONE; stencil stays off (zeroed).
		pipelineDesc.outputMerger.depth.compareOp = m_Specification.DepthTest ? Utils::GetNRICompareOp(m_Specification.DepthOperator) : nri::CompareOp::NONE;
		pipelineDesc.outputMerger.depth.write = m_Specification.DepthWrite;
		pipelineDesc.outputMerger.depthStencilFormat = framebuffer->GetNRIDepthFormat();
		if (!problem && framebuffer->HasDepthAttachment() && pipelineDesc.outputMerger.depthStencilFormat == nri::Format::UNKNOWN)
			problem = "the depth attachment format has no NRI equivalent";

		if (problem)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[Pipeline] No NRI pipeline for {}: {}", name, problem);
			return;
		}

		pipelineDesc.pipelineLayout = layout;
		pipelineDesc.shaders = shaders.data();
		pipelineDesc.shaderNum = shaderCount;

		const NRIInterface& api = RHIDevice::API();
		if (api.CreateGraphicsPipeline(RHIDevice::Get(), pipelineDesc, m_NRIPipeline) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[Pipeline] Failed to create the NRI pipeline for {}", name);
			m_NRIPipeline = nullptr;
			return;
		}

		api.SetDebugName(m_NRIPipeline, name.c_str());

		m_NRIDynamicShadingRate = pipelineDesc.rasterization.shadingRate;
		for (uint32_t i = 0; i < streamCount; i++)
			m_NRIVertexStrides[streams[i].bindingSlot] = streams[i].stride;
	}

	void Pipeline::ReleaseNRIPipeline()
	{
		if (!m_NRIPipeline)
			return;

		Renderer::SubmitResourceFree([pipeline = std::exchange(m_NRIPipeline, nullptr)]()
			{
				RHIDevice::API().DestroyPipeline(pipeline);
			});
	}

	bool Pipeline::IsDynamicLineWidth() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_Specification.Topology == PrimitiveTopology::Lines || m_Specification.Topology == PrimitiveTopology::LineStrip || m_Specification.Wireframe;
	}

}
