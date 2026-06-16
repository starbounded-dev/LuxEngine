#pragma once

#include "vulkan/vulkan.h"
#include "VulkanAllocator.h"

#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"

#include "nvrhi/nvrhi.h"

#include <string>

namespace Lux {

	enum class RenderResourceType : uint16_t
	{
		None = 0,
		UniformBuffer,
		UniformBufferSet,
		StorageBuffer,
		StorageBufferSet,
		Texture2D,
		TextureCube,
		Image2D,
		Sampler,
	};

	enum class RenderInputType : uint16_t
	{
		None = 0,
		UniformBuffer,
		StorageBuffer,
		ImageSampler,
		ImageSampler1D,
		ImageSampler2D,
		ImageSampler3D,         // SPIR-V DimCube (cubemap) — bound as TextureCube
		ImageSampler3DVolume,   // SPIR-V Dim3D (true volume) — bound as a 3D Image2D
		StorageImage1D,
		StorageImage2D,
		StorageImage3D,         // SPIR-V DimCube storage (cubemap)
		StorageImage3DVolume    // SPIR-V Dim3D storage (true volume)
	};

	struct RenderInputDeclaration
	{
		RenderInputType Type = RenderInputType::None;
		uint32_t Set = 0;
		uint32_t Binding = 0;
		uint32_t Count = 0;
		std::string Name;

		static void Serialize(StreamWriter* serializer, const RenderInputDeclaration& instance)
		{
			serializer->WriteRaw(instance.Type);
			serializer->WriteRaw(instance.Set);
			serializer->WriteRaw(instance.Binding);
			serializer->WriteRaw(instance.Count);
			serializer->WriteString(instance.Name);
		}

		static void Deserialize(StreamReader* deserializer, RenderInputDeclaration& instance)
		{
			deserializer->ReadRaw(instance.Type);
			deserializer->ReadRaw(instance.Set);
			deserializer->ReadRaw(instance.Binding);
			deserializer->ReadRaw(instance.Count);
			deserializer->ReadString(instance.Name);
		}
	};

	namespace ShaderResource {

		struct UniformBuffer
		{
			VkDescriptorBufferInfo Descriptor;
			uint32_t Size = 0;
			uint32_t BindingPoint = 0;
			std::string Name;
			nvrhi::ShaderType ShaderStage = nvrhi::ShaderType::None;

			static void Serialize(StreamWriter* serializer, const UniformBuffer& instance)
			{
				serializer->WriteRaw(instance.Descriptor);
				serializer->WriteRaw(instance.Size);
				serializer->WriteRaw(instance.BindingPoint);
				serializer->WriteString(instance.Name);
				serializer->WriteRaw(instance.ShaderStage);
			}

			static void Deserialize(StreamReader* deserializer, UniformBuffer& instance)
			{
				deserializer->ReadRaw(instance.Descriptor);
				deserializer->ReadRaw(instance.Size);
				deserializer->ReadRaw(instance.BindingPoint);
				deserializer->ReadString(instance.Name);
				deserializer->ReadRaw(instance.ShaderStage);
			}
		};

		struct StorageBuffer
		{
			VmaAllocation MemoryAlloc = nullptr;
			VkDescriptorBufferInfo Descriptor;
			uint32_t Size = 0;
			uint32_t BindingPoint = 0;
			std::string Name;
			nvrhi::ShaderType ShaderStage = nvrhi::ShaderType::None;
			bool ReadOnly = false;

			static void Serialize(StreamWriter* serializer, const StorageBuffer& instance)
			{
				serializer->WriteRaw(instance.Descriptor);
				serializer->WriteRaw(instance.Size);
				serializer->WriteRaw(instance.BindingPoint);
				serializer->WriteString(instance.Name);
				serializer->WriteRaw(instance.ShaderStage);
				serializer->WriteRaw(instance.ReadOnly);
			}

			static void Deserialize(StreamReader* deserializer, StorageBuffer& instance)
			{
				deserializer->ReadRaw(instance.Descriptor);
				deserializer->ReadRaw(instance.Size);
				deserializer->ReadRaw(instance.BindingPoint);
				deserializer->ReadString(instance.Name);
				deserializer->ReadRaw(instance.ShaderStage);
				deserializer->ReadRaw(instance.ReadOnly);
			}
		};

		struct ImageSampler
		{
			uint32_t BindingPoint = 0;
			uint32_t DescriptorSet = 0;
			uint32_t Dimension = 0;
			uint32_t ArraySize = 0;
			std::string Name;
			nvrhi::ShaderType ShaderStage = nvrhi::ShaderType::None;

			static void Serialize(StreamWriter* serializer, const ImageSampler& instance)
			{
				serializer->WriteRaw(instance.BindingPoint);
				serializer->WriteRaw(instance.DescriptorSet);
				serializer->WriteRaw(instance.Dimension);
				serializer->WriteRaw(instance.ArraySize);
				serializer->WriteString(instance.Name);
				serializer->WriteRaw(instance.ShaderStage);
			}

			static void Deserialize(StreamReader* deserializer, ImageSampler& instance)
			{
				deserializer->ReadRaw(instance.BindingPoint);
				deserializer->ReadRaw(instance.DescriptorSet);
				deserializer->ReadRaw(instance.Dimension);
				deserializer->ReadRaw(instance.ArraySize);
				deserializer->ReadString(instance.Name);
				deserializer->ReadRaw(instance.ShaderStage);
			}
		};

		struct PushConstantRange
		{
			nvrhi::ShaderType ShaderStage = nvrhi::ShaderType::None;
			uint32_t Offset = 0;
			uint32_t Size = 0;

			static void Serialize(StreamWriter* writer, const PushConstantRange& range) { writer->WriteRaw(range); }
			static void Deserialize(StreamReader* reader, PushConstantRange& range) { reader->ReadRaw(range); }
		};

		struct ShaderDescriptorSet
		{
			std::unordered_map<uint32_t, UniformBuffer> UniformBuffers;
			std::unordered_map<uint32_t, StorageBuffer> StorageBuffers;
			std::unordered_map<uint32_t, ImageSampler> ImageSamplers;
			std::unordered_map<uint32_t, ImageSampler> StorageImages;
			std::unordered_map<uint32_t, ImageSampler> SeparateTextures; // Not really an image sampler.
			std::unordered_map<uint32_t, ImageSampler> SeparateSamplers;

			std::unordered_map<std::string, RenderInputDeclaration> InputDeclarations;

			operator bool() const { return !(StorageBuffers.empty() && UniformBuffers.empty() && ImageSamplers.empty() && StorageImages.empty() && SeparateTextures.empty() && SeparateSamplers.empty()); }
		};

	}
}
