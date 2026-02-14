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
		return m_DescriptorSetManager.IsInvalidated(set, binding);
	}

	void RenderPass::SetInput(std::string_view name, Ref<UniformBufferSet> uniformBufferSet)
	{
		m_DescriptorSetManager.SetInput(name, uniformBufferSet);
	}

	void RenderPass::SetInput(std::string_view name, Ref<UniformBuffer> uniformBuffer)
	{
		m_DescriptorSetManager.SetInput(name, uniformBuffer);
	}

	void RenderPass::SetInput(std::string_view name, Ref<StorageBufferSet> storageBufferSet)
	{
		m_DescriptorSetManager.SetInput(name, storageBufferSet);
	}

	void RenderPass::SetInput(std::string_view name, Ref<StorageBuffer> storageBuffer)
	{
		m_DescriptorSetManager.SetInput(name, storageBuffer);
	}

	void RenderPass::SetInput(std::string_view name, Ref<Texture2D> texture)
	{
		m_DescriptorSetManager.SetInput(name, texture);
	}

	void RenderPass::SetInput(std::string_view name, Ref<Texture2D> texture, uint32_t arrayIndex)
	{
		m_DescriptorSetManager.SetInput(name, texture, arrayIndex);
	}

	void RenderPass::SetInput(std::string_view name, Ref<TextureCube> textureCube)
	{
		m_DescriptorSetManager.SetInput(name, textureCube);
	}

	void RenderPass::SetInput(std::string_view name, Ref<Image2D> image)
	{
		m_DescriptorSetManager.SetInput(name, image);
	}

	void RenderPass::SetInput(std::string_view name, Ref<Sampler> sampler)
	{
		m_DescriptorSetManager.SetInput(name, sampler);
	}

	Ref<Image2D> RenderPass::GetOutput(uint32_t index)
	{
		Ref<Framebuffer> framebuffer = m_Specification.Pipeline->GetSpecification().TargetFramebuffer;
		if (index > framebuffer->GetColorAttachmentCount() + 1)
			return nullptr; // Invalid index
		if (index < framebuffer->GetColorAttachmentCount())
			return framebuffer->GetImage(index);
		return framebuffer->GetDepthImage();
	}

	Ref<Image2D> RenderPass::GetDepthOutput()
	{
		Ref<Framebuffer> framebuffer = m_Specification.Pipeline->GetSpecification().TargetFramebuffer;
		if (!framebuffer->HasDepthAttachment())
			return nullptr; // No depth output
		return framebuffer->GetDepthImage();
	}

	uint32_t RenderPass::GetFirstSetIndex() const
	{
		return m_DescriptorSetManager.GetFirstSetIndex();
	}

	Ref<Framebuffer> RenderPass::GetTargetFramebuffer() const
	{
		return m_Specification.Pipeline->GetSpecification().TargetFramebuffer;
	}

	Ref<Pipeline> RenderPass::GetPipeline() const
	{
		return m_Specification.Pipeline;
	}

	bool RenderPass::Validate()
	{
		return m_DescriptorSetManager.Validate();
	}

	void RenderPass::Bake()
	{
		m_DescriptorSetManager.Bake();
	}

	void RenderPass::Prepare()
	{
		m_DescriptorSetManager.InvalidateAndUpdate();
	}

	bool RenderPass::HasDescriptorSets() const
	{
		return m_DescriptorSetManager.HasDescriptorSets();
	}

	bool RenderPass::IsInputValid(std::string_view name) const
	{
		std::string nameStr(name);
		return m_DescriptorSetManager.InputDeclarations.find(nameStr) != m_DescriptorSetManager.InputDeclarations.end();
	}

	const RenderInputDeclaration* RenderPass::GetInputDeclaration(std::string_view name) const
	{
		std::string nameStr(name);
		if (m_DescriptorSetManager.InputDeclarations.find(nameStr) == m_DescriptorSetManager.InputDeclarations.end())
			return nullptr;
		const RenderInputDeclaration& decl = m_DescriptorSetManager.InputDeclarations.at(nameStr);
		return &decl;
	}

}
