#pragma once

#include "../RCUtils/RCUtilsBase.h"
#include "../RCUtils/RCUtilsCmdLine.h"

/*
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
#endif

#ifdef APIENTRY
#undef APIENTRY
#endif
*/

#include "../volk/volk.h"

#include <GLFW/glfw3native.h>


#define VERIFY_VKRESULT(x)	if ((x) != VK_SUCCESS) { ::OutputDebugStringA(#x); ::OutputDebugStringA("\n"); check(0);}

template <typename T>
inline void ZeroVulkanMem(T& VulkanStruct, VkStructureType Type)
{
//	static_assert( (void*)&VulkanStruct == (void*)&VulkanStruct.sType, "Vulkan struct size mismatch");
	VulkanStruct.sType = Type;
	const auto Size = sizeof(VulkanStruct) - sizeof(VkStructureType);
	memset((uint8_t*)&VulkanStruct.sType + sizeof(VkStructureType), 0, Size);
}

struct SVulkan
{
	VkInstance Instance = VK_NULL_HANDLE;
	VkDebugReportCallbackEXT DebugReportCallback = nullptr;

	std::vector<VkPhysicalDevice> DiscreteDevices;
	std::vector<VkPhysicalDevice> IntegratedDevices;

	VkSurfaceKHR Surface = VK_NULL_HANDLE;

	struct FDevice
	{
		VkDevice Device = VK_NULL_HANDLE;
		VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
		VkPhysicalDeviceProperties Props;
		uint32 GfxQueue = VK_QUEUE_FAMILY_IGNORED;
		uint32 ComputeQueue = VK_QUEUE_FAMILY_IGNORED;
		uint32 TransferQueue = VK_QUEUE_FAMILY_IGNORED;
	};

	std::map<VkPhysicalDevice, FDevice> Devices;

	VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;

	void Init(GLFWwindow* Window)
	{
		VERIFY_VKRESULT(volkInitialize());

		CreateInstance();

		volkLoadInstance(Instance);

		InitDebugCallback();

		SetupDevices(Window);
	}

	static uint32 FindQueue(VkPhysicalDevice PhysicalDevice, VkQueueFlagBits QueueFlag)
	{
		uint32 NumQueues = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &NumQueues, 0);

