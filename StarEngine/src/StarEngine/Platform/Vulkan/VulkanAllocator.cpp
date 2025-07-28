#include "sepch.h"
#include "VulkanAllocator.h"

#include "VulkanContext.h"

#include "StarEngine/Utilities/StringUtils.h"

#if SE_LOG_RENDERER_ALLOCATIONS
#define SE_ALLOCATOR_LOG(...) SE_CORE_TRACE(__VA_ARGS__)
#else
#define SE_ALLOCATOR_LOG(...)
#endif

#define SE_GPU_TRACK_MEMORY_ALLOCATION 1

namespace StarEngine {

	struct VulkanAllocatorData
	{
		VmaAllocator Allocator;
		uint64_t TotalAllocatedBytes = 0;
		
		uint64_t MemoryUsage = 0; // all heaps
	};

	enum class AllocationType : uint8_t
	{
		None = 0, Buffer = 1, Image = 2
	};

	static VulkanAllocatorData* s_Data = nullptr;
	struct AllocInfo
	{
		uint64_t AllocatedSize = 0;
		AllocationType Type = AllocationType::None;
	};
	static std::map<VmaAllocation, AllocInfo> s_AllocationMap;

	VulkanAllocator::VulkanAllocator(const std::string& tag)
		: m_Tag(tag)
	{
	}

	VulkanAllocator::~VulkanAllocator()
	{
	}

#if 0
	void VulkanAllocator::Allocate(VkMemoryRequirements requirements, VkDeviceMemory* dest, VkMemoryPropertyFlags flags /*= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT*/)
	{
		SE_CORE_ASSERT(m_Device);

		// TODO: Tracking
		SE_CORE_TRACE("VulkanAllocator ({0}): allocating {1}", m_Tag, Utils::BytesToString(requirements.size));

		{
			static uint64_t totalAllocatedBytes = 0;
			totalAllocatedBytes += requirements.size;
			SE_CORE_TRACE("VulkanAllocator ({0}): total allocated since start is {1}", m_Tag, Utils::BytesToString(totalAllocatedBytes));
		}

		VkMemoryAllocateInfo memAlloc = {};
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAlloc.allocationSize = requirements.size;
		memAlloc.memoryTypeIndex = m_Device->GetPhysicalDevice()->GetMemoryTypeIndex(requirements.memoryTypeBits, flags);
		VK_CHECK_RESULT(vkAllocateMemory(m_Device->GetVulkanDevice(), &memAlloc, nullptr, dest));
	}
#endif

	VmaAllocation VulkanAllocator::AllocateBuffer(VkBufferCreateInfo bufferCreateInfo, VmaMemoryUsage usage, VkBuffer& outBuffer)
	{
		SE_CORE_VERIFY(bufferCreateInfo.size > 0);

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = usage;

		VmaAllocation allocation;
		vmaCreateBuffer(s_Data->Allocator, &bufferCreateInfo, &allocCreateInfo, &outBuffer, &allocation, nullptr);
		if (allocation == nullptr)
		{
			SE_CORE_ERROR_TAG("Renderer", "Failed to allocate GPU buffer!");
			SE_CORE_ERROR_TAG("Renderer", "  Requested size: {}", Utils::BytesToString(bufferCreateInfo.size));
			auto stats = GetStats();
			SE_CORE_ERROR_TAG("Renderer", "  GPU mem usage: {}/{}", Utils::BytesToString(stats.Used), Utils::BytesToString(stats.TotalAvailable));
		}

		// TODO: Tracking
		VmaAllocationInfo allocInfo{};
		vmaGetAllocationInfo(s_Data->Allocator, allocation, &allocInfo);
		SE_ALLOCATOR_LOG("VulkanAllocator ({0}): allocating buffer; size = {1}", m_Tag, Utils::BytesToString(allocInfo.size));

		{
			s_Data->TotalAllocatedBytes += allocInfo.size;
			SE_ALLOCATOR_LOG("VulkanAllocator ({0}): total allocated since start is {1}", m_Tag, Utils::BytesToString(s_Data->TotalAllocatedBytes));
		}

#if SE_GPU_TRACK_MEMORY_ALLOCATION
		auto& allocTrack = s_AllocationMap[allocation];
		allocTrack.AllocatedSize = allocInfo.size;
		allocTrack.Type = AllocationType::Buffer;
		s_Data->MemoryUsage += allocInfo.size;
#endif

		return allocation;
	}

