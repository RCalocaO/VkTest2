// VkTest2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <GLFW/glfw3.h>

// Fix Windows warning
#undef APIENTRY

#include "RCVulkan.h"

#pragma comment(lib, "glfw3.lib")

static SVulkan GVulkan;


static FShaderLibrary GShaderLibrary;

static FRenderTargetCache GRenderTargetCache;

static FPSOCache GPSOCache;

struct FApp
{
	SVulkan::FGfxPSO NoVBClipVSResPSO;
	SVulkan::FGfxPSO DataClipVSResPSO;
	SVulkan::FGfxPSO VBClipVSResPSO;
	FBufferWithMem ClipVB;
	VkBufferView ClipVBView;
	FBufferWithMem StagingClipVB;
	FShaderInfo* TestCS = nullptr;

	void Create(SVulkan::SDevice& Device)
	{
		// Dummy stuff
		uint32 ClipVBSize = 3 * 4 * sizeof(float);
		ClipVB.Create(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ClipVBSize);

		{
			StagingClipVB.Create(Device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ClipVBSize);
			float* Data = (float*)StagingClipVB.Lock();
			//float3 Pos[3] ={float3(0, -0.5, 1), float3(-0.5, 0.5, 1), float3(0.5, 0.5, 1)};
			*Data++ = 0;		*Data++ = -0.5f;	*Data++ = 1; *Data++ = 1;
			*Data++ = -0.5f;	*Data++ = 0.5f;		*Data++ = 1; *Data++ = 1;
			*Data++ = 0.5f;		*Data++ = 0.5f;		*Data++ = 1; *Data++ = 1;
			StagingClipVB.Unlock();
		}

		VkBufferViewCreateInfo Info;
		ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO);
		Info.buffer = ClipVB.Buffer.Buffer;
		Info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		Info.range = ClipVB.Size;
		VERIFY_VKRESULT(vkCreateBufferView(Device.Device, &Info, nullptr, &ClipVBView));
	}

	void Destroy()
	{
		vkDestroyBufferView(ClipVB.Buffer.Device, ClipVBView, nullptr);
		ClipVBView = VK_NULL_HANDLE;
		StagingClipVB.Destroy();
		ClipVB.Destroy();
	}
};

static void ClearImage(VkCommandBuffer CmdBuffer, VkImage Image, float Color[4])
{
	VkClearColorValue ClearColors;
	ClearColors.float32[0] = Color[0];
	ClearColors.float32[1] = Color[1];
	ClearColors.float32[2] = Color[2];
	ClearColors.float32[3] = Color[3];

	VkImageSubresourceRange Range;
	ZeroMem(Range);
	Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Range.layerCount = 1;
	Range.levelCount = 1;
	vkCmdClearColorImage(CmdBuffer, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearColors, 1, &Range);
}

void Render(FApp& App)
{
	SVulkan::SDevice& Device = GVulkan.Devices[GVulkan.PhysicalDevice];
	GVulkan.Swapchain.AcquireBackbuffer();

	Device.RefreshCommandBuffers();

	SVulkan::FCmdBuffer& CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);

	static bool bFirst = true;
	if (bFirst)
	{
		VkBufferCopy Region;
		ZeroMem(Region);
		Region.size = App.ClipVB.Size;
		vkCmdCopyBuffer(CmdBuffer.CmdBuffer, App.StagingClipVB.Buffer.Buffer, App.ClipVB.Buffer.Buffer, 1, &Region);
		bFirst = false;
	}

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0, 
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT);

	static float F = 0;
	F += 0.025f;
	float ClearColor[4] = {0.0f, abs(sin(F)), abs(cos(F)), 0.0f};
	ClearImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex], ClearColor);

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT);

	SVulkan::FFramebuffer* Framebuffer = GRenderTargetCache.GetOrCreateFrameBuffer(&GVulkan.Swapchain, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);

	CmdBuffer.BeginRenderPass(Framebuffer);
	if (0)
	{
		vkCmdBindPipeline(CmdBuffer.CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.NoVBClipVSResPSO.Pipeline);
	}
	else if (1)
	{
		vkCmdBindPipeline(CmdBuffer.CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.DataClipVSResPSO.Pipeline);

		VkWriteDescriptorSet DescriptorWrites;
		ZeroVulkanMem(DescriptorWrites, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		//DescriptorWrites.pNext = NULL;
		//DescriptorWrites.dstSet = 0;  // dstSet is ignored by the extension
		DescriptorWrites.descriptorCount = 1;
		DescriptorWrites.descriptorType = (VkDescriptorType)App.DataClipVSResPSO.VS[0]->bindings[0]->descriptor_type;
		DescriptorWrites.pTexelBufferView = &App.ClipVBView;
		//DescriptorWrites.pBufferInfo = &info.uniform_data.buffer_info;  // populated by init_uniform_buffer()
		//DescriptorWrites.dstArrayElement = 0;
		DescriptorWrites.dstBinding = App.DataClipVSResPSO.VS[0]->bindings[0]->binding;

		vkCmdPushDescriptorSetKHR(CmdBuffer.CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.DataClipVSResPSO.Layout, 0, 1, &DescriptorWrites);
	}
	else if (1)
	{
		vkCmdBindPipeline(CmdBuffer.CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.VBClipVSResPSO.Pipeline);
		VkDeviceSize Offsets = 0;
		vkCmdBindVertexBuffers(CmdBuffer.CmdBuffer, 0, 1, &App.ClipVB.Buffer.Buffer, &Offsets);
	}
	vkCmdDraw(CmdBuffer.CmdBuffer, 3, 1, 0, 0);

	CmdBuffer.EndRenderPass();

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0,
		VK_IMAGE_ASPECT_COLOR_BIT);

	CmdBuffer.End();

	Device.Submit(Device.PresentQueue, CmdBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, GVulkan.Swapchain.AcquireBackbufferSemaphore, GVulkan.Swapchain.FinalSemaphore);

	GVulkan.Swapchain.Present(Device.PresentQueue, GVulkan.Swapchain.FinalSemaphore);
	Device.WaitForFence(CmdBuffer.Fence, CmdBuffer.LastSubmittedFence);
}


