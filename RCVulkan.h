#pragma once

#include "../RCUtils/RCUtilsBase.h"
#include "../RCUtils/RCUtilsBit.h"
#include "../RCUtils/RCUtilsCmdLine.h"

#include <algorithm>
#include <atomic>
#include <sstream>
#include <direct.h>
#include <list>
#include <set>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../RCUtils/RCUtilsFile.h"

extern "C"
{
#include "../SPIRV-Reflect/spirv_reflect.h"
}

#include "RCVulkanBase.h"


enum class EMemLocation
{
	CPU,
	CPU_TO_GPU,
	GPU,
};

enum class EShaderStages
{
	Vertex = 0,
	Domain = 1,
	Hull = 2,
	Pixel = 3,

	Compute = 0,
};

#if USE_VMA
inline VmaMemoryUsage GetVulkanMemLocation(EMemLocation Location)
{
	switch (Location)
	{
	case EMemLocation::CPU:
		return VMA_MEMORY_USAGE_CPU_ONLY;
	case EMemLocation::GPU:
		return VMA_MEMORY_USAGE_GPU_ONLY;
	case EMemLocation::CPU_TO_GPU:
		return VMA_MEMORY_USAGE_CPU_TO_GPU;
	default:
		check(0);
	};

	return VMA_MEMORY_USAGE_CPU_TO_GPU;
}
#else
inline VkMemoryPropertyFlags GetVulkanMemLocation(EMemLocation Location)
{
	switch(Location)
	{
	case EMemLocation::CPU:
		return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	case EMemLocation::GPU:
		return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	case EMemLocation::CPU_TO_GPU:
		return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT/* | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT*/;
	default:
		check(0);
	};

	return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT/* | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT*/;
}
#endif


struct SVulkan
{
	VkInstance Instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT DebugReportCallback = (VkDebugUtilsMessengerEXT)0;

	std::vector<VkPhysicalDevice> DiscreteDevices;
	std::vector<VkPhysicalDevice> IntegratedDevices;

	struct FBuffer
	{
		VkBuffer Buffer = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;

		static VkBufferCreateInfo SetupCreateInfo(VkBufferUsageFlags UsageFlags, uint32 Size)
		{
			VkBufferCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
			Info.size = Size;
			Info.usage = UsageFlags;
			return Info;
		}

#if USE_VMA
#else
		void Create(VkDevice InDevice, VkBufferUsageFlags UsageFlags, uint32 Size)
		{
			Device = InDevice;

			VkBufferCreateInfo Info = SetupCreateInfo(UsageFlags, Size);
			VERIFY_VKRESULT(vkCreateBuffer(Device, &Info, nullptr, &Buffer));
		}

		void Destroy()
		{
			vkDestroyBuffer(Device, Buffer, nullptr);
		}
#endif
	};

	struct FImage
	{
		VkImage Image = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;
		uint32 Width = 0;
		uint32 Height = 0;

		static VkImageCreateInfo SetupCreateInfo(VkImageUsageFlags UsageFlags, VkFormat Format, uint32 InWidth, uint32 InHeight)
		{
			VkImageCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
			Info.format = Format;
			Info.extent.width = InWidth;
			Info.extent.height = InHeight;
			Info.extent.depth = 1;
			Info.imageType = VK_IMAGE_TYPE_2D;
			Info.mipLevels = 1;
			Info.usage = UsageFlags;
			Info.samples = VK_SAMPLE_COUNT_1_BIT;
			Info.arrayLayers = 1;
			return Info;
		}

#if USE_VMA
#else
		void Create(VkDevice InDevice, VkImageUsageFlags UsageFlags, VkFormat Format, uint32 InWidth, uint32 InHeight)
		{
			Device = InDevice;
			Width = InWidth;
			Height = InHeight;

			VkImageCreateInfo Info = SetupCreateInfo(UsageFlags, Format, Width, Height);
			VERIFY_VKRESULT(vkCreateImage(Device, &Info, nullptr, &Image));
		}

		void Destroy()
		{
			vkDestroyImage(Device, Image, nullptr);
		}
#endif
	};

	struct FShader
	{
		VkShaderModule ShaderModule;
		std::map<uint32, std::vector<VkDescriptorSetLayoutBinding>> SetInfoBindings;

		std::vector<char> SpirV;
		VkDevice Device;
		SpvReflectShaderModule Module;
		SpvReflectDescriptorSet* DescSetInfo = nullptr;

		bool Create(VkDevice InDevice, VkShaderStageFlagBits Stage)
		{
			Device = InDevice;

			check(!SpirV.empty());
			check(SpirV.size() % 4 == 0);

			VkShaderModuleCreateInfo CreateInfo;
			ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
			CreateInfo.codeSize = SpirV.size();
			CreateInfo.pCode = (uint32*)&SpirV[0];

			VERIFY_VKRESULT(vkCreateShaderModule(Device, &CreateInfo, nullptr, &ShaderModule));

			GenerateReflectionAndCreateLayout(Stage);

			return true;
		}

		void GenerateReflectionAndCreateLayout(VkShaderStageFlagBits Stage)
		{
			SpvReflectResult Result = spvReflectCreateShaderModule(SpirV.size(), SpirV.data(), &Module);
			check(Result == SPV_REFLECT_RESULT_SUCCESS);
			uint32 NumDescSets = 0;
			spvReflectEnumerateDescriptorSets(&Module, &NumDescSets, nullptr);
			check(NumDescSets == 0 || NumDescSets == 1);
			//DescSetInfo.resize(NumDescSets);
			spvReflectEnumerateDescriptorSets(&Module, &NumDescSets, &DescSetInfo);

			//for (auto& SetInfo : DescSetInfo)
			if (DescSetInfo)
			{
				auto& SetInfo = DescSetInfo;
				std::vector<VkDescriptorSetLayoutBinding>& InfoBindings = SetInfoBindings[SetInfo->set];
				for (uint32 Index = 0; Index < SetInfo->binding_count; ++Index)
				{
					SpvReflectDescriptorBinding* SrcBinding = SetInfo->bindings[Index];
					VkDescriptorSetLayoutBinding Binding;
					ZeroMem(Binding);
					Binding.binding = SrcBinding->binding;
					Binding.descriptorType = (VkDescriptorType)SrcBinding->descriptor_type;
					Binding.descriptorCount = SrcBinding->count;
					Binding.stageFlags = Stage;
					InfoBindings.push_back(Binding);
				}

				//VkDescriptorSetLayoutCreateInfo DSCreateInfo;
				//ZeroVulkanMem(DSCreateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
				//DSCreateInfo.bindingCount = (uint32)InfoBindings.size();
				//DSCreateInfo.pBindings = InfoBindings.data();
				//DSCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
				//VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
				//VERIFY_VKRESULT(vkCreateDescriptorSetLayout(Device, &DSCreateInfo, nullptr, &Layout));
				//SetLayouts.push_back(Layout);
			}
		}

		~FShader()
		{
/*
			for (auto Layout : SetLayouts)
			{
				vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
			}
			SetLayouts.clear();
*/
			SetInfoBindings.clear();

			spvReflectDestroyShaderModule(&Module);

			vkDestroyShaderModule(Device, ShaderModule, nullptr);
			ShaderModule = VK_NULL_HANDLE;
		}
	};

	struct FPSO
	{
		std::string Name;
		VkPipeline Pipeline = VK_NULL_HANDLE;
		VkPipelineLayout Layout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSetLayout> SetLayouts;
		std::map<EShaderStages, SVulkan::FShader*> Shaders;
	};

	struct FComputePSO : public FPSO
	{
		SpvReflectDescriptorSet* Reflection;
	};

	struct FGfxPSO : public FPSO
	{
		std::map<EShaderStages, SpvReflectDescriptorSet*> Reflection;

		void AddShader(EShaderStages Stage, SVulkan::FShader* Shader)
		{
			Reflection[Stage] = Shader->DescSetInfo;
			Shaders[Stage] = Shader;
		}
	};

	struct FFence
	{
		VkFence Fence = VK_NULL_HANDLE;
		/*std::atomic<*/uint64 Counter = 0;
		VkDevice Device = VK_NULL_HANDLE;
		enum class EState
		{
			Reset,
			WaitingSignal,
			Signaled,
		};
		EState State = EState::Reset;

		void Create(VkDevice InDevice)
		{
			Device = InDevice;
			check(Fence == VK_NULL_HANDLE);
			check(State == EState::Reset);
			VkFenceCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
			VERIFY_VKRESULT(vkCreateFence(Device, &Info, nullptr, &Fence));
		}

		void Destroy()
		{
			vkDestroyFence(Device, Fence, nullptr);
			Fence = VK_NULL_HANDLE;
		}

		operator VkFence()
		{
			return Fence;
		}

		void Reset()
		{
			check(State == EState::Signaled);
			VERIFY_VKRESULT(vkResetFences(Device, 1, &Fence));
			State = EState::Reset;
		}

		void Refresh()
		{
			check(State != EState::Reset);
			if (State == EState::WaitingSignal)
			{
				VkResult Result = vkGetFenceStatus(Device, Fence);
				if (Result == VK_SUCCESS)
				{
					++Counter;
					vkResetFences(Device, 1, &Fence);
					State = EState::Signaled;
				}
				else if (Result != VK_NOT_READY)
				{
					check(0);
				}
			}
		}

		inline bool IsSignaled() const
		{
			if (State == EState::Signaled)
			{
				return true;
			}
			else if (State != EState::WaitingSignal)
			{
				check(0);
			}

			return false;
		}

		void Wait(uint64 TimeOutInNanoseconds)
		{
			if (State == EState::WaitingSignal)
			{
				VkResult Result = vkWaitForFences(Device, 1, &Fence, VK_TRUE, TimeOutInNanoseconds);
				if (Result == VK_SUCCESS)
				{
					Refresh();
				}
				else
				{
					check(0);
				}
			}
			else if (State != EState::Signaled)
			{
				check(0);
			}
		}
	};

	struct FFramebuffer;
	struct FCmdBuffer
	{
		FFence Fence;
		/*std::atomic<*/uint64 LastSubmittedFence = 0;
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

		inline bool IsOutsideRenderPass() const
		{
			return State == EState::Begun;
		}

