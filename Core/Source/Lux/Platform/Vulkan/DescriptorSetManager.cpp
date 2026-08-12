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
				case RenderInputType::ImageSampler:         return RenderResourceType::Sampler;
				case RenderInputType::ImageSampler2D:       return RenderResourceType::Texture2D;
				case RenderInputType::ImageSampler3D:       return RenderResourceType::TextureCube;   // cubemap
				case RenderInputType::ImageSampler3DVolume: return RenderResourceType::Image2D;        // true 3D volume (SRV)
				case RenderInputType::StorageImage2D:       return RenderResourceType::Image2D;
				case RenderInputType::StorageImage3D:       return RenderResourceType::TextureCube;   // cubemap storage
				case RenderInputType::StorageImage3DVolume: return RenderResourceType::Image2D;        // true 3D volume (UAV)
				case RenderInputType::UniformBuffer:        return RenderResourceType::UniformBuffer;
				case RenderInputType::StorageBuffer:        return RenderResourceType::StorageBuffer;
			}

			LUX_CORE_ASSERT(false);
			return RenderResourceType::None;
		}
		
		inline bool IsWriteable(RenderInputType inputType)
		{
			return inputType == RenderInputType::StorageImage1D
				|| inputType == RenderInputType::StorageImage2D
				|| inputType == RenderInputType::StorageImage3D
				|| inputType == RenderInputType::StorageImage3DVolume
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
		LUX_PROFILE_FUNCTION_AUTO;
		DescriptorSetManager result(other);
		return result;
	}

	void DescriptorSetManager::Init()
	{
		LUX_PROFILE_FUNCTION_AUTO;
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

				// Always insert default resources. This block creates the RenderPassInput entry
				// itself - not just the fallback sampler/texture - so every declared input needs
				// it, not only materials. The condition here used to read
				// `m_Specification.DefaultResources || true`, i.e. unconditional: the flag had a
				// single setter (Material) and never actually gated anything. Honouring it would
				// have left every render/compute pass without input entries, so the flag is gone
				// and the behaviour is stated plainly instead.
				{
					// Create RenderPassInput
					RenderPassInput& input = InputResources[set][binding];
					input.Input.resize(inputDecl.Count);
					input.Type = Utils::GetDefaultResourceType(inputDecl.Type);
					input.IsWriteable = Utils::IsWriteable(inputDecl.Type);

					// Set default textures and samplers
					if (inputDecl.Type == RenderInputType::ImageSampler)
					{
						// Pick the default from the sampler's name. Defaulting everything to the
						// clamp sampler means a pass that declares r_RepeatSampler but never binds
						// it silently samples tiling content clamped (and r_PointSampler filtered),
						// which is wrong in a way nothing reports - the binding is "valid", just
						// the wrong sampler. r_MaterialSampler follows the repeat sampler because
						// that is what BindCommonSceneRenderPassInputs binds it to.
						Ref<Sampler> defaultSampler = Renderer::GetDefaultSampler();
						if (name == "r_RepeatSampler" || name == "r_MaterialSampler")
							defaultSampler = Renderer::GetRepeatSampler();
						else if (name == "r_PointSampler")
							defaultSampler = Renderer::GetPointSampler();
						else if (name == "r_LinearSampler")
							defaultSampler = Renderer::GetClampSampler();

						for (size_t i = 0; i < input.Input.size(); i++)
							input.Input[i] = defaultSampler;
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

	void DescriptorSetManager::OnShaderReloaded()
	{
		LUX_PROFILE_FUNCTION_AUTO;

		// An in-place shader recompile released the binding layouts the baked
		// sets were created against and may have changed the reflected set/
		// binding map. Rebuild everything from the new reflection, keeping the
		// previously bound inputs — names are the stable key across
		// permutations, set/binding indexes are not.
		std::map<std::string, RenderPassInput> savedInputs;
		for (const auto& [name, decl] : InputDeclarations)
		{
			auto setIt = InputResources.find(decl.Set);
			if (setIt == InputResources.end())
				continue;
			auto bindingIt = setIt->second.find(decl.Binding);
			if (bindingIt != setIt->second.end())
				savedInputs[name] = bindingIt->second;
		}

		InputDeclarations.clear();
		InputResources.clear();
		InvalidatedInputResources.clear();
		for (auto& frameHandles : m_BindingSetHandles)
			frameHandles.clear();
		for (auto& set : m_BindingSets)
			set = {};

		Init();

		// Re-apply the saved inputs wherever the new reflection still declares
		// them. Bindings that vanished from this permutation are dropped; new
		// ones keep Init's defaults until the usual SetInput calls fill them.
		for (auto& [name, input] : savedInputs)
		{
			auto declIt = InputDeclarations.find(name);
			if (declIt == InputDeclarations.end())
				continue;
			const RenderInputDeclaration& decl = declIt->second;
			if (input.Input.size() != (size_t)decl.Count)
				continue;

			RenderPassInput& target = InputResources[decl.Set][decl.Binding];
			const bool isWriteable = target.IsWriteable; // from the new reflection
			target = input;
			target.IsWriteable = isWriteable;
		}

		Bake();
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<UniformBufferSet> uniformBufferSet)
	{
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
		const RenderInputDeclaration* decl = GetInputDeclaration(name);
		if (decl)
			InputResources.at(decl->Set).at(decl->Binding).Set(image, arrayIndex);
		else
			LUX_CORE_WARN_TAG("Renderer", "[RenderPass ({})] Input {} not found", m_Specification.DebugName, name);
	}

	void DescriptorSetManager::SetInput(std::string_view name, Ref<Sampler> sampler)
	{
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
		if (InvalidatedInputResources.find(set) != InvalidatedInputResources.end())
		{
			const auto& resources = InvalidatedInputResources.at(set);
			return resources.find(binding) != resources.end();
		}

		return false;
	}

	std::set<uint32_t> DescriptorSetManager::HasBufferSets() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
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
		LUX_PROFILE_FUNCTION_AUTO;
		// Make sure all resources are present and we can properly bake
		if (!Validate())
		{
			LUX_CORE_ERROR_TAG("Renderer", "[RenderPass] Bake - Validate failed! {}", m_Specification.DebugName);
			return;
		}

		uint32_t descriptorSetCount = Renderer::GetConfig().FramesInFlight;

		m_BindingSets.resize(descriptorSetCount);
		for (auto& set : m_BindingSets)
			set = {};

		for (const auto& [set, setData] : InputResources)
			BakeSet(set);

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
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
	}

	// Rebuilds the binding sets for one descriptor-set index across all frames
	// in flight. Bake() calls this for every set; InvalidateAndUpdate calls it
	// only for the sets whose inputs actually changed, instead of re-creating
	// every binding set of every set on any single change.
	void DescriptorSetManager::BakeSet(uint32_t set)
	{
		auto setIt = InputResources.find(set);
		if (setIt == InputResources.end())
			return;
		const auto& setData = setIt->second;

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		const uint32_t descriptorSetCount = Renderer::GetConfig().FramesInFlight;
		if (m_BindingSets.size() < descriptorSetCount)
			m_BindingSets.resize(descriptorSetCount);

		{
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
								if (texture == nullptr)
									texture = Renderer::GetWhiteTexture();

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

								bindingSetItem.arrayElement = (uint32_t)i;
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


	}

	void DescriptorSetManager::InvalidateAndUpdate()
	{
		//LUX_PROFILE_FUNC();
		//LUX_SCOPE_PERF("DescriptorSetManager::InvalidateAndUpdate");

		if (m_State == State::Ready)
			return;

		// Start each update from a clean slate. Entries are re-added below by the
		// handle-comparison loop (and by Bake() for still-null deferred resources);
		// without this clear the set stays non-empty after the first invalidation,
		// so every subsequent frame re-Bakes ALL binding sets for ALL frames in
		// flight — permanent per-frame descriptor churn across every dynamic pass.
		InvalidatedInputResources.clear();

		uint32_t currentFrameIndex = Renderer::RT_GetCurrentFrameIndex();

		// Check for invalidated resources
		for (const auto& [set, inputs] : InputResources)
		{
			for (const auto& [binding, input] : inputs)
			{
				// A declared input may have no resource bound yet — e.g. a pass that
				// just recompiled into a variant which newly declares a resource (the
				// AO passes newly declare Camera when GTAO is toggled on) before the
				// owning pass has rebound it. Dereferencing the null Ref below would
				// crash the render thread, so skip it here and let the pass's rebind +
				// Bake() pick it up once the resource is available. Validate() likewise
				// treats a null resource as a soft failure rather than crashing.
				if (input.Input.empty() || input.Input[0] == nullptr)
					continue;

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
								texture = Renderer::GetWhiteTexture(); // TODO(Yan): error texture

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

			// Rebake only the affected descriptor sets. Snapshot the set indexes
			// first: BakeSet may re-insert still-null deferred inputs into
			// InvalidatedInputResources while we iterate.
			std::vector<uint32_t> setsToBake;
			setsToBake.reserve(InvalidatedInputResources.size());
			for (const auto& [set, bindings] : InvalidatedInputResources)
				setsToBake.push_back(set);
			for (uint32_t set : setsToBake)
				BakeSet(set);
		}

		if (!m_Specification.IsDynamic)
			m_State = State::Ready;

	}

	bool DescriptorSetManager::HasDescriptorSets() const
	{
		return !m_BindingSets.empty() && !m_BindingSets[0].empty();
	}

	uint32_t DescriptorSetManager::GetBindingSetCount() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint32_t count = 0;
		for (const auto& frameBindingSets : m_BindingSets)
		{
			for (const auto& bindingSet : frameBindingSets)
			{
				if (bindingSet != nullptr)
					count++;
			}
		}
		return count;
	}

	uint32_t DescriptorSetManager::GetFirstSetIndex() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
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
		else if (frameIndex >= m_BindingSets.size())
			frameIndex %= static_cast<uint32_t>(m_BindingSets.size());

		if (m_BindingSets[frameIndex].empty())
			return nullptr;

		return m_BindingSets[frameIndex][0];
	}

	nvrhi::BindingSetVector DescriptorSetManager::GetBindingSets(uint32_t frameIndex) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_BindingSets.empty())
			return {};

		if (frameIndex > 0 && m_BindingSets.size() == 1)
			frameIndex = 0; // Frame index is irrelevant for this type of render pass
		else if (frameIndex >= m_BindingSets.size())
			frameIndex %= static_cast<uint32_t>(m_BindingSets.size());

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