static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
}

static void SetupShaders(FApp& App)
{
	App.TestCS = GShaderLibrary.RegisterShader("Shaders/TestCS.hlsl", "WriteTriCS", FShaderInfo::EStage::Compute);
	auto* VBClipVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "VBClipVS", FShaderInfo::EStage::Vertex);
	auto* NoVBClipVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "MainNoVBClipVS", FShaderInfo::EStage::Vertex);
	auto* DataClipVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "MainBufferClipVS", FShaderInfo::EStage::Vertex);
	auto* RedPS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "RedPS", FShaderInfo::EStage::Pixel);
	GShaderLibrary.RecompileShaders();

	SVulkan::FRenderPass* RenderPass = GRenderTargetCache.GetOrCreateRenderPass(GVulkan.Swapchain.Format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);

	VkViewport Viewport = GVulkan.Swapchain.GetViewport();
	VkRect2D Scissor = GVulkan.Swapchain.GetScissor();

	App.NoVBClipVSResPSO = GPSOCache.CreateGfxPSO(NoVBClipVS, RedPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
	{
		VkPipelineViewportStateCreateInfo* ViewportInfo = (VkPipelineViewportStateCreateInfo*)GfxPipelineInfo.pViewportState;
		ViewportInfo->viewportCount = 1;
		ViewportInfo->pViewports = &Viewport;
		ViewportInfo->scissorCount = 1;
		ViewportInfo->pScissors = &Scissor;
	});

	App.DataClipVSResPSO = GPSOCache.CreateGfxPSO(DataClipVS, RedPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
	{
		VkPipelineViewportStateCreateInfo* ViewportInfo = (VkPipelineViewportStateCreateInfo*)GfxPipelineInfo.pViewportState;
		ViewportInfo->viewportCount = 1;
		ViewportInfo->pViewports = &Viewport;
		ViewportInfo->scissorCount = 1;
		ViewportInfo->pScissors = &Scissor;
	});

	VkVertexInputAttributeDescription VertexAttrDesc;
	ZeroMem(VertexAttrDesc);
	VertexAttrDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	VkVertexInputBindingDescription VertexBindDesc;
	ZeroMem(VertexBindDesc);
	VertexBindDesc.stride = 4 * sizeof(float);
	App.VBClipVSResPSO = GPSOCache.CreateGfxPSO(VBClipVS, RedPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
	{
		VkPipelineViewportStateCreateInfo* ViewportInfo = (VkPipelineViewportStateCreateInfo*)GfxPipelineInfo.pViewportState;
		ViewportInfo->viewportCount = 1;
		ViewportInfo->pViewports = &Viewport;
		ViewportInfo->scissorCount = 1;
		ViewportInfo->pScissors = &Scissor;

		VkPipelineVertexInputStateCreateInfo* VertexInputInfo = (VkPipelineVertexInputStateCreateInfo*)GfxPipelineInfo.pVertexInputState;
		VertexInputInfo->vertexAttributeDescriptionCount = 1;
		VertexInputInfo->pVertexAttributeDescriptions = &VertexAttrDesc;
		VertexInputInfo->vertexBindingDescriptionCount = 1;
		VertexInputInfo->pVertexBindingDescriptions = &VertexBindDesc;
	});
}

static GLFWwindow* Init(FApp& App)
{
	int RC = glfwInit();
	check(RC != 0);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	uint32 ResX = RCUtils::FCmdLine::Get().TryGetIntPrefix("-resx=", 1920);
	uint32 ResY = RCUtils::FCmdLine::Get().TryGetIntPrefix("-resy=", 1080);

	GLFWwindow* Window = glfwCreateWindow(ResX, ResY, "VkTest2", 0, 0);
	check(Window);

	if (RCUtils::FCmdLine::Get().Contains("-waitfordebugger"))
	{
		while (!::IsDebuggerPresent())
		{
			::Sleep(100);
		}
		__debugbreak();
	}

	GVulkan.Init(Window);
	SVulkan::SDevice& Device = GVulkan.Devices[GVulkan.PhysicalDevice];

	GRenderTargetCache.Init(Device.Device);
	GShaderLibrary.Init(Device.Device);
	GPSOCache.Init(Device.Device);

	glfwSetKeyCallback(Window, KeyCallback);

	SetupShaders(App);

	App.Create(Device);

	return Window;
}

static void Deinit(FApp& App, GLFWwindow* Window)
{
	GVulkan.DeinitPre();

	App.Destroy();

	GPSOCache.Destroy();
	GShaderLibrary.Destroy();
	GRenderTargetCache.Destroy();

	GVulkan.Deinit();

	glfwDestroyWindow(Window);
}

int main()
{
	FApp App;
	GLFWwindow* Window = Init(App);
	while (!glfwWindowShouldClose(Window))
	{
		double CpuBegin = glfwGetTime() * 1000.0;

		glfwPollEvents();

		Render(App);

		double CpuEnd = glfwGetTime() * 1000.0;

		double CpuDelta = CpuEnd - CpuBegin;
	}

	Deinit(App, Window);
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