		inline bool IsSubmitted() const
		{
			return State == EState::Submitted;
		}

		FCmdBuffer(VkDevice Device, VkCommandPool CmdPool)
		{
			VkCommandBufferAllocateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
			Info.commandBufferCount = 1;
			Info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			Info.commandPool = CmdPool;
			vkAllocateCommandBuffers(Device, &Info, &CmdBuffer);

			Fence.Create(Device);
		}

		void Destroy()
		{
			Fence.Destroy();
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

		void BeginRenderPass(FFramebuffer* Framebuffer);

		void EndRenderPass()
		{
			check(State == EState::InRenderPass);
			vkCmdEndRenderPass(CmdBuffer);
			State = EState::Begun;
		}

		void Refresh()
		{
			if (State == EState::Submitted)
			{
				Fence.Refresh();
				if (Fence.IsSignaled())
				{
					State = EState::Available;
					Fence.Reset();
					VERIFY_VKRESULT(vkResetCommandBuffer(CmdBuffer, 0));
					++LastSubmittedFence;
				}
			}
		}
	};

	struct FCommandPool
	{
		VkCommandPool CmdPool = VK_NULL_HANDLE;
		std::vector<FCmdBuffer*> CmdBuffers;
		uint32 QueueIndex  = ~0;
		VkDevice Device = VK_NULL_HANDLE;

		void Create(VkDevice InDevice, uint32 InQueueIndex)
		{
			Device = InDevice;
			QueueIndex = InQueueIndex;

			VkCommandPoolCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
			Info.queueFamilyIndex = QueueIndex;
			Info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			vkCreateCommandPool(Device, &Info, nullptr, &CmdPool);
		}

		void Destroy()
		{
			for (auto* CmdBuffer : CmdBuffers)
			{
				CmdBuffer->Destroy();
			}
			CmdBuffers.clear();

			vkDestroyCommandPool(Device, CmdPool, nullptr);
			CmdPool = VK_NULL_HANDLE;
		}

		FCmdBuffer* GetOrAddCmdBuffer()
		{
			for (auto* CmdBuffer : CmdBuffers)
			{
				if (CmdBuffer->IsAvailable())
				{
					return CmdBuffer;
				}
			}

			FCmdBuffer* CmdBuffer =  new FCmdBuffer(Device, CmdPool);
			CmdBuffers.push_back(CmdBuffer);
			return CmdBuffer;
		}

		FCmdBuffer* Begin()
		{
			Refresh();
			FCmdBuffer* CmdBuffer = GetOrAddCmdBuffer();
			CmdBuffer->Begin();
			return CmdBuffer;
		}

		void Refresh()
		{
			for (auto* CmdBuffer : CmdBuffers)
			{
				CmdBuffer->Refresh();
			}
		}
	};

#if !USE_VMA
	struct FMemAlloc
	{
		VkDeviceMemory Memory = VK_NULL_HANDLE;
		VkDeviceSize Offset = 0;
		VkDeviceSize Size = 0;
		void* MappedMem = nullptr;

		void Flush(VkDevice Device)
		{
			VkMappedMemoryRange Range;
			ZeroVulkanMem(Range, VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
			Range.memory = Memory;
			Range.offset = Offset;
			Range.size = Size;
			VERIFY_VKRESULT(vkFlushMappedMemoryRanges(Device, 1, &Range));
		}
	};
#endif

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
		VkPhysicalDeviceMemoryProperties MemProperties;

#if USE_VMA
		VmaAllocator VMAAllocator = VK_NULL_HANDLE;
#else
		std::vector<FMemAlloc*> MemAllocs;
#endif

		bool bPushDescriptor = false;

		inline uint32 FindMemoryTypeIndex(VkMemoryPropertyFlags MemProps, uint32 Type) const
		{
			for (uint32 Index = 0; Index < MemProperties.memoryTypeCount; ++Index)
			{
				if (Type & (1 << Index))
				{
					if ((MemProperties.memoryTypes[Index].propertyFlags & MemProps) == MemProps)
					{
						return Index;
					}
				}
			}

			check(0);
			return ~0;
		}

#if !USE_VMA
		FMemAlloc* AllocMemory(VkDeviceSize Size, VkMemoryPropertyFlags MemPropFlags, uint32 Type, bool bMapped)
		{
			FMemAlloc* MemAlloc = new FMemAlloc();
			VkMemoryAllocateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
			Info.allocationSize = Size;
			Info.memoryTypeIndex = FindMemoryTypeIndex(MemPropFlags, Type);
			VERIFY_VKRESULT(vkAllocateMemory(Device, &Info, nullptr, &MemAlloc->Memory));
			MemAlloc->Size = Size;
			MemAllocs.push_back(MemAlloc);

			if (bMapped)
			{
				VERIFY_VKRESULT(vkMapMemory(Device, MemAlloc->Memory, MemAlloc->Offset, MemAlloc->Size, 0, &MemAlloc->MappedMem))
			}

			return MemAlloc;
		}
#endif

		VkBufferView CreateBufferView(FBuffer& Buffer, VkFormat Format, VkDeviceSize Size, VkDeviceSize Offset = 0)
		{
			VkBufferViewCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO);
			Info.buffer = Buffer.Buffer;
			Info.format = Format;
			Info.range = Size;
			Info.offset = Offset;
			VkBufferView View = VK_NULL_HANDLE;
			VERIFY_VKRESULT(vkCreateBufferView(Device, &Info, nullptr, &View));
			return View;
		}

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

			::OutputDebugStringA("Found Device Extensions:\n");
			PrintExtensions(ExtensionProperties);

			check(Props.limits.timestampComputeAndGraphics);

			float Priorities[1] = {1.0f};

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

			VerifyExtensions(ExtensionProperties, DeviceExtensions);

			bPushDescriptor = 0&&OptionalExtension(ExtensionProperties, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
			if (bPushDescriptor)
			{
				DeviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
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

			VkPhysicalDeviceFeatures Features;
			vkGetPhysicalDeviceFeatures(PhysicalDevice, &Features);

			VkDeviceCreateInfo CreateInfo;
			ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
			CreateInfo.queueCreateInfoCount = (uint32)QueueInfos.size();
			CreateInfo.pQueueCreateInfos = QueueInfos.data();
			CreateInfo.ppEnabledExtensionNames = DeviceExtensions.data();
			CreateInfo.enabledExtensionCount = (uint32)DeviceExtensions.size();
			CreateInfo.pEnabledFeatures = &Features;
			::OutputDebugStringA("Enabled Device Extensions:\n");
			PrintList(DeviceExtensions);

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
					ss << "\t" << Index << ":" << GetHeapFlagsString(MemProperties.memoryHeaps[Index].flags) <<"(" << MemProperties.memoryHeaps[Index].flags << ") Size " << MemProperties.memoryHeaps[Index].size << std::endl;
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

		void DestroyPre()
		{
			CmdPools[GfxQueueIndex].Destroy();
			if (GfxQueueIndex != ComputeQueueIndex)
			{
				CmdPools[ComputeQueueIndex].Refresh();
			}
			if (GfxQueueIndex != TransferQueueIndex)
			{
				CmdPools[TransferQueueIndex].Refresh();
			}
		}

		void Destroy()
		{
			CmdPools[GfxQueueIndex].Destroy();
			if (GfxQueueIndex != ComputeQueueIndex)
			{
				CmdPools[ComputeQueueIndex].Destroy();
			}
			if (GfxQueueIndex != TransferQueueIndex)
			{
				CmdPools[TransferQueueIndex].Destroy();
			}

#if USE_VMA
#else
			for (FMemAlloc* Alloc : MemAllocs)
			{
				vkFreeMemory(Device, Alloc->Memory, nullptr);
				delete Alloc;
			}
			MemAllocs.clear();
#endif

#if USE_VMA
			vmaDestroyAllocator(VMAAllocator);
#endif
			vkDestroyDevice(Device, nullptr);
			Device = VK_NULL_HANDLE;
		}

		void TransitionImage(FCmdBuffer* CmdBuffer, VkImage Image, VkPipelineStageFlags SrcStageMask, VkImageLayout SrcLayout, VkAccessFlags SrcAccessMask, VkPipelineStageFlags DestStageMask, VkImageLayout DestLayout, VkAccessFlags DestAccessMask, VkImageAspectFlags AspectMask)
		{
			check(CmdBuffer->IsOutsideRenderPass());
			VkImageMemoryBarrier ImageBarrier;
			ZeroVulkanMem(ImageBarrier, VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
			ImageBarrier.srcAccessMask = SrcAccessMask;
			ImageBarrier.dstAccessMask = DestAccessMask;
			ImageBarrier.oldLayout = SrcLayout;
			ImageBarrier.newLayout = DestLayout;
			ImageBarrier.image = Image;
			ImageBarrier.subresourceRange.aspectMask = AspectMask;
			ImageBarrier.subresourceRange.layerCount = 1;
			ImageBarrier.subresourceRange.levelCount = 1;
			vkCmdPipelineBarrier(CmdBuffer->CmdBuffer, SrcStageMask, DestStageMask, 0, 0, nullptr, 0, nullptr, 1, &ImageBarrier);
		}

		void RefreshCommandBuffers()
		{
			CmdPools[GfxQueueIndex].Refresh();
			if (GfxQueueIndex != ComputeQueueIndex)
			{
				CmdPools[ComputeQueueIndex].Refresh();
			}
			if (GfxQueueIndex != TransferQueueIndex)
			{
				CmdPools[TransferQueueIndex].Refresh();
			}
		}

		FCmdBuffer* BeginCommandBuffer(uint32 QueueIndex)
		{
			return CmdPools[QueueIndex].Begin();
		}

		void Submit(VkQueue Queue, FCmdBuffer* CmdBuffer, VkPipelineStageFlags WaitFlags, VkSemaphore WaitSemaphore, VkSemaphore SignalSemaphore)
		{
			check(CmdBuffer->State == FCmdBuffer::EState::Ended);

			VkSubmitInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_SUBMIT_INFO);
			Info.commandBufferCount = 1;
			Info.pCommandBuffers = &CmdBuffer->CmdBuffer;
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
			vkQueueSubmit(Queue, 1, &Info, CmdBuffer->Fence.Fence);

			check(CmdBuffer->Fence.State == FFence::EState::Reset);
			CmdBuffer->Fence.State = FFence::EState::WaitingSignal;
			CmdBuffer->State = FCmdBuffer::EState::Submitted;
		}

		void WaitForFence(FFence& Fence, uint64 FenceCounter, uint64 TimeOutInNanoseconds = 500000000ull)
		{
			if (Fence.Counter == FenceCounter)
			{
				Fence.Wait(TimeOutInNanoseconds);
			}
		}

		template <typename T>
		void SetDebugName(T Handle,VkObjectType Type, const char* Name)
		{
			VkDebugUtilsObjectNameInfoEXT Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT);
			Info.objectHandle = (uint64)Handle;
			Info.objectType = Type;
			Info.pObjectName = Name;
			vkSetDebugUtilsObjectNameEXT(Device, &Info);
		}

		void SetDebugName(VkPipeline Pipeline, const char* Name)
		{
			SetDebugName<VkPipeline>(Pipeline, VK_OBJECT_TYPE_IMAGE, Name);
		}

		void SetDebugName(VkImage Image, const char* Name)
		{
			SetDebugName<VkImage>(Image, VK_OBJECT_TYPE_IMAGE, Name);
		}

		void SetDebugName(VkBuffer Buffer, const char* Name)
		{
			SetDebugName<VkBuffer>(Buffer, VK_OBJECT_TYPE_BUFFER, Name);
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

		void SetupSurface(SDevice* InDevice, VkInstance Instance, struct GLFWwindow* Window);

		static VkPresentModeKHR GetPresentMode(VkPhysicalDevice PhysicalDevice, VkSurfaceKHR Surface)
		{
			uint32 NumModes = 0;
			VERIFY_VKRESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &NumModes, nullptr));
			std::vector<VkPresentModeKHR> Modes(NumModes);
			VERIFY_VKRESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &NumModes, Modes.data()));

