#include "lpch.h"
#include "PipelineCompute.h"

#include "Lux/Platform/Vulkan/VulkanShader.h"

#include "Lux/Renderer/RendererAPI.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Image.h"

namespace Lux {

	namespace {
		const char* PipelineStageToString(PipelineStage stage)
		{
			switch (stage)
			{
				case PipelineStage::None: return "None";
				case PipelineStage::TopOfPipe: return "TopOfPipe";
				case PipelineStage::DrawIndirect: return "DrawIndirect";
				case PipelineStage::VertexInput: return "VertexInput";
				case PipelineStage::VertexShader: return "VertexShader";
				case PipelineStage::TesselationControlShader: return "TessControl";
				case PipelineStage::TesselationEvaluationShader: return "TessEvaluation";
				case PipelineStage::GeometryShader: return "GeometryShader";
				case PipelineStage::FragmentShader: return "FragmentShader";
				case PipelineStage::EarlyFragmentTests: return "EarlyFragmentTests";
				case PipelineStage::LateFragmentTests: return "LateFragmentTests";
				case PipelineStage::ColorAttachmentOutput: return "ColorAttachmentOutput";
				case PipelineStage::ComputeShader: return "ComputeShader";
				case PipelineStage::Transfer: return "Transfer";
				case PipelineStage::BottomOfPipe: return "BottomOfPipe";
				case PipelineStage::Host: return "Host";
				case PipelineStage::AllGraphics: return "AllGraphics";
				case PipelineStage::AllCommands: return "AllCommands";
			}

			return "UnknownStage";
		}

		void AppendAccessFlag(std::string& result, ResourceAccessFlags flags, ResourceAccessFlags flag, const char* name)
		{
			if ((static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) == 0)
				return;

			if (!result.empty())
				result += "|";
			result += name;
		}

