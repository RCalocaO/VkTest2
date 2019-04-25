
#pragma once

#include "../volk/volk.h"

template <typename T>
inline void ZeroVulkanMem(T& VulkanStruct, VkStructureType Type)
{
	//	static_assert( (void*)&VulkanStruct == (void*)&VulkanStruct.sType, "Vulkan struct size mismatch");
	VulkanStruct.sType = Type;
	const auto Size = sizeof(VulkanStruct) - sizeof(VkStructureType);
	memset((uint8*)&VulkanStruct.sType + sizeof(VkStructureType), 0, Size);
}

#define VERIFY_VKRESULT(x)	if ((x) != VK_SUCCESS) { ::OutputDebugStringA(#x); ::OutputDebugStringA("\n"); check(0);}

#if USE_VMA
#define VMA_MAX(x, y) Max((x), (y))
#define VMA_MIN(x, y) Min((x), (y))
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#include "../VulkanMemoryAllocator/src/vk_mem_alloc.h"
#endif
