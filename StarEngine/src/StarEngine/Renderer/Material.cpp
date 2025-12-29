#include "sepch.h"
#include "Material.h"

#include "StarEngine/Renderer/RendererAPI.h"
#include "StarEngine/Renderer/Renderer.h"

#include "StarEngine/Platform/Vulkan/DescriptorSetManager.h"

namespace StarEngine {

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

	}

	void Material::OnShaderReloaded()
	{

	}

	const ShaderUniform* Material::FindUniformDeclaration(const std::string& name)
	{
		const auto& shaderBuffers = m_Shader->GetShaderBuffers();

		SE_CORE_ASSERT(shaderBuffers.size() <= 1, "We currently only support ONE material buffer!");

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
		auto& resources = m_Shader->GetResources();
		if (resources.find(name) != resources.end())
			return &resources.at(name);

		return nullptr;
	}

	void Material::Set(const std::string& name, float value)
	{
		Set<float>(name, value);
	}

	void Material::Set(const std::string& name, int value)
	{
		Set<int>(name, value);
	}

	void Material::Set(const std::string& name, uint32_t value)
	{
		Set<uint32_t>(name, value);
	}

	void Material::Set(const std::string& name, bool value)
	{
		// Bools are 4-byte ints
		Set<int>(name, (int)value);
	}

	void Material::Set(const std::string& name, const glm::ivec2& value)
	{
		Set<glm::ivec2>(name, value);
	}

	void Material::Set(const std::string& name, const glm::ivec3& value)
	{
		Set<glm::ivec3>(name, value);
	}

	void Material::Set(const std::string& name, const glm::ivec4& value)
	{
		Set<glm::ivec4>(name, value);
	}

	void Material::Set(const std::string& name, const glm::vec2& value)
	{
		Set<glm::vec2>(name, value);
	}

	void Material::Set(const std::string& name, const glm::vec3& value)
	{
		Set<glm::vec3>(name, value);
	}

	void Material::Set(const std::string& name, const glm::vec4& value)
	{
		Set<glm::vec4>(name, value);
	}

	void Material::Set(const std::string& name, const glm::mat3& value)
	{
		Set<glm::mat3>(name, value);
	}

	void Material::Set(const std::string& name, const glm::mat4& value)
	{
		Set<glm::mat4>(name, value);
	}

	void Material::Set(const std::string& name, Ref<Texture2D> texture)
	{
		m_DescriptorSetManager.SetInput(name, texture);
	}

	void Material::Set(const std::string& name, Ref<Texture2D> texture, uint32_t arrayIndex)
	{
		m_DescriptorSetManager.SetInput(name, texture, arrayIndex);
	}

	void Material::Set(const std::string& name, Ref<TextureCube> texture)
	{
		m_DescriptorSetManager.SetInput(name, texture);
	}

	void Material::Set(const std::string& name, Ref<Image2D> image)
	{
		m_DescriptorSetManager.SetInput(name, image);
	}

	void Material::Set(const std::string& name, Ref<Image2D> image, uint32_t arrayIndex)
	{
		m_DescriptorSetManager.SetInput(name, image, arrayIndex);
	}

	void Material::Set(const std::string& name, Ref<ImageView> image)
	{
		m_DescriptorSetManager.SetInput(name, image);
	}

	void Material::Set(const std::string& name, Ref<ImageView> image, uint32_t arrayIndex)
	{
		m_DescriptorSetManager.SetInput(name, image, arrayIndex);
	}

	float& Material::GetFloat(const std::string& name)
	{
		return Get<float>(name);
	}

	int32_t& Material::GetInt(const std::string& name)
	{
		return Get<int32_t>(name);
	}

	uint32_t& Material::GetUInt(const std::string& name)
	{
		return Get<uint32_t>(name);
	}

	bool& Material::GetBool(const std::string& name)
	{
		return Get<bool>(name);
	}

	glm::vec2& Material::GetVector2(const std::string& name)
	{
		return Get<glm::vec2>(name);
	}

	glm::vec3& Material::GetVector3(const std::string& name)
	{
		return Get<glm::vec3>(name);
	}

	glm::vec4& Material::GetVector4(const std::string& name)
	{
		return Get<glm::vec4>(name);
	}

	glm::mat3& Material::GetMatrix3(const std::string& name)
	{
		return Get<glm::mat3>(name);
	}

	glm::mat4& Material::GetMatrix4(const std::string& name)
	{
		return Get<glm::mat4>(name);
	}

	Ref<Texture2D> Material::GetTexture2D(const std::string& name)
	{
		return GetResource<Texture2D>(name);
	}

	Ref<TextureCube> Material::TryGetTextureCube(const std::string& name)
	{
		return TryGetResource<TextureCube>(name);
	}

	Ref<Texture2D> Material::TryGetTexture2D(const std::string& name)
	{
		return TryGetResource<Texture2D>(name);
	}

	Ref<TextureCube> Material::GetTextureCube(const std::string& name)
	{
		return GetResource<TextureCube>(name);
	}

	void Material::Prepare()
	{
		m_DescriptorSetManager.InvalidateAndUpdate();
	}

	nvrhi::BindingSetHandle Material::GetBindingSet(uint32_t frameIndex) const
	{
		return m_DescriptorSetManager.GetBindingSet(frameIndex);
	}

}
