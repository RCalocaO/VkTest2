// VkTest2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <GLFW/glfw3.h>

#include "RCVulkan.h"

#pragma comment(lib, "glfw3.lib")

static SVulkan GVulkan;


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

void Render()
{
	SVulkan::SDevice& Device = GVulkan.Devices[GVulkan.PhysicalDevice];
	GVulkan.Swapchain.AcquireBackbuffer();

	vkResetCommandPool(Device.Device, Device.CmdPools[Device.GfxQueueIndex].CmdPool, 0);

	SVulkan::FCmdBuffer& CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);
	float ClearColor[4] = {1.0f, 1.0f, 0.0f, 0.0f};
	ClearImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex], ClearColor);
	CmdBuffer.End();

	uint64 FenceCounter = CmdBuffer.Fence.Counter;
	Device.Submit(Device.PresentQueue, CmdBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, GVulkan.Swapchain.AcquireBackbufferSemaphore, GVulkan.Swapchain.FinalSemaphore, CmdBuffer.Fence);

	GVulkan.Swapchain.Present(Device.TransferQueue, GVulkan.Swapchain.FinalSemaphore);
	vkWaitForFences(Device.Device, 1, &CmdBuffer.Fence.Fence, VK_TRUE, 5 * 1000 * 1000);
}


static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
}

static GLFWwindow* Init()
{
	int RC = glfwInit();
	check(RC != 0);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	GLFWwindow* Window = glfwCreateWindow(1280, 720, "VkTest2", 0, 0);
	check(Window);

	GVulkan.Init(Window);

	glfwSetKeyCallback(Window, KeyCallback);

	return Window;
}

static void Deinit(GLFWwindow* Window)
{
	glfwDestroyWindow(Window);
	GVulkan.Deinit();
}

int main()
{
	GLFWwindow* Window = Init();
	while (!glfwWindowShouldClose(Window))
	{
		double CpuBegin = glfwGetTime() * 1000.0;

		glfwPollEvents();

		Render();

		double CpuEnd = glfwGetTime() * 1000.0;

		double CpuDelta = CpuEnd - CpuBegin;
	}

	Deinit(Window);
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
