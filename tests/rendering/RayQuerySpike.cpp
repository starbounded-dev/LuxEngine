// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
//
// GI0 spike: on a real Vulkan device, outside the engine, build a BLAS + TLAS for a plane at a known
// distance, run one ray-query compute dispatch (RayQuerySpike.comp) and check the committed hit
// distances against the analytic values. Proves the hardware + shader path (VK_KHR_acceleration_structure
// + VK_KHR_ray_query) before the engine RHI wrapper is built on it. Prints SKIP when the GPU lacks ray query.

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
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

	PFN_vkCreateAccelerationStructureKHR pCreateAS;
	PFN_vkDestroyAccelerationStructureKHR pDestroyAS;
	PFN_vkGetAccelerationStructureBuildSizesKHR pBuildSizes;
	PFN_vkCmdBuildAccelerationStructuresKHR pCmdBuildAS;
	PFN_vkGetAccelerationStructureDeviceAddressKHR pASAddress;

	bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name)
	{
		return std::any_of(list.begin(), list.end(), [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
	}

	struct Gpu
	{
		VkInstance Instance{};
		VkPhysicalDevice Physical{};
		VkDevice Device{};
		VkQueue Queue{};
		uint32_t Family = 0;
		VkPhysicalDeviceMemoryProperties Memory{};
		VkDeviceSize ScratchAlignment = 256;
		std::string Name;
		bool RayQuery = false;

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

			uint32_t extensionCount = 0;
			vkEnumerateDeviceExtensionProperties(Physical, nullptr, &extensionCount, nullptr);
			std::vector<VkExtensionProperties> extensions(extensionCount);
			vkEnumerateDeviceExtensionProperties(Physical, nullptr, &extensionCount, extensions.data());
			RayQuery = HasExtension(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) && HasExtension(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME)
				&& HasExtension(extensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
			if (!RayQuery)
				return;

			vkGetPhysicalDeviceQueueFamilyProperties(Physical, &count, nullptr);
			std::vector<VkQueueFamilyProperties> families(count);
			vkGetPhysicalDeviceQueueFamilyProperties(Physical, &count, families.data());
			Family = UINT32_MAX;
			for (uint32_t i = 0; i < count && Family == UINT32_MAX; ++i)
				if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
					Family = i;
			Check(Family != UINT32_MAX, "no compute queue");

			VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
			VkPhysicalDeviceProperties2 properties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asProperties };
			vkGetPhysicalDeviceProperties2(Physical, &properties2);
			ScratchAlignment = std::max<VkDeviceSize>(asProperties.minAccelerationStructureScratchOffsetAlignment, 1);

			VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
			rayQuery.rayQuery = VK_TRUE;
			VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rayQuery };
			asFeatures.accelerationStructure = VK_TRUE;
			VkPhysicalDeviceVulkan12Features v12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &asFeatures };
			v12.bufferDeviceAddress = VK_TRUE;
			const float priority = 1;
			VkDeviceQueueCreateInfo queue{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
			queue.queueFamilyIndex = Family;
			queue.queueCount = 1;
			queue.pQueuePriorities = &priority;
			const char* enabled[] = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME };
			VkDeviceCreateInfo device{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &v12 };
			device.queueCreateInfoCount = 1;
			device.pQueueCreateInfos = &queue;
			device.enabledExtensionCount = 3;
			device.ppEnabledExtensionNames = enabled;
			Vk(vkCreateDevice(Physical, &device, nullptr, &Device), "vkCreateDevice");
			vkGetDeviceQueue(Device, Family, 0, &Queue);

#define LOAD(var, name) var = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(Device, #name)); Check(var != nullptr, "missing " #name)
			LOAD(pCreateAS, vkCreateAccelerationStructureKHR);
			LOAD(pDestroyAS, vkDestroyAccelerationStructureKHR);
			LOAD(pBuildSizes, vkGetAccelerationStructureBuildSizesKHR);
			LOAD(pCmdBuildAS, vkCmdBuildAccelerationStructuresKHR);
			LOAD(pASAddress, vkGetAccelerationStructureDeviceAddressKHR);
#undef LOAD
		}
		~Gpu()
		{
			if (Device)
				vkDestroyDevice(Device, nullptr);
			vkDestroyInstance(Instance, nullptr);
		}
	};

	// Host-visible, coherent, device-addressable buffer: simplest thing that works for a spike.
	struct Buffer
	{
		VkDevice Device{};
		VkBuffer Handle{};
		VkDeviceMemory Memory{};
		VkDeviceSize Size = 0;
		VkDeviceAddress Address = 0;
		void* Mapped = nullptr;

		Buffer(Gpu& gpu, VkDeviceSize size, VkBufferUsageFlags usage) : Device(gpu.Device), Size(std::max<VkDeviceSize>(size, 256))
		{
			VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			info.size = Size;
			info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			Vk(vkCreateBuffer(Device, &info, nullptr, &Handle), "vkCreateBuffer");
			VkMemoryRequirements requirements;
			vkGetBufferMemoryRequirements(Device, Handle, &requirements);
			const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			uint32_t type = UINT32_MAX;
			for (uint32_t i = 0; i < gpu.Memory.memoryTypeCount && type == UINT32_MAX; ++i)
				if ((requirements.memoryTypeBits & (1u << i)) && (gpu.Memory.memoryTypes[i].propertyFlags & wanted) == wanted)
					type = i;
			Check(type != UINT32_MAX, "no host-visible memory");
			VkMemoryAllocateFlagsInfo flags{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
			flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
			VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags };
			allocate.allocationSize = requirements.size;
			allocate.memoryTypeIndex = type;
			Vk(vkAllocateMemory(Device, &allocate, nullptr, &Memory), "vkAllocateMemory");
			Vk(vkBindBufferMemory(Device, Handle, Memory, 0), "vkBindBufferMemory");
			Vk(vkMapMemory(Device, Memory, 0, Size, 0, &Mapped), "vkMapMemory");
			std::memset(Mapped, 0, static_cast<size_t>(Size));
			VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			addressInfo.buffer = Handle;
			Address = vkGetBufferDeviceAddress(Device, &addressInfo);
		}
		~Buffer()
		{
			vkUnmapMemory(Device, Memory);
			vkDestroyBuffer(Device, Handle, nullptr);
			vkFreeMemory(Device, Memory, nullptr);
		}
		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;
	};

	struct AccelerationStructure
	{
		VkDevice Device{};
		VkAccelerationStructureKHR Handle{};
		Buffer* Storage = nullptr;
		VkDeviceAddress Address = 0;
	};

	VkDeviceAddress Align(VkDeviceAddress value, VkDeviceSize alignment) { return (value + alignment - 1) / alignment * alignment; }

	VkCommandBuffer BeginCommands(Gpu& gpu, VkCommandPool pool)
	{
		VkCommandBufferAllocateInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		commandInfo.commandPool = pool;
		commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandInfo.commandBufferCount = 1;
		VkCommandBuffer commands;
		Vk(vkAllocateCommandBuffers(gpu.Device, &commandInfo, &commands), "command buffer");
		VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		Vk(vkBeginCommandBuffer(commands, &begin), "begin commands");
		return commands;
	}

	void SubmitAndWait(Gpu& gpu, VkCommandBuffer commands)
	{
		Vk(vkEndCommandBuffer(commands), "end commands");
		VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &commands;
		Vk(vkQueueSubmit(gpu.Queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
		Vk(vkQueueWaitIdle(gpu.Queue), "vkQueueWaitIdle");
	}

	// Creates the AS storage for the given build info and records its build into `commands`.
	AccelerationStructure Build(Gpu& gpu, VkCommandBuffer commands, VkAccelerationStructureTypeKHR type, const VkAccelerationStructureGeometryKHR& geometry,
		uint32_t primitiveCount, std::vector<Buffer*>& keepAlive)
	{
		VkAccelerationStructureBuildGeometryInfoKHR build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		build.type = type;
		build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		build.geometryCount = 1;
		build.pGeometries = &geometry;
		VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		pBuildSizes(gpu.Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &primitiveCount, &sizes);

		AccelerationStructure as{};
		as.Device = gpu.Device;
		as.Storage = new Buffer(gpu, sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);
		keepAlive.push_back(as.Storage);
		VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		createInfo.buffer = as.Storage->Handle;
		createInfo.size = sizes.accelerationStructureSize;
		createInfo.type = type;
		Vk(pCreateAS(gpu.Device, &createInfo, nullptr, &as.Handle), "vkCreateAccelerationStructureKHR");

		auto* scratch = new Buffer(gpu, sizes.buildScratchSize + gpu.ScratchAlignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		keepAlive.push_back(scratch);
		build.dstAccelerationStructure = as.Handle;
		build.scratchData.deviceAddress = Align(scratch->Address, gpu.ScratchAlignment);
		VkAccelerationStructureBuildRangeInfoKHR range{};
		range.primitiveCount = primitiveCount;
		const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
		pCmdBuildAS(commands, 1, &build, &ranges);

		VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR };
		vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);

		// The address is valid as soon as the AS exists (the build is only recorded).
		VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		addressInfo.accelerationStructure = as.Handle;
		as.Address = pASAddress(gpu.Device, &addressInfo);
		return as;
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

	struct Ray
	{
		float Origin[3];
		float TMin;
		float Direction[3];
		float TMax;
	};
}

int main(int argc, char** argv)
{
	Check(argc >= 2, "usage: RayQuerySpike <RayQuerySpike.spv>");
	Gpu gpu;
	if (!gpu.RayQuery)
	{
		std::cout << "SKIP: " << gpu.Name << " has no VK_KHR_acceleration_structure + VK_KHR_ray_query\n";
		return 0;
	}
	std::cout << "device: " << gpu.Name << '\n';

	// Plane z = 5: two triangles covering x,y in [-10, 10].
	const float vertices[] = { -10, -10, 5, 10, -10, 5, 10, 10, 5, -10, 10, 5 };
	const uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };
	std::vector<Buffer*> keepAlive;
	constexpr VkBufferUsageFlags asInput = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	Buffer vertexBuffer(gpu, sizeof(vertices), asInput);
	Buffer indexBuffer(gpu, sizeof(indices), asInput);
	std::memcpy(vertexBuffer.Mapped, vertices, sizeof(vertices));
	std::memcpy(indexBuffer.Mapped, indices, sizeof(indices));

	VkCommandPoolCreateInfo commandPoolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	commandPoolInfo.queueFamilyIndex = gpu.Family;
	VkCommandPool commandPool;
	Vk(vkCreateCommandPool(gpu.Device, &commandPoolInfo, nullptr, &commandPool), "command pool");

	// BLAS
	VkAccelerationStructureGeometryKHR blasGeometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	blasGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	blasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	auto& triangles = blasGeometry.geometry.triangles;
	triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	triangles.vertexData.deviceAddress = vertexBuffer.Address;
	triangles.vertexStride = 12;
	triangles.maxVertex = 3;
	triangles.indexType = VK_INDEX_TYPE_UINT32;
	triangles.indexData.deviceAddress = indexBuffer.Address;

	VkCommandBuffer blasCommands = BeginCommands(gpu, commandPool);
	AccelerationStructure blas = Build(gpu, blasCommands, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blasGeometry, 2, keepAlive);
	SubmitAndWait(gpu, blasCommands);

	// TLAS with one identity instance
	VkAccelerationStructureInstanceKHR instance{};
	instance.transform = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };
	instance.mask = 0xFF;
	instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	instance.accelerationStructureReference = blas.Address;
	Buffer instanceBuffer(gpu, sizeof(instance), asInput);
	std::memcpy(instanceBuffer.Mapped, &instance, sizeof(instance));

	VkAccelerationStructureGeometryKHR tlasGeometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	tlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	tlasGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	tlasGeometry.geometry.instances.data.deviceAddress = instanceBuffer.Address;

	VkCommandBuffer tlasCommands = BeginCommands(gpu, commandPool);
	AccelerationStructure tlas = Build(gpu, tlasCommands, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, tlasGeometry, 1, keepAlive);
	SubmitAndWait(gpu, tlasCommands);

	// Rays and their analytic answers.
	struct Case { Ray Value; float Expected; };
	std::vector<Case> cases;
	auto add = [&](float ox, float oy, float oz, float dx, float dy, float dz, float expected)
	{
		const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
		cases.push_back({ { { ox, oy, oz }, 0.0f, { dx / len, dy / len, dz / len }, 1000.0f }, expected });
	};
	add(0, 0, 0, 0, 0, 1, 5.0f);                 // straight on
	add(3, -4, 0, 0, 0, 1, 5.0f);                // off-axis, still inside the quad
	add(0, 0, 1, 0, 0, 1, 4.0f);                 // origin closer to the plane
	add(0, 0, 0, 0.6f, 0, 0.8f, 6.25f);          // tilted: 5 / 0.8
	add(0, 0, 0, 0, 0, -1, -1.0f);               // pointing away: miss
	add(50, 0, 0, 0, 0, 1, -1.0f);               // outside the quad: miss
	add(0, 0, 0, 0, 0, 1, -1.0f);                // replaced below with a short tMax
	cases.back().Value.TMax = 4.0f;              // tMax before the plane: miss
	for (int i = 0; i < 100; ++i)                // a sweep of straight-on rays
		add(-9.0f + 0.18f * i, 8.0f - 0.16f * i, -2.0f, 0, 0, 1, 7.0f);

	Buffer rayBuffer(gpu, cases.size() * sizeof(Ray), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	Buffer hitBuffer(gpu, cases.size() * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	for (size_t i = 0; i < cases.size(); ++i)
		std::memcpy(static_cast<Ray*>(rayBuffer.Mapped) + i, &cases[i].Value, sizeof(Ray));

	// Compute pipeline: set 0 = {AS, rays, hits}; push constant = ray count.
	const std::vector<uint32_t> spirv = ReadSpirv(argv[1]);
	VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	moduleInfo.codeSize = spirv.size() * 4;
	moduleInfo.pCode = spirv.data();
	VkShaderModule module;
	Vk(vkCreateShaderModule(gpu.Device, &moduleInfo, nullptr, &module), "vkCreateShaderModule");
	const VkDescriptorSetLayoutBinding bindings[] = {
		{ 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
		{ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	layoutInfo.bindingCount = 3;
	layoutInfo.pBindings = bindings;
	VkDescriptorSetLayout layout;
	Vk(vkCreateDescriptorSetLayout(gpu.Device, &layoutInfo, nullptr, &layout), "set layout");
	VkPushConstantRange pushRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &layout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushRange;
	VkPipelineLayout pipelineLayout;
	Vk(vkCreatePipelineLayout(gpu.Device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "pipeline layout");
	VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pipelineInfo.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr };
	pipelineInfo.layout = pipelineLayout;
	VkPipeline pipeline;
	Vk(vkCreateComputePipelines(gpu.Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "compute pipeline");

	const VkDescriptorPoolSize poolSizes[] = { { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 } };
	VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = poolSizes;
	VkDescriptorPool pool;
	Vk(vkCreateDescriptorPool(gpu.Device, &poolInfo, nullptr, &pool), "descriptor pool");
	VkDescriptorSetAllocateInfo allocate{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocate.descriptorPool = pool;
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = &layout;
	VkDescriptorSet set;
	Vk(vkAllocateDescriptorSets(gpu.Device, &allocate, &set), "descriptor set");
	VkWriteDescriptorSetAccelerationStructureKHR asWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
	asWrite.accelerationStructureCount = 1;
	asWrite.pAccelerationStructures = &tlas.Handle;
	const VkDescriptorBufferInfo rayInfo{ rayBuffer.Handle, 0, VK_WHOLE_SIZE };
	const VkDescriptorBufferInfo hitInfo{ hitBuffer.Handle, 0, VK_WHOLE_SIZE };
	VkWriteDescriptorSet writes[3]{};
	writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &asWrite, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, nullptr, nullptr };
	writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &rayInfo, nullptr };
	writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &hitInfo, nullptr };
	vkUpdateDescriptorSets(gpu.Device, 3, writes, 0, nullptr);

	const uint32_t rayCount = static_cast<uint32_t>(cases.size());
	VkCommandBuffer commands = BeginCommands(gpu, commandPool);
	vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
	vkCmdPushConstants(commands, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &rayCount);
	vkCmdDispatch(commands, (rayCount + 63) / 64, 1, 1);
	VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
	vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	SubmitAndWait(gpu, commands);

	int failures = 0;
	const float* hits = static_cast<const float*>(hitBuffer.Mapped);
	for (size_t i = 0; i < cases.size(); ++i)
	{
		const float t = hits[i * 4 + 0];
		const bool hit = hits[i * 4 + 1] > 0.5f;
		const float expected = cases[i].Expected;
		const bool expectHit = expected >= 0.0f;
		const bool ok = (hit == expectHit) && (!expectHit || std::fabs(t - expected) < 1e-3f);
		if (!ok)
		{
			++failures;
			std::cerr << "case " << i << ": expected " << expected << " got t=" << t << " hit=" << hit << '\n';
		}
	}

	vkDestroyDescriptorPool(gpu.Device, pool, nullptr);
	vkDestroyPipeline(gpu.Device, pipeline, nullptr);
	vkDestroyPipelineLayout(gpu.Device, pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(gpu.Device, layout, nullptr);
	vkDestroyShaderModule(gpu.Device, module, nullptr);
	vkDestroyCommandPool(gpu.Device, commandPool, nullptr);
	pDestroyAS(gpu.Device, tlas.Handle, nullptr);
	pDestroyAS(gpu.Device, blas.Handle, nullptr);
	for (Buffer* buffer : keepAlive)
		delete buffer;

	Check(failures == 0, std::to_string(failures) + " of " + std::to_string(cases.size()) + " ray cases mismatched");
	std::cout << "PASS: " << cases.size() << " ray-query cases match the analytic distances\n";
	return 0;
}
