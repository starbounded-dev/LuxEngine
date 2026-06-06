#pragma once

#include "Lux/Renderer/UniformBufferSet.h"
#include "Lux/Renderer/StorageBufferSet.h"
#include "Lux/Renderer/Image.h"
#include "Lux/Renderer/Texture.h"

#include "Lux/Platform/Vulkan/VulkanShader.h"

#include <set>
#include <vector>

namespace Lux {

	// TODO(Yan):
	// - Maybe rename from RenderPassXXX to DescriptorXXX or something more
	//   generic, because these are also used for compute & materials

	struct RenderPassInput
	{
		using InputArray = std::vector<Ref<RefCounted>>;

		RenderResourceType Type = RenderResourceType::None;
		bool IsWriteable = false;
		InputArray Input;

		RenderPassInput() = default;

		RenderPassInput(Ref<UniformBuffer> uniformBuffer)
			: Type(RenderResourceType::UniformBuffer), Input(InputArray{ uniformBuffer })
		{
		}

		RenderPassInput(Ref<UniformBufferSet> uniformBufferSet)
			: Type(RenderResourceType::UniformBufferSet), Input(InputArray{ uniformBufferSet })
		{
		}

		RenderPassInput(Ref<StorageBuffer> storageBuffer)
			: Type(RenderResourceType::StorageBuffer), Input(InputArray{ storageBuffer })
		{
		}

		RenderPassInput(Ref<StorageBufferSet> storageBufferSet)
			: Type(RenderResourceType::StorageBufferSet), Input(InputArray{ storageBufferSet })
		{
		}

		RenderPassInput(Ref<Texture2D> texture)
			: Type(RenderResourceType::Texture2D), Input(InputArray{ texture })
		{
		}

		RenderPassInput(Ref<TextureCube> texture)
			: Type(RenderResourceType::TextureCube), Input(InputArray{ texture })
		{
		}

		RenderPassInput(Ref<Image2D> image)
			: Type(RenderResourceType::Image2D), Input(InputArray{ image })
		{
		}

		void Set(Ref<UniformBuffer> uniformBuffer, uint32_t arrayIndex = 0)
		{
			Type = RenderResourceType::UniformBuffer;
			EnsureSize(arrayIndex);
			Input[arrayIndex] = uniformBuffer;
		}

		void Set(Ref<UniformBufferSet> uniformBufferSet, uint32_t arrayIndex = 0)
		{
			Type = RenderResourceType::UniformBufferSet;
			EnsureSize(arrayIndex);
			Input[arrayIndex] = uniformBufferSet;
		}

		void Set(Ref<StorageBuffer> storageBuffer, uint32_t arrayIndex = 0)
		{
			Type = RenderResourceType::StorageBuffer;
			EnsureSize(arrayIndex);
			Input[arrayIndex] = storageBuffer;
		}

		void Set(Ref<StorageBufferSet> storageBufferSet, uint32_t arrayIndex = 0)
		{
			Type = RenderResourceType::StorageBufferSet;
			EnsureSize(arrayIndex);
			Input[arrayIndex] = storageBufferSet;
		}

		void Set(Ref<Texture2D> texture, uint32_t arrayIndex = 0)
		{
			Type = RenderResourceType::Texture2D;
			EnsureSize(arrayIndex);
			Input[arrayIndex] = texture;
		}

		void Set(Ref<TextureCube> texture, uint32_t arrayIndex = 0)
		{
			Type = RenderResourceType::TextureCube;
			EnsureSize(arrayIndex);
			Input[arrayIndex] = texture;
		}

		void Set(Ref<Image2D> image, uint32_t arrayIndex = 0)
		{
			Type = RenderResourceType::Image2D;
			EnsureSize(arrayIndex);
			Input[arrayIndex] = image;
		}

		void Set(Ref<ImageView> image, uint32_t arrayIndex = 0)
		{
			Type = RenderResourceType::Image2D;
			EnsureSize(arrayIndex);
			Input[arrayIndex] = image;
		}

		void Set(Ref<Sampler> sampler, uint32_t arrayIndex = 0)
		{
			Type = RenderResourceType::Sampler;
			EnsureSize(arrayIndex);
			Input[arrayIndex] = sampler;
		}

	private:
		void EnsureSize(uint32_t arrayIndex)
		{
			if (Input.size() <= arrayIndex)
				Input.resize((size_t)arrayIndex + 1);
		}

	};