		std::vector<VkQueueFamilyProperties> QueueProps(NumQueues);
		vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &NumQueues, QueueProps.data());

		for (uint32 i = 0; i < NumQueues; ++i)
		{
			if (QueueProps[i].queueFlags & QueueFlag)
			{
				return i;
			}
		}

		return VK_QUEUE_FAMILY_IGNORED;
	}

	void GetPhysicalDevices()
	{
		uint32 NumDevices = 0;
		VERIFY_VKRESULT(vkEnumeratePhysicalDevices(Instance, &NumDevices, nullptr));

		std::vector<VkPhysicalDevice> PhysicalDevices(NumDevices);
		VERIFY_VKRESULT(vkEnumeratePhysicalDevices(Instance, &NumDevices, PhysicalDevices.data()));

		for (auto& PD : PhysicalDevices)
		{
			VkPhysicalDeviceProperties Props;
			vkGetPhysicalDeviceProperties(PD, &Props);

			uint32 GfxQueue = FindQueue(PD, VK_QUEUE_GRAPHICS_BIT);
			uint32 ComputeQueue = FindQueue(PD, VK_QUEUE_COMPUTE_BIT);
			uint32 TransferQueue = FindQueue(PD, VK_QUEUE_TRANSFER_BIT);
			if (GfxQueue == VK_QUEUE_FAMILY_IGNORED)
			{
				continue;
			}

#if defined(VK_USE_PLATFORM_WIN32_KHR) && VK_USE_PLATFORM_WIN32_KHR
			if (!vkGetPhysicalDeviceWin32PresentationSupportKHR(PD, GfxQueue))
			{
				continue;
			}
#endif
			if (Props.apiVersion < VK_API_VERSION_1_1)
			{
				continue;
			}

			if (Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				DiscreteDevices.push_back(PD);
			}
			else if (Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
			{
				IntegratedDevices.push_back(PD);
			}
			else
			{
				// What is this? :)
				check(0);
			}

			auto& Device = Devices[PD];
			Device.PhysicalDevice = PD;
			Device.Props = Props;
			Device.ComputeQueue = ComputeQueue;
			Device.GfxQueue = GfxQueue;
			Device.TransferQueue = TransferQueue;
		}
	}

	static void CreateDevice(FDevice& Device)
	{
		uint32 NumExtensions = 0;
		VERIFY_VKRESULT(vkEnumerateDeviceExtensionProperties(Device.PhysicalDevice, nullptr, &NumExtensions, nullptr));
		std::vector<VkExtensionProperties> ExtensionProperties(NumExtensions);
		VERIFY_VKRESULT(vkEnumerateDeviceExtensionProperties(Device.PhysicalDevice, nullptr, &NumExtensions, ExtensionProperties.data()));

		check(Device.Props.limits.timestampComputeAndGraphics);

		float Priorities[1] = {1.0f};

		const char* DeviceExtensions[] =
		{
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
/*
			VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
			VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
			VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
*/
		};

		VerifyExtensions(ExtensionProperties, DeviceExtensions);

		VkDeviceQueueCreateInfo QueueInfo;
		ZeroVulkanMem(QueueInfo, VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
		QueueInfo.queueFamilyIndex =Device.GfxQueue;
		QueueInfo.queueCount = 1;
		QueueInfo.pQueuePriorities = Priorities;

		VkDeviceCreateInfo CreateInfo;
		ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
		CreateInfo.queueCreateInfoCount = 1;
		CreateInfo.pQueueCreateInfos = &QueueInfo;
		CreateInfo.ppEnabledExtensionNames = DeviceExtensions;
		CreateInfo.enabledExtensionCount = sizeof(DeviceExtensions) / sizeof(DeviceExtensions[0]);

		VERIFY_VKRESULT(vkCreateDevice(Device.PhysicalDevice, &CreateInfo, nullptr, &Device.Device));
	}

	void SetupSwapchain(FDevice& Device, GLFWwindow* Window)
	{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
		VkWin32SurfaceCreateInfoKHR CreateInfo;
		ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
		CreateInfo.hinstance = GetModuleHandle(0);
		CreateInfo.hwnd = glfwGetWin32Window(Window);

		VERIFY_VKRESULT(vkCreateWin32SurfaceKHR(Instance, &CreateInfo, 0, &Surface));
#endif

		uint32 NumFormats = 0;
		VERIFY_VKRESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(Device.PhysicalDevice, Surface, &NumFormats, nullptr));
		check(NumFormats > 0);
		std::vector<VkSurfaceFormatKHR> Formats(NumFormats);
		VERIFY_VKRESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(Device.PhysicalDevice, Surface, &NumFormats, Formats.data()));

		VkFormat Format = VK_FORMAT_UNDEFINED;
		if (Formats.size() > 1)
		{
			for (VkSurfaceFormatKHR FoundFormat : Formats)
			{
				if (FoundFormat.format == VK_FORMAT_R8G8B8A8_UNORM || FoundFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
				{
					Format = FoundFormat.format;
					break;
				}
			}
		}

		if (Format == VK_FORMAT_UNDEFINED)
		{
			Format = Formats[0].format;
		}
	}

	VkPhysicalDevice FindPhysicalDeviceByVendorID(uint32 VendorID)
	{
		for (auto Pair : Devices)
		{
			if (Pair.second.Props.vendorID == VendorID)
			{
				return Pair.first;
			}
		}
		return VK_NULL_HANDLE;
	}

	VkPhysicalDevice SelectPreferredDevice()
	{
		RCUtils::FCmdLine& CmdLine = RCUtils::FCmdLine::Get();
		if (CmdLine.Contains("-preferIntel"))
		{
			return FindPhysicalDeviceByVendorID(0x8086);
		}
/*
		else if (CmdLine.Contains("-preferAMD"))
		{
			return FindPhysicalDeviceByVendorID(0);
		}
*/
		else if (CmdLine.Contains("-preferNVidia"))
		{
			return FindPhysicalDeviceByVendorID(0x10de);
		}

		return VK_NULL_HANDLE;
	}

	void SetupDevices(GLFWwindow* Window)
	{
		GetPhysicalDevices();
		check(!DiscreteDevices.empty() || !IntegratedDevices.empty());

		PhysicalDevice = SelectPreferredDevice();
		if (PhysicalDevice == VK_NULL_HANDLE)
		{
			if (!DiscreteDevices.empty())
			{
				PhysicalDevice = DiscreteDevices.front();
			}
			else
			{
				PhysicalDevice = IntegratedDevices.front();
			}
		}

		CreateDevice(Devices[PhysicalDevice]);
		SetupSwapchain(Devices[PhysicalDevice], Window);
	}

	static VkBool32 DebugReport(VkDebugReportFlagsEXT Flags, VkDebugReportObjectTypeEXT ObjectType, uint64_t Object,
		size_t Location, int32_t MessageCode, const char* LayerPrefix, const char* Message, void* UserData)
	{
		std::string s = (Flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) ? "[Error]" : "";
		s += (Flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) ? "[Warning]" : "";
		s += (Flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) ? "[Perf]" : "";
		s += " ";
		s += Message;
		::OutputDebugStringA(s.c_str());
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

		uint32_t NumLayers = 0;
		VERIFY_VKRESULT(vkEnumerateInstanceLayerProperties(&NumLayers, nullptr));

		std::vector<VkLayerProperties> LayerProperties(NumLayers);
		VERIFY_VKRESULT(vkEnumerateInstanceLayerProperties(&NumLayers, LayerProperties.data()));

		uint32 NumExtensions = 0;
		VERIFY_VKRESULT(vkEnumerateInstanceExtensionProperties(nullptr, &NumExtensions, nullptr));

		std::vector<VkExtensionProperties> ExtensionProperties(NumExtensions);
		VERIFY_VKRESULT(vkEnumerateInstanceExtensionProperties(nullptr, &NumExtensions, ExtensionProperties.data()));

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
#if defined(VK_USE_PLATFORM_WIN32_KHR) && VK_USE_PLATFORM_WIN32_KHR
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
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
