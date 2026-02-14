#pragma once

#include "Lux/Core/Base.h"

#include "Lux/Renderer/Shader.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Platform/Vulkan/DescriptorSetManager.h"

#include <unordered_set>

namespace Lux {

	enum class MaterialFlag
	{
		None = BIT(0),
		DepthTest = BIT(1),
		Blend = BIT(2),
		TwoSided = BIT(3),
		DisableShadowCasting = BIT(4)
	};

	class Material : public RefCounted
	{
	public:
		static Ref<Material> Create(Ref<Shader> shader, const std::string& name = "") { return Ref<Material>::Create(shader, name); }
		static Ref<Material> Copy(Ref<Material> other, const std::string& name = "") { return Ref<Material>::Create(other, name); }
		virtual ~Material() {}

		void Invalidate();
		void OnShaderReloaded();

		const ShaderUniform* FindUniformDeclaration(const std::string& name);
		const ShaderResourceDeclaration* FindResourceDeclaration(const std::string& name);

		void Set(const std::string& name, float value);
		void Set(const std::string& name, int value);
		void Set(const std::string& name, uint32_t value);
		void Set(const std::string& name, bool value);
		void Set(const std::string& name, const glm::vec2& value);
		void Set(const std::string& name, const glm::vec3& value);
		void Set(const std::string& name, const glm::vec4& value);
		void Set(const std::string& name, const glm::ivec2& value);
		void Set(const std::string& name, const glm::ivec3& value);
		void Set(const std::string& name, const glm::ivec4& value);

		void Set(const std::string& name, const glm::mat3& value);
		void Set(const std::string& name, const glm::mat4& value);

		void Set(const std::string& name, Ref<Texture2D> texture);
		void Set(const std::string& name, Ref<Texture2D> texture, uint32_t arrayIndex);
		void Set(const std::string& name, Ref<TextureCube> texture);
		void Set(const std::string& name, Ref<Image2D> image);
		void Set(const std::string& name, Ref<Image2D> image, uint32_t arrayIndex);
		void Set(const std::string& name, Ref<ImageView> image);
		void Set(const std::string& name, Ref<ImageView> image, uint32_t arrayIndex);

		float& GetFloat(const std::string& name);
		int32_t& GetInt(const std::string& name);
		uint32_t& GetUInt(const std::string& name);
		bool& GetBool(const std::string& name);
		glm::vec2& GetVector2(const std::string& name);
		glm::vec3& GetVector3(const std::string& name);
		glm::vec4& GetVector4(const std::string& name);
		glm::mat3& GetMatrix3(const std::string& name);
		glm::mat4& GetMatrix4(const std::string& name);

		Ref<Texture2D> GetTexture2D(const std::string& name);
		Ref<TextureCube> GetTextureCube(const std::string& name);

		Ref<Texture2D> TryGetTexture2D(const std::string& name);
		Ref<TextureCube> TryGetTextureCube(const std::string& name);
		template <typename T>
		void Set(const std::string& name, const T& value)
		{
			auto decl = FindUniformDeclaration(name);
			LUX_CORE_ASSERT(decl, "Could not find uniform!");
			if (!decl)
				return;

			auto& buffer = m_UniformStorageBuffer;
			buffer.Write((byte*)&value, decl->GetSize(), decl->GetOffset());
		}

		template<typename T>
		T& Get(const std::string& name)
		{
			auto decl = FindUniformDeclaration(name);
			LUX_CORE_ASSERT(decl, "Could not find uniform with name 'x'");
			auto& buffer = m_UniformStorageBuffer;
			return buffer.Read<T>(decl->GetOffset());
		}

		template<typename T>
		Ref<T> GetResource(const std::string& name)
		{
			return nullptr; // m_DescriptorSetManager.GetInput<T>(name);
		}

		template<typename T>
		Ref<T> TryGetResource(const std::string& name)
		{
			return nullptr; //m_DescriptorSetManager.GetInput<T>(name);
		}

		uint32_t GetFlags() const { return m_MaterialFlags; }
		void SetFlags(uint32_t flags) { m_MaterialFlags = flags; }
		bool GetFlag(MaterialFlag flag) const { return (uint32_t)flag & m_MaterialFlags; }
		void SetFlag(MaterialFlag flag, bool value = true)
		{
			if (value)
			{
				m_MaterialFlags |= (uint32_t)flag;
			}
			else
			{
				m_MaterialFlags &= ~(uint32_t)flag;
			}
		}

		Ref<Shader> GetShader() { return m_Shader; }
		const std::string& GetName() const { return m_Name; }

		void Prepare();
		nvrhi::BindingSetHandle GetBindingSet(uint32_t frameIndex) const;

		Buffer GetUniformStorageBuffer() const { return m_UniformStorageBuffer; }
	public:
		Material(Ref<Shader> shader, const std::string& name);
		Material(Ref<Material> material, const std::string& name);
	private:
		void Init();
		void AllocateStorage();
	private:
		Ref<Shader> m_Shader;
		std::string m_Name;
		uint32_t m_MaterialFlags = 0;

		Buffer m_UniformStorageBuffer;

		DescriptorSetManager m_DescriptorSetManager;
	};

}
