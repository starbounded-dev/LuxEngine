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
			case PrimitiveTopology::LineStrip:		return nvrhi::PrimitiveType::LineStrip;
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

		// The NRI pipeline is translated from the finished NVRHI desc, so the two pipelines cannot
		// drift apart while both exist. These go away with NVRHI (NRI migration Phase 15).

		// False for topologies NRI cannot express: it has no triangle fans (D3D12 has none).
		static bool TryGetNRITopology(nvrhi::PrimitiveType type, nri::Topology& outTopology)
		{
			switch (type)
			{
				case nvrhi::PrimitiveType::PointList:		outTopology = nri::Topology::POINT_LIST; return true;
				case nvrhi::PrimitiveType::LineList:		outTopology = nri::Topology::LINE_LIST; return true;
				case nvrhi::PrimitiveType::LineStrip:		outTopology = nri::Topology::LINE_STRIP; return true;
				case nvrhi::PrimitiveType::TriangleList:	outTopology = nri::Topology::TRIANGLE_LIST; return true;
				case nvrhi::PrimitiveType::TriangleStrip:	outTopology = nri::Topology::TRIANGLE_STRIP; return true;
				default:									return false;
			}
		}

		static nri::BlendFactor GetNRIBlendFactor(nvrhi::BlendFactor factor)
		{
			switch (factor)
			{
				case nvrhi::BlendFactor::Zero:				return nri::BlendFactor::ZERO;
				case nvrhi::BlendFactor::One:				return nri::BlendFactor::ONE;
				case nvrhi::BlendFactor::SrcColor:			return nri::BlendFactor::SRC_COLOR;
				case nvrhi::BlendFactor::InvSrcColor:		return nri::BlendFactor::ONE_MINUS_SRC_COLOR;
				case nvrhi::BlendFactor::SrcAlpha:			return nri::BlendFactor::SRC_ALPHA;
				case nvrhi::BlendFactor::InvSrcAlpha:		return nri::BlendFactor::ONE_MINUS_SRC_ALPHA;
				case nvrhi::BlendFactor::DstAlpha:			return nri::BlendFactor::DST_ALPHA;
				case nvrhi::BlendFactor::InvDstAlpha:		return nri::BlendFactor::ONE_MINUS_DST_ALPHA;
				case nvrhi::BlendFactor::DstColor:			return nri::BlendFactor::DST_COLOR;
				case nvrhi::BlendFactor::InvDstColor:		return nri::BlendFactor::ONE_MINUS_DST_COLOR;
				case nvrhi::BlendFactor::SrcAlphaSaturate:	return nri::BlendFactor::SRC_ALPHA_SATURATE;
				case nvrhi::BlendFactor::ConstantColor:		return nri::BlendFactor::CONSTANT_COLOR;
				case nvrhi::BlendFactor::InvConstantColor:	return nri::BlendFactor::ONE_MINUS_CONSTANT_COLOR;
				case nvrhi::BlendFactor::Src1Color:			return nri::BlendFactor::SRC1_COLOR;
				case nvrhi::BlendFactor::InvSrc1Color:		return nri::BlendFactor::ONE_MINUS_SRC1_COLOR;
				case nvrhi::BlendFactor::Src1Alpha:			return nri::BlendFactor::SRC1_ALPHA;
				case nvrhi::BlendFactor::InvSrc1Alpha:		return nri::BlendFactor::ONE_MINUS_SRC1_ALPHA;
			}
			LUX_CORE_ASSERT(false, "Unknown blend factor");
			return nri::BlendFactor::ONE;
		}

		static nri::BlendOp GetNRIBlendOp(nvrhi::BlendOp op)
		{
			switch (op)
			{
				case nvrhi::BlendOp::Add:				return nri::BlendOp::ADD;
				case nvrhi::BlendOp::Subtract:			return nri::BlendOp::SUBTRACT;
				case nvrhi::BlendOp::ReverseSubtract:	return nri::BlendOp::REVERSE_SUBTRACT;
				case nvrhi::BlendOp::Min:				return nri::BlendOp::MIN;
				case nvrhi::BlendOp::Max:				return nri::BlendOp::MAX;
			}
			LUX_CORE_ASSERT(false, "Unknown blend op");
			return nri::BlendOp::ADD;
		}

		static nri::ColorWriteBits GetNRIColorWriteMask(nvrhi::ColorMask mask)
		{
			const uint8_t bits = static_cast<uint8_t>(mask);
			nri::ColorWriteBits result = nri::ColorWriteBits::NONE;
			if (bits & static_cast<uint8_t>(nvrhi::ColorMask::Red))
				result |= nri::ColorWriteBits::R;
			if (bits & static_cast<uint8_t>(nvrhi::ColorMask::Green))
				result |= nri::ColorWriteBits::G;
			if (bits & static_cast<uint8_t>(nvrhi::ColorMask::Blue))
				result |= nri::ColorWriteBits::B;
			if (bits & static_cast<uint8_t>(nvrhi::ColorMask::Alpha))
				result |= nri::ColorWriteBits::A;
			return result;
		}

		static nri::CompareOp GetNRICompareOp(nvrhi::ComparisonFunc func)
		{
			switch (func)
			{
				case nvrhi::ComparisonFunc::Never:			return nri::CompareOp::NEVER;
				case nvrhi::ComparisonFunc::Less:			return nri::CompareOp::LESS;
				case nvrhi::ComparisonFunc::Equal:			return nri::CompareOp::EQUAL;
				case nvrhi::ComparisonFunc::LessOrEqual:	return nri::CompareOp::LESS_EQUAL;
				case nvrhi::ComparisonFunc::Greater:		return nri::CompareOp::GREATER;
				case nvrhi::ComparisonFunc::NotEqual:		return nri::CompareOp::NOT_EQUAL;
				case nvrhi::ComparisonFunc::GreaterOrEqual:	return nri::CompareOp::GREATER_EQUAL;
				case nvrhi::ComparisonFunc::Always:			return nri::CompareOp::ALWAYS;
			}
			LUX_CORE_ASSERT(false, "Unknown comparison function");
			return nri::CompareOp::NEVER;
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

		nvrhi::IDevice* device = Application::Get().GetWindow().GetDeviceManager()->GetDevice();
		Ref<VulkanShader> vulkanShader = Ref<VulkanShader>(m_Specification.Shader);
		Ref<Framebuffer> framebuffer = m_Specification.TargetFramebuffer;

		nvrhi::GraphicsPipelineDesc pipelineDesc;

#pragma region Shaders

		pipelineDesc.bindingLayouts = vulkanShader->GetAllDescriptorSetLayouts();

		const auto& shaderHandles = vulkanShader->GetHandles();
		const bool isMeshletPipeline = shaderHandles.contains(ShaderStage::Mesh);
		if (shaderHandles.contains(ShaderStage::Vertex))
			pipelineDesc.VS = shaderHandles.at(ShaderStage::Vertex);
		if (shaderHandles.contains(ShaderStage::Pixel))
			pipelineDesc.PS = shaderHandles.at(ShaderStage::Pixel);

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
				case FramebufferBlendMode::Additive:
					// Colour adds; alpha keeps the destination's (an additive pass layers light, it
					// does not change coverage).
					renderTarget.srcBlend = nvrhi::BlendFactor::One;
					renderTarget.destBlend = nvrhi::BlendFactor::One;
					renderTarget.srcBlendAlpha = nvrhi::BlendFactor::Zero;
					renderTarget.destBlendAlpha = nvrhi::BlendFactor::One;
					break;
				default:
					LUX_CORE_VERIFY(false);
				}
			}
		}

