
#include "pch.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "RCVulkan.h"

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugReport(VkDebugUtilsMessageSeverityFlagBitsEXT MessageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT MessageType, const VkDebugUtilsMessengerCallbackDataEXT* CallbackData,
	void* UserData)
{
	//#todo-rco
	if (MessageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		if (MessageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
		{
/*
			if (!strcmp("vkQueuePresentKHR: Presenting image without calling vkGetPhysicalDeviceSurfaceSupportKHR", CallbackData->pMessage))
			{
				return VK_FALSE;
			}
*/
		}
	}

	if (CallbackData->pMessageIdName)
	{
		if (!strcmp(CallbackData->pMessageIdName, "UNASSIGNED-CoreValidation-Shader-InputNotProduced"))
		{
			return VK_FALSE;
		}
	}

	std::string s = "***";
	s += (MessageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) ? "[Valid]" : "";
	s += (MessageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) ? "[Gen]" : "";
	s += (MessageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) ? "[Perf]" : "";
	s += (MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "[Error]" : "";
	s += (MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "[Warn]" : "";
	s += (MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) ? "[Info]" : "";
	s += (MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) ? "[Verb]" : "";
	s += " ";
	if (CallbackData && CallbackData->pMessage)
	{
		s += CallbackData->pMessage;
	}
	s += "\n";

	::OutputDebugStringA(s.c_str());
	return VK_FALSE;
}

void SVulkan::InitDebugCallback()
{
	VkDebugUtilsMessengerCreateInfoEXT Info;
	ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
	Info.messageSeverity =
		//VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	Info.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	Info.pfnUserCallback = DebugReport;

	VERIFY_VKRESULT(vkCreateDebugUtilsMessengerEXT(Instance, &Info, nullptr, &DebugReportCallback));
}


void SVulkan::FSwapchain::SetupSurface(SDevice* InDevice, VkInstance Instance, struct GLFWwindow* Window)
{
	Device = InDevice;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	{
		VkWin32SurfaceCreateInfoKHR CreateInfo;
		ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
		CreateInfo.hinstance = GetModuleHandle(0);
		CreateInfo.hwnd = glfwGetWin32Window(Window);

		VERIFY_VKRESULT(vkCreateWin32SurfaceKHR(Instance, &CreateInfo, 0, &Surface));
	}
#endif
	VkBool32 bSupportsPresent = false;
	VERIFY_VKRESULT(vkGetPhysicalDeviceSurfaceSupportKHR(Device->PhysicalDevice, Device->PresentQueueIndex, Surface, &bSupportsPresent));
	check(bSupportsPresent);

	uint32 NumFormats = 0;
	VERIFY_VKRESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(Device->PhysicalDevice, Surface, &NumFormats, nullptr));
	check(NumFormats > 0);
	std::vector<VkSurfaceFormatKHR> Formats(NumFormats);
	VERIFY_VKRESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(Device->PhysicalDevice, Surface, &NumFormats, Formats.data()));

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

	VERIFY_VKRESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Device->PhysicalDevice, Surface, &SurfaceCaps));

	AcquireBackbufferSemaphore = Device->CreateSemaphore();
	FinalSemaphore = Device->CreateSemaphore();
}


void SVulkan::FSwapchain::Create(SDevice& Device, GLFWwindow* Window)
{
	DestroyImages();

	VkSwapchainCreateInfoKHR CreateInfo;
	ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
	CreateInfo.surface = Surface;
	CreateInfo.minImageCount = 3;
	//CreateInfo.maxImageCount = std::max(3u, SurfaceCaps.maxImageCount);
	CreateInfo.imageFormat = Format;
	CreateInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	CreateInfo.imageArrayLayers = 1;
	CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	CreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	CreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	CreateInfo.presentMode = GetPresentMode(Device.PhysicalDevice, Surface);
	CreateInfo.clipped = VK_TRUE;
	CreateInfo.imageExtent.width = SurfaceCaps.currentExtent.width;
	CreateInfo.imageExtent.height = SurfaceCaps.currentExtent.height;
	CreateInfo.queueFamilyIndexCount = 1;
	CreateInfo.pQueueFamilyIndices = &Device.PresentQueueIndex;
	CreateInfo.oldSwapchain = Swapchain;

	VERIFY_VKRESULT(vkCreateSwapchainKHR(Device.Device, &CreateInfo, nullptr, &Swapchain));

	uint32 NumImages = 0;
	VERIFY_VKRESULT(vkGetSwapchainImagesKHR(Device.Device, Swapchain, &NumImages, nullptr));
	Images.resize(NumImages);
	VERIFY_VKRESULT(vkGetSwapchainImagesKHR(Device.Device, Swapchain, &NumImages, Images.data()));

	ImageViews.resize(NumImages);
	for (uint32 i = 0; i < NumImages; ++i)
	{
		ImageViews[i] = Device.CreateImageView(Images[i], Format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D);
	}
}
