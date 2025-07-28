#pragma once

#include "StarEngine/Core/Base.h"

#include "StarEngine/Platform/Vulkan/DescriptorSetManager.h"
#include "PipelineCompute.h"

namespace StarEngine {

	struct ComputePassSpecification
	{
		Ref<PipelineCompute> Pipeline;
		std::string DebugName;
		glm::vec4 MarkerColor;
	};

	class ComputePass : public RefCounted
	{
	public:
		static Ref<ComputePass> Create(const ComputePassSpecification& specification) { return Ref<ComputePass>::Create(specification); }

		ComputePassSpecification& GetSpecification() { return m_Specification; }
		const ComputePassSpecification& GetSpecification() const { return m_Specification; }

		Ref<Shader> GetShader() const { return m_Specification.Pipeline->GetShader(); }

		void SetInput(std::string_view name, Ref<UniformBufferSet> uniformBufferSet);
		void SetInput(std::string_view name, Ref<UniformBuffer> uniformBuffer);

		void SetInput(std::string_view name, Ref<StorageBufferSet> storageBufferSet);
		void SetInput(std::string_view name, Ref<StorageBuffer> storageBuffer);

		void SetInput(std::string_view name, Ref<Texture2D> texture);
		void SetInput(std::string_view name, Ref<TextureCube> textureCube);
		void SetInput(std::string_view name, Ref<Image2D> image);

		Ref<Image2D> GetOutput(uint32_t index);
		Ref<Image2D> GetDepthOutput();
		bool HasDescriptorSets() const;
		uint32_t GetFirstSetIndex() const;

		bool Validate();
		void Bake();
		void Prepare();

		const nvrhi::BindingSetVector& GetBindingSets(uint32_t frameIndex) const { return m_DescriptorSetManager.GetBindingSets(frameIndex); }

		virtual Ref<PipelineCompute> GetPipeline() const;

		bool IsInputValid(std::string_view name) const;
		const RenderInputDeclaration* GetInputDeclaration(std::string_view name) const;
	public:
		ComputePass(const ComputePassSpecification& spec);
		virtual ~ComputePass() = default;
	private:
		bool IsInvalidated(uint32_t set, uint32_t binding) const;
	private:
		ComputePassSpecification m_Specification;
		DescriptorSetManager m_DescriptorSetManager;
	};

}
