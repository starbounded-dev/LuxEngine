#include "lpch.h"
#include "DescriptorSetManager.h"

#include "Lux/Renderer/Renderer.h"

#include "VulkanAPI.h"

#include "Lux/Debug/Profiler.h"

namespace Lux {
	
	namespace Utils {

		inline RenderResourceType GetDefaultResourceType(RenderInputType inputType)
		{
			switch (inputType)
			{
				case RenderInputType::ImageSampler:   return RenderResourceType::Sampler;
				case RenderInputType::ImageSampler2D: return RenderResourceType::Texture2D;
				case RenderInputType::ImageSampler3D: return RenderResourceType::TextureCube;
				case RenderInputType::StorageImage2D: return RenderResourceType::Image2D;
				case RenderInputType::StorageImage3D: return RenderResourceType::TextureCube;
				case RenderInputType::UniformBuffer:  return RenderResourceType::UniformBuffer;
				case RenderInputType::StorageBuffer:  return RenderResourceType::StorageBuffer;
			}

			LUX_CORE_ASSERT(false);
			return RenderResourceType::None;
		}
		
		inline bool IsWriteable(RenderInputType inputType)
		{
			return inputType == RenderInputType::StorageImage1D
				|| inputType == RenderInputType::StorageImage2D
				|| inputType == RenderInputType::StorageImage3D
				|| inputType == RenderInputType::StorageBuffer;
		}

		inline nvrhi::ResourceType GetBindingLayoutType(nvrhi::BindingLayoutHandle bindingLayout, uint32_t binding)
		{
			const nvrhi::BindingLayoutDesc* desc = bindingLayout->getDesc();
			if (!desc)
				return nvrhi::ResourceType::None;

			for (const auto& item : desc->bindings)
			{
				if (item.slot == binding)
					return item.type;
			}
			return nvrhi::ResourceType::None;
		}

	}

	DescriptorSetManager::DescriptorSetManager(const DescriptorSetManagerSpecification& specification)
		: m_Specification(specification)
	{
		Init();
	}

	DescriptorSetManager::DescriptorSetManager(const DescriptorSetManager& other)
		: m_Specification(other.m_Specification)
	{
		Init();
		InputResources = other.InputResources;
		Bake();
	}

	DescriptorSetManager DescriptorSetManager::Copy(const DescriptorSetManager& other)
	{
		DescriptorSetManager result(other);
		return result;
	}

