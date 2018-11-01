#pragma once

#include "../RCUtils/RCUtilsBase.h"
#include "../RCUtils/RCUtilsCmdLine.h"

#include <algorithm>
#include <atomic>
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
inline void ZeroMem(T& Object)
{
	const auto Size = sizeof(T);
	memset(&Object, 0, Size);
}

template <typename T>
inline void ZeroVulkanMem(T& VulkanStruct, VkStructureType Type)
{
//	static_assert( (void*)&VulkanStruct == (void*)&VulkanStruct.sType, "Vulkan struct size mismatch");
	VulkanStruct.sType = Type;
	const auto Size = sizeof(VulkanStruct) - sizeof(VkStructureType);
	memset((uint8*)&VulkanStruct.sType + sizeof(VkStructureType), 0, Size);
}

struct SVulkan
{
	VkInstance Instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT DebugReportCallback = nullptr;

	std::vector<VkPhysicalDevice> DiscreteDevices;
	std::vector<VkPhysicalDevice> IntegratedDevices;

	struct FFence
	{
		VkFence Fence = VK_NULL_HANDLE;
		/*std::atomic<*/uint64 Counter = 0;
		VkDevice Device = VK_NULL_HANDLE;

		void Create(VkDevice Device)
		{
			check(Fence == VK_NULL_HANDLE);
			VkFenceCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
			VERIFY_VKRESULT(vkCreateFence(Device, &Info, nullptr, &Fence));
		}

		operator VkFence()
		{
			return Fence;
		}

		void Refresh()
		{
			VkResult Result = vkGetFenceStatus(Device, Fence);
			if (Result == VK_SUCCESS)
			{
				++Counter;
				vkResetFences(Device, 1, &Fence);
			}
			else if (Result != VK_NOT_READY)
			{
				check(0);
			}
		}
	};

	struct FCmdBuffer
	{
		FFence Fence;
		VkCommandBuffer CmdBuffer = VK_NULL_HANDLE;

		enum class EState
		{
			Available,
			Begun,
			InRenderPass,
			Ended,
			Submitted,
		};
		EState State = EState::Available;

		void Create(VkDevice Device, VkCommandPool CmdPool)
		{
			VkCommandBufferAllocateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
			Info.commandBufferCount = 1;
			Info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			Info.commandPool = CmdPool;
			vkAllocateCommandBuffers(Device, &Info, &CmdBuffer);

			Fence.Create(Device);
		}

		operator VkCommandBuffer()
		{
			return CmdBuffer;
		}

		inline bool IsAvailable() const
		{
			return State == EState::Available;
		}

		void Begin()
		{
			check(State == EState::Available);
			VkCommandBufferBeginInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
			Info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			VERIFY_VKRESULT(vkBeginCommandBuffer(CmdBuffer, &Info));
			State = EState::Begun;
		}

		void End()
		{
			check(State == EState::Begun);
			vkEndCommandBuffer(CmdBuffer);
			State = EState::Ended;
		}
	};

	struct FCommandPool
	{
		VkCommandPool CmdPool = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;

		void Create(VkDevice InDevice, uint32 InQueueIndex)
		{
			Device = InDevice;
			QueueIndex = InQueueIndex;

			VkCommandPoolCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
			Info.queueFamilyIndex = QueueIndex;
			vkCreateCommandPool(Device, &Info, nullptr, &CmdPool);
		}

		FCmdBuffer& GetOrAddCmdBuffer()
		{
			for (auto& CmdBuffer : CmdBuffers)
			{
				if (CmdBuffer.IsAvailable())
				{
					return CmdBuffer;
				}
			}

			FCmdBuffer CmdBuffer;
			CmdBuffer.Create(Device, CmdPool);
			CmdBuffers.push_back(CmdBuffer);

			return CmdBuffers.back();
		}

		FCmdBuffer& Begin()
		{
			FCmdBuffer& CmdBuffer = GetOrAddCmdBuffer();
			CmdBuffer.Begin();
			return CmdBuffer;
		}

		std::vector<FCmdBuffer> CmdBuffers;
		uint32 QueueIndex  = ~0;
	};