			uint32 RequestedPresent = RCUtils::FCmdLine::Get().TryGetIntPrefix("-present=", ~0);
			if (RequestedPresent != ~0)
			{
				for (auto Mode : Modes)
				{
					if (Mode == RequestedPresent)
					{
						return Mode;
					}
				}
			}

			check(!Modes.empty());
			return Modes.front();
		}

		void Create(SDevice& Device, GLFWwindow* Window);

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

		inline VkViewport GetViewport() const
		{
			VkViewport Viewport;
			ZeroMem(Viewport);
			Viewport.width = (float)SurfaceCaps.currentExtent.width;
			Viewport.height = (float)SurfaceCaps.currentExtent.height;
			Viewport.maxDepth = 1.0f;
			return Viewport;
		}

		inline VkRect2D GetScissor() const
		{
			VkRect2D Scissor;
			ZeroMem(Scissor);
			Scissor.extent.width =SurfaceCaps.currentExtent.width;
			Scissor.extent.height = SurfaceCaps.currentExtent.height;
			return Scissor;
		}


		void Destroy()
		{
			DestroyImages();
			vkDestroySemaphore(Device->Device, AcquireBackbufferSemaphore, nullptr);
			AcquireBackbufferSemaphore = VK_NULL_HANDLE;
			vkDestroySemaphore(Device->Device, FinalSemaphore, nullptr);
			FinalSemaphore = VK_NULL_HANDLE;
			vkDestroySwapchainKHR(Device->Device, Swapchain, nullptr);
			Swapchain = VK_NULL_HANDLE;
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

		void SetViewportAndScissor(FCmdBuffer* CmdBuffer)
		{
			VkViewport Viewport = GetViewport();
			vkCmdSetViewport(CmdBuffer->CmdBuffer, 0, 1, &Viewport);
			VkRect2D Scissor = GetScissor();
			vkCmdSetScissor(CmdBuffer->CmdBuffer, 0, 1, &Scissor);
		}
	};
	FSwapchain Swapchain;

	struct FRenderPass
	{
		VkRenderPass RenderPass = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;

		void Create(VkDevice InDevice, VkFormat Format, VkAttachmentLoadOp LoadOp, VkAttachmentStoreOp StoreOp)
		{
			Device = InDevice;

			VkAttachmentReference ColorAttachment;
			ZeroMem(ColorAttachment);
			ColorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription SubPassDesc;
			ZeroMem(SubPassDesc);
			SubPassDesc.colorAttachmentCount = 1;
			SubPassDesc.pColorAttachments = &ColorAttachment;

			VkAttachmentDescription Attachments;
			ZeroMem(Attachments);
			Attachments.format = Format;
			Attachments.samples = VK_SAMPLE_COUNT_1_BIT;
			Attachments.loadOp = LoadOp;
			Attachments.storeOp = StoreOp;
			Attachments.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			Attachments.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkRenderPassCreateInfo CreateInfo;
			ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
			CreateInfo.subpassCount = 1;
			CreateInfo.pSubpasses = &SubPassDesc;
			CreateInfo.attachmentCount = 1;
			CreateInfo.pAttachments = &Attachments;

			VERIFY_VKRESULT(vkCreateRenderPass(Device, &CreateInfo, nullptr, &RenderPass));
		}

		void Destroy()
		{
			vkDestroyRenderPass(Device, RenderPass, nullptr);
			RenderPass = VK_NULL_HANDLE;
		}
	};

	struct FFramebuffer
	{
		VkFramebuffer Framebuffer = VK_NULL_HANDLE;
		uint32 Width = 0;
		uint32 Height = 0;
		FRenderPass* RenderPass = nullptr;
		VkDevice Device = VK_NULL_HANDLE;

		void Create(VkDevice InDevice, VkImageView ImageView, uint32 InWidth, uint32 InHeight, FRenderPass* InRenderPass)
		{
			Device = InDevice;
			Width = InWidth;
			Height = InHeight;
			RenderPass = InRenderPass;

			VkFramebufferCreateInfo CreateInfo;
			ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
			CreateInfo.renderPass = RenderPass->RenderPass;
			CreateInfo.attachmentCount = 1;
			CreateInfo.pAttachments = &ImageView;
			CreateInfo.width = Width;
			CreateInfo.height = Height;
			CreateInfo.layers = 1;

			VERIFY_VKRESULT(vkCreateFramebuffer(Device, &CreateInfo, nullptr, &Framebuffer));
		}

		void Destroy()
		{
			vkDestroyFramebuffer(Device, Framebuffer, nullptr);
			Framebuffer = VK_NULL_HANDLE;
		}
	};


	std::map<VkPhysicalDevice, SDevice> Devices;

	VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
	bool bRenderDoc = false;

	void Init(GLFWwindow* Window)
	{
		VERIFY_VKRESULT(volkInitialize());

		CreateInstance();

		volkLoadInstance(Instance);

		InitDebugCallback();

		SetupDevices(Window);
	}

	static uint32 FindQueue(const std::vector<VkQueueFamilyProperties>& QueueProps, VkQueueFlagBits QueueFlag)
	{
		struct FPair
		{
			uint32 Bits;
			uint32 Index;
		};
		std::vector<FPair> QueuePropBits(QueueProps.size());
		QueuePropBits.clear();

		for (uint32 i = 0; i < QueueProps.size(); ++i)
		{
			if (QueueProps[i].queueFlags & QueueFlag)
			{
				FPair Pair = {QueueProps[i].queueFlags, i};
				QueuePropBits.push_back(Pair);
			}
		}
		
		if (!QueuePropBits.empty())
		{
			std::sort(QueuePropBits.begin(), QueuePropBits.end(), [](FPair ValueA, FPair ValueB)
			{
				uint32 FlagsA = GetNumberOfBitsSet(ValueA.Bits);
				uint32 FlagsB = GetNumberOfBitsSet(ValueB.Bits);
				//#todo-rco: Swap condition temporarily
				return !!(FlagsA < FlagsB);
			});

			uint32 Family = QueuePropBits.front().Index;
			return Family;
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
		else if (CmdLine.Contains("-preferAMD"))
		{
			return FindPhysicalDeviceByVendorID(0x1002);
		}
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

	void InitDebugCallback();

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

	static bool ContainsLayer(const std::vector<VkLayerProperties>& LayerProperties, const char* Name)
	{
		for (const auto& Entry : LayerProperties)
		{
			if (!strcmp(Entry.layerName, Name))
			{
				return true;
			}
		}

		return false;
	}

	static void VerifyLayers(const std::vector<VkLayerProperties>& LayerProperties, const std::vector<const char*>& Layers)
	{
		for (const auto& Layer : Layers)
		{
			VerifyLayer(LayerProperties, Layer);
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

	static void VerifyExtensions(const std::vector<VkExtensionProperties>& ExtensionProperties, const std::vector<const char*>& Extensions)
	{
		for (size_t t = 0; t < Extensions.size(); ++t)
		{
			VerifyExtension(ExtensionProperties, Extensions[t]);
		}
	}

	static bool OptionalExtension(const std::vector<VkExtensionProperties>& ExtensionProperties, const char* Name)
	{
		for (const auto& Entry : ExtensionProperties)
		{
			if (!strcmp(Entry.extensionName, Name))
			{
				return true;
			}
		}

		return false;
	}

	static void PrintLayers(const std::vector<VkLayerProperties>& List)
	{
		for (const auto& Entry : List)
		{
			::OutputDebugStringA("* ");
			::OutputDebugStringA(Entry.layerName);
			::OutputDebugStringA("\n");
		}
	}

	static void PrintExtensions(const std::vector<VkExtensionProperties>& List)
	{
		for (const auto& Entry : List)
		{
			::OutputDebugStringA("* ");
			::OutputDebugStringA(Entry.extensionName);
			::OutputDebugStringA("\n");
		}
	}

	static void PrintList(const std::vector<const char*>& List)
	{
		for (const auto& Entry : List)
		{
			::OutputDebugStringA("* ");
			::OutputDebugStringA(Entry);
			::OutputDebugStringA("\n");
		}
	}

	void CreateInstance()
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

		::OutputDebugStringA("Found Instance Extensions:\n");
		PrintExtensions(ExtensionProperties);

		std::vector<const char*> Layers;

		if (RCUtils::FCmdLine::Get().Contains("-apidump"))
		{
			Layers.push_back("VK_LAYER_LUNARG_api_dump");
		}

		if (!RCUtils::FCmdLine::Get().Contains("-novalidation"))
		{
			Layers.push_back("VK_LAYER_KHRONOS_validation");
			//Layers.push_back("VK_LAYER_LUNARG_standard_validation");
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

	void DestroyDevices()
	{
		Devices[PhysicalDevice].Destroy();
	}

	void DeinitPre()
	{
		vkDeviceWaitIdle(Devices[PhysicalDevice].Device);
		Devices[PhysicalDevice].DestroyPre();
	}

	void Deinit()
	{
		Swapchain.Destroy();
		DestroyDevices();
		if (DebugReportCallback != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(Instance, DebugReportCallback, nullptr);
			DebugReportCallback = VK_NULL_HANDLE;
		}

		vkDestroyInstance(Instance, nullptr);
		Instance = VK_NULL_HANDLE;
	}
};

struct FBufferWithMem
{
	SVulkan::FBuffer Buffer;
#if USE_VMA
	VmaAllocator Allocator = {};
	VmaAllocation Mem = {};
	VmaAllocationInfo AllocInfo = {};
#else
	SVulkan::FMemAlloc* Mem = nullptr;
#endif
	uint32 Size = 0;

	void Create(SVulkan::SDevice& InDevice, VkBufferUsageFlags UsageFlags, EMemLocation Location, uint32 InSize, bool bMapped)
	{
		Size = InSize;
#if USE_VMA
		VmaMemoryUsage MemUsage = GetVulkanMemLocation(Location);

		Allocator = InDevice.VMAAllocator;

		Buffer.Device = InDevice.Device;

		VkBufferCreateInfo Info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		Info.size = InSize;
		Info.usage = UsageFlags;

		VmaAllocationCreateInfo AllocCreateInfo = {};
		AllocCreateInfo.usage = MemUsage;

		VERIFY_VKRESULT(vmaCreateBuffer(Allocator, &Info, &AllocCreateInfo, &Buffer.Buffer, &Mem, &AllocInfo));
#else
		VkMemoryPropertyFlags MemPropFlags = GetVulkanMemLocation(Location);

		Buffer.Create(InDevice.Device, UsageFlags, Size);

		VkMemoryRequirements MemReqs;
		vkGetBufferMemoryRequirements(InDevice.Device, Buffer.Buffer, &MemReqs);
		Mem = InDevice.AllocMemory(MemReqs.size, MemPropFlags, MemReqs.memoryTypeBits, bMapped);
		VERIFY_VKRESULT(vkBindBufferMemory(InDevice.Device, Buffer.Buffer, Mem->Memory, Mem->Offset));
#endif
	}

	void Destroy()
	{
#if USE_VMA
		vmaDestroyBuffer(Allocator, Buffer.Buffer, Mem);
		Mem = {};
		this->AllocInfo = {};
#else
		Buffer.Destroy();
		Mem = nullptr;
#endif
	}

	void* Lock()
	{
#if USE_VMA
		void* Data = nullptr;
		VERIFY_VKRESULT(vmaMapMemory(Allocator, Mem, &Data));
		return Data;
#else
		check(Mem->MappedMem);
		return Mem->MappedMem;
#endif
	}

	void Unlock()
	{
#if USE_VMA
		vmaUnmapMemory(Allocator, Mem);
		vmaFlushAllocation(Allocator, Mem, 0, Size);
#else
#endif
	}
};


struct FBufferWithMemAndView : public FBufferWithMem
{
	VkBufferView View = VK_NULL_HANDLE;
	void Create(SVulkan::SDevice& InDevice, VkBufferUsageFlags UsageFlags, 
		EMemLocation Location, uint32 InSize, VkFormat Format, bool bMapped)
	{
		FBufferWithMem::Create(InDevice, UsageFlags, Location, InSize, bMapped);
		View = InDevice.CreateBufferView(Buffer, Format, InSize);
	}

	void Destroy()
	{
		vkDestroyBufferView(Buffer.Device, View, nullptr);
		View = VK_NULL_HANDLE;

		FBufferWithMem::Destroy();
	}
};

struct FImageWithMem
{
	SVulkan::FImage Image;
#if USE_VMA
	VmaAllocator Allocator = {};
	VmaAllocation Mem = {};
	VmaAllocationInfo AllocInfo = {};
#else
	SVulkan::FMemAlloc* Mem = nullptr;
#endif

	void Create(SVulkan::SDevice& InDevice, VkFormat Format, VkImageUsageFlags UsageFlags,
		EMemLocation Location, uint32 Width, uint32 Height)
	{
#if USE_VMA
		VmaMemoryUsage MemPropFlags = GetVulkanMemLocation(Location);
		Allocator = InDevice.VMAAllocator;
		Image.Device = InDevice.Device;

		VkImageCreateInfo Info = SVulkan::FImage::SetupCreateInfo(UsageFlags, Format, Width, Height);

		VmaAllocationCreateInfo AllocCreateInfo = {};
		AllocCreateInfo.usage = MemPropFlags;

		VERIFY_VKRESULT(vmaCreateImage(Allocator, &Info, &AllocCreateInfo, &Image.Image, &Mem, &AllocInfo));
#else
		VkMemoryPropertyFlags MemPropFlags = GetVulkanMemLocation(Location);
		Image.Create(InDevice.Device, UsageFlags, Format, Width, Height);

		VkMemoryRequirements MemReqs;
		vkGetImageMemoryRequirements(InDevice.Device, Image.Image, &MemReqs);

		Mem = InDevice.AllocMemory(MemReqs.size, MemPropFlags, MemReqs.memoryTypeBits, false);
		VERIFY_VKRESULT(vkBindImageMemory(InDevice.Device, Image.Image, Mem->Memory, Mem->Offset));
#endif
		Image.Width = Width;
		Image.Height = Height;
	}

	void Destroy()
	{
#if USE_VMA
		vmaDestroyImage(Allocator, Image.Image, Mem);
		Mem = {};
		AllocInfo = {};
#else
		Image.Destroy();
#endif
	}
};

struct FImageWithMemAndView : public FImageWithMem
{
	VkImageView View = VK_NULL_HANDLE;

	void Create(SVulkan::SDevice& InDevice, VkFormat Format, VkImageUsageFlags UsageFlags, 
		EMemLocation Location, uint32 Width, uint32 Height, VkFormat ViewFormat)
	{
		FImageWithMem::Create(InDevice, Format, UsageFlags, Location, Width, Height);
		View = InDevice.CreateImageView(Image.Image, ViewFormat, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D);
	}

	void Destroy()
	{
		vkDestroyImageView(Image.Device, View, nullptr);
		View = VK_NULL_HANDLE;
		FImageWithMem::Destroy();
	}
};

struct FShaderInfo
{
	enum class EStage
	{
		Unknown = -1,
		Vertex,
		Pixel,
		Hull,
		Domain,

		Compute = 16,
	};
	EStage Stage = EStage::Unknown;
	std::string EntryPoint;
	std::string SourceFile;
	std::string BinaryFile;
	std::string AsmFile;
	SVulkan::FShader* Shader = nullptr;

	~FShaderInfo()
	{
		check(!Shader);
	}

	inline bool NeedsRecompiling() const
	{
		return RCUtils::IsNewerThan(SourceFile, BinaryFile);
	}
};

struct FShaderLibrary
{
	std::vector<FShaderInfo*> ShaderInfos;

	VkDevice Device =  VK_NULL_HANDLE;
	void Init(VkDevice InDevice)
	{
		Device = InDevice;
	}

	FShaderInfo* RegisterShader(const char* OriginalFilename, const char* EntryPoint, FShaderInfo::EStage Stage)
	{
		FShaderInfo* Info = new FShaderInfo;
		Info->EntryPoint = EntryPoint;
		Info->Stage = Stage;

		std::string RootDir;
		std::string BaseFilename;
		std::string Extension = RCUtils::SplitPath(OriginalFilename, RootDir, BaseFilename, false);

		std::string OutDir = RCUtils::MakePath(RootDir, "out");
		_mkdir(OutDir.c_str());

		Info->SourceFile = RCUtils::MakePath(RootDir, BaseFilename + "." + Extension);
		Info->BinaryFile = RCUtils::MakePath(OutDir, BaseFilename + "." + Info->EntryPoint + ".spv");
		Info->AsmFile = RCUtils::MakePath(OutDir, BaseFilename + "." + Info->EntryPoint + ".spvasm");

		ShaderInfos.push_back(Info);

		return Info;
	}

	void RecompileShaders()
	{
		for (auto* Info : ShaderInfos)
		{
			if (Info->NeedsRecompiling())
			{
				DoCompileFromSource(Info);
			}
			else if (!Info->Shader)
			{
				DoCompileFromBinary(Info);
			}
		}
	}

	static std::string GetGlslangCommandLine()
	{
		std::string Out;
		char Glslang[MAX_PATH];
		char SDKDir[MAX_PATH];
		::GetEnvironmentVariableA("VULKAN_SDK", SDKDir, MAX_PATH - 1);
		sprintf_s(Glslang, "%s\\Bin\\glslangValidator.exe", SDKDir);
		Out = Glslang;
		Out += " -V -r -l -H -D --hlsl-iomap --auto-map-bindings";
		return Out;
	}

	static std::string GetStageName(FShaderInfo::EStage Stage)
	{
		switch (Stage)
		{
		case FShaderInfo::EStage::Compute:	return "comp";
		case FShaderInfo::EStage::Vertex:	return "vert";
		case FShaderInfo::EStage::Pixel:	return "frag";
		case FShaderInfo::EStage::Hull:		return "tesc";
		case FShaderInfo::EStage::Domain:	return "tese";
		default:
			break;
		}

		return "INVALID";
	}

	static VkShaderStageFlagBits GetVulkanStage(FShaderInfo::EStage Stage)
	{
		switch (Stage)
		{
		case FShaderInfo::EStage::Compute:	return VK_SHADER_STAGE_COMPUTE_BIT;
		case FShaderInfo::EStage::Vertex:	return VK_SHADER_STAGE_VERTEX_BIT;
		case FShaderInfo::EStage::Pixel:	return VK_SHADER_STAGE_FRAGMENT_BIT;
		case FShaderInfo::EStage::Hull:		return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		case FShaderInfo::EStage::Domain:	return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		default:
			break;
		}

		return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
	}

	bool CreateShader(FShaderInfo* Info, std::vector<char>& Data)
	{
		Info->Shader = new SVulkan::FShader;
		Info->Shader->SpirV = Data;
		return Info->Shader->Create(Device, GetVulkanStage(Info->Stage));
	}

	bool DoCompileFromBinary(FShaderInfo* Info)
	{
		std::vector<char> File = RCUtils::LoadFileToArray(Info->BinaryFile.c_str());
		if (File.empty())
		{
			check(0);
			return false;
		}

		check(!Info->Shader);
		//#todo: Destroy old; sync with rendering
		//if (Info.Shader)
		//{
		//	ShadersToDestroy.push_back(Info.Shader);
		//}
		return CreateShader(Info, File);
	}

	bool DoCompileFromSource(FShaderInfo* Info)
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

	void DestroyShaders()
	{
		//#todo-rco: Sync with GPU
		for (auto* Info : ShaderInfos)
		{
			delete Info->Shader;
			Info->Shader = nullptr;
		}
	}

	void Destroy()
	{
		DestroyShaders();

		for (auto* Info : ShaderInfos)
		{
			delete Info;
		}
		ShaderInfos.clear();
	}
};

struct FRenderTargetCache
{
	std::map<uint64, SVulkan::FRenderPass*> RenderPasses;
	std::vector<SVulkan::FFramebuffer*> Framebuffers;

	VkDevice Device =  VK_NULL_HANDLE;
	void Init(VkDevice InDevice)
	{
		Device = InDevice;
	}

	SVulkan::FRenderPass* GetOrCreateRenderPass(VkFormat Format, VkAttachmentLoadOp LoadOp, VkAttachmentStoreOp StoreOp)
	{
		uint64 Key = (uint64)Format << (uint64)32;
		Key = Key | ((uint64)LoadOp << 24);
		Key = Key | ((uint64)StoreOp << 16);
		auto Found = RenderPasses.find(Key);
		if (Found != RenderPasses.end())
		{
			return Found->second;
		}

		SVulkan::FRenderPass* RenderPass = new SVulkan::FRenderPass();
		RenderPass->Create(Device, Format, LoadOp, StoreOp);
		RenderPasses[Key] = RenderPass;
		return RenderPass;
	}

	SVulkan::FFramebuffer* GetOrCreateFrameBuffer(SVulkan::FSwapchain* Swapchain, VkAttachmentLoadOp LoadOp, VkAttachmentStoreOp StoreOp)
	{
		if (Swapchain->ImageIndex < Framebuffers.size())
		{
			return Framebuffers[Swapchain->ImageIndex];
		}

		SVulkan::FRenderPass* RenderPass = GetOrCreateRenderPass(Swapchain->Format, LoadOp, StoreOp);

		SVulkan::FFramebuffer* FB = new SVulkan::FFramebuffer();
		VkViewport Viewport = Swapchain->GetViewport();
		FB->Create(Device, Swapchain->ImageViews[Swapchain->ImageIndex], (uint32)Viewport.width, (uint32)Viewport.height, RenderPass);
		Framebuffers.push_back(FB);
		return FB;
	}

	void Destroy()
	{
		for (auto* FB : Framebuffers)
		{
			FB->Destroy();
			delete FB;
		}
		Framebuffers.clear();

		for (auto Pair : RenderPasses)
		{
			Pair.second->Destroy();
			delete Pair.second;
		}
		RenderPasses.clear();
	}
};

struct FPSOCache
{
	struct FVertexDecl
	{
		std::vector<VkVertexInputAttributeDescription> AttrDescs;
		std::vector<std::string> Names;

		std::vector<VkVertexInputBindingDescription> BindingDescs;

		void AddAttribute(uint32 BindingIndex, uint32 Location, VkFormat Format, uint32 AttributeOffset, const char* Name)
		{
			VkVertexInputAttributeDescription Attr;
			Attr.location = Location;
			Attr.binding = BindingIndex;
			Attr.format = Format;
			Attr.offset = AttributeOffset;
			AttrDescs.push_back(Attr);
			Names.push_back(Name);
		}

		void AddBinding(uint32 BindingIndex, uint32 Stride)
		{
			VkVertexInputBindingDescription Desc;
			Desc.binding = BindingIndex;
			Desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			Desc.stride = Stride;
			BindingDescs.push_back(Desc);
		}
	};
	std::vector<FVertexDecl> VertexDecls;

	int32 FindOrAddVertexDecl(const FVertexDecl& VertexDecl)
	{
		int32 Index = 0;
		for (auto& Entry : VertexDecls)
		{
			if (Entry.AttrDescs == VertexDecl.AttrDescs
				&& Entry.BindingDescs == VertexDecl.BindingDescs
				&& Entry.Names == VertexDecl.Names
				)
			{
				check(Entry.Names == VertexDecl.Names);
				return Index;
			}

			++Index;
		}

		VertexDecls.push_back(VertexDecl);
		check(Index == (int32)VertexDecls.size() - 1);
		return Index;
	}

	struct FLayout
	{
		VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSetLayout> DSLayouts;

		void Destroy(VkDevice Device)
		{
			for (auto Layout : DSLayouts)
			{
				vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
			}
			vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);

			PipelineLayout = VK_NULL_HANDLE;
			DSLayouts.clear();
		}

		std::vector<SVulkan::FShader*> Shaders;
	};
	std::vector<FLayout> PipelineLayouts;
	std::map<int32, std::map<int32, SVulkan::FGfxPSO>> GfxPSOs;
	std::vector<SVulkan::FComputePSO> ComputePSOs;

	SVulkan::SDevice* Device =  nullptr;
	FBufferWithMem ZeroBuffer;

	struct FPSOHandle
	{
		int32 Index;

		FPSOHandle()
			: Index(-1)
		{
		}

		FPSOHandle(size_t In)
		{
			Index = (int32)In;
		}
	};

	struct FGfxPSOEntry
	{
		VkGraphicsPipelineCreateInfo GfxPipelineInfo;
		VkPipelineInputAssemblyStateCreateInfo IAInfo;
		VkPipelineVertexInputStateCreateInfo VertexInputInfo;
		VkPipelineShaderStageCreateInfo StageInfos[5];
		VkPipelineRasterizationStateCreateInfo RasterizerInfo;
		VkPipelineColorBlendAttachmentState BlendAttachState;
		VkPipelineColorBlendStateCreateInfo BlendInfo;
		VkPipelineDepthStencilStateCreateInfo DepthStateInfo;
		VkPipelineMultisampleStateCreateInfo MSInfo;
		VkPipelineDynamicStateCreateInfo DynamicInfo;
		VkPipelineViewportStateCreateInfo ViewportState;
		VkDynamicState DynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkViewport Viewport;
		VkRect2D Scissor;
		std::string Name = "<Unknown>";

		FGfxPSOEntry()
		{
			ZeroVulkanMem(GfxPipelineInfo, VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
			
			ZeroVulkanMem(IAInfo, VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
			IAInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			ZeroVulkanMem(VertexInputInfo, VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);

			ZeroMem(StageInfos);
			
			ZeroVulkanMem(RasterizerInfo, VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
			RasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
			RasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
			RasterizerInfo.lineWidth = 1.0f;

			ZeroMem(BlendAttachState);
			BlendAttachState.blendEnable = VK_TRUE;
			BlendAttachState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			BlendAttachState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			BlendAttachState.colorBlendOp = VK_BLEND_OP_ADD;
			BlendAttachState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			BlendAttachState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			BlendAttachState.alphaBlendOp = VK_BLEND_OP_ADD;
			BlendAttachState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			ZeroVulkanMem(BlendInfo, VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
			BlendInfo.attachmentCount = 1;

			ZeroVulkanMem(DepthStateInfo, VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
			DepthStateInfo.front = DepthStateInfo.back;

			ZeroVulkanMem(MSInfo, VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
			MSInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			ZeroVulkanMem(DynamicInfo, VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
			DynamicInfo.dynamicStateCount = sizeof(DynamicStates) / sizeof(DynamicStates[0]);

			ZeroMem(Viewport);
			Viewport.width = 1280;
			Viewport.height = 720;
			Viewport.maxDepth = 1;

			ZeroMem(Scissor);
			Scissor.extent.width = (uint32)Viewport.width;
			Scissor.extent.height = (uint32)Viewport.height;

			ZeroVulkanMem(ViewportState, VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
			ViewportState.scissorCount = 1;
			ViewportState.viewportCount = 1;
		}

		std::map<EShaderStages, SpvReflectDescriptorSet*> Reflection;
		std::map<EShaderStages, SVulkan::FShader*> Shaders;
		std::vector<VkDescriptorSetLayout> SetLayouts;

		void AddShader(EShaderStages Stage, FShaderInfo* SI, VkShaderStageFlagBits Flag)
		{
			Reflection[Stage] = SI->Shader->DescSetInfo;
			Shaders[Stage] = SI->Shader;

			ZeroVulkanMem(StageInfos[GfxPipelineInfo.stageCount], VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
			StageInfos[GfxPipelineInfo.stageCount].stage = Flag;
			StageInfos[GfxPipelineInfo.stageCount].module = SI->Shader->ShaderModule;
			StageInfos[GfxPipelineInfo.stageCount].pName = SI->EntryPoint.c_str();

			++GfxPipelineInfo.stageCount;
		}

		void Finalize(/*VkRenderPass RenderPass, */FVertexDecl* VertexDecl)
		{
			//GfxPipelineInfo.renderPass = RenderPass;

			if (VertexDecl)
			{
				VertexInputInfo.vertexAttributeDescriptionCount = (uint32)VertexDecl->AttrDescs.size();
				VertexInputInfo.pVertexAttributeDescriptions = VertexDecl->AttrDescs.data();
				VertexInputInfo.vertexBindingDescriptionCount = (uint32)VertexDecl->BindingDescs.size();
				VertexInputInfo.pVertexBindingDescriptions = VertexDecl->BindingDescs.data();
			}
		}

		void FixPointers()
		{

			ViewportState.pScissors = &Scissor;
			ViewportState.pViewports = &Viewport;

			GfxPipelineInfo.pInputAssemblyState = &IAInfo;
			GfxPipelineInfo.pVertexInputState = &VertexInputInfo;
			GfxPipelineInfo.pStages = StageInfos;
			GfxPipelineInfo.pRasterizationState = &RasterizerInfo;
			BlendInfo.pAttachments = &BlendAttachState;
			GfxPipelineInfo.pColorBlendState = &BlendInfo;
			ViewportState.pScissors = &Scissor;
			ViewportState.pViewports = &Viewport;
			GfxPipelineInfo.pViewportState = &ViewportState;
			GfxPipelineInfo.pDepthStencilState = &DepthStateInfo;
			GfxPipelineInfo.pMultisampleState = &MSInfo;
			DynamicInfo.pDynamicStates = DynamicStates;
			GfxPipelineInfo.pDynamicState = &DynamicInfo;
		}
	};

	std::vector<FGfxPSOEntry> GfxPSOEntries;

	SVulkan::FGfxPSO* GetGfxPSO(FPSOHandle GfxEntryHandle, int32 VertexDeclHandle = -1)
	{
		check(GfxEntryHandle.Index != -1);
		auto& VertexDeclMap = GfxPSOs[GfxEntryHandle.Index];
		auto FoundVertexDecl = VertexDeclMap.find(VertexDeclHandle);
		if (FoundVertexDecl == VertexDeclMap.end())
		{
			FGfxPSOEntry Entry = GfxPSOEntries[GfxEntryHandle.Index];
			Entry.FixPointers();
			SVulkan::FGfxPSO PSO;
			PSO.Reflection = Entry.Reflection;
			PSO.Shaders = Entry.Shaders;
			PSO.SetLayouts = Entry.SetLayouts;
			PSO.Layout = Entry.GfxPipelineInfo.layout;
			Entry.Finalize(/*RenderPass->RenderPass, */VertexDeclHandle == -1 ? nullptr : &VertexDecls[VertexDeclHandle]);
			VERIFY_VKRESULT(vkCreateGraphicsPipelines(Device->Device, VK_NULL_HANDLE, 1, &Entry.GfxPipelineInfo, nullptr, &PSO.Pipeline));
			VertexDeclMap[VertexDeclHandle] = PSO;
			Device->SetDebugName(PSO.Pipeline, Entry.Name.c_str());
		}
		return &VertexDeclMap[VertexDeclHandle];
	}
	SVulkan::FComputePSO* GetComputePSO(FPSOHandle Handle)
	{
		check(Handle.Index != -1);
		return &ComputePSOs[Handle.Index];
	}

	void Init(SVulkan::SDevice* InDevice)
	{
		Device = InDevice;
		ZeroBuffer.Create(*InDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, 4 * sizeof(float), true);
		{
			void* Mem = ZeroBuffer.Lock();
			memset(Mem, ZeroBuffer.Size, 255);
			ZeroBuffer.Unlock();
		}
	}

	VkPipelineLayout GetOrCreatePipelineLayout(SVulkan::FShader* VS, SVulkan::FShader* HS, SVulkan::FShader* DS, SVulkan::FShader* PS, std::vector<VkDescriptorSetLayout>& OutLayouts)
	{
		std::vector<SVulkan::FShader*> Shaders;
		Shaders.push_back(VS);
		if (HS && DS)
		{
			Shaders.push_back(HS);
			Shaders.push_back(DS);
		}
		if (PS)
		{
			Shaders.push_back(PS);
		}

		for (const FLayout& Layout : PipelineLayouts)
		{
			if (Layout.Shaders == Shaders)
			{
				return Layout.PipelineLayout;
			}
		}

		VkPipelineLayoutCreateInfo Info;
		ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
		std::map<uint32, std::vector<VkDescriptorSetLayoutBinding>> LayoutBindings = VS->SetInfoBindings;

		auto AddBindings = [&](SVulkan::FShader* Shader)
		{
			for (auto Pair : Shader->SetInfoBindings)
			{
				auto& SetBindings = LayoutBindings[Pair.first];
				SetBindings.insert(SetBindings.end(), Pair.second.begin(), Pair.second.end());
			}
		};

		if (HS && DS)
		{
			AddBindings(HS);
			AddBindings(DS);
		}

		if (PS)
		{
			AddBindings(PS);
		}

		FLayout Layout;

		for (auto Pair : LayoutBindings)
		{
			VkDescriptorSetLayoutCreateInfo DSInfo;
			ZeroVulkanMem(DSInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
			DSInfo.bindingCount = (uint32)Pair.second.size();
			DSInfo.pBindings = Pair.second.data();
			DSInfo.flags = Device->bPushDescriptor ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0;
			VkDescriptorSetLayout DSLayout = VK_NULL_HANDLE;
			VERIFY_VKRESULT(vkCreateDescriptorSetLayout(Device->Device, &DSInfo, nullptr, &DSLayout));
			Layout.DSLayouts.push_back(DSLayout);
		}

		Info.setLayoutCount = (uint32)Layout.DSLayouts.size();
		Info.pSetLayouts = Layout.DSLayouts.data();

		check(Layout.DSLayouts.size() > 0 || LayoutBindings.size() == 0);
		OutLayouts = Layout.DSLayouts;

		VERIFY_VKRESULT(vkCreatePipelineLayout(Device->Device, &Info, nullptr, &Layout.PipelineLayout));

		Layout.Shaders = Shaders;
		PipelineLayouts.push_back(Layout);

		return Layout.PipelineLayout;
	}

	template <typename TFunction>
	FPSOHandle CreateGfxPSO(const char* Name, FShaderInfo* VS, FShaderInfo* HS, FShaderInfo* DS, FShaderInfo* PS, SVulkan::FRenderPass* RenderPass, TFunction Callback)
	{
		check(VS->Shader && VS->Shader->ShaderModule);
		if (HS || DS)
		{
			check(HS && DS);
			check(HS->Shader && DS->Shader);
		}
		if (PS)
		{
			check(PS->Shader && PS->Shader->ShaderModule);
		}

		FGfxPSOEntry Entry;

		Entry.AddShader(EShaderStages::Vertex, VS, VK_SHADER_STAGE_VERTEX_BIT);

		if (HS && DS)
		{
			Entry.AddShader(EShaderStages::Hull, HS, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
			Entry.AddShader(EShaderStages::Domain, DS, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
		}

		if (PS)
		{
			Entry.AddShader(EShaderStages::Pixel, PS, VK_SHADER_STAGE_FRAGMENT_BIT);
		}

/*
		SVulkan::FGfxPSO PSO;
		PSO.AddShader(EShaderStages::Vertex, VS->Shader);
		if (HS && DS)
		{
			PSO.AddShader(EShaderStages::Hull, HS->Shader);
			PSO.AddShader(EShaderStages::Domain, DS->Shader);
		}
		if (PS)
		{
			PSO.AddShader(EShaderStages::Pixel, PS->Shader);
		}
*/
		auto Layout = GetOrCreatePipelineLayout(VS->Shader, HS ? HS->Shader : nullptr, DS ? DS->Shader : nullptr, PS ? PS->Shader : nullptr, Entry.SetLayouts);

		Entry.GfxPipelineInfo.layout = /*PSO.*/Layout;
		Entry.GfxPipelineInfo.renderPass = RenderPass->RenderPass;
		Entry.FixPointers();
		Callback(Entry.GfxPipelineInfo);
		Entry.Name = Name;
		GfxPSOEntries.push_back(Entry);

		//VERIFY_VKRESULT(vkCreateGraphicsPipelines(Device->Device, VK_NULL_HANDLE, 1, &Entry.GfxPipelineInfo, nullptr, &PSO.Pipeline));
		//GfxPSOs.push_back(PSO);

		//Device->SetDebugName(PSO.Pipeline, Name);
		return FPSOHandle(GfxPSOEntries.size() - 1);
	}

	template <typename TFunction>
	inline FPSOHandle CreateGfxPSO(const char* Name, FShaderInfo* VS, FShaderInfo* PS, SVulkan::FRenderPass* RenderPass, TFunction Callback)
	{
		return CreateGfxPSO(Name, VS, nullptr, nullptr, PS, RenderPass, Callback);
	}

	inline FPSOHandle CreateGfxPSO(const char* Name, FShaderInfo* VS, FShaderInfo* PS, SVulkan::FRenderPass* RenderPass)
	{
		return CreateGfxPSO(Name, VS, nullptr, nullptr, PS, RenderPass,
			[=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
			{
			});
	}

	inline FPSOHandle CreateGfxPSO(const char* Name, FShaderInfo* VS, FShaderInfo* HS, FShaderInfo* DS, FShaderInfo* PS, SVulkan::FRenderPass* RenderPass)
	{
		return CreateGfxPSO(Name, VS, HS, DS, PS, RenderPass,
			[=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
		{
		});
	}

	FPSOHandle CreateComputePSO(const char* Name, FShaderInfo* CS)
	{
		check(CS->Shader && CS->Shader->ShaderModule);

		VkComputePipelineCreateInfo PipelineInfo;
		ZeroVulkanMem(PipelineInfo, VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
		ZeroVulkanMem(PipelineInfo.stage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
		PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		PipelineInfo.stage.module = CS->Shader->ShaderModule;
		PipelineInfo.stage.pName = CS->EntryPoint.c_str();

		SVulkan::FComputePSO PSO;
		PSO.Reflection = CS->Shader->DescSetInfo;
		PSO.Layout = GetOrCreatePipelineLayout(CS->Shader, nullptr, nullptr, nullptr, PSO.SetLayouts);
		PSO.Shaders[EShaderStages::Compute] = CS->Shader;

		PipelineInfo.layout = PSO.Layout;

		VERIFY_VKRESULT(vkCreateComputePipelines(Device->Device, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &PSO.Pipeline));
		PSO.Name = Name;
		ComputePSOs.push_back(PSO);
		Device->SetDebugName(PSO.Pipeline, Name);
		return FPSOHandle(ComputePSOs.size() - 1);
	}

	template <typename TPSO>
	static void FreePSOs(VkDevice Device, std::vector<TPSO>& PSOs)
	{
		for (auto& PSO : PSOs)
		{
			vkDestroyPipeline(Device, PSO.Pipeline, nullptr);
		}
		PSOs.clear();
	};

	void Destroy()
	{
		for (auto PL : PipelineLayouts)
		{
			PL.Destroy(Device->Device);
		}

		PipelineLayouts.clear();

		for (auto& OuterPair : GfxPSOs)
		{
			for (auto& InnerPar : OuterPair.second)
			{
				vkDestroyPipeline(Device->Device, InnerPar.second.Pipeline, nullptr);
			}
		}
		GfxPSOs.clear();
		FreePSOs(Device->Device, ComputePSOs);
	}
};

struct FDescriptorCache
{
	SVulkan::SDevice* Device = nullptr;

	struct FDescriptorSets
	{
		std::vector<VkDescriptorSet> Sets;

		void UpdateDescriptorWrites(uint32 NumWrites, VkWriteDescriptorSet* DescriptorWrites, const std::vector<uint32>& NumDescriptorsPerSet)
		{
			VkWriteDescriptorSet* Write = DescriptorWrites;
			check(NumDescriptorsPerSet.size() == Sets.size());
			uint32 SetIndex = 0;
			for (uint32 NumDescriptors : NumDescriptorsPerSet)
			{
				check(Write < DescriptorWrites + NumWrites);
				for (uint32 Index = 0; Index < NumDescriptors; ++Index)
				{
					Write->dstSet = Sets[SetIndex];
					++Write;
				}

				++SetIndex;
			}
		}
	};

	struct FDescriptorData
	{
		struct FPool
		{
			VkDescriptorPool Pool = VK_NULL_HANDLE;
			std::vector<FDescriptorSets> FreeSets;
			std::vector<VkDescriptorSetLayout>* Layouts = nullptr;

			struct FUsedSets
			{
				SVulkan::FCmdBuffer* CmdBuffer = nullptr;
				uint64 FenceCounter = 0;
				FDescriptorSets Sets;
			};

			std::vector<FUsedSets> UsedSets;
			uint32 NumAvailable = 0;

			void Alloc(FDescriptorSets& OutSets, SVulkan::FCmdBuffer* CmdBuffer)
			{
				if (Layouts->size() == 0)
				{
					return;
				}
				check(!FreeSets.empty());
				OutSets = FreeSets.back();
				FreeSets.resize(FreeSets.size() - 1);

				FUsedSets Entry;
				Entry.CmdBuffer = CmdBuffer;
				Entry.FenceCounter = CmdBuffer->LastSubmittedFence;
				Entry.Sets = OutSets;
				UsedSets.push_back(Entry);

				NumAvailable += (uint32)Layouts->size();

			}
		};
		std::vector<FPool> Pools;
		VkDevice Device = VK_NULL_HANDLE;
		std::vector<VkDescriptorSetLayout> Layouts;
		std::vector<uint32> NumDescriptorsPerSet;

		uint32 MaxAvailable = 0;

		const uint32 NumEntries = 32;
		std::vector<VkDescriptorPoolSize> PoolSizes;

		void Init(VkDevice InDevice, SVulkan::FPSO* PSO)
		{
			Device = InDevice;
			check(PSO->SetLayouts.size() > 0);
			Layouts = PSO->SetLayouts;

			{
				std::map<VkDescriptorType, uint32> TypeCounts;
				NumDescriptorsPerSet.push_back(0);
				for (auto OuterPair : PSO->Shaders)
				{
					for (auto Pair : OuterPair.second->SetInfoBindings)
					{
						for (VkDescriptorSetLayoutBinding Binding : Pair.second)
						{
							TypeCounts[Binding.descriptorType] += Binding.descriptorCount;
						}
						NumDescriptorsPerSet[0] += (uint32)Pair.second.size();
					}
				}

				for (auto Pair : TypeCounts)
				{
					VkDescriptorPoolSize Size;
					ZeroMem(Size);
					Size.type = Pair.first;
					Size.descriptorCount = Pair.second * NumEntries;
					PoolSizes.push_back(Size);
				}
			}

			MaxAvailable = NumEntries * (uint32)Layouts.size();
		}

		FPool* CreatePool()
		{
			VkDescriptorPool Pool = VK_NULL_HANDLE;

			VkDescriptorPoolCreateInfo PoolInfo;
			ZeroVulkanMem(PoolInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
			PoolInfo.maxSets = MaxAvailable;
			PoolInfo.poolSizeCount = (uint32)PoolSizes.size();
			PoolInfo.pPoolSizes = PoolSizes.data();

			VERIFY_VKRESULT(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool));

			FPool NewPool;
			NewPool.Layouts = &Layouts;
			NewPool.Pool = Pool;
			NewPool.FreeSets.resize(MaxAvailable);
			for (uint32 Index = 0; Index < MaxAvailable; ++Index)
			{
				VkDescriptorSetAllocateInfo AllocInfo;
				ZeroVulkanMem(AllocInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
				AllocInfo.descriptorPool = Pool;
				AllocInfo.descriptorSetCount = (uint32)Layouts.size();
				AllocInfo.pSetLayouts = Layouts.data();

				FDescriptorSets& Sets = NewPool.FreeSets[Index];
				Sets.Sets.resize(Layouts.size());
				VERIFY_VKRESULT(vkAllocateDescriptorSets(Device, &AllocInfo, Sets.Sets.data()));
			}
			Pools.push_back(NewPool);

			return &Pools.back();
		}

		void RefreshSets()
		{
			for (auto& Pool : Pools)
			{
				for (int32 Index = (int32)Pool.UsedSets.size() - 1; Index >= 0; --Index)
				{
					auto& Used = Pool.UsedSets[Index];
					if (Used.FenceCounter < Used.CmdBuffer->LastSubmittedFence)
					{
						Pool.FreeSets.push_back(Used.Sets);

						if (Index < (int)Pool.UsedSets.size() - 1)
						{
							Pool.UsedSets[Index] = Pool.UsedSets[Pool.UsedSets.size() - 1];
						}
						Pool.UsedSets.resize(Pool.UsedSets.size() - 1);
					}
				}
			}
		}

		FPool* FindFreePool()
		{
			for (auto& Pool : Pools)
			{
				if (!Pool.FreeSets.empty())
				{
					return &Pool;
				}
			}

			return nullptr;
		}


		FDescriptorSets AllocSets(SVulkan::FCmdBuffer* CmdBuffer)
		{
			RefreshSets();
			FPool* Pool = FindFreePool();
			if (!Pool)
			{
				Pool = CreatePool();
				check(Pool);
			}

			FDescriptorSets Sets;
			Pool->Alloc(Sets, CmdBuffer);

			return Sets;
		}

		void Destroy()
		{
			for (auto& Pool : Pools)
			{
				vkDestroyDescriptorPool(Device, Pool.Pool, nullptr);
				Pool.Pool = VK_NULL_HANDLE;
			}
		}
	};

	std::map<SVulkan::FPSO*, FDescriptorData> PSODescriptors;

	void Init(SVulkan::SDevice* InDevice)
	{
		Device = InDevice;
	}

	void Destroy()
	{
		for (auto Pair : PSODescriptors)
		{
			Pair.second.Destroy();
		}
		PSODescriptors.clear();
	}

	void UpdateDescriptors(SVulkan::FCmdBuffer* CmdBuffer, uint32 NumWrites, VkWriteDescriptorSet* DescriptorWrites, SVulkan::FGfxPSO* InPSO)
	{
		if (Device->bPushDescriptor)
		{
			vkCmdPushDescriptorSetKHR(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, InPSO->Layout, 0, NumWrites, DescriptorWrites);
		}
		else
		{
			if (PSODescriptors.find(InPSO) == PSODescriptors.end())
			{
				PSODescriptors[InPSO].Init(Device->Device, InPSO);
			}

			auto Sets = PSODescriptors[InPSO].AllocSets(CmdBuffer);
			Sets.UpdateDescriptorWrites(NumWrites, DescriptorWrites, PSODescriptors[InPSO].NumDescriptorsPerSet);
			vkUpdateDescriptorSets(Device->Device, NumWrites, DescriptorWrites, 0, nullptr);
			vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, InPSO->Layout, 0, (uint32)Sets.Sets.size(), Sets.Sets.data(), 0, nullptr);
		}
	}

	void UpdateDescriptors(SVulkan::FCmdBuffer* CmdBuffer, uint32 NumWrites, VkWriteDescriptorSet* DescriptorWrites, SVulkan::FComputePSO* InPSO)
	{
		if (Device->bPushDescriptor)
		{
			vkCmdPushDescriptorSetKHR(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, InPSO->Layout, 0, NumWrites, DescriptorWrites);
		}
		else
		{
			if (PSODescriptors.find(InPSO) == PSODescriptors.end())
			{
				PSODescriptors[InPSO].Init(Device->Device, InPSO);
			}

			auto Sets = PSODescriptors[InPSO].AllocSets(CmdBuffer);
			Sets.UpdateDescriptorWrites(NumWrites, DescriptorWrites, PSODescriptors[InPSO].NumDescriptorsPerSet);
			vkUpdateDescriptorSets(Device->Device, NumWrites, DescriptorWrites, 0, nullptr);
			vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, InPSO->Layout, 0, (uint32)Sets.Sets.size(), Sets.Sets.data(), 0, nullptr);
		}
	}
};


struct FStagingBuffer
{
	FBufferWithMem* Buffer = nullptr;
	SVulkan::FCmdBuffer* CmdBuffer = nullptr;
	uint64 Fence = 0;
};

struct FStagingBufferManager
{
	std::list<FStagingBuffer*> FreeEntries;
	std::vector<FStagingBuffer*> UsedEntries;

	SVulkan::SDevice* Device = nullptr;

	void Init(SVulkan::SDevice* InDevice)
	{
		Device = InDevice;
	}

	void Destroy()
	{
		for (int32 Index = (int32)UsedEntries.size() - 1; Index >= 0; --Index)
		{
			FStagingBuffer* Entry = UsedEntries[Index];
			if (Entry->CmdBuffer->Fence.Counter > Entry->Fence)
			{
				Entry->Buffer->Destroy();
				delete Entry->Buffer;
				delete Entry;
			}
			else
			{
				//check(0);
			}
		}

		//check(UsedEntries.empty());

		for (FStagingBuffer* Buffer : FreeEntries)
		{
			Buffer->Buffer->Destroy();
			delete Buffer->Buffer;
			delete Buffer;
		}
		FreeEntries.clear();
	}

	void Refresh()
	{
		for (int32 Index = (int32)UsedEntries.size() - 1; Index >= 0; --Index)
		{
			FStagingBuffer* Entry = UsedEntries[Index];
			if (Entry->CmdBuffer)
			{
				if (Entry->CmdBuffer->Fence.Counter > Entry->Fence)
				{
					FreeEntries.push_back(Entry);
					UsedEntries[Index] = UsedEntries[UsedEntries.size() - 1];
					UsedEntries.resize(UsedEntries.size() - 1);
				}
			}
		}
	}

	FStagingBuffer* AcquireBuffer(uint32 Size, SVulkan::FCmdBuffer* CurrentCmdBuffer)
	{
		for (auto* Buffer : FreeEntries)
		{
			if (Buffer->Buffer->Size == Size)
			{
				Buffer->CmdBuffer = CurrentCmdBuffer;
				Buffer->Fence = CurrentCmdBuffer ? CurrentCmdBuffer->Fence.Counter : 0;
				UsedEntries.push_back(Buffer);
				FreeEntries.remove(Buffer);
				return Buffer;
			}
		}

		FStagingBuffer* Entry = new FStagingBuffer;
		Entry->Buffer = new FBufferWithMem;
		Entry->Buffer->Create(*Device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, EMemLocation::CPU, Size, true);
		Entry->CmdBuffer = CurrentCmdBuffer;
		Entry->Fence = CurrentCmdBuffer ? CurrentCmdBuffer->Fence.Counter : 0;
		UsedEntries.push_back(Entry);
		return Entry;
	}
};


struct FGPUTiming
{
	VkQueryPool QueryPool = VK_NULL_HANDLE;
	FBufferWithMem QueryResultsBuffer;
	SVulkan::SDevice* Device = nullptr;

	void Init(SVulkan::SDevice* InDevice)
	{
		Device = InDevice;

		VkQueryPoolCreateInfo PoolCreateInfo;
		ZeroVulkanMem(PoolCreateInfo, VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
		PoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		PoolCreateInfo.queryCount = 2;
		VERIFY_VKRESULT(vkCreateQueryPool(Device->Device, &PoolCreateInfo, nullptr, &QueryPool));

		QueryResultsBuffer.Create(*Device, VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, 2 * sizeof(uint64), true);
	}

	void Destroy()
	{
		QueryResultsBuffer.Destroy();
		vkDestroyQueryPool(Device->Device, QueryPool, nullptr);
	}

	void BeginTimestamp(SVulkan::FCmdBuffer* CmdBuffer)
	{
		vkCmdWriteTimestamp(CmdBuffer->CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, QueryPool, 0);
	}

	void EndTimestamp(SVulkan::FCmdBuffer* CmdBuffer)
	{
		vkCmdWriteTimestamp(CmdBuffer->CmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, QueryPool, 1);
		vkCmdCopyQueryPoolResults(CmdBuffer->CmdBuffer, QueryPool, 0, 2, QueryResultsBuffer.Buffer.Buffer, 0, sizeof(uint64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
		vkCmdResetQueryPool(CmdBuffer->CmdBuffer, QueryPool, 0, 2);
	}

	double ReadTimestamp()
	{
#if USE_VMA
		uint64* Values;
		vmaMapMemory(QueryResultsBuffer.Allocator, QueryResultsBuffer.Mem, (void**)&Values);
		double DeltaMs = (Values[1] - Values[0]) * (Device->Props.limits.timestampPeriod * 1e-6);
		vmaUnmapMemory(QueryResultsBuffer.Allocator, QueryResultsBuffer.Mem);
		return DeltaMs;
#else
		uint64* Values = (uint64*)QueryResultsBuffer.Mem->MappedMem;
		double DeltaMs = (Values[1] - Values[0]) * (Device->Props.limits.timestampPeriod * 1e-6);
		return DeltaMs;
#endif
	}
};

inline void SVulkan::FCmdBuffer::BeginRenderPass(FFramebuffer* Framebuffer)
{
	check(State == EState::Begun);
	VkRenderPassBeginInfo Info;
	ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
	Info.renderPass = Framebuffer->RenderPass->RenderPass;
	Info.renderArea.extent.width = Framebuffer->Width;
	Info.renderArea.extent.height = Framebuffer->Height;
	Info.framebuffer = Framebuffer->Framebuffer;
	vkCmdBeginRenderPass(CmdBuffer, &Info, VK_SUBPASS_CONTENTS_INLINE);
	State = EState::InRenderPass;
}


struct FPendingOpsManager
{
	struct FPendingOp
	{
		enum OpType
		{
			EInvalid,
			ECopyBuffers,
			ECopyBufferToImage,
			EUpdateBuffer,
		};

		OpType Op = EInvalid;

		struct FCopyBuffer
		{
			FStagingBuffer* SrcStaging = nullptr;
			VkBuffer Dest = VK_NULL_HANDLE;
			uint32 Size = 0;
			//uint32 SrcOffset = 0;
			//uint32 DestOffset = 0;
		};
		FCopyBuffer Copy;

		struct FUpdateBuffer
		{
			std::vector<char> Data;
			VkBuffer Dest = VK_NULL_HANDLE;
			uint32 Size = 0;
			uint32 Offset = 0;
		};
		FUpdateBuffer Update;

		struct FCopyBufferToImage
		{
			FStagingBuffer* SrcStaging = nullptr;
			VkImage Dest = VK_NULL_HANDLE;
			VkImageLayout SrcLayout = VK_IMAGE_LAYOUT_MAX_ENUM;
			VkImageLayout DestLayout = VK_IMAGE_LAYOUT_MAX_ENUM;
			VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
			uint32 Width = 0;
			uint32 Height = 0;
			uint32 BufferOffset = 0;
		};
		FCopyBufferToImage CopyImage;

		void Exec(SVulkan::SDevice& Device, SVulkan::FCmdBuffer* CmdBuffer)
		{
			switch (Op)
			{
			case ECopyBuffers:
			{
				VkBufferCopy Region;
				ZeroMem(Region);
				Region.size = Copy.Size;
				Region.srcOffset = 0;//Copy.SrcOffset;
				Region.dstOffset = 0;//Copy.DestOffset;
				vkCmdCopyBuffer(CmdBuffer->CmdBuffer, Copy.SrcStaging->Buffer->Buffer.Buffer, Copy.Dest, 1, &Region);
				Copy.SrcStaging->CmdBuffer = CmdBuffer;
				Copy.SrcStaging->Fence = CmdBuffer->LastSubmittedFence;
			}
				break;
			case ECopyBufferToImage:
			{
				VkBufferImageCopy Region;
				ZeroMem(Region);
				Region.imageSubresource.aspectMask = CopyImage.Aspect;
				Region.imageSubresource.layerCount = 1;
				Region.bufferOffset = CopyImage.BufferOffset;
				Region.imageExtent.width = CopyImage.Width;
				Region.imageExtent.height = CopyImage.Height;
				Region.imageExtent.depth = 1;

				if (CopyImage.SrcLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
				{
					check(CopyImage.SrcLayout == VK_IMAGE_LAYOUT_UNDEFINED);
					Device.TransitionImage(CmdBuffer, CopyImage.Dest,
						VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0,
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
						CopyImage.Aspect);
				}

				vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, CopyImage.SrcStaging->Buffer->Buffer.Buffer, CopyImage.Dest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

				if (CopyImage.DestLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
				{
					check(CopyImage.DestLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
					Device.TransitionImage(CmdBuffer, CopyImage.Dest,
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
						CopyImage.Aspect);
				}

				CopyImage.SrcStaging->CmdBuffer = CmdBuffer;
				CopyImage.SrcStaging->Fence = CmdBuffer->LastSubmittedFence;
			}
				break;
			case EUpdateBuffer:
			{
				vkCmdUpdateBuffer(CmdBuffer->CmdBuffer, Update.Dest, Update.Offset, Update.Size, Update.Data.data());
			}
				break;

			default:
				check(0);
			}
		}
	};

	void ExecutePendingStagingOps(SVulkan::SDevice& Device, SVulkan::FCmdBuffer* CmdBuffer)
	{
		if (!Ops.empty())
		{
			for (auto& Op : Ops)
			{
				Op.Exec(Device, CmdBuffer);
			}
			Ops.resize(0);
		}
	}

	void AddCopyBuffers(FStagingBuffer* SrcBuffer, SVulkan::FBuffer* DestBuffer)
	{
		FPendingOp Op;
		Op.Op = FPendingOp::ECopyBuffers;
		Op.Copy.SrcStaging = SrcBuffer;
		Op.Copy.Dest = DestBuffer->Buffer;
		Op.Copy.Size = SrcBuffer->Buffer->Size;

		Ops.push_back(Op);
	}

	void AddCopyBufferToImage(FStagingBuffer* Buffer, SVulkan::FImage& Image, VkImageLayout StartLayout, VkImageLayout FinalLayout)
	{
		FPendingOp Op;
		Op.Op = FPendingOp::ECopyBufferToImage;
		Op.CopyImage.SrcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		Op.CopyImage.DestLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		Op.CopyImage.Dest = Image.Image;
		Op.CopyImage.Width = Image.Width;
		Op.CopyImage.Height = Image.Height;
		Op.CopyImage.SrcStaging = Buffer;
		Op.CopyImage.SrcLayout = StartLayout;
		Op.CopyImage.DestLayout = FinalLayout;

		Ops.push_back(Op);
	}

	template <typename TCallback>
	void AddUpdateBuffer(VkBuffer Buffer, uint32 Size, TCallback Callback, uint32 Offset = 0)
	{
		FPendingOp Op;
		Op.Op = FPendingOp::EUpdateBuffer;
		Op.Update.Dest = Buffer;
		Op.Update.Offset = Offset;
		Op.Update.Size = Size;
		Op.Update.Data.resize(Size);
		Callback(Op.Update.Data.data());

		Ops.push_back(Op);
	}

	std::vector<FPendingOp> Ops;
};