	void DescriptorSetManager::Init()
	{
		const auto& shaderDescriptorSets = m_Specification.Shader->GetShaderDescriptorSets();
		uint32_t framesInFlight = Renderer::GetConfig().FramesInFlight;
		m_BindingSetHandles.resize(framesInFlight);

		for (uint32_t set = m_Specification.StartSet; set <= m_Specification.EndSet; set++)
		{
			if (set >= shaderDescriptorSets.size())
				break;

			const auto& shaderDescriptor = shaderDescriptorSets[set];
			for (auto&& [bname, inputDecl] : shaderDescriptor.InputDeclarations)
			{
				// NOTE(Emily): This is a hack to fix a bad input decl name
				//				Coming from somewhere.
				const char* broken = strrchr(bname.c_str(), '.');
				std::string name = broken ? broken + 1 : bname;
				
				InputDeclarations[name] = inputDecl;

				uint32_t binding = inputDecl.Binding;

				// Insert default resources (useful for materials)
				if (m_Specification.DefaultResources || true)
				{
					// Create RenderPassInput
					RenderPassInput& input = InputResources[set][binding];
					input.Input.resize(inputDecl.Count);
					input.Type = Utils::GetDefaultResourceType(inputDecl.Type);
					input.IsWriteable = Utils::IsWriteable(inputDecl.Type);

					// Set default textures and samplers
					if (inputDecl.Type == RenderInputType::ImageSampler)
					{
						for (size_t i = 0; i < input.Input.size(); i++)
							input.Input[i] = Renderer::GetDefaultSampler();
					}
					if (inputDecl.Type == RenderInputType::ImageSampler2D)
					{
						for (size_t i = 0; i < input.Input.size(); i++)
							input.Input[i] = Renderer::GetWhiteTexture();
					}
					else if (inputDecl.Type == RenderInputType::ImageSampler3D)
					{
						for (size_t i = 0; i < input.Input.size(); i++)
							input.Input[i] = Renderer::GetBlackCubeTexture();
					}
				}

				for (uint32_t frameIndex = 0; frameIndex < framesInFlight; frameIndex++)
					m_BindingSetHandles[frameIndex][set][binding].resize(inputDecl.Count);

			}
		}
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<UniformBufferSet> uniformBufferSet)
	{
		const RenderInputDeclaration* decl = GetInputDeclaration(name);
		if (decl)
		{
			InputResources.at(decl->Set).at(decl->Binding).Set(uniformBufferSet);
			m_State = State::Pending;
		}
		else
		{
			LUX_CORE_WARN_TAG("Renderer", "[RenderPass ({})] Input {} not found", m_Specification.DebugName, name);
		}
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<UniformBuffer> uniformBuffer)
	{
		const RenderInputDeclaration* decl = GetInputDeclaration(name);
		if (decl)
		{
			InputResources.at(decl->Set).at(decl->Binding).Set(uniformBuffer);
			m_State = State::Pending;
		}
		else
		{
			LUX_CORE_WARN_TAG("Renderer", "[RenderPass ({})] Input {} not found", m_Specification.DebugName, name);
		}
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<StorageBufferSet> storageBufferSet)
	{
		const RenderInputDeclaration* decl = GetInputDeclaration(name);
		if (decl)
		{
			InputResources.at(decl->Set).at(decl->Binding).Set(storageBufferSet);
			m_State = State::Pending;
		}
		else
		{
			LUX_CORE_WARN_TAG("Renderer", "[RenderPass ({})] Input {} not found", m_Specification.DebugName, name);
		}
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<StorageBuffer> storageBuffer)
	{
		const RenderInputDeclaration* decl = GetInputDeclaration(name);
		if (decl)
		{
			InputResources.at(decl->Set).at(decl->Binding).Set(storageBuffer);
			m_State = State::Pending;
		}
		else
		{
			LUX_CORE_WARN_TAG("Renderer", "[RenderPass ({})] Input {} not found", m_Specification.DebugName, name);
		}
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<Texture2D> texture, uint32_t arrayIndex)
	{
		const RenderInputDeclaration* decl = GetInputDeclaration(name);
		if (decl)
		{
			InputResources.at(decl->Set).at(decl->Binding).Set(texture, arrayIndex);
			m_State = State::Pending;
		}
		else
		{
			LUX_CORE_WARN_TAG("Renderer", "[RenderPass ({})] Input {} not found", m_Specification.DebugName, name);
		}
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<TextureCube> textureCube)
	{
		const RenderInputDeclaration* decl = GetInputDeclaration(name);
		if (decl)
		{
			InputResources.at(decl->Set).at(decl->Binding).Set(textureCube);
			m_State = State::Pending;
		}
		else
		{
			LUX_CORE_WARN_TAG("Renderer", "[RenderPass ({})] Input {} not found", m_Specification.DebugName, name);
		}
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<Image2D> image, uint32_t arrayIndex)
	{
		const RenderInputDeclaration* decl = GetInputDeclaration(name);
		if (decl)
		{
			InputResources.at(decl->Set).at(decl->Binding).Set(image, arrayIndex);
			m_State = State::Pending;
		}
		else
		{
			LUX_CORE_WARN_TAG("Renderer", "[RenderPass ({})] Input {} not found", m_Specification.DebugName, name);
		}
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<ImageView> image, uint32_t arrayIndex)
	{
		const RenderInputDeclaration* decl = GetInputDeclaration(name);
		if (decl)
			InputResources.at(decl->Set).at(decl->Binding).Set(image, arrayIndex);
		else
			LUX_CORE_WARN_TAG("Renderer", "[RenderPass ({})] Input {} not found", m_Specification.DebugName, name);
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<Sampler> sampler)
	{
		const RenderInputDeclaration* decl = GetInputDeclaration(name);
		if (decl)
		{
			InputResources.at(decl->Set).at(decl->Binding).Set(sampler);
			m_State = State::Pending;
		}
		else
		{
			LUX_CORE_WARN_TAG("Renderer", "[RenderPass ({})] Input {} not found", m_Specification.DebugName, name);
		}
	}

	bool DescriptorSetManager::IsInvalidated(uint32_t set, uint32_t binding) const
	{
		if (InvalidatedInputResources.find(set) != InvalidatedInputResources.end())
		{
			const auto& resources = InvalidatedInputResources.at(set);
			return resources.find(binding) != resources.end();
		}

		return false;
	}

	std::set<uint32_t> DescriptorSetManager::HasBufferSets() const
	{
		// Find all descriptor sets that have either UniformBufferSet or StorageBufferSet descriptors
		std::set<uint32_t> sets;

		for (const auto& [set, resources] : InputResources)
		{
			for (const auto& [binding, input] : resources)
			{
				if (input.Type == RenderResourceType::UniformBufferSet || input.Type == RenderResourceType::StorageBufferSet)
				{
					sets.insert(set);
					break;
				}
			}
		}
		return sets;
	}


	bool DescriptorSetManager::Validate()
	{
		// Go through pipeline requirements to make sure we have all required resource
		const auto& shaderDescriptorSets = m_Specification.Shader->GetShaderDescriptorSets();

		// Nothing to validate, pipeline only contains material inputs
		//if (shaderDescriptorSets.size() < 2)
		//	return true;

		for (uint32_t set = m_Specification.StartSet; set <= m_Specification.EndSet; set++)
		{
			if (set >= shaderDescriptorSets.size())
				break;

			// No descriptors in this set
			if (!shaderDescriptorSets[set])
				continue;

			if (InputResources.find(set) == InputResources.end())
			{
				LUX_CORE_ERROR_TAG("Renderer", "[RenderPass ({})] No input resources for Set {}", m_Specification.DebugName, set);
				return false;
			}

			const auto& setInputResources = InputResources.at(set);

			const auto& shaderDescriptor = shaderDescriptorSets[set];
			for (auto&& [name, inputDecl] : shaderDescriptor.InputDeclarations)
			{
				uint32_t binding = inputDecl.Binding;
				if (setInputResources.find(binding) == setInputResources.end())
				{
					LUX_CORE_ERROR_TAG("Renderer", "[RenderPass ({})] No input resource for {}.{}", m_Specification.DebugName, set, binding);
					LUX_CORE_ERROR_TAG("Renderer", "[RenderPass ({})] Required resource is {} ({})", m_Specification.DebugName, name, (int)inputDecl.Type);
					return false;
				}

				const auto& resource = setInputResources.at(binding);
				if (!IsCompatibleInput(resource.Type, inputDecl.Type))
				{
					LUX_CORE_ERROR_TAG("Renderer", "[RenderPass ({})] Required resource is wrong type! {} but needs {}", m_Specification.DebugName, (uint16_t)resource.Type, (int)inputDecl.Type);
					return false;
				}

				if (resource.Type != RenderResourceType::Image2D && resource.Input[0] == nullptr)
				{
					LUX_CORE_ERROR_TAG("Renderer", "[RenderPass ({})] Resource is null! {} ({}.{})", m_Specification.DebugName, name, set, binding);
					return false;
				}
			}
		}

		// All resources present
		return true;
	}

