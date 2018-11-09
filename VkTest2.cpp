// VkTest2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <GLFW/glfw3.h>

#include "RCVulkan.h"
#include "../RCUtils/RCUtilsFile.h"
#include <direct.h>

#pragma comment(lib, "glfw3.lib")

static SVulkan GVulkan;

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

	bool CreateShader(FShaderInfo* Info, std::vector<char>& Data)
	{
		Info->Shader = new SVulkan::FShader;
		Info->Shader->SpirV = Data;
		return Info->Shader->Create(Device);
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
		ShaderInfos.resize(0);
	}
};
static FShaderLibrary GShaderLibrary;

struct FPSOCache
{
	std::map<SVulkan::FShader*, std::map<SVulkan::FShader*, VkPipelineLayout>> PipelineLayouts;

	VkDevice Device =  VK_NULL_HANDLE;
	void Init(VkDevice InDevice)
	{
		Device = InDevice;
	}

	VkPipelineLayout GetOrCreatePipelineLayout(SVulkan::FShader* VS, SVulkan::FShader* PS)
	{
		auto& VSList = PipelineLayouts[VS];
		auto Found = VSList.find(PS);
		if (Found != VSList.end())
		{
			return Found->second;
		}

		VkPipelineLayoutCreateInfo Info;
		ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);

		VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
		VERIFY_VKRESULT(vkCreatePipelineLayout(Device, &Info, nullptr, &PipelineLayout));

		VSList[PS] = PipelineLayout;

		return PipelineLayout;
	}

	template <typename TFunction>
	void CreatePSO(FShaderInfo* VS, FShaderInfo* PS, TFunction Callback)
	{
		check(VS->Shader && VS->Shader->ShaderModule);
		if (PS)
		{
			check(PS->Shader && PS->Shader->ShaderModule);
		}

		VkGraphicsPipelineCreateInfo GfxPipelineInfo;
		ZeroVulkanMem(GfxPipelineInfo, VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);

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

		VkPipelineLayout PipelineLayout = GetOrCreatePipelineLayout(VS->Shader, PS ? PS->Shader : nullptr);

		VkPipelineRasterizationStateCreateInfo RasterizerInfo;
		ZeroVulkanMem(RasterizerInfo, VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
		RasterizerInfo.cullMode = VK_CULL_MODE_NONE;
		GfxPipelineInfo.pRasterizationState = &RasterizerInfo;

		GfxPipelineInfo.layout = PipelineLayout;

		Callback(GfxPipelineInfo);

		VkPipeline Pipeline;
		VERIFY_VKRESULT(vkCreateGraphicsPipelines(GVulkan.Devices[GVulkan.PhysicalDevice].Device, VK_NULL_HANDLE, 1, &GfxPipelineInfo, nullptr, &Pipeline));
	}

	void Destroy()
	{
		for (auto VSPair : PipelineLayouts)
		{
			for (auto PSPair : VSPair.second)
			{
				vkDestroyPipelineLayout(Device, PSPair.second, nullptr);
			}
		}

		PipelineLayouts.clear();
	}
};
static FPSOCache GPSOCache;

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

	Device.ResetCommandPools();

	SVulkan::FCmdBuffer& CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);

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
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0,
		VK_IMAGE_ASPECT_COLOR_BIT);

	CmdBuffer.End();

	uint64 FenceCounter = CmdBuffer.Fence.Counter;
	Device.Submit(Device.PresentQueue, CmdBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, GVulkan.Swapchain.AcquireBackbufferSemaphore, GVulkan.Swapchain.FinalSemaphore, CmdBuffer.Fence);

	GVulkan.Swapchain.Present(Device.PresentQueue, GVulkan.Swapchain.FinalSemaphore);
	Device.WaitForFence(CmdBuffer.Fence);
}


static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
}

static void SetupShaders()
{
	auto* NoVBClipVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "MainNoVBClipVS", FShaderInfo::EStage::Vertex);
	auto* RedPS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "RedPS", FShaderInfo::EStage::Pixel);
	GShaderLibrary.RecompileShaders();

	GPSOCache.CreatePSO(NoVBClipVS, RedPS, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
	{
	});
}

static GLFWwindow* Init()
{
	int RC = glfwInit();
	check(RC != 0);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	GLFWwindow* Window = glfwCreateWindow(1280, 720, "VkTest2", 0, 0);
	check(Window);

	GVulkan.Init(Window);
	GShaderLibrary.Init(GVulkan.Devices[GVulkan.PhysicalDevice].Device);
	GPSOCache.Init(GVulkan.Devices[GVulkan.PhysicalDevice].Device);

	glfwSetKeyCallback(Window, KeyCallback);

	SetupShaders();

	return Window;
}

static void Deinit(GLFWwindow* Window)
{
	glfwDestroyWindow(Window);

	GPSOCache.Destroy();
	GShaderLibrary.Destroy();

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
