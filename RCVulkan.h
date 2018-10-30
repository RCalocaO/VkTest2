#pragma once

#include "../RCUtils/RCUtilsBase.h"
#include "../volk/volk.h"


struct SVulkanInstance
{
	VkInstance Instance = VK_NULL_HANDLE;

	void Init()
	{
		VkResult Result = volkInitialize();
		check(Result == VK_SUCCESS);

		volkLoadInstance(Instance);
	}

	void Deinit()
	{

	}
};