	// TODO(Yan): revisit resources not existing at this time, since we now (mostly) create them immediately
	void DescriptorSetManager::Bake()
	{
		// Make sure all resources are present and we can properly bake
		if (!Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RenderPass] Bake - Validate failed! {}", m_Specification.DebugName);
			return;
		}
		
		// If valid, we can create descriptor sets
		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();

		auto bufferSets = HasBufferSets();
		bool perFrameInFlight = !bufferSets.empty();
		perFrameInFlight = true; // always
		uint32_t descriptorSetCount = Renderer::GetConfig().FramesInFlight;
		if (!perFrameInFlight)
			descriptorSetCount = 1;

		m_BindingSets.resize(descriptorSetCount);
		for (auto& set : m_BindingSets)
			set = {};

		// for (auto& set : m_BindingSetHandles)
		// 	set.clear();

		for (const auto& [set, setData] : InputResources)
		{
			uint32_t descriptorCountInSet = bufferSets.find(set) != bufferSets.end() ? descriptorSetCount : 1;
			for (uint32_t frameIndex = 0; frameIndex < descriptorSetCount; frameIndex++)
			{
				nvrhi::BindingLayoutHandle bindingLayout = m_Specification.Shader->GetDescriptorSetLayout(set);

				nvrhi::BindingSetDesc bindingSetDesc;

				if (m_BindingSets[frameIndex].size() <= set)
					m_BindingSets[frameIndex].resize(set + 1);

				auto& bindingSetHandleMap = m_BindingSetHandles[frameIndex].at(set);
				std::vector<std::vector<nvrhi::TextureHandle>> imageInfoStorage;
				uint32_t imageInfoStorageIndex = 0;

				for (const auto& [binding, input] : setData)
				{
					auto& storedHandles = bindingSetHandleMap.at(binding);
					storedHandles.resize(input.Input.size());

					// VkWriteDescriptorSet& writeDescriptor = storedWriteDescriptor.WriteDescriptorSet;
					// writeDescriptor.dstSet = descriptorSet;

					switch (input.Type)
					{
						case RenderResourceType::UniformBuffer:
						{
							Ref<UniformBuffer> buffer = input.Input[0].As<UniformBuffer>();
							nvrhi::BufferHandle handle = buffer->GetHandle();
							bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(binding, handle));
							storedHandles[0] = handle;

							// Defer if resource doesn't exist
							if (buffer->GetHandle() == nullptr)
								InvalidatedInputResources[set][binding] = input;

							break;
						}
						case RenderResourceType::UniformBufferSet:
						{
							Ref<UniformBufferSet> buffer = input.Input[0].As<UniformBufferSet>();
							nvrhi::BufferHandle handle = buffer->Get(frameIndex)->GetHandle();
							bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::ConstantBuffer(binding, handle));
							storedHandles[0] = handle;

							break;
						}
						case RenderResourceType::StorageBuffer:
						{
							Ref<StorageBuffer> buffer = input.Input[0].As<StorageBuffer>();
							nvrhi::BufferHandle handle = buffer->GetHandle();
							nvrhi::ResourceType layoutType = Utils::GetBindingLayoutType(bindingLayout, binding);
							if (layoutType == nvrhi::ResourceType::RawBuffer_SRV)
								bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::RawBuffer_SRV(binding, handle));
							else
								bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::RawBuffer_UAV(binding, handle));
							storedHandles[0] = handle;

							break;
						}
						case RenderResourceType::StorageBufferSet:
						{
							Ref<StorageBufferSet> buffer = input.Input[0].As<StorageBufferSet>();
							nvrhi::BufferHandle handle = buffer->Get(frameIndex)->GetHandle();
							nvrhi::ResourceType layoutType = Utils::GetBindingLayoutType(bindingLayout, binding);
							if (layoutType == nvrhi::ResourceType::RawBuffer_SRV)
								bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::RawBuffer_SRV(binding, handle));
							else
								bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::RawBuffer_UAV(binding, handle));
							storedHandles[0] = handle;

							break;
						}
						case RenderResourceType::Texture2D:
						{
							for (size_t i = 0; i < input.Input.size(); i++)
							{
								Ref<Texture2D> texture = input.Input[i].As<Texture2D>();

								nvrhi::TextureHandle handle = texture->GetHandle();
								nvrhi::BindingSetItem bindingSetItem = nvrhi::BindingSetItem::Texture_SRV(binding, handle);
								bindingSetItem.arrayElement = (uint32_t)i;
								bindingSetDesc.bindings.push_back(bindingSetItem);
								
								storedHandles[i] = handle;
							}
		
							break;
						}
						case RenderResourceType::TextureCube:
						{
							Ref<TextureCube> texture = input.Input[0].As<TextureCube>();
							ImageInfo* imageInfo = (ImageInfo*)texture->GetDescriptorInfo();
							
							nvrhi::TextureHandle handle = imageInfo->ImageHandle;

							nvrhi::BindingSetItem bindingSetItem = input.IsWriteable
								? nvrhi::BindingSetItem::Texture_UAV(binding, handle)
								: nvrhi::BindingSetItem::Texture_SRV(binding, handle);

							bindingSetItem.dimension = imageInfo->Dimension;
							LUX_CORE_ASSERT(bindingSetItem.dimension == nvrhi::TextureDimension::TextureCube);

							bindingSetDesc.bindings.push_back(bindingSetItem);
							storedHandles[0] = handle;

							break;
						}
						case RenderResourceType::Image2D:
						{
							for (size_t i = 0; i < input.Input.size(); i++)
							{
								Ref<RendererResource> image = input.Input[i].As<RendererResource>();
								// Defer if resource doesn't exist
								if (image == nullptr)
								{
									InvalidatedInputResources[set][binding] = input;
									break;
								}

								ImageInfo* imageInfo = (ImageInfo*)image->GetDescriptorInfo();
								nvrhi::TextureHandle handle = imageInfo->ImageHandle;

								nvrhi::BindingSetItem bindingSetItem = input.IsWriteable
									? nvrhi::BindingSetItem::Texture_UAV(binding, handle)
									: nvrhi::BindingSetItem::Texture_SRV(binding, handle);

								bindingSetItem.arrayElement = i;
								bindingSetItem.subresources = imageInfo->ImageView;
								bindingSetItem.dimension = imageInfo->Dimension;

								bindingSetDesc.bindings.push_back(bindingSetItem);
								storedHandles[i] = handle;
							}

							break;
						}
						case RenderResourceType::Sampler:
						{
							Ref<RendererResource> sampler = input.Input[0].As<RendererResource>();
							// Defer if resource doesn't exist
							if (sampler == nullptr)
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}

							nvrhi::SamplerHandle handle = ((Sampler*)sampler->GetDescriptorInfo())->GetHandle();
							bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::Sampler(binding, handle));
							storedHandles[0] = handle;

							break;
						}
					}
				}

				if (!bindingSetDesc.bindings.empty())
				{
					m_BindingSets[frameIndex][set] = device->createBindingSet(bindingSetDesc, bindingLayout);
				}
			}
		}

