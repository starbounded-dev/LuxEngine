#include "lpch.h"
#include "Material.h"

#include "Lux/Renderer/RendererAPI.h"
#include "Lux/Renderer/Renderer.h"

#include "Lux/Platform/Vulkan/DescriptorSetManager.h"

namespace Lux {

	namespace {
		static const nvrhi::BindingLayoutDesc* GetDescriptorSetLayoutDesc(Ref<Shader> shader, uint32_t set)
		{
			if (!shader)
				return nullptr;

			Ref<VulkanShader> vulkanShader = shader.As<VulkanShader>();
			const auto& descriptorSets = vulkanShader->GetShaderDescriptorSets();
			if (set >= descriptorSets.size() || !descriptorSets[set])
				return nullptr;

			nvrhi::BindingLayoutHandle layout = vulkanShader->GetDescriptorSetLayout(set);
			return layout ? layout->getDesc() : nullptr;
		}

		static bool BindingLayoutsMatch(const nvrhi::BindingLayoutDesc& lhs, const nvrhi::BindingLayoutDesc& rhs)
		{
			if (lhs.bindings.size() != rhs.bindings.size())
				return false;

			for (const nvrhi::BindingLayoutItem& lhsItem : lhs.bindings)
			{
				bool found = false;
				for (const nvrhi::BindingLayoutItem& rhsItem : rhs.bindings)
				{
					if (lhsItem == rhsItem)
					{
						found = true;
						break;
					}
				}

				if (!found)
					return false;
			}

			return true;
		}
	}

	Material::Material(Ref<Shader> shader, const std::string& name)
		: m_Shader(shader), m_Name(name)
	{
		Init();
		Renderer::RegisterShaderDependency(shader, this);
	}

	Material::Material(Ref<Material> other, const std::string& name)
		: m_Shader(other->GetShader()), m_Name(name)
	{
		if (name.empty())
			m_Name = other->GetName();

		Init();
		Renderer::RegisterShaderDependency(m_Shader, this);

		m_UniformStorageBuffer = Buffer::Copy(other->m_UniformStorageBuffer.Data, other->m_UniformStorageBuffer.Size);
		m_DescriptorSetManager = DescriptorSetManager::Copy(other->m_DescriptorSetManager);
	}

	void Material::Init()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		AllocateStorage();

		m_MaterialFlags |= (uint32_t)MaterialFlag::DepthTest;
		m_MaterialFlags |= (uint32_t)MaterialFlag::Blend;

		DescriptorSetManagerSpecification dmSpec;
		dmSpec.DebugName = m_Name.empty() ? std::format("{} (Material)", m_Shader->GetName()) : m_Name;
		dmSpec.Shader = m_Shader.As<VulkanShader>();
		dmSpec.StartSet = 0;
		dmSpec.EndSet = 0;
		dmSpec.IsDynamic = false;
		dmSpec.DefaultResources = true;
		m_DescriptorSetManager = DescriptorSetManager(dmSpec);

		// TODO(Yan): I don't think we need this
#if 0
		for (const auto& [name, decl] : m_DescriptorSetManager.InputDeclarations)
		{
			switch (decl.Type)
			{
			case RenderInputType::ImageSampler:
			{
				m_DescriptorSetManager.SetInput(name, Renderer::GetDefaultSampler());
				break;
			}
			case RenderInputType::ImageSampler1D:
			case RenderInputType::ImageSampler2D:
			{
				for (uint32_t i = 0; i < decl.Count; i++)
					m_DescriptorSetManager.SetInput(name, Renderer::GetWhiteTexture(), i);
				break;
			}
			case RenderInputType::ImageSampler3D:
			{
				m_DescriptorSetManager.SetInput(name, Renderer::GetBlackCubeTexture());
				break;
			}
			}
		}

