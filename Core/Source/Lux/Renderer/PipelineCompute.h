// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Renderer/Shader.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/StorageBuffer.h"
#include "Lux/Renderer/StorageBufferSet.h"

namespace nri {
	struct Pipeline;
}

namespace Lux {

	class PipelineCompute : public RefCounted
	{
	public:
		static Ref<PipelineCompute> Create(Ref<Shader> computeShader) { return Ref<PipelineCompute>::Create(computeShader); }

		void BufferMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<StorageBuffer> storageBuffer, ResourceAccessFlags fromAccess, ResourceAccessFlags toAccess);
		void BufferMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<StorageBuffer> storageBuffer, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess);

		// Resolve frame-indexed buffers on the render thread, matching the descriptors.
		void BufferMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<StorageBufferSet> storageBuffers, ResourceAccessFlags fromAccess, ResourceAccessFlags toAccess);
		void BufferMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<StorageBufferSet> storageBuffers, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess);

		void ImageMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, ResourceAccessFlags fromAccess, ResourceAccessFlags toAccess);
		void ImageMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<Image2D> image, PipelineStage fromStage, ResourceAccessFlags fromAccess, PipelineStage toStage, ResourceAccessFlags toAccess);

		void CreatePipeline();

		nvrhi::ComputePipelineHandle GetHandle() const { return m_Handle; }
		// The NRI compute pipeline over the shader's NRI layout (NRI migration Phase 9), created and
		// recreated with the NVRHI pipeline. Null if NRI rejected it.
		nri::Pipeline* GetNRIPipeline() const { return m_NRIPipeline; }
		Ref<Shader> GetShader() const { return m_Shader; }
	public:
		PipelineCompute(Ref<Shader> computeShader);
		~PipelineCompute();
	private:
		void RT_CreatePipeline();
		static void RT_BufferMemoryBarrier(Ref<RenderCommandBuffer> renderCommandBuffer, Ref<StorageBuffer> storageBuffer, ResourceAccessFlags toAccess, const std::string& markerName);
		void CreateNRIPipeline();
		void ReleaseNRIPipeline();
	private:
		Ref<Shader> m_Shader;
		nvrhi::ComputePipelineHandle m_Handle = nullptr;
		nri::Pipeline* m_NRIPipeline = nullptr;
	};

}
