// VkTest2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <GLFW/glfw3.h>

#include "RCVulkan.h"

#pragma comment(lib, "glfw3.lib")

static SVulkanInstance GVulkanInstance;


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

	GVulkanInstance.Init(Window);

	glfwSetKeyCallback(Window, KeyCallback);

	return Window;
}

static void Deinit(GLFWwindow* Window)
{
	glfwDestroyWindow(Window);
	GVulkanInstance.Deinit();
}

int main()
{
	GLFWwindow* Window = Init();
	while (!glfwWindowShouldClose(Window))
	{
		double CpuBegin = glfwGetTime() * 1000.0;

		glfwPollEvents();


		double CpuEnd = glfwGetTime() * 1000.0;
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
