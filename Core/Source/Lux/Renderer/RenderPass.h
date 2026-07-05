#pragma once

#include "Lux/Core/Base.h"

#include "Lux/Platform/Vulkan/DescriptorSetManager.h"

#include "Pipeline.h"

namespace Lux {

	struct RenderPassSpecification
	{
		Ref<Pipeline> Pipeline;
		std::string DebugName;
		glm::vec4 MarkerColor;
		uint32_t StartSet = 1;  // Descriptor set index to start managing from (0 for full control, 1 to leave set 0 for materials)
	};

	class RenderPass : public RefCounted
	{
	public:
		static Ref<RenderPass> Create(const RenderPassSpecification& specification) { return Ref<RenderPass>::Create(specification); }

		RenderPassSpecification& GetSpecification() { return m_Specification; }
		const RenderPassSpecification& GetSpecification() const { return m_Specification; }

		void SetInput(std::string_view name, Ref<UniformBufferSet> uniformBufferSet);
		void SetInput(std::string_view name, Ref<UniformBuffer> uniformBuffer);

		void SetInput(std::string_view name, Ref<StorageBufferSet> storageBufferSet);
		void SetInput(std::string_view name, Ref<StorageBuffer> storageBuffer);

		void SetInput(std::string_view name, Ref<Texture2D> texture);
		void SetInput(std::string_view name, Ref<Texture2D> texture, uint32_t arrayIndex);
		void SetInput(std::string_view name, Ref<TextureCube> textureCube);
		void SetInput(std::string_view name, Ref<Image2D> image);
		void SetInput(std::string_view name, Ref<Sampler> sampler);

		Ref<Image2D> GetOutput(uint32_t index);
		Ref<Image2D> GetDepthOutput();
		uint32_t GetFirstSetIndex() const;

		Ref<Framebuffer> GetTargetFramebuffer() const;
		Ref<Pipeline> GetPipeline() const;

		bool Validate();
		void Bake();
		void Prepare();
		// Called by the Renderer after this pass's shader is recompiled in
		// place; re-bakes the descriptor sets against the new binding layouts.
		void OnShaderReloaded();

		bool HasDescriptorSets() const;
		uint32_t GetBindingSetCount() const { return m_DescriptorSetManager.GetBindingSetCount(); }
		nvrhi::BindingSetVector GetBindingSets(uint32_t frameIndex) const { return m_DescriptorSetManager.GetBindingSets(frameIndex); }

		bool IsInputValid(std::string_view name) const;
		const RenderInputDeclaration* GetInputDeclaration(std::string_view name) const;
	public:
		RenderPass(const RenderPassSpecification& spec);
		virtual ~RenderPass() = default;
	private:
		bool IsInvalidated(uint32_t set, uint32_t binding) const;
	private:
		RenderPassSpecification m_Specification;
		DescriptorSetManager m_DescriptorSetManager;
	};

}