#pragma endregion

		pipelineDesc.dynamicLineWidth = IsDynamicLineWidth();

		CreateNRIPipeline(pipelineDesc, vertexAttributes.data(), static_cast<uint32_t>(vertexAttributes.size()), isMeshletPipeline);

		if (isMeshletPipeline)
		{
			// Mesh-shader pipeline: same render state, no vertex input; task
			// (amplification) stage is optional.
			nvrhi::MeshletPipelineDesc meshletDesc;
			meshletDesc.bindingLayouts = pipelineDesc.bindingLayouts;
			if (shaderHandles.contains(ShaderStage::Amplification))
				meshletDesc.AS = shaderHandles.at(ShaderStage::Amplification);
			meshletDesc.MS = shaderHandles.at(ShaderStage::Mesh);
			meshletDesc.PS = pipelineDesc.PS;
			meshletDesc.primType = pipelineDesc.primType;
			meshletDesc.renderState = pipelineDesc.renderState;

			m_Handle = nullptr;
			m_MeshletHandle = device->createMeshletPipeline(meshletDesc, m_Specification.TargetFramebuffer->GetHandle());
			return;
		}

		m_Handle = device->createGraphicsPipeline(pipelineDesc, m_Specification.TargetFramebuffer->GetHandle());
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

	void Pipeline::CreateNRIPipeline(const nvrhi::GraphicsPipelineDesc& desc, const nvrhi::VertexAttributeDesc* vertexAttributes, uint32_t vertexAttributeCount, bool isMeshletPipeline)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		ReleaseNRIPipeline();
		m_NRIDynamicShadingRate = false;
		m_NRIVertexStrides = {};

		const std::string& name = m_Specification.DebugName.empty() ? m_Specification.Shader->GetName() : m_Specification.DebugName;
		Ref<VulkanShader> shader = m_Specification.Shader.As<VulkanShader>();
		nri::PipelineLayout* layout = shader->GetNRIPipelineLayout();
		// The framebuffer NVRHI builds its pipeline against, so both name the same attachment formats.
		nvrhi::IFramebuffer* framebuffer = m_Specification.TargetFramebuffer->GetHandle();

		nri::GraphicsPipelineDesc pipelineDesc = {};
		const char* problem = !layout ? "no NRI pipeline layout"
			: !framebuffer ? "no framebuffer"
			: !Utils::TryGetNRITopology(desc.primType, pipelineDesc.inputAssembly.topology) ? "its topology has no NRI equivalent (NRI has no triangle fans)"
			: nullptr;
		if (problem)
		{
			LUX_CORE_ERROR_TAG("Renderer", "[Pipeline] No NRI pipeline for {}: {}", name, problem);
			return;
		}

		// Shaders.
		static_assert(std::size(Utils::k_MeshletPipelineStages) >= std::size(Utils::k_VertexPipelineStages));
		std::array<nri::ShaderDesc, std::size(Utils::k_MeshletPipelineStages)> shaders = {};
		uint32_t shaderCount = 0;
		const std::span<const Utils::NRIShaderStage> stages = isMeshletPipeline
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
		if (shaderCount == 0)
			problem = "no SPIR-V";

		// Vertex input: NVRHI's input layout, attribute i at location i and one binding per buffer
		// index. Lux's buffer indices run 0..n-1, so stream k is binding slot k.
		std::array<nri::VertexAttributeDesc, nvrhi::c_MaxVertexAttributes> attributes = {};
		std::array<nri::VertexStreamDesc, nvrhi::c_MaxVertexAttributes> streams = {};
		uint32_t streamCount = 0;
		nri::VertexInputDesc vertexInput = {};
		if (!isMeshletPipeline)
		{
			LUX_CORE_ASSERT(vertexAttributeCount <= attributes.size());
			for (uint32_t i = 0; i < vertexAttributeCount && !problem; i++)
			{
				const nvrhi::VertexAttributeDesc& source = vertexAttributes[i];
				nri::VertexAttributeDesc& attribute = attributes[i];
				attribute.vk.location = i;
				attribute.offset = source.offset;
				attribute.format = ToNRIFormat(source.format);
				attribute.streamIndex = static_cast<uint16_t>(source.bufferIndex);
				if (attribute.format == nri::Format::UNKNOWN)
					problem = "a vertex attribute format has no NRI equivalent";
				// An array attribute takes several NVRHI locations, which this numbering does not replicate.
				else if (source.arraySize != 1)
					problem = "it has an array vertex attribute";
				else if (source.bufferIndex > streamCount)
					problem = "its vertex buffer indices have a gap";

				if (!problem && source.bufferIndex == streamCount)
				{
					nri::VertexStreamDesc& stream = streams[streamCount++];
					stream.bindingSlot = static_cast<uint16_t>(source.bufferIndex);
					stream.stepRate = source.isInstanced ? nri::VertexStreamStepRate::PER_INSTANCE : nri::VertexStreamStepRate::PER_VERTEX;
					stream.stride = static_cast<uint16_t>(source.elementStride);
				}
			}

			vertexInput.attributes = attributes.data();
			vertexInput.attributeNum = static_cast<uint8_t>(vertexAttributeCount);
			vertexInput.streams = streams.data();
			vertexInput.streamNum = static_cast<uint8_t>(streamCount);
			pipelineDesc.vertexInput = &vertexInput;
		}

		// Rasterization.
		const nvrhi::RasterState& rasterState = desc.renderState.rasterState;
		pipelineDesc.rasterization.fillMode = rasterState.fillMode == nvrhi::RasterFillMode::Wireframe ? nri::FillMode::WIREFRAME : nri::FillMode::SOLID;
		pipelineDesc.rasterization.cullMode = rasterState.cullMode == nvrhi::RasterCullMode::Back ? nri::CullMode::BACK
			: rasterState.cullMode == nvrhi::RasterCullMode::Front ? nri::CullMode::FRONT
			: nri::CullMode::NONE;
		pipelineDesc.rasterization.frontCounterClockwise = rasterState.frontCounterClockwise;
		pipelineDesc.rasterization.conservativeRaster = rasterState.conservativeRasterEnable;
		// NVRHI's mesh pipelines take no shading-rate state (MeshletPipelineDesc has none).
		pipelineDesc.rasterization.shadingRate = desc.shadingRateState.enabled && !isMeshletPipeline;
		if (!problem && pipelineDesc.rasterization.shadingRate && RHIDevice::GetDesc().tiers.shadingRate == 0)
			problem = "it uses variable rate shading, which NRI reports as unsupported";
		// NVRHI enables depth bias only for a non-zero constant bias.
		if (rasterState.depthBias != 0)
		{
			pipelineDesc.rasterization.depthBias.constant = static_cast<float>(rasterState.depthBias);
			pipelineDesc.rasterization.depthBias.clamp = rasterState.depthBiasClamp;
			pipelineDesc.rasterization.depthBias.slope = rasterState.slopeScaledDepthBias;
		}

		// Multisampling, from the framebuffer as in NVRHI.
		const nvrhi::FramebufferInfo& framebufferInfo = framebuffer->getFramebufferInfo();
		const nvrhi::BlendState& blendState = desc.renderState.blendState;
		nri::MultisampleDesc multisample = {};
		if (framebufferInfo.sampleCount > 1 || blendState.alphaToCoverageEnable)
		{
			multisample.sampleMask = nri::ALL;
			multisample.sampleNum = static_cast<nri::Sample_t>(framebufferInfo.sampleCount);
			multisample.alphaToCoverage = blendState.alphaToCoverageEnable;
			pipelineDesc.multisample = &multisample;
		}

		// Output merger: one blend state per color attachment of the framebuffer.
		std::array<nri::ColorAttachmentDesc, nvrhi::c_MaxRenderTargets> colors = {};
		for (uint32_t i = 0; i < framebufferInfo.colorFormats.size(); i++)
		{
			const nvrhi::BlendState::RenderTarget& target = blendState.targets[i];
			nri::ColorAttachmentDesc& color = colors[i];
			color.format = ToNRIFormat(framebufferInfo.colorFormats[i]);
			color.blendEnabled = target.blendEnable;
			color.colorBlend = { Utils::GetNRIBlendFactor(target.srcBlend), Utils::GetNRIBlendFactor(target.destBlend), Utils::GetNRIBlendOp(target.blendOp) };
			color.alphaBlend = { Utils::GetNRIBlendFactor(target.srcBlendAlpha), Utils::GetNRIBlendFactor(target.destBlendAlpha), Utils::GetNRIBlendOp(target.blendOpAlpha) };
			color.colorWriteMask = Utils::GetNRIColorWriteMask(target.colorWriteMask);
			if (!problem && color.format == nri::Format::UNKNOWN)
				problem = "a color attachment format has no NRI equivalent";
		}
		pipelineDesc.outputMerger.colors = colors.data();
		pipelineDesc.outputMerger.colorNum = static_cast<uint32_t>(framebufferInfo.colorFormats.size());

		// NRI disables the depth test with CompareOp::NONE; stencil stays off (zeroed), as in NVRHI.
		const nvrhi::DepthStencilState& depthStencilState = desc.renderState.depthStencilState;
		if (!problem && depthStencilState.stencilEnable)
			problem = "it uses stencil state, which is not translated to NRI";
		pipelineDesc.outputMerger.depth.compareOp = depthStencilState.depthTestEnable ? Utils::GetNRICompareOp(depthStencilState.depthFunc) : nri::CompareOp::NONE;
		pipelineDesc.outputMerger.depth.write = depthStencilState.depthWriteEnable;
		if (framebufferInfo.depthFormat != nvrhi::Format::UNKNOWN)
		{
			pipelineDesc.outputMerger.depthStencilFormat = ToNRIFormat(framebufferInfo.depthFormat);
			if (!problem && pipelineDesc.outputMerger.depthStencilFormat == nri::Format::UNKNOWN)
				problem = "the depth attachment format has no NRI equivalent";
		}

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

	bool Pipeline::IsDynamicLineWidth() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_Specification.Topology == PrimitiveTopology::Lines || m_Specification.Topology == PrimitiveTopology::LineStrip || m_Specification.Wireframe;
	}

}