	VmaAllocation VulkanAllocator::AllocateImage(VkImageCreateInfo imageCreateInfo, VmaMemoryUsage usage, VkImage& outImage, VkDeviceSize* allocatedSize)
	{
		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = usage;

		VmaAllocation allocation;
		vmaCreateImage(s_Data->Allocator, &imageCreateInfo, &allocCreateInfo, &outImage, &allocation, nullptr);
		if (allocation == nullptr)
		{
			SE_CORE_ERROR_TAG("Renderer", "Failed to allocate GPU image!");
			SE_CORE_ERROR_TAG("Renderer", "  Requested size: {}x{}x{}", imageCreateInfo.extent.width, imageCreateInfo.extent.height, imageCreateInfo.extent.depth);
			SE_CORE_ERROR_TAG("Renderer", "  Mips: {}", imageCreateInfo.mipLevels);
			SE_CORE_ERROR_TAG("Renderer", "  Layers: {}", imageCreateInfo.arrayLayers);
			auto stats = GetStats();
			SE_CORE_ERROR_TAG("Renderer", "  GPU mem usage: {}/{}", Utils::BytesToString(stats.Used), Utils::BytesToString(stats.TotalAvailable));
		}

		// TODO: Tracking
		VmaAllocationInfo allocInfo;
		vmaGetAllocationInfo(s_Data->Allocator, allocation, &allocInfo);
		if (allocatedSize)
			*allocatedSize = allocInfo.size;
		SE_ALLOCATOR_LOG("VulkanAllocator ({0}): allocating image; size = {1}", m_Tag, Utils::BytesToString(allocInfo.size));

		{
			s_Data->TotalAllocatedBytes += allocInfo.size;
			SE_ALLOCATOR_LOG("VulkanAllocator ({0}): total allocated since start is {1}", m_Tag, Utils::BytesToString(s_Data->TotalAllocatedBytes));
		}

#if SE_GPU_TRACK_MEMORY_ALLOCATION
		auto& allocTrack = s_AllocationMap[allocation];
		allocTrack.AllocatedSize = allocInfo.size;
		allocTrack.Type = AllocationType::Image;
		s_Data->MemoryUsage += allocInfo.size;
#endif

		return allocation;
	}

	void VulkanAllocator::Free(VmaAllocation allocation)
	{
		vmaFreeMemory(s_Data->Allocator, allocation);

#if SE_GPU_TRACK_MEMORY_ALLOCATION
		auto it = s_AllocationMap.find(allocation);
		if (it != s_AllocationMap.end())
		{
			s_Data->MemoryUsage -= it->second.AllocatedSize;
			s_AllocationMap.erase(it);
		}
		else
		{
			SE_CORE_ERROR("Could not find GPU memory allocation: {}", (void*)allocation);
		}
#endif
	}

	void VulkanAllocator::DestroyImage(VkImage image, VmaAllocation allocation)
	{
		SE_CORE_ASSERT(image);
		SE_CORE_ASSERT(allocation);
		vmaDestroyImage(s_Data->Allocator, image, allocation);

#if SE_GPU_TRACK_MEMORY_ALLOCATION
		auto it = s_AllocationMap.find(allocation);
		if (it != s_AllocationMap.end())
		{
			s_Data->MemoryUsage -= it->second.AllocatedSize;
			s_AllocationMap.erase(it);
		}
		else
		{
			SE_CORE_ERROR("Could not find GPU memory allocation: {}", (void*)allocation);
		}
#endif
	}