		HZ_CORE_VERIFY(m_DescriptorSetManager.Validate());
		m_DescriptorSetManager.Bake();
#endif
	}

	void Material::AllocateStorage()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const auto& shaderBuffers = m_Shader->GetShaderBuffers();

		if (shaderBuffers.size() > 0)
		{
			uint32_t size = 0;
			for (auto [name, shaderBuffer] : shaderBuffers)
				size += shaderBuffer.Size;

			m_UniformStorageBuffer.Allocate(size);
			m_UniformStorageBuffer.ZeroInitialize();
		}
	}

	void Material::Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;

	}

	void Material::OnShaderReloaded()
	{
		LUX_PROFILE_FUNCTION_AUTO;

	}

	const ShaderUniform* Material::FindUniformDeclaration(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const auto& shaderBuffers = m_Shader->GetShaderBuffers();

		LUX_CORE_ASSERT(shaderBuffers.size() <= 1, "We currently only support ONE material buffer!");

		if (shaderBuffers.size() > 0)
		{
			const ShaderBuffer& buffer = (*shaderBuffers.begin()).second;
			if (buffer.Uniforms.find(name) == buffer.Uniforms.end())
				return nullptr;

			return &buffer.Uniforms.at(name);
		}
		return nullptr;
	}

	const ShaderResourceDeclaration* Material::FindResourceDeclaration(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		auto& resources = m_Shader->GetResources();
		if (resources.find(name) != resources.end())
			return &resources.at(name);

		return nullptr;
	}

	void Material::Set(const std::string& name, float value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<float>(name, value);
	}

	void Material::Set(const std::string& name, int value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<int>(name, value);
	}

	void Material::Set(const std::string& name, uint32_t value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<uint32_t>(name, value);
	}

	void Material::Set(const std::string& name, bool value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// Bools are 4-byte ints
		Set<int>(name, (int)value);
	}

	void Material::Set(const std::string& name, const glm::ivec2& value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<glm::ivec2>(name, value);
	}

	void Material::Set(const std::string& name, const glm::ivec3& value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<glm::ivec3>(name, value);
	}

	void Material::Set(const std::string& name, const glm::ivec4& value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<glm::ivec4>(name, value);
	}

	void Material::Set(const std::string& name, const glm::vec2& value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<glm::vec2>(name, value);
	}

	void Material::Set(const std::string& name, const glm::vec3& value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<glm::vec3>(name, value);
	}

	void Material::Set(const std::string& name, const glm::vec4& value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<glm::vec4>(name, value);
	}

	void Material::Set(const std::string& name, const glm::mat3& value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<glm::mat3>(name, value);
	}

	void Material::Set(const std::string& name, const glm::mat4& value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Set<glm::mat4>(name, value);
	}

	void Material::Set(const std::string& name, Ref<Texture2D> texture)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, texture);
	}

	void Material::Set(const std::string& name, Ref<Texture2D> texture, uint32_t arrayIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, texture, arrayIndex);
	}

	void Material::Set(const std::string& name, Ref<TextureCube> texture)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, texture);
	}

	void Material::Set(const std::string& name, Ref<Image2D> image)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, image);
	}

	void Material::Set(const std::string& name, Ref<Image2D> image, uint32_t arrayIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, image, arrayIndex);
	}

	void Material::Set(const std::string& name, Ref<ImageView> image)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, image);
	}

	void Material::Set(const std::string& name, Ref<ImageView> image, uint32_t arrayIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.SetInput(name, image, arrayIndex);
	}

	float& Material::GetFloat(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Get<float>(name);
	}

	int32_t& Material::GetInt(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Get<int32_t>(name);
	}

	uint32_t& Material::GetUInt(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Get<uint32_t>(name);
	}

	bool& Material::GetBool(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Get<bool>(name);
	}

	glm::vec2& Material::GetVector2(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Get<glm::vec2>(name);
	}

	glm::vec3& Material::GetVector3(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Get<glm::vec3>(name);
	}

	glm::vec4& Material::GetVector4(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Get<glm::vec4>(name);
	}

	glm::mat3& Material::GetMatrix3(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Get<glm::mat3>(name);
	}

	glm::mat4& Material::GetMatrix4(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Get<glm::mat4>(name);
	}

	Ref<Texture2D> Material::GetTexture2D(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return GetResource<Texture2D>(name);
	}

	Ref<TextureCube> Material::TryGetTextureCube(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return TryGetResource<TextureCube>(name);
	}

	Ref<Texture2D> Material::TryGetTexture2D(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return TryGetResource<Texture2D>(name);
	}

	Ref<TextureCube> Material::GetTextureCube(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return GetResource<TextureCube>(name);
	}

	bool Material::IsDescriptorSetCompatible(Ref<Shader> pipelineShader, uint32_t set) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const nvrhi::BindingLayoutDesc* materialLayout = GetDescriptorSetLayoutDesc(m_Shader, set);
		const nvrhi::BindingLayoutDesc* pipelineLayout = GetDescriptorSetLayoutDesc(pipelineShader, set);
		if (!materialLayout || !pipelineLayout)
			return false;

		return BindingLayoutsMatch(*materialLayout, *pipelineLayout);
	}

	void Material::Prepare()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_DescriptorSetManager.InvalidateAndUpdate();
	}

	nvrhi::BindingSetHandle Material::GetBindingSet(uint32_t frameIndex) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_DescriptorSetManager.GetBindingSet(frameIndex);
	}

}
