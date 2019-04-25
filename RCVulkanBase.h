
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