	struct SDevice
	{
		VkDevice Device = VK_NULL_HANDLE;
		std::map<uint32, FCommandPool> CmdPools;
		VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
		VkPhysicalDeviceProperties Props;
		VkQueue GfxQueue = VK_NULL_HANDLE;
		VkQueue ComputeQueue = VK_NULL_HANDLE;
		VkQueue TransferQueue = VK_NULL_HANDLE;
		VkQueue PresentQueue = VK_NULL_HANDLE;
		uint32 GfxQueueIndex = VK_QUEUE_FAMILY_IGNORED;
		uint32 ComputeQueueIndex = VK_QUEUE_FAMILY_IGNORED;
		uint32 TransferQueueIndex = VK_QUEUE_FAMILY_IGNORED;
		uint32 PresentQueueIndex = VK_QUEUE_FAMILY_IGNORED;

		VkImageView CreateImageView(VkImage Image, VkFormat Format, VkImageAspectFlags Aspect, VkImageViewType ViewType)
		{
			VkImageViewCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
			Info.format = Format;
			Info.image = Image;
			Info.viewType = ViewType;
			Info.subresourceRange.aspectMask = Aspect;
			Info.subresourceRange.layerCount = 1;
			Info.subresourceRange.levelCount = 1;

			VkImageView ImageView = VK_NULL_HANDLE;
			VERIFY_VKRESULT(vkCreateImageView(Device, &Info, nullptr, &ImageView));
			return ImageView;
		}

		VkSemaphore CreateSemaphore()
		{
			VkSemaphoreCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
			VkSemaphore Semaphore = VK_NULL_HANDLE;
			VERIFY_VKRESULT(vkCreateSemaphore(Device, &Info, nullptr, &Semaphore));
			return Semaphore;
		}

		void Create()
		{
			uint32 NumExtensions = 0;
			VERIFY_VKRESULT(vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &NumExtensions, nullptr));
			std::vector<VkExtensionProperties> ExtensionProperties(NumExtensions);
			VERIFY_VKRESULT(vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &NumExtensions, ExtensionProperties.data()));

			check(Props.limits.timestampComputeAndGraphics);

			float Priorities[1] ={1.0f};

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
			QueueInfo.queueFamilyIndex = GfxQueueIndex;
			QueueInfo.queueCount = 1;
			QueueInfo.pQueuePriorities = Priorities;

