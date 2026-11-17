// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
//
// GPU/CPU parity for Skinning.glsl on a real Vulkan device, outside the engine: the shader the engine
// ships skins a synthetic mesh and must agree with an independent CPU reference (positions, and the
// tangent frame the QTangent encodes). Needs the Vulkan SDK; no editor, project or renderer.

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{
	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}
	void Vk(VkResult result, const char* what) { Check(result == VK_SUCCESS, std::string(what) + " failed (" + std::to_string(result) + ")"); }

	struct Gpu
	{
		VkInstance Instance{};
		VkPhysicalDevice Physical{};
		VkDevice Device{};
		VkQueue Queue{};
		uint32_t Family = 0;
		VkPhysicalDeviceMemoryProperties Memory{};
		std::string Name;

		Gpu()
		{
			VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
			app.apiVersion = VK_API_VERSION_1_2;
			VkInstanceCreateInfo info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
			info.pApplicationInfo = &app;
			Vk(vkCreateInstance(&info, nullptr, &Instance), "vkCreateInstance");
			uint32_t count = 0;
			vkEnumeratePhysicalDevices(Instance, &count, nullptr);
			Check(count > 0, "no Vulkan device");
			std::vector<VkPhysicalDevice> devices(count);
			vkEnumeratePhysicalDevices(Instance, &count, devices.data());
			Physical = devices[0];
			for (auto device : devices)
			{
				VkPhysicalDeviceProperties properties;
				vkGetPhysicalDeviceProperties(device, &properties);
				if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
					Physical = device;
			}
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(Physical, &properties);
			Name = properties.deviceName;
			vkGetPhysicalDeviceMemoryProperties(Physical, &Memory);
			vkGetPhysicalDeviceQueueFamilyProperties(Physical, &count, nullptr);
			std::vector<VkQueueFamilyProperties> families(count);
			vkGetPhysicalDeviceQueueFamilyProperties(Physical, &count, families.data());
			Family = UINT32_MAX;
			for (uint32_t i = 0; i < count && Family == UINT32_MAX; ++i)
				if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
					Family = i;
			Check(Family != UINT32_MAX, "no compute queue");
			const float priority = 1;
			VkDeviceQueueCreateInfo queue{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
			queue.queueFamilyIndex = Family;
			queue.queueCount = 1;
			queue.pQueuePriorities = &priority;
			VkDeviceCreateInfo device{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
			device.queueCreateInfoCount = 1;
			device.pQueueCreateInfos = &queue;
			Vk(vkCreateDevice(Physical, &device, nullptr, &Device), "vkCreateDevice");
			vkGetDeviceQueue(Device, Family, 0, &Queue);
		}
		~Gpu()
		{
			vkDestroyDevice(Device, nullptr);
			vkDestroyInstance(Instance, nullptr);
		}
	};

	struct HostBuffer
	{
		VkDevice Device{};
		VkBuffer Buffer{};
		VkDeviceMemory Memory{};
		VkDeviceSize Size = 0;
		void* Mapped = nullptr;
		HostBuffer(Gpu& gpu, VkDeviceSize size) : Device(gpu.Device), Size(size < 16 ? 16 : size)
		{
			VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			info.size = Size;
			info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			Vk(vkCreateBuffer(Device, &info, nullptr, &Buffer), "vkCreateBuffer");
			VkMemoryRequirements requirements;
			vkGetBufferMemoryRequirements(Device, Buffer, &requirements);
			const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			uint32_t type = UINT32_MAX;
			for (uint32_t i = 0; i < gpu.Memory.memoryTypeCount && type == UINT32_MAX; ++i)
				if ((requirements.memoryTypeBits & (1u << i)) && (gpu.Memory.memoryTypes[i].propertyFlags & wanted) == wanted)
					type = i;
			Check(type != UINT32_MAX, "no host-visible memory");
			VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocate.allocationSize = requirements.size;
			allocate.memoryTypeIndex = type;
			Vk(vkAllocateMemory(Device, &allocate, nullptr, &Memory), "vkAllocateMemory");
			Vk(vkBindBufferMemory(Device, Buffer, Memory, 0), "vkBindBufferMemory");
			Vk(vkMapMemory(Device, Memory, 0, Size, 0, &Mapped), "vkMapMemory");
			std::memset(Mapped, 0, static_cast<size_t>(Size));
		}
		~HostBuffer()
		{
			vkUnmapMemory(Device, Memory);
			vkDestroyBuffer(Device, Buffer, nullptr);
			vkFreeMemory(Device, Memory, nullptr);
		}
		void Write(const void* data, size_t bytes) { Check(bytes <= Size, "buffer overflow"); std::memcpy(Mapped, data, bytes); }
	};

	constexpr uint32_t k_FirstBinding = 40; // Skinning.glsl: set 1, bindings 40..43

	struct PushConstants
	{
		uint32_t VertexCount;
		uint32_t PaletteBase;
		uint32_t OutputBase;
		uint32_t Pad0;
	};

	// Runs the shader with the four storage buffers at set 1, bindings 40..43 and its push constants.
	void Dispatch(Gpu& gpu, const std::vector<uint32_t>& spirv, std::vector<HostBuffer*> buffers, const PushConstants& push, uint32_t groups)
	{
		VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		moduleInfo.codeSize = spirv.size() * 4;
		moduleInfo.pCode = spirv.data();
		VkShaderModule module;
		Vk(vkCreateShaderModule(gpu.Device, &moduleInfo, nullptr, &module), "vkCreateShaderModule");
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		for (uint32_t i = 0; i < buffers.size(); ++i)
			bindings.push_back({ k_FirstBinding + i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
		VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		VkDescriptorSetLayout empty, layout;
		Vk(vkCreateDescriptorSetLayout(gpu.Device, &layoutInfo, nullptr, &empty), "empty set layout");
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		Vk(vkCreateDescriptorSetLayout(gpu.Device, &layoutInfo, nullptr, &layout), "set layout");
		const VkDescriptorSetLayout sets[] = { empty, layout };
		VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants) };
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutInfo.setLayoutCount = 2;
		pipelineLayoutInfo.pSetLayouts = sets;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &range;
		VkPipelineLayout pipelineLayout;
		Vk(vkCreatePipelineLayout(gpu.Device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "pipeline layout");
		VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		pipelineInfo.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr };
		pipelineInfo.layout = pipelineLayout;
		VkPipeline pipeline;
		Vk(vkCreateComputePipelines(gpu.Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "compute pipeline");
		VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(buffers.size()) };
		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		VkDescriptorPool pool;
		Vk(vkCreateDescriptorPool(gpu.Device, &poolInfo, nullptr, &pool), "descriptor pool");
		VkDescriptorSetAllocateInfo allocate{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocate.descriptorPool = pool;
		allocate.descriptorSetCount = 1;
		allocate.pSetLayouts = &layout;
		VkDescriptorSet set;
		Vk(vkAllocateDescriptorSets(gpu.Device, &allocate, &set), "descriptor set");
		std::vector<VkDescriptorBufferInfo> infos;
		for (auto* buffer : buffers)
			infos.push_back({ buffer->Buffer, 0, VK_WHOLE_SIZE });
		std::vector<VkWriteDescriptorSet> writes;
		for (uint32_t i = 0; i < buffers.size(); ++i)
			writes.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, k_FirstBinding + i, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &infos[i], nullptr });
		vkUpdateDescriptorSets(gpu.Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		VkCommandPoolCreateInfo commandPoolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		commandPoolInfo.queueFamilyIndex = gpu.Family;
		VkCommandPool commandPool;
		Vk(vkCreateCommandPool(gpu.Device, &commandPoolInfo, nullptr, &commandPool), "command pool");
		VkCommandBufferAllocateInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		commandInfo.commandPool = commandPool;
		commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandInfo.commandBufferCount = 1;
		VkCommandBuffer commands;
		Vk(vkAllocateCommandBuffers(gpu.Device, &commandInfo, &commands), "command buffer");
		VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		vkBeginCommandBuffer(commands, &begin);
		vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 1, 1, &set, 0, nullptr);
		vkCmdPushConstants(commands, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &push);
		vkCmdDispatch(commands, groups, 1, 1);
		VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
		vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
		vkEndCommandBuffer(commands);
		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &commands;
		Vk(vkQueueSubmit(gpu.Queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
		Vk(vkQueueWaitIdle(gpu.Queue), "vkQueueWaitIdle");
		vkDestroyCommandPool(gpu.Device, commandPool, nullptr);
		vkDestroyDescriptorPool(gpu.Device, pool, nullptr);
		vkDestroyPipeline(gpu.Device, pipeline, nullptr);
		vkDestroyPipelineLayout(gpu.Device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(gpu.Device, layout, nullptr);
		vkDestroyDescriptorSetLayout(gpu.Device, empty, nullptr);
		vkDestroyShaderModule(gpu.Device, module, nullptr);
	}

	std::vector<uint32_t> ReadSpirv(const char* path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		Check(file.good(), std::string("cannot read ") + path);
		std::vector<uint32_t> words(static_cast<size_t>(file.tellg()) / 4);
		file.seekg(0);
		file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(words.size() * 4));
		return words;
	}

	// ---- the CPU reference: independent of the shader, written from the PackedVertex / QTangent contract ----

	constexpr uint32_t k_VertexWords = 7;
	constexpr uint32_t k_InfluenceWords = 8;

	struct Frame { glm::vec3 Tangent, Binormal, Normal; };

	// PackedVertex::PackedVertex's encoding of a frame (Mesh.cpp), as four snorm16 packed into two words.
	void EncodeFrame(const Frame& frame, float handedness, uint32_t& word0, uint32_t& word1)
	{
		const glm::mat3 matrix(frame.Tangent, glm::cross(frame.Normal, frame.Tangent), frame.Normal);
		glm::quat q = glm::normalize(glm::quat_cast(matrix));
		if (q.w < 0.0f)
			q = -q;
		if (q.w < 1.0f / 32767.0f)
		{
			q.w = 1.0f / 32767.0f;
			q = glm::normalize(q);
		}
		if (handedness < 0.0f)
			q = -q;
		auto snorm = [](float v) { return static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); };
		const int16_t c[4] = { snorm(q.x), snorm(q.y), snorm(q.z), snorm(q.w) };
		word0 = static_cast<uint16_t>(c[0]) | (static_cast<uint32_t>(static_cast<uint16_t>(c[1])) << 16);
		word1 = static_cast<uint16_t>(c[2]) | (static_cast<uint32_t>(static_cast<uint16_t>(c[3])) << 16);
	}

	// QTangent.glslh's QTangent_ToTBN: columns tangent, binormal (handedness applied), normal.
	Frame DecodeFrame(uint32_t word0, uint32_t word1)
	{
		const glm::vec2 xy = glm::unpackSnorm2x16(word0);
		const glm::vec2 zw = glm::unpackSnorm2x16(word1);
		glm::vec4 q(xy.x, xy.y, zw.x, zw.y);
		const float handedness = q.w < 0.0f ? -1.0f : 1.0f;
		q *= handedness;
		const float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
		const float xx = q.x * x2, xy2 = q.x * y2, xz = q.x * z2, yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
		const float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
		Frame frame;
		frame.Tangent = { 1.0f - (yy + zz), xy2 + wz, xz - wy };
		frame.Binormal = glm::vec3(xy2 - wz, 1.0f - (xx + zz), yz + wx) * handedness;
		frame.Normal = { xz + wy, yz - wx, 1.0f - (xx + yy) };
		return frame;
	}

	uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
	float Float(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

	struct Influence { uint32_t Index[4]; float Weight[4]; };

	void Reference(const std::vector<uint32_t>& source, const std::vector<Influence>& influences, const std::vector<glm::mat4>& palette,
		uint32_t vertex, glm::vec3& position, Frame& frame, bool& skinned)
	{
		const uint32_t base = vertex * k_VertexWords;
		position = { Float(source[base]), Float(source[base + 1]), Float(source[base + 2]) };
		const Frame in = DecodeFrame(source[base + 3], source[base + 4]);
		const Influence& influence = influences[vertex];
		const float sum = influence.Weight[0] + influence.Weight[1] + influence.Weight[2] + influence.Weight[3];
		skinned = sum > 1e-6f;
		frame = in;
		if (!skinned)
			return;
		glm::mat4 blended(0.0f);
		for (int i = 0; i < 4; i++)
			blended += palette[influence.Index[i]] * influence.Weight[i];
		position = glm::vec3(blended * glm::vec4(position, 1.0f));
		const glm::mat3 linear(blended);
		const float handedness = glm::dot(glm::cross(in.Normal, in.Tangent), in.Binormal) >= 0.0f ? 1.0f : -1.0f;
		const glm::vec3 normal = glm::normalize(linear * in.Normal);
		glm::vec3 tangent = linear * in.Tangent;
		tangent = glm::normalize(tangent - normal * glm::dot(normal, tangent));
		frame.Normal = normal;
		frame.Tangent = tangent;
		frame.Binormal = glm::cross(normal, tangent) * handedness;
	}
}

int main(int argc, char** argv)
{
	Check(argc == 2, "usage: SkinningGpuParity <Skinning.spv>");
	const std::vector<uint32_t> spirv = ReadSpirv(argv[1]);

	constexpr uint32_t vertexCount = 6000;
	constexpr uint32_t boneCount = 12;
	constexpr uint32_t paletteBase = 5; // the entity's palette starts part-way into the shared buffer
	constexpr uint32_t outputBase = 9;  // and so does its output, in the shared skinned-vertex buffer

	std::mt19937 random(12345);
	auto uniform = [&](float low, float high) { return std::uniform_real_distribution<float>(low, high)(random); };
	auto unitVector = [&]()
	{
		glm::vec3 v;
		do { v = { uniform(-1, 1), uniform(-1, 1), uniform(-1, 1) }; } while (glm::dot(v, v) < 0.01f);
		return glm::normalize(v);
	};

	// A palette of rotations, translations and (uniform) scales.
	std::vector<glm::mat4> palette(paletteBase + boneCount, glm::mat4(1.0f));
	for (uint32_t b = 0; b < boneCount; b++)
	{
		glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(uniform(-3, 3), uniform(-3, 3), uniform(-3, 3)));
		m = m * glm::rotate(glm::mat4(1.0f), uniform(-3.0f, 3.0f), unitVector());
		if (b % 3 == 0)
			m = m * glm::scale(glm::mat4(1.0f), glm::vec3(uniform(0.7f, 1.4f)));
		palette[paletteBase + b] = m;
	}

	std::vector<uint32_t> source(static_cast<size_t>(vertexCount) * k_VertexWords);
	std::vector<Influence> influences(vertexCount);
	for (uint32_t v = 0; v < vertexCount; v++)
	{
		const glm::vec3 position(uniform(-5, 5), uniform(-5, 5), uniform(-5, 5));
		const glm::vec3 normal = unitVector();
		glm::vec3 tangent = glm::cross(unitVector(), normal);
		if (glm::dot(tangent, tangent) < 1e-4f)
			tangent = glm::cross(glm::vec3(0, 1, 0), normal);
		tangent = glm::normalize(tangent);
		const float handedness = (v % 5 == 0) ? -1.0f : 1.0f;
		const uint32_t base = v * k_VertexWords;
		source[base] = Bits(position.x);
		source[base + 1] = Bits(position.y);
		source[base + 2] = Bits(position.z);
		EncodeFrame({ tangent, glm::cross(normal, tangent), normal }, handedness, source[base + 3], source[base + 4]);
		source[base + 5] = Bits(uniform(0, 1));
		source[base + 6] = Bits(uniform(0, 1));

		Influence& influence = influences[v];
		if (v % 97 == 0)
		{
			// no bone touches it: copied through
			continue;
		}
		const int used = (v % 7 == 0) ? 1 : (v % 3 == 0 ? 2 : 4);
		float total = 0.0f;
		for (int i = 0; i < used; i++)
		{
			influence.Index[i] = static_cast<uint32_t>(random() % boneCount);
			influence.Weight[i] = uniform(0.05f, 1.0f);
			total += influence.Weight[i];
		}
		for (int i = 0; i < used; i++)
			influence.Weight[i] /= total;
	}

	Gpu gpu;
	std::cout << "Skinning parity on " << gpu.Name << '\n';
	HostBuffer sourceBuffer(gpu, source.size() * 4);
	HostBuffer influenceBuffer(gpu, static_cast<VkDeviceSize>(vertexCount) * k_InfluenceWords * 4);
	HostBuffer paletteBuffer(gpu, palette.size() * sizeof(glm::mat4));
	HostBuffer outputBuffer(gpu, (source.size() + static_cast<size_t>(outputBase) * k_VertexWords) * 4);
	sourceBuffer.Write(source.data(), source.size() * 4);
	{
		std::vector<uint32_t> words(static_cast<size_t>(vertexCount) * k_InfluenceWords);
		for (uint32_t v = 0; v < vertexCount; v++)
		{
			for (int i = 0; i < 4; i++)
			{
				words[v * k_InfluenceWords + i] = influences[v].Index[i];
				words[v * k_InfluenceWords + 4 + i] = Bits(influences[v].Weight[i]);
			}
		}
		influenceBuffer.Write(words.data(), words.size() * 4);
	}
	paletteBuffer.Write(palette.data(), palette.size() * sizeof(glm::mat4));

	Dispatch(gpu, spirv, { &sourceBuffer, &influenceBuffer, &paletteBuffer, &outputBuffer }, { vertexCount, paletteBase, outputBase, 0 }, (vertexCount + 63) / 64);

	const uint32_t* output = static_cast<const uint32_t*>(outputBuffer.Mapped) + static_cast<size_t>(outputBase) * k_VertexWords;
	float worstPosition = 0.0f, worstFrame = 1.0f;
	uint32_t skinnedCount = 0, copiedCount = 0;
	for (uint32_t v = 0; v < vertexCount; v++)
	{
		glm::vec3 expectedPosition;
		Frame expectedFrame;
		bool skinned = false;
		// The reference palette index is relative to the entity's base, like the shader's.
		std::vector<glm::mat4> entityPalette(palette.begin() + paletteBase, palette.end());
		Reference(source, influences, entityPalette, v, expectedPosition, expectedFrame, skinned);

		const uint32_t base = v * k_VertexWords;
		const glm::vec3 gpuPosition(Float(output[base]), Float(output[base + 1]), Float(output[base + 2]));
		const Frame gpuFrame = DecodeFrame(output[base + 3], output[base + 4]);

		if (!skinned)
		{
			copiedCount++;
			for (uint32_t w = 0; w < k_VertexWords; w++)
				Check(output[base + w] == source[base + w], "vertex " + std::to_string(v) + " with no weights was not copied unchanged");
		}
		else
		{
			skinnedCount++;
		}
		Check(output[base + 5] == source[base + 5] && output[base + 6] == source[base + 6], "vertex " + std::to_string(v) + " texcoord changed");

		worstPosition = std::max(worstPosition, glm::length(gpuPosition - expectedPosition));
		worstFrame = std::min({ worstFrame, glm::dot(gpuFrame.Tangent, expectedFrame.Tangent), glm::dot(gpuFrame.Binormal, expectedFrame.Binormal),
			glm::dot(gpuFrame.Normal, expectedFrame.Normal) });
	}

	std::cout << "vertices: " << skinnedCount << " skinned, " << copiedCount << " copied\n";
	std::cout << "worst position error: " << worstPosition << ", worst frame cosine: " << worstFrame << '\n';
	Check(skinnedCount > 0 && copiedCount > 0, "the test mesh must contain both skinned and copied vertices");
	Check(worstPosition < 1e-3f, "positions differ from the CPU reference");
	Check(worstFrame > 0.9995f, "tangent frames differ from the CPU reference");
	std::cout << "skinning parity: passed\n";
	return 0;
}