	void VulkanAllocator::DestroyBuffer(VkBuffer buffer, VmaAllocation allocation)
	{
		SE_CORE_ASSERT(buffer);
		SE_CORE_ASSERT(allocation);
		vmaDestroyBuffer(s_Data->Allocator, buffer, allocation);

#if SE_GPU_TRACK_MEMORY_ALLOCATION
		auto it = s_AllocationMap.find(allocation);
		if (it != s_AllocationMap.end())
		{
			s_Data->MemoryUsage -= it->second.AllocatedSize;
			s_AllocationMap.erase(it);
		}
		else
		{
			SE_CORE_ERROR("Could not find GPU memory allocation: {}", (void*)allocation);
		}
#endif
	}

	void VulkanAllocator::UnmapMemory(VmaAllocation allocation)
	{
		vmaUnmapMemory(s_Data->Allocator, allocation);
	}

	void VulkanAllocator::DumpStats()
	{
		const auto& memoryProps = VulkanContext::GetCurrentDevice()->GetPhysicalDevice()->GetMemoryProperties();
		std::vector<VmaBudget> budgets(memoryProps.memoryHeapCount);
		vmaGetBudget(s_Data->Allocator, budgets.data());

		SE_CORE_WARN("-----------------------------------");
		for (VmaBudget& b : budgets)
		{
			SE_CORE_WARN("VmaBudget.allocationBytes = {0}", Utils::BytesToString(b.allocationBytes));
			SE_CORE_WARN("VmaBudget.blockBytes = {0}", Utils::BytesToString(b.blockBytes));
			SE_CORE_WARN("VmaBudget.usage = {0}", Utils::BytesToString(b.usage));
			SE_CORE_WARN("VmaBudget.budget = {0}", Utils::BytesToString(b.budget));
		}
		SE_CORE_WARN("-----------------------------------");
	}

	GPUMemoryStats VulkanAllocator::GetStats()
	{
		const auto& memoryProps = VulkanContext::GetCurrentDevice()->GetPhysicalDevice()->GetMemoryProperties();
		std::vector<VmaBudget> budgets(memoryProps.memoryHeapCount);
		vmaGetBudget(s_Data->Allocator, budgets.data());

		uint64_t budget = 0;
		for (VmaBudget& b : budgets)
			budget += b.budget;

		GPUMemoryStats result;
		for (const auto& [k, v] : s_AllocationMap)
		{
			if (v.Type == AllocationType::Buffer)
			{
				result.BufferAllocationCount++;
				result.BufferAllocationSize += v.AllocatedSize;
			}
			else if (v.Type == AllocationType::Image)
			{
				result.ImageAllocationCount++;
				result.ImageAllocationSize += v.AllocatedSize;
			}
		}

		result.AllocationCount = s_AllocationMap.size();
		result.Used = s_Data->MemoryUsage;
		result.TotalAvailable = budget;
		return result;
#if 0
		VmaStats stats;
		vmaCalculateStats(s_Data->Allocator, &stats);

		uint64_t usedMemory = stats.total.usedBytes;
		uint64_t freeMemory = stats.total.unusedBytes;

		return { usedMemory, freeMemory };
#endif
	}

	void VulkanAllocator::Init(Ref<VulkanDevice> device)
	{
		s_Data = snew VulkanAllocatorData();

		// Initialize VulkanMemoryAllocator
		VmaAllocatorCreateInfo allocatorInfo = {};
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
		allocatorInfo.physicalDevice = device->GetPhysicalDevice()->GetVulkanPhysicalDevice();
		allocatorInfo.device = device->GetVulkanDevice();
		allocatorInfo.instance = VulkanContext::GetInstance();

		vmaCreateAllocator(&allocatorInfo, &s_Data->Allocator);
	}

	void VulkanAllocator::Shutdown()
	{
		vmaDestroyAllocator(s_Data->Allocator);

		delete s_Data;
		s_Data = nullptr;
	}

	VmaAllocator& VulkanAllocator::GetVMAAllocator()
	{
		return s_Data->Allocator;
	}

} 
