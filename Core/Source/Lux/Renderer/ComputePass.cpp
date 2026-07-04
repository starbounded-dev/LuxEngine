#include "lpch.h"
#include "ComputePass.h"

#include "Renderer.h"

#include "Lux/Renderer/RendererAPI.h"

namespace Lux {

	ComputePass::ComputePass(const ComputePassSpecification& spec)
		: m_Specification(spec)
	{
		LUX_CORE_VERIFY(spec.Pipeline);

		DescriptorSetManagerSpecification dmSpec;
		dmSpec.DebugName = spec.DebugName;
		dmSpec.Shader = spec.Pipeline->GetShader().As<VulkanShader>();
		dmSpec.StartSet = 1;
		m_DescriptorSetManager = DescriptorSetManager(dmSpec);

		Renderer::RegisterShaderDependency(spec.Pipeline->GetShader(), this);
	}

	void ComputePass::OnShaderReloaded()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.OnShaderReloaded();
	}

	bool ComputePass::IsInvalidated(uint32_t set, uint32_t binding) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_DescriptorSetManager.IsInvalidated(set, binding);
	}

	void ComputePass::SetInput(std::string_view name, Ref<UniformBufferSet> uniformBufferSet)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, uniformBufferSet);
	}

	void ComputePass::SetInput(std::string_view name, Ref<UniformBuffer> uniformBuffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, uniformBuffer);
	}

	void ComputePass::SetInput(std::string_view name, Ref<StorageBufferSet> storageBufferSet)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, storageBufferSet);
	}

	void ComputePass::SetInput(std::string_view name, Ref<StorageBuffer> storageBuffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, storageBuffer);
	}

	void ComputePass::SetInput(std::string_view name, Ref<Texture2D> texture)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, texture);
	}

	void ComputePass::SetInput(std::string_view name, Ref<TextureCube> textureCube)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, textureCube);
	}

	void ComputePass::SetInput(std::string_view name, Ref<Image2D> image)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, image);
	}

	void ComputePass::SetInput(std::string_view name, Ref<Sampler> sampler)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, sampler);
	}

	Ref<Image2D> ComputePass::GetOutput(uint32_t index)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(false, "Not implemented");
		return nullptr;
	}

	Ref<Image2D> ComputePass::GetDepthOutput()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(false, "Not implemented");
		return nullptr;
	}

	bool ComputePass::HasDescriptorSets() const
	{
		return m_DescriptorSetManager.HasDescriptorSets();
	}

	uint32_t ComputePass::GetFirstSetIndex() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_DescriptorSetManager.GetFirstSetIndex();
	}

	bool ComputePass::Validate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_DescriptorSetManager.Validate();
	}

	void ComputePass::Bake()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.Bake();
	}

	void ComputePass::Prepare()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.InvalidateAndUpdate();
	}

	Ref<PipelineCompute> ComputePass::GetPipeline() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
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