			VkDeviceCreateInfo CreateInfo;
			ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
			CreateInfo.queueCreateInfoCount = 1;
			CreateInfo.pQueueCreateInfos = &QueueInfo;
			CreateInfo.ppEnabledExtensionNames = DeviceExtensions;
			CreateInfo.enabledExtensionCount = sizeof(DeviceExtensions) / sizeof(DeviceExtensions[0]);

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
		}

		FCmdBuffer& BeginCommandBuffer(uint32 QueueIndex)
		{
			return CmdPools[QueueIndex].Begin();
		}

		void Submit(VkQueue Queue, FCmdBuffer& CmdBuffer, VkPipelineStageFlags WaitFlags, VkSemaphore WaitSemaphore, VkSemaphore SignalSemaphore, VkFence Fence)
		{
			check(CmdBuffer.State == FCmdBuffer::EState::Ended);

			VkSubmitInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_SUBMIT_INFO);
			Info.commandBufferCount = 1;
			Info.pCommandBuffers = &CmdBuffer.CmdBuffer;
			if (WaitSemaphore != VK_NULL_HANDLE)
			{
				Info.waitSemaphoreCount = 1;
				Info.pWaitSemaphores = &WaitSemaphore;
				Info.pWaitDstStageMask = &WaitFlags;
			}
			if (SignalSemaphore != VK_NULL_HANDLE)
			{
				Info.signalSemaphoreCount = 1;
				Info.pSignalSemaphores = &SignalSemaphore;
			}
			vkQueueSubmit(Queue, 1, &Info, Fence);

			CmdBuffer.State = FCmdBuffer::EState::Submitted;
		}
	};

	struct FSwapchain
	{
		SDevice* Device = nullptr;
		VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
		VkSemaphore AcquireBackbufferSemaphore = VK_NULL_HANDLE;
		VkSemaphore FinalSemaphore = VK_NULL_HANDLE;
		uint32 ImageIndex = ~0;
		std::vector<VkImage> Images;
		std::vector<VkImageView> ImageViews;
		VkSurfaceKHR Surface = VK_NULL_HANDLE;
		VkFormat Format = VK_FORMAT_UNDEFINED;
		VkSurfaceCapabilitiesKHR SurfaceCaps;

		void SetupSurface(SDevice* InDevice, VkInstance Instance, GLFWwindow* Window)
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

		void Create(SDevice& Device, GLFWwindow* Window)
		{
			DestroyImages();

			VkSwapchainCreateInfoKHR CreateInfo;
			ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
			CreateInfo.surface = Surface;
			CreateInfo.minImageCount = Min(2u, SurfaceCaps.minImageCount);
			//CreateInfo.maxImageCount = std::max(3u, SurfaceCaps.maxImageCount);
			CreateInfo.imageFormat = Format;
			CreateInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
			CreateInfo.imageExtent.width = SurfaceCaps.currentExtent.width;
			CreateInfo.imageExtent.height = SurfaceCaps.currentExtent.height;
			CreateInfo.imageArrayLayers = 1;
			CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			CreateInfo.queueFamilyIndexCount = 1;
			CreateInfo.pQueueFamilyIndices = &Device.PresentQueueIndex;
			CreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
			CreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			CreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
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

		void DestroyImages()
		{
			for (VkImageView& ImageView : ImageViews)
			{
				if (ImageView != VK_NULL_HANDLE)
				{
					vkDestroyImageView(Device->Device, ImageView, nullptr);
					ImageView = VK_NULL_HANDLE;
				}
			}
		}

		void AcquireBackbuffer()
		{
			uint64 Timeout = 5 * 1000 * 1000;
			VERIFY_VKRESULT(vkAcquireNextImageKHR(Device->Device, Swapchain, Timeout, AcquireBackbufferSemaphore, VK_NULL_HANDLE, &ImageIndex));
		}

		void Present(VkQueue Queue, VkSemaphore WaitSemaphore)
		{
			VkPresentInfoKHR Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
			Info.waitSemaphoreCount = 1;
			Info.pWaitSemaphores = &WaitSemaphore;
			Info.swapchainCount = 1;
			Info.pSwapchains = &Swapchain;
			Info.pImageIndices = &ImageIndex;

			VERIFY_VKRESULT(vkQueuePresentKHR(Queue, &Info));
		}
	};
	FSwapchain Swapchain;

	struct FRenderPass
	{
		VkRenderPass RenderPass = VK_NULL_HANDLE;
	};

	std::map<VkPhysicalDevice, SDevice> Devices;

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

			uint32 GfxQueueIndex = FindQueue(PD, VK_QUEUE_GRAPHICS_BIT);
			uint32 ComputeQueueIndex = FindQueue(PD, VK_QUEUE_COMPUTE_BIT);
			uint32 TransferQueueIndex = FindQueue(PD, VK_QUEUE_TRANSFER_BIT);
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

	void SetupSwapchain(SDevice& Device, GLFWwindow* Window)
	{
		Swapchain.SetupSurface(&Device, Instance, Window);
		Swapchain.Create(Device, Window);
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

		Devices[PhysicalDevice].Create();
		SetupSwapchain(Devices[PhysicalDevice], Window);
	}

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

		::OutputDebugStringA(s.c_str());
		return VK_FALSE;
	}

	void InitDebugCallback()
	{
		VkDebugUtilsMessengerCreateInfoEXT Info;
		ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
		Info.messageSeverity =
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		Info.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		Info.pfnUserCallback = DebugReport;

		VERIFY_VKRESULT(vkCreateDebugUtilsMessengerEXT(Instance, &Info, nullptr, &DebugReportCallback));
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

		uint32 NumLayers = 0;
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
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
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
		if (DebugReportCallback != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(Instance, DebugReportCallback, nullptr);
			DebugReportCallback = VK_NULL_HANDLE;
		}

		vkDestroyInstance(Instance, nullptr);
		Instance = VK_NULL_HANDLE;
	}
};
