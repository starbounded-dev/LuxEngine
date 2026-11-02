// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
// GPU/CPU parity for Soundwave's compute kernels on a real Vulkan device, outside the engine:
// the shaders the engine ships run here on flattened test scenes and must agree with the CPU
// reference within the authored tolerance (0.5 dB, Decision 10).

#include <soundwave/Flatten.h>
#include <soundwave/Reverb.h>
#include <vulkan/vulkan.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace Lux::Soundwave;
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
			std::memset(Mapped, 0, Size);
		}
		~HostBuffer()
		{
			vkUnmapMemory(Device, Memory);
			vkDestroyBuffer(Device, Buffer, nullptr);
			vkFreeMemory(Device, Memory, nullptr);
		}
		void Write(const void* data, size_t bytes) { Check(bytes <= Size, "buffer overflow"); std::memcpy(Mapped, data, bytes); }
	};

	// Runs one compute shader with storage buffers at set 1, bindings 30..30+N-1.
	void Dispatch(Gpu& gpu, const std::vector<uint32_t>& spirv, std::vector<HostBuffer*> buffers, uint32_t groups)
	{
		VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		moduleInfo.codeSize = spirv.size() * 4;
		moduleInfo.pCode = spirv.data();
		VkShaderModule module;
		Vk(vkCreateShaderModule(gpu.Device, &moduleInfo, nullptr, &module), "vkCreateShaderModule");
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		for (uint32_t i = 0; i < buffers.size(); ++i)
			bindings.push_back({ 30 + i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
		VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		VkDescriptorSetLayout empty, layout;
		Vk(vkCreateDescriptorSetLayout(gpu.Device, &layoutInfo, nullptr, &empty), "empty set layout");
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		Vk(vkCreateDescriptorSetLayout(gpu.Device, &layoutInfo, nullptr, &layout), "set layout");
		const VkDescriptorSetLayout sets[] = { empty, layout };
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutInfo.setLayoutCount = 2;
		pipelineLayoutInfo.pSetLayouts = sets;
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
			writes.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 30 + i, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &infos[i], nullptr });
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

	// The request block of SoundwaveReverb.glsl.
	struct alignas(16) ReverbRequest
	{
		float Listener[4];
		uint32_t Config[4];
		float Params[4];
		float Materials[kMaterialTagCount][4];
	};
	struct GpuReverbRay { float Absorbed, Travelled; uint32_t Hits, Traces; };

	double DecibelGap(double a, double b)
	{
		if (a == b)
			return 0;
		return std::abs(20.0 * std::log10(std::max(a, 1e-6) / std::max(b, 1e-6)));
	}

	struct Scene
	{
		const char* Name;
		std::vector<Triangle> Triangles;
		Position Listener;
	};
	void Box(std::vector<Triangle>& triangles, Position low, Position high, std::uint64_t instance, MaterialTag material, bool openEnds = false)
	{
		constexpr int indices[] = { 0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2, 1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3 };
		const auto point = [&](int i) { return Position{ i & 1 ? high.X : low.X, i & 2 ? high.Y : low.Y, i & 4 ? high.Z : low.Z }; };
		for (int i = openEnds ? 6 : 0; i < 36; i += 3)
			triangles.push_back({ point(indices[i]), point(indices[i + 1]), point(indices[i + 2]), instance, 0, material, false });
	}
}