	inline bool IsCompatibleInput(RenderResourceType inputResource, RenderInputType inputType)
	{
		switch (inputType)
		{
			case RenderInputType::ImageSampler:
			{
				return inputResource == RenderResourceType::Sampler;
			}
			case RenderInputType::ImageSampler2D:
			{
				return inputResource == RenderResourceType::Texture2D || inputResource == RenderResourceType::Image2D;
			}
			case RenderInputType::ImageSampler3D:
			{
				return inputResource == RenderResourceType::TextureCube;
			}
			case RenderInputType::StorageImage2D:
			{
				return inputResource == RenderResourceType::Image2D;
			}
			case RenderInputType::StorageImage3D:
			{
				// Image2D is also valid here because ImageView can reference a cubemap subresource
				return inputResource == RenderResourceType::TextureCube || inputResource == RenderResourceType::Image2D;
			}
			case RenderInputType::UniformBuffer:
			{
				return inputResource == RenderResourceType::UniformBuffer || inputResource == RenderResourceType::UniformBufferSet;
			}
			case RenderInputType::StorageBuffer:
			{
				return inputResource == RenderResourceType::StorageBuffer || inputResource == RenderResourceType::StorageBufferSet;
			}
		}
		return false;
	}

	struct DescriptorSetManagerSpecification
	{
		Ref<VulkanShader> Shader;
		std::string DebugName;

		// Which descriptor sets should be managed
		uint32_t StartSet = 0, EndSet = 3;

		bool IsDynamic = true; // Automatically check resources for change
		bool DefaultResources = false;
	};

	struct DescriptorSetManager
	{
		enum class State
		{
			None = 0,
			Pending = 1,
			Ready = 2
		};

		//
		// Input Resources (map of set->binding->resource)
		// 
		// Invalidated input resources will attempt to be assigned on Renderer::BeginRenderPass
		// This is useful for resources that may not exist at RenderPass creation but will be
		// present during actual rendering
		std::map<uint32_t, std::map<uint32_t, RenderPassInput>> InputResources;
		std::map<uint32_t, std::map<uint32_t, RenderPassInput>> InvalidatedInputResources;
		std::map<std::string, RenderInputDeclaration> InputDeclarations;

		DescriptorSetManager() = default;
		DescriptorSetManager(const DescriptorSetManager& other);
		DescriptorSetManager(const DescriptorSetManagerSpecification& specification);
		static DescriptorSetManager Copy(const DescriptorSetManager& other);

		void SetInput(std::string_view name, Ref<UniformBufferSet> uniformBufferSet);
		void SetInput(std::string_view name, Ref<UniformBuffer> uniformBuffer);
		void SetInput(std::string_view name, Ref<StorageBufferSet> storageBufferSet);
		void SetInput(std::string_view name, Ref<StorageBuffer> storageBuffer);
		void SetInput(std::string_view name, Ref<Texture2D> texture, uint32_t arrayIndex = 0);
		void SetInput(std::string_view name, Ref<TextureCube> textureCube);
		void SetInput(std::string_view name, Ref<Image2D> image, uint32_t arrayIndex = 0);
		void SetInput(std::string_view name, Ref<ImageView> image, uint32_t arrayIndex = 0);
		void SetInput(std::string_view name, Ref<Sampler> sampler);

		template<typename T>
		Ref<T> GetInput(std::string_view name)
		{
			const RenderInputDeclaration* decl = GetInputDeclaration(name);
			if (decl)
			{
				auto setIt = InputResources.find(decl->Set);
				if (setIt != InputResources.end())
				{
					auto resourceIt = setIt->second.find(decl->Binding);
					if (resourceIt != setIt->second.end())
						return resourceIt->second.Input[0].As<T>();
				}
			}
			return nullptr;
		}

		bool IsInvalidated(uint32_t set, uint32_t binding) const;
		bool Validate();
		void Bake();

		std::set<uint32_t> HasBufferSets() const;
		void InvalidateAndUpdate();

		bool HasDescriptorSets() const;
		uint32_t GetBindingSetCount() const;
		uint32_t GetFirstSetIndex() const;
		nvrhi::BindingSetHandle GetBindingSet(uint32_t frameIndex) const;
		nvrhi::BindingSetVector GetBindingSets(uint32_t frameIndex) const;
		bool IsInputValid(std::string_view name) const;
		const RenderInputDeclaration* GetInputDeclaration(std::string_view name) const;
	private:
		void Init();
	private:
		DescriptorSetManagerSpecification m_Specification;
		State m_State = State::None;

		// Per-frame in flight
		nvrhi::static_vector<nvrhi::static_vector<nvrhi::BindingSetHandle, nvrhi::c_MaxBindingLayouts>, 3> m_BindingSets;
		// Frame->set->binding
		nvrhi::static_vector<std::map<uint32_t, std::map<uint32_t, std::vector<nvrhi::ResourceHandle>>>, 3> m_BindingSetHandles;

	};

}
