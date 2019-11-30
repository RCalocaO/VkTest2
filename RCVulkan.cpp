
#include "pch.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define VMA_IMPLEMENTATION
#include "RCVulkan.h"

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugReport(VkDebugUtilsMessageSeverityFlagBitsEXT MessageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT MessageType, const VkDebugUtilsMessengerCallbackDataEXT* CallbackData,
	void* UserData)
{
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

	if (MessageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		if (MessageType == VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
		{
//			if (!strcmp("vkQueuePresentKHR: Presenting image without calling vkGetPhysicalDeviceSurfaceSupportKHR", CallbackData->pMessage))
			{
				return VK_FALSE;
			}
		}
	}

	if (CallbackData->pMessageIdName)
	{
		if (!strcmp(CallbackData->pMessageIdName, "UNASSIGNED-GPU-Assisted Validation Setup Error."))
		{
			return VK_FALSE;
		}
	}

	for (uint32 Index = 0; Index < CallbackData->objectCount; ++Index)
	{
		const VkDebugUtilsObjectNameInfoEXT& Info = CallbackData->pObjects[Index];
		switch (Info.objectType)
		{
		case VK_OBJECT_TYPE_COMMAND_BUFFER:
			s += "\tCmdBuf ";
			break;
		case VK_OBJECT_TYPE_BUFFER:
			s += "\tBuffer ";
			break;
		case VK_OBJECT_TYPE_BUFFER_VIEW:
			s += "\tBufferView ";
			break;
		case VK_OBJECT_TYPE_SAMPLER:
			s += "\tSampler ";
			break;
		case VK_OBJECT_TYPE_IMAGE:
			s += "\tImage ";
			break;
		case VK_OBJECT_TYPE_IMAGE_VIEW:
			s += "\tImageView ";
			break;
		case VK_OBJECT_TYPE_RENDER_PASS:
			s += "\tRenderpass ";
			break;
		case VK_OBJECT_TYPE_FRAMEBUFFER:
			s += "\tFramebuffer ";
			break;
		case VK_OBJECT_TYPE_DESCRIPTOR_SET:
			s += "\tDescriptorSet ";
			break;
		case VK_OBJECT_TYPE_PIPELINE:
			s += "\tPipeline ";
			break;
		case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
			s += "\tPipelineLayout ";
			break;
		case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
			s += "\tDescriptorSetLayout ";
			break;
		case VK_OBJECT_TYPE_SHADER_MODULE:
			s += "\tShaderModule ";
			break;
		case VK_OBJECT_TYPE_UNKNOWN:
			continue;
		default:
			check(0);
		}

		char Handle[128];
		if (Info.pObjectName && *Info.pObjectName)
		{
			sprintf(Handle, "'%s' %p\n", Info.pObjectName, (void*)CallbackData->pObjects[Index].objectHandle);
		}
		else
		{
			sprintf(Handle, "%p\n", (void*)CallbackData->pObjects[Index].objectHandle);
		}
		s += Handle;
	}

	::OutputDebugStringA(s.c_str());
	return VK_FALSE;
}

void SVulkan::GetPhysicalDevices()
{
	uint32 NumDevices = 0;
	VERIFY_VKRESULT(vkEnumeratePhysicalDevices(Instance, &NumDevices, nullptr));

	std::vector<VkPhysicalDevice> PhysicalDevices(NumDevices);
	VERIFY_VKRESULT(vkEnumeratePhysicalDevices(Instance, &NumDevices, PhysicalDevices.data()));

	for (auto& PD : PhysicalDevices)
	{
		VkPhysicalDeviceProperties Props;
		vkGetPhysicalDeviceProperties(PD, &Props);

		uint32 NumQueues = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(PD, &NumQueues, 0);

		std::vector<VkQueueFamilyProperties> QueueProps(NumQueues);
		vkGetPhysicalDeviceQueueFamilyProperties(PD, &NumQueues, QueueProps.data());

		uint32 GfxQueueIndex = FindQueue(QueueProps, VK_QUEUE_GRAPHICS_BIT);
		uint32 ComputeQueueIndex = FindQueue(QueueProps, VK_QUEUE_COMPUTE_BIT);
		uint32 TransferQueueIndex = FindQueue(QueueProps, VK_QUEUE_TRANSFER_BIT);
		uint32 PresentQueueIndex = VK_QUEUE_FAMILY_IGNORED;
#if defined(VK_USE_PLATFORM_WIN32_KHR) && VK_USE_PLATFORM_WIN32_KHR
		if (vkGetPhysicalDeviceWin32PresentationSupportKHR(PD, GfxQueueIndex))
		{
			PresentQueueIndex = GfxQueueIndex;
		}
		else if (GfxQueueIndex != ComputeQueueIndex && vkGetPhysicalDeviceWin32PresentationSupportKHR(PD, ComputeQueueIndex))
		{
			PresentQueueIndex = ComputeQueueIndex;
		}
		else if (GfxQueueIndex != TransferQueueIndex && vkGetPhysicalDeviceWin32PresentationSupportKHR(PD, TransferQueueIndex))
		{
			PresentQueueIndex = TransferQueueIndex;
		}
#endif
		if (Props.apiVersion < VK_API_VERSION_1_1)
		{
			continue;
		}

		{
			std::stringstream ss;
			ss << "*** Device " << Props.deviceName << " ID " << Props.deviceID << " Driver " << Props.driverVersion << "\n";
			ss << "\tGfx queue " << GfxQueueIndex << "\n";
			ss << "\tCompute queue " << ComputeQueueIndex << "\n";
			ss << "\tTransfer queue " << TransferQueueIndex << "\n";
			ss << "\tPresent queue " << PresentQueueIndex << "\n";
			ss.flush();
			::OutputDebugStringA(ss.str().c_str());
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
		Device.ComputeQueueIndex = ComputeQueueIndex;
		Device.GfxQueueIndex = GfxQueueIndex;
		Device.TransferQueueIndex = TransferQueueIndex;
		Device.PresentQueueIndex = PresentQueueIndex;
	}
}

void SVulkan::InitDebugCallback()
{
	VkDebugUtilsMessengerCreateInfoEXT Info;
	ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
	Info.messageSeverity = 0
		//| VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
		;
	Info.messageType = 0
		| VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
		| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
		;
	Info.pfnUserCallback = DebugReport;

	VERIFY_VKRESULT(vkCreateDebugUtilsMessengerEXT(Instance, &Info, nullptr, &DebugReportCallback));
}


void SVulkan::CreateInstance()
{
	uint32 ApiVersion = 0;
	VERIFY_VKRESULT(vkEnumerateInstanceVersion(&ApiVersion));
	if (ApiVersion < VK_API_VERSION_1_1)
	{
		// 1.1 not available
		check(0);
	}

	VkInstanceCreateInfo Info;
	ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

	uint32 NumLayers = 0;
	VERIFY_VKRESULT(vkEnumerateInstanceLayerProperties(&NumLayers, nullptr));

	std::vector<VkLayerProperties> LayerProperties(NumLayers);
	VERIFY_VKRESULT(vkEnumerateInstanceLayerProperties(&NumLayers, LayerProperties.data()));

	::OutputDebugStringA("Found Instance Layers:\n");
	PrintLayers(LayerProperties);

	uint32 NumExtensions = 0;
	VERIFY_VKRESULT(vkEnumerateInstanceExtensionProperties(nullptr, &NumExtensions, nullptr));

	std::vector<VkExtensionProperties> ExtensionProperties(NumExtensions);
	VERIFY_VKRESULT(vkEnumerateInstanceExtensionProperties(nullptr, &NumExtensions, ExtensionProperties.data()));

/*
	for (auto& Layer : LayerProperties)
	{
		uint32 NumLayerExtensions = 0;
		VERIFY_VKRESULT(vkEnumerateInstanceExtensionProperties(Layer.layerName, &NumLayerExtensions, nullptr));

		auto LastSize = ExtensionProperties.size();
		ExtensionProperties.resize(NumLayerExtensions + LastSize);
		VERIFY_VKRESULT(vkEnumerateInstanceExtensionProperties(Layer.layerName, &NumLayerExtensions, ExtensionProperties.data() + LastSize));
	}
*/

	::OutputDebugStringA("Found Instance Extensions:\n");
	PrintExtensions(ExtensionProperties);

	std::vector<const char*> Layers;

	if (RCUtils::FCmdLine::Get().Contains("-apidump"))
	{
		Layers.push_back("VK_LAYER_LUNARG_api_dump");
	}

	if (!RCUtils::FCmdLine::Get().Contains("-novalidation"))
	{
		//Layers.push_back("VK_LAYER_KHRONOS_validation");
		Layers.push_back("VK_LAYER_LUNARG_standard_validation");
	}

	VerifyLayers(LayerProperties, Layers);
	Info.ppEnabledLayerNames = Layers.data();
	Info.enabledLayerCount = (uint32)Layers.size();

	::OutputDebugStringA("Using Instance Layers:\n");
	PrintList(Layers);

	std::vector<const char*> Extensions =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR) && VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
	};
	VerifyExtensions(ExtensionProperties, Extensions);
	Info.ppEnabledExtensionNames = Extensions.data();
	Info.enabledExtensionCount = (uint32)Extensions.size();

	::OutputDebugStringA("Using Instance Extensions:\n");
	PrintList(Extensions);

	// Needed to enable 1.1
	VkApplicationInfo AppInfo;
	ZeroVulkanMem(AppInfo, VK_STRUCTURE_TYPE_APPLICATION_INFO);
	AppInfo.apiVersion = VK_API_VERSION_1_1;

	Info.pApplicationInfo = &AppInfo;

	VERIFY_VKRESULT(vkCreateInstance(&Info, nullptr, &Instance));
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

bool FDescriptorPSOCache::GetParameter(const char* Name, uint32& OutBinding, VkDescriptorType& OutType)
{
	check(Name && *Name);
	auto Found = PSO->ParameterMap.find(Name);
	if (Found == PSO->ParameterMap.end())
	{
		return false;
	}

	OutBinding = Found->second.first;
	OutType = Found->second.second;
	return true;
}

void SVulkan::SDevice::Create()
{
	uint32 NumExtensions = 0;
	VERIFY_VKRESULT(vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &NumExtensions, nullptr));
	std::vector<VkExtensionProperties> ExtensionProperties(NumExtensions);
	VERIFY_VKRESULT(vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &NumExtensions, ExtensionProperties.data()));

	::OutputDebugStringA("Found Device Extensions:\n");
	PrintExtensions(ExtensionProperties);

	check(Props.limits.timestampComputeAndGraphics);

	float Priorities[1] = { 1.0f };

	std::vector<const char*> DeviceExtensions =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		//VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
		VK_KHR_MAINTENANCE1_EXTENSION_NAME,
		VK_KHR_MAINTENANCE2_EXTENSION_NAME,
		//VK_KHR_MULTIVIEW_EXTENSION_NAME,
		//VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME,
		//VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
		//VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
	};

	bUseVertexDivisor = 0 && OptionalExtension(ExtensionProperties, VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME);
	if (bUseVertexDivisor)
	{
		DeviceExtensions.push_back(VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME);
		bUseVertexDivisor = true;
	}

	VerifyExtensions(ExtensionProperties, DeviceExtensions);

	bPushDescriptor = 0 && OptionalExtension(ExtensionProperties, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
	if (bPushDescriptor)
	{
		DeviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
	}

	if (OptionalExtension(ExtensionProperties, VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
	{
		DeviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
		bHasMarkerExtension = true;
	}

	std::vector<VkDeviceQueueCreateInfo> QueueInfos(1);
	ZeroVulkanMem(QueueInfos[0], VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
	QueueInfos[0].queueFamilyIndex = GfxQueueIndex;
	QueueInfos[0].queueCount = 1;
	QueueInfos[0].pQueuePriorities = Priorities;
	if (GfxQueueIndex != ComputeQueueIndex)
	{
		VkDeviceQueueCreateInfo Info;
		ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
		Info.queueFamilyIndex = ComputeQueueIndex;
		Info.queueCount = 1;
		Info.pQueuePriorities = Priorities;
		QueueInfos.push_back(Info);
	}
	if (GfxQueueIndex != TransferQueueIndex)
	{
		VkDeviceQueueCreateInfo Info;
		ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
		Info.queueFamilyIndex = TransferQueueIndex;
		Info.queueCount = 1;
		Info.pQueuePriorities = Priorities;
		QueueInfos.push_back(Info);
	}

	VkPhysicalDeviceFeatures2 Features;
	ZeroVulkanMem(Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
	vkGetPhysicalDeviceFeatures2(PhysicalDevice, &Features);

	VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT Divisor;
	ZeroVulkanMem(Divisor, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT);
	if (bUseVertexDivisor)
	{
		Divisor.vertexAttributeInstanceRateDivisor = VK_TRUE;
		Divisor.vertexAttributeInstanceRateZeroDivisor = VK_TRUE;
		Features.pNext = &Divisor;
	}
	VkDeviceCreateInfo CreateInfo;
	ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
	CreateInfo.queueCreateInfoCount = (uint32)QueueInfos.size();
	CreateInfo.pQueueCreateInfos = QueueInfos.data();
	CreateInfo.ppEnabledExtensionNames = DeviceExtensions.data();
	CreateInfo.enabledExtensionCount = (uint32)DeviceExtensions.size();
	//CreateInfo.pEnabledFeatures = &Features.features;
	::OutputDebugStringA("Enabled Device Extensions:\n");
	PrintList(DeviceExtensions);

	CreateInfo.pNext = &Features;

	VERIFY_VKRESULT(vkCreateDevice(PhysicalDevice, &CreateInfo, nullptr, &Device));

	vkGetDeviceQueue(Device, GfxQueueIndex, 0, &GfxQueue);
	vkGetDeviceQueue(Device, TransferQueueIndex, 0, &TransferQueue);
	vkGetDeviceQueue(Device, ComputeQueueIndex, 0, &ComputeQueue);
	vkGetDeviceQueue(Device, PresentQueueIndex, 0, &PresentQueue);

	CmdPools[GfxQueueIndex].Create(Device, GfxQueueIndex);
	if (GfxQueueIndex != ComputeQueueIndex)
	{
		CmdPools[ComputeQueueIndex].Create(Device, ComputeQueueIndex);
	}
	if (GfxQueueIndex != TransferQueueIndex)
	{
		CmdPools[TransferQueueIndex].Create(Device, TransferQueueIndex);
	}

	vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemProperties);

	{
		auto GetHeapFlagsString = [](VkMemoryHeapFlags Flags)
		{
			std::string s;
			if (Flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
			{
				s += " Local";
			}
			if (Flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT)
			{
				s += " Instance";
			}

			return s;
		};
		std::stringstream ss;
		ss << "*** " << MemProperties.memoryHeapCount << " Mem Heaps" << std::endl;
		for (uint32 Index = 0; Index < MemProperties.memoryHeapCount; ++Index)
		{
			ss << "\t" << Index << ":" << GetHeapFlagsString(MemProperties.memoryHeaps[Index].flags) << "(" << MemProperties.memoryHeaps[Index].flags << ") Size " << MemProperties.memoryHeaps[Index].size << std::endl;
		}
		auto GetTypeFlagsString = [](VkMemoryPropertyFlags Flags)
		{
			std::string s;
			if (Flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
			{
				s += " Local";
			}
			if (Flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
			{
				s += " HostVis";
			}
			if (Flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
			{
				s += " HostCoherent";
			}
			if (Flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
			{
				s += " HostCached";
			}
			if (Flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
			{
				s += " Lazy";
			}
			if (Flags & VK_MEMORY_PROPERTY_PROTECTED_BIT)
			{
				s += " Protected";
			}

			return s;
		};
		ss << MemProperties.memoryTypeCount << " Mem Types" << std::endl;
		for (uint32 Index = 0; Index < MemProperties.memoryTypeCount; ++Index)
		{
			ss << "\t" << Index << ":" << GetTypeFlagsString(MemProperties.memoryTypes[Index].propertyFlags) << "(" << MemProperties.memoryTypes[Index].propertyFlags << ") Heap " << MemProperties.memoryTypes[Index].heapIndex << std::endl;
		}
		::OutputDebugStringA(ss.str().c_str());
	}

#if USE_VMA
	{
		VmaAllocatorCreateInfo VMACreateInfo = {};
		VMACreateInfo.physicalDevice = PhysicalDevice;
		VMACreateInfo.device = Device;

		VmaVulkanFunctions Funcs = {};

		Funcs.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
		Funcs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
		Funcs.vkAllocateMemory = vkAllocateMemory;
		Funcs.vkFreeMemory = vkFreeMemory;
		Funcs.vkMapMemory = vkMapMemory;
		Funcs.vkUnmapMemory = vkUnmapMemory;
		Funcs.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
		Funcs.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
		Funcs.vkBindBufferMemory = vkBindBufferMemory;
		Funcs.vkBindImageMemory = vkBindImageMemory;
		Funcs.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
		Funcs.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
		Funcs.vkCreateBuffer = vkCreateBuffer;
		Funcs.vkDestroyBuffer = vkDestroyBuffer;
		Funcs.vkCreateImage = vkCreateImage;
		Funcs.vkDestroyImage = vkDestroyImage;
		Funcs.vkCmdCopyBuffer = vkCmdCopyBuffer;
#if VMA_DEDICATED_ALLOCATION
		Funcs.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR;
		Funcs.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR;
#endif

		VMACreateInfo.pVulkanFunctions = &Funcs;

		vmaCreateAllocator(&VMACreateInfo, &VMAAllocator);
	}
#endif
}

void SVulkan::FRenderPass::Create(VkDevice InDevice, const FAttachmentInfo& Color, const FAttachmentInfo& Depth)
{
	Device = InDevice;

	const bool bHasDepth = Depth.Format != VK_FORMAT_UNDEFINED;
	VkAttachmentReference AttachmentReferences[2];
	ZeroMem(AttachmentReferences);
	check(Color.Format != VK_FORMAT_UNDEFINED);
	AttachmentReferences[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription SubPassDesc;
	ZeroMem(SubPassDesc);
	SubPassDesc.colorAttachmentCount = 1;
	SubPassDesc.pColorAttachments = &AttachmentReferences[0];

	bClearsColor = Color.LoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;
	bClearsDepth = Depth.LoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;

	VkAttachmentDescription Attachments[2];
	ZeroMem(Attachments);
	Attachments[0].format = Color.Format;
	Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	Attachments[0].loadOp = Color.LoadOp;
	Attachments[0].storeOp = Color.StoreOp;
	Attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	Attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	if (bHasDepth)
	{
		AttachmentReferences[1].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		AttachmentReferences[1].attachment = 1;
		SubPassDesc.pDepthStencilAttachment = &AttachmentReferences[1];
		Attachments[1].format = Depth.Format;
		Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		Attachments[1].loadOp = Depth.LoadOp;
		Attachments[1].storeOp = Depth.StoreOp;
		Attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		Attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	VkRenderPassCreateInfo CreateInfo;
	ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
	CreateInfo.subpassCount = 1;
	CreateInfo.pSubpasses = &SubPassDesc;
	CreateInfo.attachmentCount = bHasDepth ? 2 : 1;
	CreateInfo.pAttachments = Attachments;

	VERIFY_VKRESULT(vkCreateRenderPass(Device, &CreateInfo, nullptr, &RenderPass));
}

bool FShaderLibrary::DoCompileFromSource(FShaderInfo* Info)
{
	static const std::string GlslangProlog = GetGlslangCommandLine();

	std::string Compile = GlslangProlog;
	Compile += " -e " + Info->EntryPoint;
	Compile += " -o " + RCUtils::AddQuotes(Info->BinaryFile);
	Compile += " -S " + GetStageName(Info->Stage);
	Compile += " " + RCUtils::AddQuotes(Info->SourceFile);
	Compile += " > " + RCUtils::AddQuotes(Info->AsmFile);
	int ReturnCode = system(Compile.c_str());
	if (ReturnCode)
	{
		std::vector<char> File = RCUtils::LoadFileToArray(Info->AsmFile.c_str());
		if (File.empty())
		{
			std::string Error = "Compile error: No output for file ";
			Error += Info->SourceFile;
			::OutputDebugStringA(Error.c_str());
		}
		else
		{
			std::string FileString = &File[0];
			FileString.resize(File.size());
			std::string Error = "Compile error:\n";
			Error += FileString;
			Error += "\n";
			::OutputDebugStringA(Error.c_str());

			int DialogResult = ::MessageBoxA(nullptr, Error.c_str(), Info->SourceFile.c_str(), MB_CANCELTRYCONTINUE);
			if (DialogResult == IDTRYAGAIN)
			{
				return DoCompileFromSource(Info);
			}
		}

		return false;
	}

	return DoCompileFromBinary(Info);
}

void FGPUTiming::Init(SVulkan::SDevice* InDevice, FPendingOpsManager& PendingOpsMgr)
{
	Device = InDevice;

	VkQueryPoolCreateInfo PoolCreateInfo;
	ZeroVulkanMem(PoolCreateInfo, VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
	PoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	PoolCreateInfo.queryCount = 2;
	VERIFY_VKRESULT(vkCreateQueryPool(Device->Device, &PoolCreateInfo, nullptr, &QueryPool));

	QueryResultsBuffer.Create(*Device, VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, 2 * sizeof(uint64), true);

	PendingOpsMgr.AddResetQueryPool(QueryPool, 0, 2);
}