#if 1
		for (uint32_t frameIndex = 0; frameIndex < descriptorSetCount; frameIndex++)
		{
			if (!m_BindingSets[frameIndex].empty() && m_BindingSets[frameIndex][0] == nullptr)
			{
				nvrhi::BindingLayoutHandle bindingLayout = m_Specification.Shader->GetDescriptorSetLayout(0);

				nvrhi::BindingSetDesc bindingSetDesc;
				m_BindingSets[frameIndex][0] = device->createBindingSet(bindingSetDesc, bindingLayout);
			}
		}
#endif


#if TODO
		// Create Descriptor Pool
		VkDescriptorPoolSize poolSizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
		};

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 10 * 3; // frames in flight should partially determine this
		poolInfo.poolSizeCount = 10;
		poolInfo.pPoolSizes = poolSizes;

		VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool));

		auto bufferSets = HasBufferSets();
		bool perFrameInFlight = !bufferSets.empty();
		perFrameInFlight = true; // always
		uint32_t descriptorSetCount = Renderer::GetConfig().FramesInFlight;
		if (!perFrameInFlight)
			descriptorSetCount = 1;

		if (m_DescriptorSets.size() < 1)
		{
			for (uint32_t i = 0; i < descriptorSetCount; i++)
				m_DescriptorSets.emplace_back();
		}

		for (auto& descriptorSet : m_DescriptorSets)
			descriptorSet.clear();

		for (const auto& [set, setData] : InputResources)
		{
			uint32_t descriptorCountInSet = bufferSets.find(set) != bufferSets.end() ? descriptorSetCount : 1;
			for (uint32_t frameIndex = 0; frameIndex < descriptorSetCount; frameIndex++)
			{
				nvrhi::BindingLayoutHandle dsl = m_Specification.Shader->GetDescriptorSetLayout(set);
				VkDescriptorSetAllocateInfo descriptorSetAllocInfo = Vulkan::DescriptorSetAllocInfo(&dsl);
				descriptorSetAllocInfo.descriptorPool = m_DescriptorPool;
				VkDescriptorSet descriptorSet = nullptr;
				VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSet));

				m_DescriptorSets[frameIndex].emplace_back(descriptorSet);

				auto& writeDescriptorMap = WriteDescriptorMap[frameIndex].at(set);
				std::vector<std::vector<VkDescriptorImageInfo>> imageInfoStorage;
				uint32_t imageInfoStorageIndex = 0;

				for (const auto& [binding, input] : setData)
				{
					auto& storedWriteDescriptor = writeDescriptorMap.at(binding);

					VkWriteDescriptorSet& writeDescriptor = storedWriteDescriptor.WriteDescriptorSet;
					writeDescriptor.dstSet = descriptorSet;

					switch (input.Type)
					{
						case RenderResourceType::UniformBuffer:
						{
							Ref<VulkanUniformBuffer> buffer = input.Input[0].As<VulkanUniformBuffer>();
							writeDescriptor.pBufferInfo = &buffer->GetDescriptorBufferInfo();
							storedWriteDescriptor.ResourceHandles[0] = writeDescriptor.pBufferInfo->buffer;

							// Defer if resource doesn't exist
							if (writeDescriptor.pBufferInfo->buffer == nullptr)
								InvalidatedInputResources[set][binding] = input;

							break;
						}
						case RenderResourceType::UniformBufferSet:
						{
							Ref<UniformBufferSet> buffer = input.Input[0].As<UniformBufferSet>();
							// TODO: replace 0 with current frame in flight (i.e. create bindings for all frames)
							writeDescriptor.pBufferInfo = &buffer->Get(frameIndex).As<VulkanUniformBuffer>()->GetDescriptorBufferInfo();
							storedWriteDescriptor.ResourceHandles[0] = writeDescriptor.pBufferInfo->buffer;

							// Defer if resource doesn't exist
							if (writeDescriptor.pBufferInfo->buffer == nullptr)
								InvalidatedInputResources[set][binding] = input;

							break;
						}
						case RenderResourceType::StorageBuffer:
						{
							Ref<VulkanStorageBuffer> buffer = input.Input[0].As<VulkanStorageBuffer>();
							writeDescriptor.pBufferInfo = &buffer->GetDescriptorBufferInfo();
							storedWriteDescriptor.ResourceHandles[0] = writeDescriptor.pBufferInfo->buffer;

							// Defer if resource doesn't exist
							if (writeDescriptor.pBufferInfo->buffer == nullptr)
								InvalidatedInputResources[set][binding] = input;

							break;
						}
						case RenderResourceType::StorageBufferSet:
						{
							Ref<StorageBufferSet> buffer = input.Input[0].As<StorageBufferSet>();
							// TODO: replace 0 with current frame in flight (i.e. create bindings for all frames)
							writeDescriptor.pBufferInfo = &buffer->Get(frameIndex).As<VulkanStorageBuffer>()->GetDescriptorBufferInfo();
							storedWriteDescriptor.ResourceHandles[0] = writeDescriptor.pBufferInfo->buffer;

							// Defer if resource doesn't exist
							if (writeDescriptor.pBufferInfo->buffer == nullptr)
								InvalidatedInputResources[set][binding] = input;

							break;
						}
						case RenderResourceType::Texture2D:
						{
							if (input.Input.size() > 1)
							{
								imageInfoStorage.emplace_back(input.Input.size());
								for (size_t i = 0; i < input.Input.size(); i++)
								{
									Ref<VulkanTexture2D> texture = input.Input[i].As<VulkanTexture2D>();
									imageInfoStorage[imageInfoStorageIndex][i] = texture->GetDescriptorInfoVulkan();

								}
								writeDescriptor.pImageInfo = imageInfoStorage[imageInfoStorageIndex].data();
								imageInfoStorageIndex++;
							}
							else
							{
								Ref<VulkanTexture2D> texture = input.Input[0].As<VulkanTexture2D>();
								writeDescriptor.pImageInfo = &texture->GetDescriptorInfoVulkan();
							}
							storedWriteDescriptor.ResourceHandles[0] = writeDescriptor.pImageInfo->imageView;

							// Defer if resource doesn't exist
							if (writeDescriptor.pImageInfo->imageView == nullptr)
								InvalidatedInputResources[set][binding] = input;

							break;
						}
						case RenderResourceType::TextureCube:
						{
							Ref<VulkanTextureCube> texture = input.Input[0].As<VulkanTextureCube>();
							writeDescriptor.pImageInfo = &texture->GetDescriptorInfoVulkan();
							storedWriteDescriptor.ResourceHandles[0] = writeDescriptor.pImageInfo->imageView;

							// Defer if resource doesn't exist
							if (writeDescriptor.pImageInfo->imageView == nullptr)
								InvalidatedInputResources[set][binding] = input;

							break;
						}
						case RenderResourceType::Image2D:
						{
							Ref<RendererResource> image = input.Input[0].As<RendererResource>();
							// Defer if resource doesn't exist
							if (image == nullptr)
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}

							writeDescriptor.pImageInfo = (VkDescriptorImageInfo*)image->GetDescriptorInfo();
							storedWriteDescriptor.ResourceHandles[0] = writeDescriptor.pImageInfo->imageView;

							// Defer if resource doesn't exist
							if (writeDescriptor.pImageInfo->imageView == nullptr)
								InvalidatedInputResources[set][binding] = input;

							break;
						}
					}
				}

				std::vector<VkWriteDescriptorSet> writeDescriptors;
				for (auto&& [binding, writeDescriptor] : writeDescriptorMap)
				{
					// Include if valid, otherwise defer (these will be resolved if possible at Prepare stage)
					if (!IsInvalidated(set, binding))
						writeDescriptors.emplace_back(writeDescriptor.WriteDescriptorSet);
				}

				if (!writeDescriptors.empty())
				{
					LUX_CORE_INFO_TAG("Renderer", "Render pass update {} descriptors in set {}", writeDescriptors.size(), set);
					vkUpdateDescriptorSets(device, (uint32_t)writeDescriptors.size(), writeDescriptors.data(), 0, nullptr);
				}
			}
		}
