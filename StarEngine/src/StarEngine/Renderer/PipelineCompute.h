#pragma once

#include "StarEngine/Core/Base.h"
#include "StarEngine/Renderer/Shader.h"
#include "StarEngine/Renderer/RenderCommandBuffer.h"
#include "StarEngine/Renderer/StorageBuffer.h"

namespace StarEngine {

	class PipelineCompute : public RefCounted
	{
	public:
		static Ref<PipelineCompute> Create(Ref<Shader> computeShader) { return Ref<PipelineCompute>::Create(computeShader); }

		void Begin(Ref<RenderCommandBuffer> renderCommandBuffer = nullptr);
		void RT_Begin(Ref<RenderCommandBuffer> renderCommandBuffer = nullptr);
		void End();

		void BufferMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<StorageBuffer> storageBuffer, ResourceAccessFlags fromAccess, ResourceAccessFlags toAccess);
		void BufferMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<StorageBuffer> storageBuffer, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess);

		void ImageMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, ResourceAccessFlags fromAccess, ResourceAccessFlags toAccess);
		void ImageMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess);
		
		void Execute(void* descriptorSets, uint32_t descriptorSetCount, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

		void SetPushConstants(Buffer constants) const;
		void CreatePipeline();

		nvrhi::ComputePipelineHandle GetHandle() const { return m_Handle; }
		Ref<Shader> GetShader() const { return m_Shader; }
	public:
		PipelineCompute(Ref<Shader> computeShader);
	private:
		void RT_CreatePipeline();
	private:
		Ref<Shader> m_Shader;
		nvrhi::ComputePipelineHandle m_Handle = nullptr;

		Ref<RenderCommandBuffer> m_CommandList;

		bool m_UsingGraphicsQueue = false;
	};

}
