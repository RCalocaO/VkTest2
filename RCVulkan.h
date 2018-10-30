#pragma once

#include "../RCUtils/RCUtilsBase.h"
#include "../volk/volk.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define VERIFY_VKRESULT(x)	if ((x) != VK_SUCCESS) { ::OutputDebugStringA(#x); ::OutputDebugStringA("\n"); check(0);}


template <typename T>
inline void ZeroVulkanMem(T& VulkanStruct, VkStructureType Type)
{
//	static_assert( (void*)&VulkanStruct == (void*)&VulkanStruct.sType, "Vulkan struct size mismatch");
	VulkanStruct.sType = Type;
	const auto Size = sizeof(VulkanStruct) - sizeof(VkStructureType);
	memset((uint8_t*)&VulkanStruct.sType + sizeof(VkStructureType), 0, Size);
}

struct SVulkanInstance
{
	VkInstance Instance = VK_NULL_HANDLE;
	VkDebugReportCallbackEXT DebugReportCallback = nullptr;

	void Init()
	{
		VERIFY_VKRESULT(volkInitialize());

		CreateInstance();

		volkLoadInstance(Instance);

		InitDebugCallback();
	}

	static VkBool32 DebugReport(VkDebugReportFlagsEXT Flags, VkDebugReportObjectTypeEXT ObjectType, uint64_t Object,
		size_t Location, int32_t MessageCode, const char* LayerPrefix, const char* Message, void* UserData)
	{
		return VK_FALSE;
	}

	void InitDebugCallback()
	{
		VkDebugReportCallbackCreateInfoEXT Info;
		ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT);
		Info.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT;
		Info.pfnCallback = DebugReport;

		VERIFY_VKRESULT(vkCreateDebugReportCallbackEXT(Instance, &Info, 0, &DebugReportCallback));
	}

	static void VerifyLayer(const std::vector<VkLayerProperties>& LayerProperties, const char* Name)
	{
		for (const auto& Entry : LayerProperties)
		{
			if (!strcmp(Entry.layerName, Name))
			{
				return;
			}
		}

		check(0);
	}

	template <size_t N>
	static void VerifyLayers(const std::vector<VkLayerProperties>& LayerProperties, const char* (&Layers)[N])
	{
		for (size_t t = 0; t < N; ++t)
		{
			VerifyLayer(LayerProperties, Layers[t]);
		}
	}

	static void VerifyExtension(const std::vector<VkExtensionProperties>& ExtensionProperties, const char* Name)
	{
		for (const auto& Entry : ExtensionProperties)
		{
			if (!strcmp(Entry.extensionName, Name))
			{
				return;
			}
		}

		check(0);
	}

	template <size_t N>
	static void VerifyExtensions(const std::vector<VkExtensionProperties>& ExtensionProperties, const char* (&Extensions)[N])
	{
		for (size_t t = 0; t < N; ++t)
		{
			VerifyExtension(ExtensionProperties, Extensions[t]);
		}
	}

	void CreateInstance()
	{
		VkInstanceCreateInfo Info;
		ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
		//Info.

		uint32_t Count = 0;
		VERIFY_VKRESULT(vkEnumerateInstanceLayerProperties(&Count, nullptr));

		std::vector<VkLayerProperties> LayerProperties;
		LayerProperties.resize(Count);
		VERIFY_VKRESULT(vkEnumerateInstanceLayerProperties(&Count, &LayerProperties[0]));

		Count = 0;
		VERIFY_VKRESULT(vkEnumerateInstanceExtensionProperties(nullptr, &Count, nullptr));

		std::vector<VkExtensionProperties> ExtensionProperties;
		ExtensionProperties.resize(Count);
		VERIFY_VKRESULT(vkEnumerateInstanceExtensionProperties(nullptr, &Count, &ExtensionProperties[0]));

		const char* Layers[] =
		{
			"VK_LAYER_LUNARG_standard_validation",
		};
		VerifyLayers(LayerProperties, Layers);
		Info.ppEnabledLayerNames = Layers;
		Info.enabledLayerCount = sizeof(Layers) / sizeof(Layers[0]);

		const char* Extensions[] = 
		{
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
		};
		VerifyExtensions(ExtensionProperties, Extensions);
		Info.ppEnabledExtensionNames = Extensions;
		Info.enabledExtensionCount = sizeof(Extensions) / sizeof(Extensions[0]);

		VERIFY_VKRESULT(vkCreateInstance(&Info, nullptr, &Instance));
	}

	void Deinit()
	{
		vkDestroyInstance(Instance, nullptr);
		Instance = VK_NULL_HANDLE;
	}
};