		std::string ResourceAccessFlagsToString(ResourceAccessFlags flags)
		{
			if (flags == ResourceAccessFlags::None)
				return "None";

			std::string result;
			AppendAccessFlag(result, flags, ResourceAccessFlags::IndirectCommandRead, "IndirectRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::IndexRead, "IndexRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::VertexAttributeRead, "VertexRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::UniformRead, "UniformRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::InputAttachmentRead, "InputAttachmentRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::ShaderRead, "ShaderRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::ShaderWrite, "ShaderWrite");
			AppendAccessFlag(result, flags, ResourceAccessFlags::ColorAttachmentRead, "ColorRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::ColorAttachmentWrite, "ColorWrite");
			AppendAccessFlag(result, flags, ResourceAccessFlags::DepthStencilAttachmentRead, "DepthRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::DepthStencilAttachmentWrite, "DepthWrite");
			AppendAccessFlag(result, flags, ResourceAccessFlags::TransferRead, "TransferRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::TransferWrite, "TransferWrite");
			AppendAccessFlag(result, flags, ResourceAccessFlags::HostRead, "HostRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::HostWrite, "HostWrite");
			AppendAccessFlag(result, flags, ResourceAccessFlags::MemoryRead, "MemoryRead");
			AppendAccessFlag(result, flags, ResourceAccessFlags::MemoryWrite, "MemoryWrite");
			return result.empty() ? "UnknownAccess" : result;
		}

		std::string GetImageDebugName(const Ref<Image2D>& image)
		{
			if (!image)
				return "NullImage";

			const auto& spec = image->GetSpecification();
			return spec.DebugName.empty() ? "Image2D" : spec.DebugName;
		}

		std::string GetStorageBufferDebugName(const Ref<StorageBuffer>& storageBuffer)
		{
			if (!storageBuffer)
				return "NullStorageBuffer";

			const auto& spec = storageBuffer->GetSpecification();
			return spec.DebugName.empty() ? "StorageBuffer" : spec.DebugName;
		}

		std::string BuildBarrierMarkerName(const char* resourceType, const std::string& passName, const std::string& resourceName, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess)
		{
			return std::string("Barrier ") + resourceType + " [" + passName + "] " + resourceName + ": "
				+ PipelineStageToString(fromStage) + "/" + ResourceAccessFlagsToString(fromAccess)
				+ " -> " + PipelineStageToString(toStage) + "/" + ResourceAccessFlagsToString(toAccess);
		}
	}

	static nvrhi::ResourceStates MapAccessFlagsToResourceState(ResourceAccessFlags accessFlags)
	{
		nvrhi::ResourceStates state = nvrhi::ResourceStates::Unknown;

		if ((uint32_t)accessFlags & (uint32_t)ResourceAccessFlags::ShaderRead)
			state = state | nvrhi::ResourceStates::ShaderResource;
		if ((uint32_t)accessFlags & (uint32_t)ResourceAccessFlags::ShaderWrite)
			state = state | nvrhi::ResourceStates::UnorderedAccess;
		if ((uint32_t)accessFlags & (uint32_t)ResourceAccessFlags::TransferRead)
			state = state | nvrhi::ResourceStates::CopySource;
		if ((uint32_t)accessFlags & (uint32_t)ResourceAccessFlags::TransferWrite)
			state = state | nvrhi::ResourceStates::CopyDest;
		if ((uint32_t)accessFlags & (uint32_t)ResourceAccessFlags::ColorAttachmentWrite)
			state = state | nvrhi::ResourceStates::RenderTarget;
		if ((uint32_t)accessFlags & (uint32_t)ResourceAccessFlags::DepthStencilAttachmentRead)
			state = state | nvrhi::ResourceStates::DepthRead;
		if ((uint32_t)accessFlags & (uint32_t)ResourceAccessFlags::DepthStencilAttachmentWrite)
			state = state | nvrhi::ResourceStates::DepthWrite;

		// Default to ShaderResource if no specific flags matched
		if (state == nvrhi::ResourceStates::Unknown)
			state = nvrhi::ResourceStates::ShaderResource;

		return state;
	}

	PipelineCompute::PipelineCompute(Ref<Shader> computeShader)
		: m_Shader(computeShader)
	{
		// Ref<PipelineCompute> instance = this;
		// Renderer::Submit([instance]() mutable
		// {
		// 	instance->RT_CreatePipeline();
		// });

		RT_CreatePipeline();
		Renderer::RegisterShaderDependency(computeShader, this);
	}

	void PipelineCompute::RT_CreatePipeline()
	{
		LUX_CORE_INFO_TAG("Renderer", "[PipelineCompute] Creating compute pipeline: {}", m_Shader->GetName());

		nvrhi::ComputePipelineDesc desc;
		desc.CS = m_Shader->GetHandle();
		desc.bindingLayouts = m_Shader.As<VulkanShader>()->GetAllDescriptorSetLayouts();
		// TODO: binding layouts

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		m_Handle = device->createComputePipeline(desc);

		m_CommandList = RenderCommandBuffer::Create(1, "PipelineCompute");
	}

	void PipelineCompute::Begin(Ref<RenderCommandBuffer> renderCommandBuffer)
	{

	}

	void PipelineCompute::RT_Begin(Ref<RenderCommandBuffer> renderCommandBuffer)
	{

	}

	void PipelineCompute::End()
	{

	}

	void PipelineCompute::BufferMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<StorageBuffer> storageBuffer, ResourceAccessFlags fromAccess, ResourceAccessFlags toAccess)
	{
		BufferMemoryBarrier(renderCommandBuffer, storageBuffer, PipelineStage::ComputeShader, fromAccess, PipelineStage::AllCommands, toAccess);
	}

	void PipelineCompute::BufferMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<StorageBuffer> storageBuffer, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess)
	{
		const std::string markerName = BuildBarrierMarkerName("Buffer", m_Shader ? m_Shader->GetName() : "PipelineCompute", GetStorageBufferDebugName(storageBuffer), fromStage, fromAccess, toStage, toAccess);
		Renderer::Submit([renderCommandBuffer, storageBuffer, toAccess, markerName]() mutable
			{
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
				nvrhi::ResourceStates targetState = MapAccessFlagsToResourceState(toAccess);
				renderCommandBuffer->RT_BeginMarker(markerName);
				commandList->setBufferState(storageBuffer->GetHandle(), targetState);
				commandList->commitBarriers();
				renderCommandBuffer->RT_EndMarker();
			});
	}

	void PipelineCompute::ImageMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, ResourceAccessFlags fromAccess, ResourceAccessFlags toAccess)
	{
		ImageMemoryBarrier(renderCommandBuffer, image, PipelineStage::ComputeShader, fromAccess, PipelineStage::AllCommands, toAccess);
	}

	void PipelineCompute::ImageMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess)
	{
		const std::string markerName = BuildBarrierMarkerName("Image", m_Shader ? m_Shader->GetName() : "PipelineCompute", GetImageDebugName(image), fromStage, fromAccess, toStage, toAccess);
		Renderer::Submit([renderCommandBuffer, image, toAccess, markerName]() mutable
			{
				nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
				nvrhi::ResourceStates targetState = MapAccessFlagsToResourceState(toAccess);
				renderCommandBuffer->RT_BeginMarker(markerName);
				commandList->setTextureState(image->GetHandle(), nvrhi::AllSubresources, targetState);
				commandList->commitBarriers();
				renderCommandBuffer->RT_EndMarker();
			});
	}

	void PipelineCompute::Execute(void* descriptorSets, uint32_t descriptorSetCount, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
	{
		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();

		nvrhi::ComputeState computeState;
		computeState.pipeline = m_Handle;

		m_CommandList->RT_Begin();
		m_CommandList->GetActive()->setComputeState(computeState);
		m_CommandList->GetActive()->dispatch(groupCountX, groupCountY, groupCountZ);

		m_CommandList->RT_End();
		m_CommandList->RT_Submit();
	}

	void PipelineCompute::SetPushConstants(Buffer constants) const
	{

	}

	void PipelineCompute::CreatePipeline()
	{
		Ref<PipelineCompute> instance = this;
		Renderer::Submit([instance]() mutable
			{
				instance->RT_CreatePipeline();
			});
	}

}