#endif
	}

	void DescriptorSetManager::InvalidateAndUpdate()
	{
		//LUX_PROFILE_FUNC();
		//LUX_SCOPE_PERF("DescriptorSetManager::InvalidateAndUpdate");

		if (m_State == State::Ready)
			return;

		uint32_t currentFrameIndex = Renderer::RT_GetCurrentFrameIndex();

		// Check for invalidated resources
		for (const auto& [set, inputs] : InputResources)
		{
			for (const auto& [binding, input] : inputs)
			{
				const auto& bindingSetHandleArray = m_BindingSetHandles[currentFrameIndex].at(set).at(binding);
				const auto& bindingSetHandle = bindingSetHandleArray[0];

				switch (input.Type)
				{
					case RenderResourceType::UniformBuffer:
					{
						nvrhi::BufferHandle handle = input.Input[0].As<UniformBuffer>()->GetHandle();
						if (handle != bindingSetHandle)
						{
							InvalidatedInputResources[set][binding] = input;
							break;
						}
						break;
					}
					case RenderResourceType::UniformBufferSet:
					{
						nvrhi::BufferHandle handle = input.Input[0].As<UniformBufferSet>()->Get(currentFrameIndex)->GetHandle();
						if (handle != bindingSetHandle)
						{
							InvalidatedInputResources[set][binding] = input;
							break;
						}
						break;
					}
					case RenderResourceType::StorageBuffer:
					{
						nvrhi::BufferHandle handle = input.Input[0].As<StorageBuffer>()->GetHandle();
						if (handle != bindingSetHandle)
						{
							InvalidatedInputResources[set][binding] = input;
							break;
						}
						break;
					}
					case RenderResourceType::StorageBufferSet:
					{
						nvrhi::BufferHandle handle = input.Input[0].As<StorageBufferSet>()->Get(currentFrameIndex)->GetHandle();
						if (handle != bindingSetHandle)
						{
							InvalidatedInputResources[set][binding] = input;
							break;
						}
						break;
					}
					case RenderResourceType::Texture2D:
					{
						for (size_t i = 0; i < input.Input.size(); i++)
						{
							Ref<Texture2D> texture = input.Input[i].As<Texture2D>();
							if (texture == nullptr)
							{
								texture = Renderer::GetWhiteTexture(); // TODO(Yan): error texture
								LUX_CORE_VERIFY(false);
							}

							if (texture->GetHandle() != bindingSetHandleArray[i])
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}
						}
						break;
					}
					case RenderResourceType::TextureCube:
					{
						nvrhi::TextureHandle handle = input.Input[0].As<TextureCube>()->GetHandle();
						if (handle != bindingSetHandle)
						{
							InvalidatedInputResources[set][binding] = input;
							break;
						}
						break;
					}
					case RenderResourceType::Image2D:
					{
						for (size_t i = 0; i < input.Input.size(); i++)
						{
							Ref<RendererResource> image = input.Input[i].As<RendererResource>();
							nvrhi::TextureHandle handle = ((ImageInfo*)image->GetDescriptorInfo())->ImageHandle;
							if (handle != bindingSetHandleArray[i])
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}
						}
						break;
					}
					case RenderResourceType::Sampler:
					{
						Ref<RendererResource> image = input.Input[0].As<RendererResource>();
						nvrhi::SamplerHandle handle = ((Sampler*)image->GetDescriptorInfo())->GetHandle();
						if (handle != bindingSetHandle)
						{
							InvalidatedInputResources[set][binding] = input;
							break;
						}
						break;
					}
				}
			}
		}

		if (!InvalidatedInputResources.empty())
		{
			LUX_CORE_TRACE_TAG("Renderer", "DescriptorSetManager::InvalidateAndUpdate ({}) - updating {} descriptors (frameIndex={})", m_Specification.DebugName, InvalidatedInputResources.size(), currentFrameIndex);
			Bake();
		}

		if (!m_Specification.IsDynamic)
			m_State = State::Ready;

