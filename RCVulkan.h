#pragma once

#include "../RCUtils/RCUtilsBase.h"
#include "../RCUtils/RCUtilsBit.h"
#include "../RCUtils/RCUtilsCmdLine.h"

#include <algorithm>
#include <atomic>
#include <sstream>
#include <direct.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../RCUtils/RCUtilsFile.h"

extern "C"
{
#include "../SPIRV-Reflect/spirv_reflect.h"
}

/*

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

	struct FBuffer
	{
		VkBuffer Buffer = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;

		void Create(VkDevice InDevice, VkBufferUsageFlags UsageFlags, uint32 Size)
		{
			Device = InDevice;

			VkBufferCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
			Info.size = Size;
			Info.usage = UsageFlags;
			VERIFY_VKRESULT(vkCreateBuffer(Device, &Info, nullptr, &Buffer));
		}

		void Destroy()
		{
			vkDestroyBuffer(Device, Buffer, nullptr);
		}
	};

	struct FShader
	{
		VkShaderModule ShaderModule;
		std::map<uint32, std::vector<VkDescriptorSetLayoutBinding>> SetInfoBindings;

		std::vector<char> SpirV;
		VkDevice Device;
		SpvReflectShaderModule Module;
		std::vector<SpvReflectDescriptorSet*> DescSetInfo;

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
			DescSetInfo.resize(NumDescSets);
			spvReflectEnumerateDescriptorSets(&Module, &NumDescSets, DescSetInfo.data());

			for (auto& SetInfo : DescSetInfo)
			{
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
		VkPipeline Pipeline = VK_NULL_HANDLE;
		VkPipelineLayout Layout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSetLayout> SetLayouts;
		std::vector<SVulkan::FShader*> Shaders;
	};

	struct FComputePSO : public FPSO
	{
		std::vector<SpvReflectDescriptorSet*> CS;
	};

	struct FGfxPSO : public FPSO
	{
		std::vector<SpvReflectDescriptorSet*> VS;
		std::vector<SpvReflectDescriptorSet*> PS;
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
				}
			}
		}
	};

	struct FCommandPool
	{
		VkCommandPool CmdPool = VK_NULL_HANDLE;
		std::vector<FCmdBuffer> CmdBuffers;
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
			for (auto& CmdBuffer : CmdBuffers)
			{
				CmdBuffer.Destroy();
			}
			CmdBuffers.clear();

			vkDestroyCommandPool(Device, CmdPool, nullptr);
			CmdPool = VK_NULL_HANDLE;
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
			Refresh();
			FCmdBuffer& CmdBuffer = GetOrAddCmdBuffer();
			CmdBuffer.Begin();
			return CmdBuffer;
		}

		void Refresh()
		{
			for (auto& CmdBuffer : CmdBuffers)
			{
				CmdBuffer.Refresh();
			}
		}
	};

	struct FMemAlloc
	{
		VkDeviceMemory Memory = VK_NULL_HANDLE;
		VkDeviceSize Offset = 0;
		VkDeviceSize Size = 0;
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
		VkPhysicalDeviceMemoryProperties MemProperties;
		VkQueryPool QueryPool = VK_NULL_HANDLE;
		SVulkan::FBuffer QueryResultsBuffer;
		SVulkan::FMemAlloc* QueryResultsMem = nullptr;

		std::vector<FMemAlloc*> MemAllocs;

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

		FMemAlloc* AllocMemory(VkDeviceSize Size, VkMemoryPropertyFlags MemPropFlags, uint32 Type)
		{
			FMemAlloc* MemAlloc = new FMemAlloc();
			VkMemoryAllocateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
			Info.allocationSize = Size;
			Info.memoryTypeIndex = FindMemoryTypeIndex(MemPropFlags, Type);
			VERIFY_VKRESULT(vkAllocateMemory(Device, &Info, nullptr, &MemAlloc->Memory));
			MemAlloc->Size = Size;
			MemAllocs.push_back(MemAlloc);
			return MemAlloc;
		}

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

			float Priorities[1] ={1.0f};

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

			bPushDescriptor = OptionalExtension(ExtensionProperties, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
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

			VkDeviceCreateInfo CreateInfo;
			ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
			CreateInfo.queueCreateInfoCount = (uint32)QueueInfos.size();
			CreateInfo.pQueueCreateInfos = QueueInfos.data();
			CreateInfo.ppEnabledExtensionNames = DeviceExtensions.data();
			CreateInfo.enabledExtensionCount = (uint32)DeviceExtensions.size();
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


			VkQueryPoolCreateInfo PoolCreateInfo;
			ZeroVulkanMem(PoolCreateInfo, VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
			PoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			PoolCreateInfo.queryCount = 2;
			VERIFY_VKRESULT(vkCreateQueryPool(Device, &PoolCreateInfo, nullptr, &QueryPool));

			QueryResultsBuffer.Create(Device, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 2 * sizeof(uint64));

			{
				VkMemoryRequirements MemReqs;
				vkGetBufferMemoryRequirements(Device, QueryResultsBuffer.Buffer, &MemReqs);

				QueryResultsMem = AllocMemory(MemReqs.size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, MemReqs.memoryTypeBits);
				VERIFY_VKRESULT(vkBindBufferMemory(Device, QueryResultsBuffer.Buffer, QueryResultsMem->Memory, QueryResultsMem->Offset));
			}
		}

		void DestroyPre()
		{
			QueryResultsBuffer.Destroy();

			vkDestroyQueryPool(Device, QueryPool, nullptr);

			CmdPools[GfxQueueIndex].Destroy();
			if (GfxQueueIndex != ComputeQueueIndex)
			{
				CmdPools[ComputeQueueIndex].Destroy();
			}
			if (GfxQueueIndex != TransferQueueIndex)
			{
				CmdPools[TransferQueueIndex].Destroy();
			}
		}

		void Destroy()
		{
			for (FMemAlloc* Alloc : MemAllocs)
			{
				vkFreeMemory(Device, Alloc->Memory, nullptr);
				delete Alloc;
			}
			MemAllocs.clear();

			vkDestroyDevice(Device, nullptr);
			Device = VK_NULL_HANDLE;
		}

		void TransitionImage(FCmdBuffer& CmdBuffer, VkImage Image, VkPipelineStageFlags SrcStageMask, VkImageLayout SrcLayout, VkAccessFlags SrcAccessMask, VkPipelineStageFlags DestStageMask, VkImageLayout DestLayout, VkAccessFlags DestAccessMask, VkImageAspectFlags AspectMask)
		{
			check(CmdBuffer.IsOutsideRenderPass());
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
			vkCmdPipelineBarrier(CmdBuffer.CmdBuffer, SrcStageMask, DestStageMask, 0, 0, nullptr, 0, nullptr, 1, &ImageBarrier);
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

		FCmdBuffer& BeginCommandBuffer(uint32 QueueIndex)
		{
			return CmdPools[QueueIndex].Begin();
		}

		void Submit(VkQueue Queue, FCmdBuffer& CmdBuffer, VkPipelineStageFlags WaitFlags, VkSemaphore WaitSemaphore, VkSemaphore SignalSemaphore)
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
			vkQueueSubmit(Queue, 1, &Info, CmdBuffer.Fence.Fence);

			check(CmdBuffer.Fence.State == FFence::EState::Reset);
			CmdBuffer.Fence.State = FFence::EState::WaitingSignal;
			CmdBuffer.State = FCmdBuffer::EState::Submitted;
		}

		void WaitForFence(FFence& Fence, uint64 FenceCounter, uint64 TimeOutInNanoseconds = 500000000ull)
		{
			if (Fence.Counter == FenceCounter)
			{
				Fence.Wait(TimeOutInNanoseconds);
			}
		}

		void BeginTimestamp(FCmdBuffer& CmdBuffer)
		{
			vkCmdWriteTimestamp(CmdBuffer.CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, QueryPool, 0);
		}

		void EndTimestamp(FCmdBuffer& CmdBuffer)
		{
			vkCmdWriteTimestamp(CmdBuffer.CmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, QueryPool, 1);
			vkCmdCopyQueryPoolResults(CmdBuffer.CmdBuffer, QueryPool, 0, 2, QueryResultsBuffer.Buffer, 0, sizeof(uint64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
			vkCmdResetQueryPool(CmdBuffer.CmdBuffer, QueryPool, 0, 2);
		}

		double ReadTimestamp()
		{
			uint64* Values;
/*
			uint64 Values[2] ={0, 0};
			VERIFY_VKRESULT(vkGetQueryPoolResults(Device, QueryPool, 0, 2, 2 * sizeof(uint64), &Values, sizeof(uint64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
*/
			VERIFY_VKRESULT(vkMapMemory(Device, QueryResultsMem->Memory, 0, 2 * sizeof(uint64), 0, (void**)&Values));
			double DeltaMs = (Values[1] - Values[0]) * (Props.limits.timestampPeriod * 1e-6);
			vkUnmapMemory(Device, QueryResultsMem->Memory);
			return DeltaMs;
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

		void Create(SDevice& Device, GLFWwindow* Window)
		{
			DestroyImages();

			VkSwapchainCreateInfoKHR CreateInfo;
			ZeroVulkanMem(CreateInfo, VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
			CreateInfo.surface = Surface;
			CreateInfo.minImageCount = 3;
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
			CreateInfo.presentMode = GetPresentMode(Device.PhysicalDevice, Surface);
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

			std::stringstream ss;
			ss << "*** Device " << Props.deviceName << " ID " << Props.deviceID << " Driver " << Props.driverVersion << "\n";
			ss << "\tGfx queue " << GfxQueueIndex << "\n";
			ss << "\tCompute queue " << ComputeQueueIndex << "\n";
			ss << "\tTransfer queue " << TransferQueueIndex << "\n";
			ss << "\tPresent queue " << PresentQueueIndex << "\n";
			ss.flush();
			::OutputDebugStringA(ss.str().c_str());

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
	SVulkan::FMemAlloc* Mem = nullptr;
	uint32 Size = 0;

	void Create(SVulkan::SDevice& InDevice, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemPropFlags, uint32 InSize)
	{
		Size = InSize;

		Buffer.Create(InDevice.Device, UsageFlags, Size);

		VkMemoryRequirements MemReqs;
		vkGetBufferMemoryRequirements(InDevice.Device, Buffer.Buffer, &MemReqs);

		Mem = InDevice.AllocMemory(MemReqs.size, MemPropFlags, MemReqs.memoryTypeBits);
		VERIFY_VKRESULT(vkBindBufferMemory(InDevice.Device, Buffer.Buffer, Mem->Memory, Mem->Offset));
	}

	void Destroy()
	{
		Buffer.Destroy();
		Mem = nullptr;
	}

	void* Lock()
	{
		void* Data = nullptr;
		VERIFY_VKRESULT(vkMapMemory(Buffer.Device, Mem->Memory, Mem->Offset, Mem->Size, 0, &Data));
		return Data;
	}

	void Unlock()
	{
		vkUnmapMemory(Buffer.Device, Mem->Memory);
	}
};

struct FShaderInfo
{
	enum class EStage
	{
		Unknown = -1,
		Vertex,
		Pixel,

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
	};
	std::map<SVulkan::FShader*, std::map<SVulkan::FShader*, FLayout>> PipelineLayouts;
	std::vector<SVulkan::FPSO> PSOs;

	SVulkan::SDevice* Device =  nullptr;
	void Init(SVulkan::SDevice* InDevice)
	{
		Device = InDevice;
	}

	VkPipelineLayout GetOrCreatePipelineLayout(SVulkan::FShader* VS, SVulkan::FShader* PS, std::vector<VkDescriptorSetLayout>& OutLayouts)
	{
		auto& VSList = PipelineLayouts[VS];
		auto Found = VSList.find(PS);
		if (Found != VSList.end())
		{
			OutLayouts = Found->second.DSLayouts;
			return Found->second.PipelineLayout;
		}

		VkPipelineLayoutCreateInfo Info;
		ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
		std::map<uint32, std::vector<VkDescriptorSetLayoutBinding>> LayoutBindings = VS->SetInfoBindings;
		if (PS)
		{
			for (auto Pair : PS->SetInfoBindings)
			{
				auto& SetBindings = LayoutBindings[Pair.first];
				SetBindings.insert(SetBindings.end(), Pair.second.begin(), Pair.second.end());
			}
		}

		auto& Layout = VSList[PS];

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

		OutLayouts = Layout.DSLayouts;

		VERIFY_VKRESULT(vkCreatePipelineLayout(Device->Device, &Info, nullptr, &Layout.PipelineLayout));
		return Layout.PipelineLayout;
	}

	template <typename TFunction>
	SVulkan::FGfxPSO CreateGfxPSO(FShaderInfo* VS, FShaderInfo* PS, SVulkan::FRenderPass* RenderPass, TFunction Callback)
	{
		check(VS->Shader && VS->Shader->ShaderModule);
		if (PS)
		{
			check(PS->Shader && PS->Shader->ShaderModule);
		}

		VkGraphicsPipelineCreateInfo GfxPipelineInfo;
		ZeroVulkanMem(GfxPipelineInfo, VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);

		GfxPipelineInfo.renderPass = RenderPass->RenderPass;

		VkPipelineInputAssemblyStateCreateInfo IAInfo;
		ZeroVulkanMem(IAInfo, VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
		IAInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		GfxPipelineInfo.pInputAssemblyState = &IAInfo;

		VkPipelineVertexInputStateCreateInfo VertexInputInfo;
		ZeroVulkanMem(VertexInputInfo, VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
		GfxPipelineInfo.pVertexInputState = &VertexInputInfo;

		VkPipelineShaderStageCreateInfo StageInfos[2];
		ZeroVulkanMem(StageInfos[0], VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
		StageInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		StageInfos[0].module = VS->Shader->ShaderModule;
		StageInfos[0].pName = VS->EntryPoint.c_str();
		GfxPipelineInfo.stageCount = 1;

		if (PS)
		{
			ZeroVulkanMem(StageInfos[1], VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
			StageInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			StageInfos[1].module = PS->Shader->ShaderModule;
			StageInfos[1].pName = PS->EntryPoint.c_str();
			++GfxPipelineInfo.stageCount;
		}
		GfxPipelineInfo.pStages = StageInfos;

		SVulkan::FGfxPSO PSO;
		PSO.VS = VS->Shader->DescSetInfo;
		if (PS)
		{
			PSO.PS = PS->Shader->DescSetInfo;
		}
		PSO.Layout = GetOrCreatePipelineLayout(VS->Shader, PS ? PS->Shader : nullptr, PSO.SetLayouts);
		PSO.Shaders.push_back(VS->Shader);
		if (PS)
		{
			PSO.Shaders.push_back(PS->Shader);
		}

		VkPipelineRasterizationStateCreateInfo RasterizerInfo;
		ZeroVulkanMem(RasterizerInfo, VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
		RasterizerInfo.cullMode = VK_CULL_MODE_NONE;
		RasterizerInfo.lineWidth = 1.0f;
		GfxPipelineInfo.pRasterizationState = &RasterizerInfo;

		VkPipelineColorBlendAttachmentState BlendAttachState;
		ZeroMem(BlendAttachState);
		BlendAttachState.colorWriteMask = 0xf;
		VkPipelineColorBlendStateCreateInfo BlendInfo;
		ZeroVulkanMem(BlendInfo, VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
		BlendInfo.attachmentCount = 1;
		BlendInfo.pAttachments = &BlendAttachState;
		GfxPipelineInfo.pColorBlendState = &BlendInfo;

		VkViewport Viewport;
		ZeroMem(Viewport);
		Viewport.width = 1280;
		Viewport.height = 960;
		Viewport.maxDepth = 1;
		VkRect2D Scissor;
		Scissor.extent.width = (uint32)Viewport.width;
		Scissor.extent.height = (uint32)Viewport.height;

		VkPipelineViewportStateCreateInfo ViewportState;
		ZeroVulkanMem(ViewportState, VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
		GfxPipelineInfo.pViewportState = &ViewportState;

		GfxPipelineInfo.layout = PSO.Layout;

		VkPipelineDepthStencilStateCreateInfo DepthStateInfo;
		ZeroVulkanMem(DepthStateInfo, VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
		DepthStateInfo.front = DepthStateInfo.back;
		GfxPipelineInfo.pDepthStencilState = &DepthStateInfo;

		VkPipelineMultisampleStateCreateInfo MSInfo;
		ZeroVulkanMem(MSInfo, VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
		MSInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		GfxPipelineInfo.pMultisampleState = &MSInfo;

		Callback(GfxPipelineInfo);

		VERIFY_VKRESULT(vkCreateGraphicsPipelines(Device->Device, VK_NULL_HANDLE, 1, &GfxPipelineInfo, nullptr, &PSO.Pipeline));
		PSOs.push_back(PSO);
		return PSO;
	}

	SVulkan::FComputePSO CreateComputePSO(FShaderInfo* CS)
	{
		check(CS->Shader && CS->Shader->ShaderModule);

		VkComputePipelineCreateInfo PipelineInfo;
		ZeroVulkanMem(PipelineInfo, VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
		ZeroVulkanMem(PipelineInfo.stage, VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
		PipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		PipelineInfo.stage.module = CS->Shader->ShaderModule;
		PipelineInfo.stage.pName = CS->EntryPoint.c_str();

		SVulkan::FComputePSO PSO;
		PSO.CS = CS->Shader->DescSetInfo;
		PSO.Layout = GetOrCreatePipelineLayout(CS->Shader, nullptr, PSO.SetLayouts);
		PSO.Shaders.push_back(CS->Shader);

		PipelineInfo.layout = PSO.Layout;

		VERIFY_VKRESULT(vkCreateComputePipelines(Device->Device, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &PSO.Pipeline));
		PSOs.push_back(PSO);
		return PSO;
	}

	void Destroy()
	{
		for (auto VSPair : PipelineLayouts)
		{
			for (auto PSPair : VSPair.second)
			{
				PSPair.second.Destroy(Device->Device);
			}
		}

		PipelineLayouts.clear();

		for (auto& PSO : PSOs)
		{
			vkDestroyPipeline(Device->Device, PSO.Pipeline, nullptr);
		}
		PSOs.clear();
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
		VkDescriptorPool Pool = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;
		std::vector<VkDescriptorSetLayout> Layouts;
		std::vector<FDescriptorSets> FreeSets;
		std::vector<uint32> NumDescriptorsPerSet;
		struct FUsedSets
		{
			SVulkan::FCmdBuffer* CmdBuffer = nullptr;
			uint64 FenceCounter = 0;
			FDescriptorSets Sets;
		};
		std::vector<FUsedSets> UsedSets;

		void Init(VkDevice InDevice, SVulkan::FPSO& PSO)
		{
			Device = InDevice;

			Layouts = PSO.SetLayouts;

			std::vector<VkDescriptorPoolSize> PoolSizes;
			{
				std::map<VkDescriptorType, uint32> TypeCounts;
				NumDescriptorsPerSet.push_back(0);
				for (auto* Shader : PSO.Shaders)
				{
					for (auto Pair : Shader->SetInfoBindings)
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
					Size.descriptorCount = Pair.second;
					PoolSizes.push_back(Size);
				}
			}

			VkDescriptorPoolCreateInfo PoolInfo;
			ZeroVulkanMem(PoolInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
			PoolInfo.maxSets = 1024 * (uint32)Layouts.size();
			PoolInfo.poolSizeCount = (uint32)PoolSizes.size();
			PoolInfo.pPoolSizes = PoolSizes.data();

			VERIFY_VKRESULT(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool))
		}

		FDescriptorSets AllocSets()
		{
			FDescriptorSets Sets;
			Sets.Sets.resize(Layouts.size());

			VkDescriptorSetAllocateInfo AllocInfo;
			ZeroVulkanMem(AllocInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
			AllocInfo.descriptorPool = Pool;
			AllocInfo.descriptorSetCount = (uint32)Layouts.size();
			AllocInfo.pSetLayouts = Layouts.data();
			VERIFY_VKRESULT(vkAllocateDescriptorSets(Device, &AllocInfo, Sets.Sets.data()));

			return Sets;
		}

		void Destroy()
		{
			check(0);
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

	void UpdateDescriptors(SVulkan::FCmdBuffer& CmdBuffer, uint32 NumWrites, VkWriteDescriptorSet* DescriptorWrites, SVulkan::FGfxPSO& InPSO)
	{
		if (Device->bPushDescriptor)
		{
			vkCmdPushDescriptorSetKHR(CmdBuffer.CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, InPSO.Layout, 0, NumWrites, DescriptorWrites);
		}
		else
		{
			SVulkan::FPSO* PSO = &InPSO;
			if (PSODescriptors.find(PSO) == PSODescriptors.end())
			{
				PSODescriptors[PSO].Init(Device->Device, InPSO);
			}

			auto Sets = PSODescriptors[PSO].AllocSets();
			Sets.UpdateDescriptorWrites(NumWrites, DescriptorWrites, PSODescriptors[PSO].NumDescriptorsPerSet);
			vkUpdateDescriptorSets(Device->Device, NumWrites, DescriptorWrites, 0, nullptr);
			vkCmdBindDescriptorSets(CmdBuffer.CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, InPSO.Layout, 0, (uint32)Sets.Sets.size(), Sets.Sets.data(), 0, nullptr);
		}
	}

	void UpdateDescriptors(SVulkan::FCmdBuffer& CmdBuffer, uint32 NumWrites, VkWriteDescriptorSet* DescriptorWrites, SVulkan::FComputePSO& InPSO)
	{
		if (Device->bPushDescriptor)
		{
			vkCmdPushDescriptorSetKHR(CmdBuffer.CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, InPSO.Layout, 0, NumWrites, DescriptorWrites);
		}
		else
		{
			SVulkan::FPSO* PSO = &InPSO;
			if (PSODescriptors.find(PSO) == PSODescriptors.end())
			{
				PSODescriptors[PSO].Init(Device->Device, InPSO);
			}

			auto Sets = PSODescriptors[PSO].AllocSets();
			Sets.UpdateDescriptorWrites(NumWrites, DescriptorWrites, PSODescriptors[PSO].NumDescriptorsPerSet);
			vkUpdateDescriptorSets(Device->Device, NumWrites, DescriptorWrites, 0, nullptr);
			vkCmdBindDescriptorSets(CmdBuffer.CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, InPSO.Layout, 0, (uint32)Sets.Sets.size(), Sets.Sets.data(), 0, nullptr);
		}
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