int main(int argc, char** argv)
{
	Check(argc == 2, "usage: SoundwaveGpuParity <SoundwaveReverb.spv>");
	const auto reverbShader = ReadSpirv(argv[1]);
	Gpu gpu;
	std::cout << "Device: " << gpu.Name << '\n';

	std::vector<Scene> scenes;
	scenes.push_back({ "stone room", {}, {0,1.7f,0} });
	Box(scenes.back().Triangles, {-5,0,-5}, {5,4,5}, 1, 10);
	scenes.push_back({ "corridor", {}, {0,1.7f,0} });
	Box(scenes.back().Triangles, {-1,0,-10}, {1,3,10}, 1, 10);
	scenes.push_back({ "open corridor", {}, {0,1.7f,0} });
	Box(scenes.back().Triangles, {-1,0,-10}, {1,3,10}, 1, 10, true);
	scenes.push_back({ "carpet room", {}, {0,1.7f,0} });
	Box(scenes.back().Triangles, {-5,0,-5}, {5,4,5}, 1, 2);
	scenes.push_back({ "pillar hall", {}, {0.5f,1.7f,0.5f} });
	Box(scenes.back().Triangles, {-12,0,-12}, {12,6,12}, 1, 4);
	for (int x = -9; x <= 9; x += 3)
		for (int z = -9; z <= 9; z += 3)
			Box(scenes.back().Triangles, {x-0.3f,0,z-0.3f}, {x+0.3f,6,z+0.3f}, 100 + static_cast<std::uint64_t>((x + 9) * 7 + z + 9), 1);

	double worst = 0;
	for (const auto& scene : scenes)
	{
		Grid grid;
		Check(grid.Build(scene.Triangles) == GeometryError::None, std::string(scene.Name) + " geometry invalid");
		// Exclude one pillar to check the baked active flags.
		const std::uint64_t excluded[] = { 120 };
		const GeometryView view(grid, nullptr, std::string(scene.Name) == "pillar hall" ? std::span<const std::uint64_t>(excluded) : std::span<const std::uint64_t>{});
		Settings settings;
		const auto materials = DefaultMaterials();
		std::vector<RayHit> hits;
		ReverbResult cpu;
		Check(EvaluateReverb(view, materials, settings, scene.Listener, hits, cpu) == EvaluationError::None, "CPU reverb failed");

		FlatGeometry flat;
		Check(Flatten(view, flat), "flatten failed");
		HostBuffer nodes(gpu, flat.Nodes.size() * sizeof(FlatNode)), order(gpu, flat.Order.size() * 4),
			triangles(gpu, flat.Triangles.size() * sizeof(FlatTriangle)), request(gpu, sizeof(ReverbRequest)),
			output(gpu, settings.ReverbRays * sizeof(GpuReverbRay));
		nodes.Write(flat.Nodes.data(), flat.Nodes.size() * sizeof(FlatNode));
		order.Write(flat.Order.data(), flat.Order.size() * 4);
		triangles.Write(flat.Triangles.data(), flat.Triangles.size() * sizeof(FlatTriangle));
		ReverbRequest block{};
		block.Listener[0] = scene.Listener.X; block.Listener[1] = scene.Listener.Y; block.Listener[2] = scene.Listener.Z;
		block.Config[0] = settings.ReverbRays; block.Config[1] = settings.ReverbBounces;
		block.Config[2] = flat.Roots[0]; block.Config[3] = flat.Roots[1];
		block.Params[0] = settings.Absorption;
		for (size_t i = 0; i < kMaterialTagCount; ++i)
		{
			block.Materials[i][0] = materials[i].AbsorptionLF;
			block.Materials[i][1] = materials[i].AbsorptionHF;
			block.Materials[i][2] = materials[i].Scattering;
		}
		request.Write(&block, sizeof(block));
		Dispatch(gpu, reverbShader, { &nodes, &order, &triangles, &request, &output }, (settings.ReverbRays + 63) / 64);

		std::vector<ReverbRay> rays(settings.ReverbRays);
		const auto* written = static_cast<const GpuReverbRay*>(output.Mapped);
		uint32_t differing = 0;
		for (uint32_t i = 0; i < settings.ReverbRays; ++i)
		{
			rays[i] = { written[i].Absorbed, written[i].Travelled, written[i].Hits, written[i].Traces & 0x7FFFFFFFu, written[i].Traces >> 31 };
			ReverbRay reference;
			Check(TraceReverbRay(view, materials, settings, scene.Listener, i, hits, reference) == EvaluationError::None, "CPU ray failed");
			differing += reference.Hits != rays[i].Hits || reference.Escaped != rays[i].Escaped;
		}
		ReverbResult gpuResult;
		Check(FinishReverb(rays, settings, gpuResult) == EvaluationError::None, "GPU reverb did not reduce");
		const double gap = DecibelGap(gpuResult.Send, cpu.Send);
		worst = std::max(worst, gap);
		std::cout << scene.Name << ": send cpu=" << cpu.Send << " gpu=" << gpuResult.Send << " (" << gap << " dB), decay cpu="
			<< cpu.DecaySeconds << " gpu=" << gpuResult.DecaySeconds << ", rays with different paths " << differing << '/' << settings.ReverbRays << '\n';
		Check(gap <= 0.5, std::string(scene.Name) + ": GPU reverb send outside 0.5 dB of the CPU reference");
	}
	std::cout << "PASS: Soundwave GPU reverb within " << worst << " dB of the CPU reference on " << scenes.size() << " scenes\n";
}
