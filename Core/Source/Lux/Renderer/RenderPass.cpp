#include "lpch.h"
#include "RenderPass.h"

#include "Renderer.h"

#include "Lux/Renderer/RendererAPI.h"

namespace Lux {

	RenderPass::RenderPass(const RenderPassSpecification& spec)
		: m_Specification(spec)
	{
		LUX_CORE_VERIFY(spec.Pipeline);

		DescriptorSetManagerSpecification dmSpec;
		dmSpec.DebugName = spec.DebugName;
		dmSpec.Shader = spec.Pipeline->GetSpecification().Shader.As<VulkanShader>();
		dmSpec.StartSet = spec.StartSet;
		m_DescriptorSetManager = DescriptorSetManager(dmSpec);
	}

	bool RenderPass::IsInvalidated(uint32_t set, uint32_t binding) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_DescriptorSetManager.IsInvalidated(set, binding);
	}

	void RenderPass::SetInput(std::string_view name, Ref<UniformBufferSet> uniformBufferSet)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, uniformBufferSet);
	}

	void RenderPass::SetInput(std::string_view name, Ref<UniformBuffer> uniformBuffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, uniformBuffer);
	}

	void RenderPass::SetInput(std::string_view name, Ref<StorageBufferSet> storageBufferSet)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, storageBufferSet);
	}

	void RenderPass::SetInput(std::string_view name, Ref<StorageBuffer> storageBuffer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, storageBuffer);
	}

	void RenderPass::SetInput(std::string_view name, Ref<Texture2D> texture)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, texture);
	}

	void RenderPass::SetInput(std::string_view name, Ref<Texture2D> texture, uint32_t arrayIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, texture, arrayIndex);
	}

	void RenderPass::SetInput(std::string_view name, Ref<TextureCube> textureCube)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, textureCube);
	}

	void RenderPass::SetInput(std::string_view name, Ref<Image2D> image)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, image);
	}

	void RenderPass::SetInput(std::string_view name, Ref<Sampler> sampler)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, sampler);
	}

	Ref<Image2D> RenderPass::GetOutput(uint32_t index)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref<Framebuffer> framebuffer = m_Specification.Pipeline->GetSpecification().TargetFramebuffer;
		if (index > framebuffer->GetColorAttachmentCount() + 1)
			return nullptr; // Invalid index
		if (index < framebuffer->GetColorAttachmentCount())
			return framebuffer->GetImage(index);
		return framebuffer->GetDepthImage();
	}

	Ref<Image2D> RenderPass::GetDepthOutput()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref<Framebuffer> framebuffer = m_Specification.Pipeline->GetSpecification().TargetFramebuffer;
		if (!framebuffer->HasDepthAttachment())
			return nullptr; // No depth output
		return framebuffer->GetDepthImage();
	}

	uint32_t RenderPass::GetFirstSetIndex() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_DescriptorSetManager.GetFirstSetIndex();
	}

	Ref<Framebuffer> RenderPass::GetTargetFramebuffer() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_Specification.Pipeline->GetSpecification().TargetFramebuffer;
	}

	Ref<Pipeline> RenderPass::GetPipeline() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_Specification.Pipeline;
	}

	bool RenderPass::Validate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_DescriptorSetManager.Validate();
	}

	void RenderPass::Bake()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.Bake();
	}

	void RenderPass::Prepare()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.InvalidateAndUpdate();
	}

	bool RenderPass::HasDescriptorSets() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_DescriptorSetManager.HasDescriptorSets();
	}

	bool RenderPass::IsInputValid(std::string_view name) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::string nameStr(name);
		return m_DescriptorSetManager.InputDeclarations.find(nameStr) != m_DescriptorSetManager.InputDeclarations.end();
	}

	const RenderInputDeclaration* RenderPass::GetInputDeclaration(std::string_view name) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::string nameStr(name);
		if (m_DescriptorSetManager.InputDeclarations.find(nameStr) == m_DescriptorSetManager.InputDeclarations.end())
			return nullptr;
		const RenderInputDeclaration& decl = m_DescriptorSetManager.InputDeclarations.at(nameStr);
		return &decl;
	}

}