#if TODO
		LUX_PROFILE_FUNC();
		LUX_SCOPE_PERF("DescriptorSetManager::InvalidateAndUpdate");

		uint32_t currentFrameIndex = Renderer::RT_GetCurrentFrameIndex();

		// Check for invalidated resources
		for (const auto& [set, inputs] : InputResources)
		{
			for (const auto& [binding, input] : inputs)
			{
				switch (input.Type)
				{
					case RenderResourceType::UniformBuffer:
					{
						//for (uint32_t frameIndex = 0; frameIndex < (uint32_t)WriteDescriptorMap.size(); frameIndex++)
						{
							const VkDescriptorBufferInfo& bufferInfo = input.Input[0].As<VulkanUniformBuffer>()->GetDescriptorBufferInfo();
							if (bufferInfo.buffer != WriteDescriptorMap[currentFrameIndex].at(set).at(binding).ResourceHandles[0])
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}
						}
						break;
					}
					case RenderResourceType::UniformBufferSet:
					{
						//for (uint32_t frameIndex = 0; frameIndex < (uint32_t)WriteDescriptorMap.size(); frameIndex++)
						{
							const VkDescriptorBufferInfo& bufferInfo = input.Input[0].As<VulkanUniformBufferSet>()->Get(currentFrameIndex).As<VulkanUniformBuffer>()->GetDescriptorBufferInfo();
							if (bufferInfo.buffer != WriteDescriptorMap[currentFrameIndex].at(set).at(binding).ResourceHandles[0])
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}
						}
						break;
					}
					case RenderResourceType::StorageBuffer:
					{

						//for (uint32_t frameIndex = 0; frameIndex < (uint32_t)WriteDescriptorMap.size(); frameIndex++)
						{
							const VkDescriptorBufferInfo& bufferInfo = input.Input[0].As<VulkanStorageBuffer>()->GetDescriptorBufferInfo();
							if (bufferInfo.buffer != WriteDescriptorMap[currentFrameIndex].at(set).at(binding).ResourceHandles[0])
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}
						}
						break;
					}
					case RenderResourceType::StorageBufferSet:
					{
						//for (uint32_t frameIndex = 0; frameIndex < (uint32_t)WriteDescriptorMap.size(); frameIndex++)
						{
							const VkDescriptorBufferInfo& bufferInfo = input.Input[0].As<VulkanStorageBufferSet>()->Get(currentFrameIndex).As<VulkanStorageBuffer>()->GetDescriptorBufferInfo();
							if (bufferInfo.buffer != WriteDescriptorMap[currentFrameIndex].at(set).at(binding).ResourceHandles[0])
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}
						}
						break;
					}
					case RenderResourceType::Texture2D:
					{
						for (size_t i = 0; i < input.Input.size(); i++)
						{
							Ref<VulkanTexture2D> vulkanTexture = input.Input[i].As<VulkanTexture2D>();
							if (vulkanTexture == nullptr)
								vulkanTexture = Renderer::GetWhiteTexture().As<VulkanTexture2D>(); // TODO(Yan): error texture

							const VkDescriptorImageInfo& imageInfo = vulkanTexture->GetDescriptorInfoVulkan();
							if (imageInfo.imageView != WriteDescriptorMap[currentFrameIndex].at(set).at(binding).ResourceHandles[i])
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}
						}
						break;
					}
					case RenderResourceType::TextureCube:
					{
						//for (uint32_t frameIndex = 0; frameIndex < (uint32_t)WriteDescriptorMap.size(); frameIndex++)
						{
							const VkDescriptorImageInfo& imageInfo = input.Input[0].As<VulkanTextureCube>()->GetDescriptorInfoVulkan();
							if (imageInfo.imageView != WriteDescriptorMap[currentFrameIndex].at(set).at(binding).ResourceHandles[0])
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}
						}
						break;
					}
					case RenderResourceType::Image2D:
					{
						//for (uint32_t frameIndex = 0; frameIndex < (uint32_t)WriteDescriptorMap.size(); frameIndex++)
						{
							const VkDescriptorImageInfo& imageInfo = *(VkDescriptorImageInfo*)input.Input[0].As<RendererResource>()->GetDescriptorInfo();
							if (imageInfo.imageView != WriteDescriptorMap[currentFrameIndex].at(set).at(binding).ResourceHandles[0])
							{
								InvalidatedInputResources[set][binding] = input;
								break;
							}
						}
						break;
					}
				}
			}
		}

		// Nothing to do
		if (InvalidatedInputResources.empty())
			return;

		auto bufferSets = HasBufferSets();
		bool perFrameInFlight = !bufferSets.empty();
		perFrameInFlight = true; // always
		uint32_t descriptorSetCount = Renderer::GetConfig().FramesInFlight;
		if (!perFrameInFlight)
			descriptorSetCount = 1;


		// TODO(Yan): handle these if they fail (although Vulkan will probably give us a validation error if they do anyway)
		for (const auto& [set, setData] : InvalidatedInputResources)
		{
			uint32_t descriptorCountInSet = bufferSets.find(set) != bufferSets.end() ? descriptorSetCount : 1;
			//for (uint32_t frameIndex = currentFrameIndex; frameIndex < descriptorSetCount; frameIndex++)
			uint32_t frameIndex = perFrameInFlight ? currentFrameIndex : 0;
			{
				// Go through every resource here and call vkUpdateDescriptorSets with write descriptors
				// If we don't have valid buffers/images to bind to here, that's an error and needs to be
				// probably handled by putting in some error resources, otherwise we'll crash
				std::vector<VkWriteDescriptorSet> writeDescriptorsToUpdate;
				writeDescriptorsToUpdate.reserve(setData.size());
				std::vector<std::vector<VkDescriptorImageInfo>> imageInfoStorage;
				uint32_t imageInfoStorageIndex = 0;
				for (const auto& [binding, input] : setData)
				{
					// Update stored write descriptor
					auto& wd = WriteDescriptorMap[frameIndex].at(set).at(binding);
					VkWriteDescriptorSet& writeDescriptor = wd.WriteDescriptorSet;
					switch (input.Type)
					{
						case RenderResourceType::UniformBuffer:
						{
							Ref<VulkanUniformBuffer> buffer = input.Input[0].As<VulkanUniformBuffer>();
							writeDescriptor.pBufferInfo = &buffer->GetDescriptorBufferInfo();
							wd.ResourceHandles[0] = writeDescriptor.pBufferInfo->buffer;
							break;
						}
						case RenderResourceType::UniformBufferSet:
						{
							Ref<UniformBufferSet> buffer = input.Input[0].As<UniformBufferSet>();
							writeDescriptor.pBufferInfo = &buffer->Get(frameIndex).As<VulkanUniformBuffer>()->GetDescriptorBufferInfo();
							wd.ResourceHandles[0] = writeDescriptor.pBufferInfo->buffer;
							break;
						}
						case RenderResourceType::StorageBuffer:
						{
							Ref<VulkanStorageBuffer> buffer = input.Input[0].As<VulkanStorageBuffer>();
							writeDescriptor.pBufferInfo = &buffer->GetDescriptorBufferInfo();
							wd.ResourceHandles[0] = writeDescriptor.pBufferInfo->buffer;
							break;
						}
						case RenderResourceType::StorageBufferSet:
						{
							Ref<StorageBufferSet> buffer = input.Input[0].As<StorageBufferSet>();
							writeDescriptor.pBufferInfo = &buffer->Get(frameIndex).As<VulkanStorageBuffer>()->GetDescriptorBufferInfo();
							wd.ResourceHandles[0] = writeDescriptor.pBufferInfo->buffer;
							break;
						}
						case RenderResourceType::Texture2D:
						{

							if (input.Input.size() > 1)
							{
								imageInfoStorage.emplace_back(input.Input.size());
								for (size_t i = 0; i < input.Input.size(); i++)
								{
									Ref<VulkanTexture2D> texture = input.Input[i].As<VulkanTexture2D>();
									imageInfoStorage[imageInfoStorageIndex][i] = texture->GetDescriptorInfoVulkan();
									wd.ResourceHandles[i] = imageInfoStorage[imageInfoStorageIndex][i].imageView;
								}
								writeDescriptor.pImageInfo = imageInfoStorage[imageInfoStorageIndex].data();
								imageInfoStorageIndex++;
							}
							else
							{
								Ref<VulkanTexture2D> texture = input.Input[0].As<VulkanTexture2D>();
								writeDescriptor.pImageInfo = &texture->GetDescriptorInfoVulkan();
								wd.ResourceHandles[0] = writeDescriptor.pImageInfo->imageView;
							}

							break;
						}
						case RenderResourceType::TextureCube:
						{
							Ref<VulkanTextureCube> texture = input.Input[0].As<VulkanTextureCube>();
							writeDescriptor.pImageInfo = &texture->GetDescriptorInfoVulkan();
							wd.ResourceHandles[0] = writeDescriptor.pImageInfo->imageView;
							break;
						}
						case RenderResourceType::Image2D:
						{
							Ref<RendererResource> image = input.Input[0].As<RendererResource>();
							writeDescriptor.pImageInfo = (VkDescriptorImageInfo*)image->GetDescriptorInfo();
							LUX_CORE_VERIFY(writeDescriptor.pImageInfo->imageView);
							wd.ResourceHandles[0] = writeDescriptor.pImageInfo->imageView;
							break;
						}
					}
					writeDescriptorsToUpdate.emplace_back(writeDescriptor);
				}
				// LUX_CORE_TRACE_TAG("Renderer", "RenderPass::Prepare ({}) - updating {} descriptors in set {} (frameIndex={})", m_Specification.DebugName, writeDescriptorsToUpdate.size(), set, frameIndex);
				LUX_CORE_TRACE_TAG("Renderer", "DescriptorSetManager::InvalidateAndUpdate ({}) - updating {} descriptors in set {} (frameIndex={})", m_Specification.DebugName, writeDescriptorsToUpdate.size(), set, frameIndex);
				VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();
				vkUpdateDescriptorSets(device, (uint32_t)writeDescriptorsToUpdate.size(), writeDescriptorsToUpdate.data(), 0, nullptr);
			}
		}

		InvalidatedInputResources.clear();
