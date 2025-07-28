#include "sepch.h"
#include "ComputePass.h"

#include "Renderer.h"

#include "StarEngine/Renderer/RendererAPI.h"

namespace StarEngine {

	ComputePass::ComputePass(const ComputePassSpecification& spec)
		: m_Specification(spec)
	{
		SE_CORE_VERIFY(spec.Pipeline);

		DescriptorSetManagerSpecification dmSpec;
		dmSpec.DebugName = spec.DebugName;
		dmSpec.Shader = spec.Pipeline->GetShader().As<Shader>();
		dmSpec.StartSet = 1;
		m_DescriptorSetManager = DescriptorSetManager(dmSpec);
	}

	bool ComputePass::IsInvalidated(uint32_t set, uint32_t binding) const
	{
		return m_DescriptorSetManager.IsInvalidated(set, binding);
	}

	void ComputePass::SetInput(std::string_view name, Ref<UniformBufferSet> uniformBufferSet)
	{
		m_DescriptorSetManager.SetInput(name, uniformBufferSet);
	}

	void ComputePass::SetInput(std::string_view name, Ref<UniformBuffer> uniformBuffer)
	{
		m_DescriptorSetManager.SetInput(name, uniformBuffer);
	}

	void ComputePass::SetInput(std::string_view name, Ref<StorageBufferSet> storageBufferSet)
	{
		m_DescriptorSetManager.SetInput(name, storageBufferSet);
	}

	void ComputePass::SetInput(std::string_view name, Ref<StorageBuffer> storageBuffer)
	{
		m_DescriptorSetManager.SetInput(name, storageBuffer);
	}

	void ComputePass::SetInput(std::string_view name, Ref<Texture2D> texture)
	{
		m_DescriptorSetManager.SetInput(name, texture);
	}

	void ComputePass::SetInput(std::string_view name, Ref<TextureCube> textureCube)
	{
		m_DescriptorSetManager.SetInput(name, textureCube);
	}

	void ComputePass::SetInput(std::string_view name, Ref<Image2D> image)
	{
		m_DescriptorSetManager.SetInput(name, image);
	}

	Ref<Image2D> ComputePass::GetOutput(uint32_t index)
	{
		SE_CORE_VERIFY(false, "Not implemented");
		return nullptr;
	}

	Ref<Image2D> ComputePass::GetDepthOutput()
	{
		SE_CORE_VERIFY(false, "Not implemented");
		return nullptr;
	}

	bool ComputePass::HasDescriptorSets() const
	{
		return m_DescriptorSetManager.HasDescriptorSets();
	}

	uint32_t ComputePass::GetFirstSetIndex() const
	{
		return m_DescriptorSetManager.GetFirstSetIndex();
	}

	bool ComputePass::Validate()
	{
		return m_DescriptorSetManager.Validate();
	}

	void ComputePass::Bake()
	{
		m_DescriptorSetManager.Bake();
	}

	void ComputePass::Prepare()
	{
		m_DescriptorSetManager.InvalidateAndUpdate();
	}

	Ref<PipelineCompute> ComputePass::GetPipeline() const
	{
		return m_Specification.Pipeline;
	}

	bool ComputePass::IsInputValid(std::string_view name) const
	{
		std::string nameStr(name);
		return m_DescriptorSetManager.InputDeclarations.find(nameStr) != m_DescriptorSetManager.InputDeclarations.end();
	}

	const RenderInputDeclaration* ComputePass::GetInputDeclaration(std::string_view name) const
	{
		return m_DescriptorSetManager.GetInputDeclaration(name);
	}

}
