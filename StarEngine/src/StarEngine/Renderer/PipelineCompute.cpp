#include "sepch.h"
#include "PipelineCompute.h"

#include "StarEngine/Renderer/RendererAPI.h"
#include "StarEngine/Renderer/Renderer.h"

namespace StarEngine {

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
		desc.bindingLayouts = m_Shader.As<Shader>()->GetAllDescriptorSetLayouts();
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

	}

	void PipelineCompute::BufferMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<StorageBuffer> storageBuffer, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess)
	{

	}

	void PipelineCompute::ImageMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, ResourceAccessFlags fromAccess, ResourceAccessFlags toAccess)
	{

	}

	void PipelineCompute::ImageMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess)
	{

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

	}

}