#endif
	}

	bool DescriptorSetManager::HasDescriptorSets() const
	{
		return !m_BindingSets.empty() && !m_BindingSets[0].empty();
	}

	uint32_t DescriptorSetManager::GetFirstSetIndex() const
	{
		if (InputResources.empty())
			return UINT32_MAX;

		// Return first key (key == descriptor set index)
		return InputResources.begin()->first;
	}

	nvrhi::BindingSetHandle DescriptorSetManager::GetBindingSet(uint32_t frameIndex) const
	{
		if (m_BindingSets.empty())
			return nullptr;

		if (frameIndex > 0 && m_BindingSets.size() == 1)
			frameIndex = 0; // Frame index is irrelevant for this type of render pass

		if (m_BindingSets[frameIndex].empty())
			return nullptr;

		return m_BindingSets[frameIndex][0];
	}

	nvrhi::BindingSetVector DescriptorSetManager::GetBindingSets(uint32_t frameIndex) const
	{
		if (m_BindingSets.empty())
			return {};

		if (frameIndex > 0 && m_BindingSets.size() == 1)
			frameIndex = 0; // Frame index is irrelevant for this type of render pass

		nvrhi::BindingSetVector result(m_BindingSets[frameIndex].size());
		for (size_t i = 0; i < result.size(); i++)
			result[i] = m_BindingSets[frameIndex][i];
		
		return result;
	}

	bool DescriptorSetManager::IsInputValid(std::string_view name) const
	{
		std::string nameStr(name);
		return InputDeclarations.find(nameStr) != InputDeclarations.end();
	}

	const RenderInputDeclaration* DescriptorSetManager::GetInputDeclaration(std::string_view name) const
	{
		std::string nameStr(name);
		if (InputDeclarations.find(nameStr) == InputDeclarations.end())
			return nullptr;

		const RenderInputDeclaration& decl = InputDeclarations.at(nameStr);
		return &decl;
	}



}
