#include "sepch.h"
#include "PipelineCompute.h"

#include "StarEngine/Renderer/RendererAPI.h"
#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Renderer/Image.h"

namespace StarEngine {

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
		SE_CORE_INFO_TAG("Renderer", "[PipelineCompute] Creating compute pipeline: {}", m_Shader->GetName());
		
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
		Renderer::Submit([renderCommandBuffer, storageBuffer, toAccess]() mutable
		{
			nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
			nvrhi::ResourceStates targetState = MapAccessFlagsToResourceState(toAccess);
			commandList->setBufferState(storageBuffer->GetHandle(), targetState);			
		});
	}

	void PipelineCompute::ImageMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, ResourceAccessFlags fromAccess, ResourceAccessFlags toAccess)
	{
		ImageMemoryBarrier(renderCommandBuffer, image, PipelineStage::ComputeShader, fromAccess, PipelineStage::AllCommands, toAccess);
	}

	void PipelineCompute::ImageMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess)
	{
		Renderer::Submit([renderCommandBuffer, image, toAccess]() mutable
		{
			nvrhi::CommandListHandle commandList = renderCommandBuffer->GetActive();
			nvrhi::ResourceStates targetState = MapAccessFlagsToResourceState(toAccess);
			commandList->setTextureState(image->GetHandle(), nvrhi::AllSubresources, targetState);
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
