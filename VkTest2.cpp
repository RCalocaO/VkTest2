// VkTest2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

// Fix Windows warning
#undef APIENTRY

#include "RCVulkan.h"

#include "imgui.h"
#include "examples/imgui_impl_glfw.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "RCScene.h"

#include "Shaders/ShaderDefines.h"

#include "../RCUtils/RCUtilsMath.h"

#include <thread>


struct FUnlitVertex
{
	FVector3 Position;
	uint32 Color;

	void Set(FVector3 V, uint32 C)
	{
		Position = V;
		Color = C;
	}
};

static void GenerateBoundingBox(FStagingBuffer* VB, FStagingBuffer* IB, const FBoundingBox& BB)
{
	enum EBoundingBoxCorners
	{
		iii,
		aii,
		aia,
		iia,
		iai,
		aai,
		aaa,
		iaa,
	};

#if SCENE_USE_SINGLE_BUFFERS
	uint32 Color = 0xff00ffff;
	FUnlitVertex* Pos = (FUnlitVertex*)VB->Buffer->Lock();
	Pos[iii].Set(FVector3(BB.Min.x, BB.Min.y, BB.Min.z), Color);
	Pos[aii].Set(FVector3(BB.Max.x, BB.Min.y, BB.Min.z), Color);
	Pos[iia].Set(FVector3(BB.Min.x, BB.Min.y, BB.Max.z), Color);
	Pos[aia].Set(FVector3(BB.Max.x, BB.Min.y, BB.Max.z), Color);
	Pos[iai].Set(FVector3(BB.Min.x, BB.Max.y, BB.Min.z), Color);
	Pos[aai].Set(FVector3(BB.Max.x, BB.Max.y, BB.Min.z), Color);
	Pos[iaa].Set(FVector3(BB.Min.x, BB.Max.y, BB.Max.z), Color);
	Pos[aaa].Set(FVector3(BB.Max.x, BB.Max.y, BB.Max.z), Color);
	VB->Buffer->Unlock();

	uint32* Indices = (uint32*)IB->Buffer->Lock();
	Indices[0] = iii; Indices[1] = aii;
	Indices[2] = iii; Indices[3] = iai;
	Indices[4] = iii; Indices[5] = iia;
	Indices[6] = aii; Indices[7] = aia;
	Indices[8] = aii; Indices[9] = aai;
	Indices[10] = iia; Indices[11] = aia;
	Indices[12] = aia; Indices[13] = aaa;
	Indices[14] = aaa; Indices[15] = aai;
	Indices[16] = aaa; Indices[17] = iaa;
	Indices[18] = aai; Indices[19] = iai;
	Indices[20] = iai; Indices[21] = iaa;
	Indices[22] = iia; Indices[23] = iaa;
	IB->Buffer->Unlock();
#else
	check(0);
#endif
}

static void GenerateIcosahedron(FStagingBuffer* VB, FStagingBuffer* IB, FVector3 Center, float Radius, uint32 Color = 0xff0000ff)
{
	uint32 SrcIndices[] = { 
		0,4,1,
		0,9,4,
		9,5,4,
		4,5,8,
		4,8,1,
		8,10,1,
		8,3,10,
		5,3,8,
		5,2,3,
		2,7,3,
		7,10,3,
		7,6,10,
		7,11,6,
		11,0,6,
		0,1,6,
		6,1,10,
		9,0,11,
		9,11,2,
		9,2,5,
		7,2,11 };
	uint32* Indices = (uint32*)IB->Buffer->Lock();
	memcpy(Indices, SrcIndices, sizeof(SrcIndices));
	IB->Buffer->Unlock();

#if SCENE_USE_SINGLE_BUFFERS
	FUnlitVertex* Pos = (FUnlitVertex*)VB->Buffer->Lock();

	float X = 0.525731112119133606f * Radius;
	float Z = 0.850650808352039932f * Radius;

	Pos[0].Set(FVector3(-X, 0, Z) + Center, Color);
	Pos[1].Set(FVector3(X, 0, Z) + Center, Color);
	Pos[2].Set(FVector3(-X, 0, -Z) + Center, Color);
	Pos[3].Set(FVector3(X, 0, -Z) + Center, Color);
	Pos[4].Set(FVector3(0, Z, X) + Center, Color);
	Pos[5].Set(FVector3(0, Z, -X) + Center, Color);
	Pos[6].Set(FVector3(0, -Z, X) + Center, Color);
	Pos[7].Set(FVector3(0, -Z, -X) + Center, Color);
	Pos[8].Set(FVector3(Z, X, 0) + Center, Color);
	Pos[9].Set(FVector3(-Z, X, 0) + Center, Color);
	Pos[10].Set(FVector3(Z, -X, 0) + Center, Color);
	Pos[11].Set(FVector3(-Z, -X, 0) + Center, Color);

	VB->Buffer->Unlock();
#else
	check(0);
#endif
}

//extern void RenderJOTL(SVulkan::SDevice& Device, FStagingBufferManager& StagingMgr, FDescriptorCache& DescriptorCache, SVulkan::FGfxPSO* PSO, SVulkan::FCmdBuffer*);

static SVulkan GVulkan;

static FShaderLibrary GShaderLibrary;

static FRenderTargetCache GRenderTargetCache;

static FPSOCache GPSOCache;

static FDescriptorCache GDescriptorCache;

static FStagingBufferManager GStagingBufferMgr;

struct FGLTFLoader;
extern FGLTFLoader* CreateGLTFLoader(const char* Filename);
extern bool IsGLTFLoaderFinished(FGLTFLoader* Loader);
extern const char* GetGLTFFilename(FGLTFLoader* Loader);
extern void CreateGLTFGfxResources(FGLTFLoader* Loader, SVulkan::SDevice& Device, FPSOCache& PSOCache, FScene& Scene, FPendingOpsManager& PendingStagingOps, FStagingBufferManager* StagingMgr);
extern void FreeGLTFLoader(FGLTFLoader* Loader);


struct FCamera
{
	FVector3 Pos = { 0, 0, 0 };
	FVector3 Front = { 0, 0, 1 };
	FVector3 Up = { 0, 1, 0 };

	float LastX;
	float LastY;
	bool bFirstTime = true;

	union
	{
		FVector2 Rot;
		struct
		{
			float Yaw;
			float Pitch;
		};
	};

	FCamera()
	{
		Yaw = 90;
		Pitch = 0;
	}

	void Init(float Width, float Height)
	{
		LastX = Width / 2.0f;
		LastY = Height / 2.0f;
	}

	FMatrix4x4 Update()
	{
		Pitch = Clamp(-89.0f, Pitch, 89.0f);

		FVector3 Direction;
		Direction.x = cos(ToRadians(Yaw)) * cos(ToRadians(Pitch));
		Direction.y = sin(ToRadians(Pitch));
		Direction.z = sin(ToRadians(Yaw)) * cos(ToRadians(Pitch));
		Front = Direction.GetNormalized();

		FVector3 f = Front;
		FVector3 u = Up.GetNormalized();
		FVector3 s = FVector3::Cross(u, f).GetNormalized();
		u = FVector3::Cross(f, s);

		FMatrix4x4 ViewMtx = FMatrix4x4::GetIdentity();
		ViewMtx.Set(0, 0, s.x);
		ViewMtx.Set(0, 1, u.x);
		ViewMtx.Set(0, 2, f.x);

		ViewMtx.Set(1, 0, s.y);
		ViewMtx.Set(1, 1, u.y);
		ViewMtx.Set(1, 2, f.y);

		ViewMtx.Set(2, 0, s.z);
		ViewMtx.Set(2, 1, u.z);
		ViewMtx.Set(2, 2, f.z);

		ViewMtx.Set(3, 0, -FVector3::Dot(s, Pos));
		ViewMtx.Set(3, 1, -FVector3::Dot(u, Pos));
		ViewMtx.Set(3, 2, -FVector3::Dot(f, Pos));

		return ViewMtx;
	}
};

static FIntVector4 g_vMode = {0, 0, 0 ,0};
static FIntVector4 g_vMode2 = { 0, 0, 0 ,0 };
static bool g_bWireframe = false;

//extern bool LoadGLTF(SVulkan::SDevice& Device, const char* Filename, FPSOCache& PSOCache, FScene& Scene, FPendingOpsManager& PendingStagingOps, FStagingBufferManager* StagingMgr);


double GetTimeInMs()
{
	double Time = glfwGetTime() * 1000.0;
	return Time;
}

FVector2 TryGetVector2Prefix(const char* Prefix, FVector2 Value)
{
	check(Prefix);
	uint32 PrefixLength = (uint32)strlen(Prefix);
	for (const auto& Arg : RCUtils::FCmdLine::Get().Args)
	{
		if (!_strnicmp(Arg.c_str(), Prefix, PrefixLength))
		{
			const char* Vector3String = Arg.c_str() + PrefixLength;
			sscanf(Vector3String, "%f,%f", &Value.x, &Value.y);
			break;
		}
	}

	return Value;
}

FVector3 TryGetVector3Prefix(const char* Prefix, FVector3 Value)
{
	check(Prefix);
	uint32 PrefixLength = (uint32)strlen(Prefix);
	for (const auto& Arg : RCUtils::FCmdLine::Get().Args)
	{
		if (!_strnicmp(Arg.c_str(), Prefix, PrefixLength))
		{
			const char* Vector3String = Arg.c_str() + PrefixLength;
			sscanf(Vector3String, "%f,%f,%f", &Value.x, &Value.y, &Value.z);
			break;
		}
	}

	return Value;
}

FVector4 TryGetVector4Prefix(const char* Prefix, FVector4 Value)
{
	check(Prefix);
	uint32 PrefixLength = (uint32)strlen(Prefix);
	for (const auto& Arg : RCUtils::FCmdLine::Get().Args)
	{
		if (!_strnicmp(Arg.c_str(), Prefix, PrefixLength))
		{
			const char* Vector4String = Arg.c_str() + PrefixLength;
			sscanf(Vector4String, "%f,%f,%f,%f", &Value.x, &Value.y, &Value.z, &Value.w);
			break;
		}
	}

	return Value;
}

static void ResizeCallback(GLFWwindow*, int w, int h);

enum class ELoadingState
{
	Idle,
	BeginLoading,
	Loading,
	FinishedLoading,
};

struct FApp
{
	std::atomic<ELoadingState> LoadingState = ELoadingState::Idle;

	bool bResizeSwapchain = false;
	bool bLMouseButtonHeld = false;
	bool bRMouseButtonHeld = false;
	uint32 FrameIndex = 0;

	FImageWithMemAndView DepthBuffer;

	FPSOCache::FPSOHandle TestGLTFPSO;
	FPSOCache::FPSOHandle TestCSPSO;

	FPSOCache::FPSOHandle ImGUIPSO;
	int32 ImGUIVertexDecl = -1;

	FPSOCache::FPSOHandle UnlitPSO;
	int32 UnlitVertexDecl = -1;

	FBufferWithMemAndView ClipVB;
	FBufferWithMemAndView TestCSBuffer;
	FBufferWithMem TestCSUB;
	FBufferWithMem ColorUB;
	GLFWwindow* Window = nullptr;
	uint32 ImGuiMaxVertices = 32768 * 3;
	uint32 ImGuiMaxIndices = 32768 * 3;
	FImageWithMemAndView ImGuiFont;
	VkSampler ImGuiFontSampler = VK_NULL_HANDLE;
	VkSampler LinearMipSampler = VK_NULL_HANDLE;
	FImageWithMemAndView WhiteTexture;
	FImageWithMemAndView DefaultNormalMapTexture;
	FGPUTiming GPUTiming;
	enum
	{
		NUM_IMGUI_BUFFERS = 3,
	};
	FBufferWithMem ImGuiVB[NUM_IMGUI_BUFFERS];
	FBufferWithMem ImGuiIB[NUM_IMGUI_BUFFERS];
	FBufferWithMem ImGuiScaleTranslateUB[NUM_IMGUI_BUFFERS];
	double Time = 0;

	float LastDelta = 1.0f / 60.0f;
	double GpuDelta = 0;
	double CpuDelta = 0;

	void Create(SVulkan::SDevice& Device, GLFWwindow* InWindow)
	{
		Window = InWindow;

		// Dummy stuff
		uint32 ClipVBSize = 3 * 4 * sizeof(float);
		ClipVB.Create(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::GPU, ClipVBSize, VK_FORMAT_R32G32B32A32_SFLOAT, false);
		PendingOpsMgr.AddUpdateBuffer(ClipVB.Buffer.Buffer, ClipVBSize, 
			[](void* InData)
			{
				float* Data = (float*)InData;
				*Data++ = 0;		*Data++ = -0.5f;	*Data++ = 1; *Data++ = 1;
				*Data++ = 0.5f;		*Data++ = 0.5f;		*Data++ = 1; *Data++ = 1;
				*Data++ = -0.5f;	*Data++ = 0.5f;		*Data++ = 1; *Data++ = 1;
			});

		ColorUB.Create(Device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, EMemLocation::CPU_TO_GPU, 4 * sizeof(float), true);
		{
			float* Data = (float*)ColorUB.Lock();
			*Data++ = 0.0f;
			*Data++ = 1.0f;
			*Data++ = 0.0f;
			*Data++ = 1.0f;
			ColorUB.Unlock();
		}

		TestCSBuffer.Create(Device, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, EMemLocation::CPU_TO_GPU, 256 * 256 * 4 * sizeof(float), VK_FORMAT_R32G32B32A32_SFLOAT, false);

		TestCSUB.Create(Device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, EMemLocation::CPU_TO_GPU, 4 * sizeof(uint32) + 16 * 1024, true);
		{
			uint32* Data = (uint32*)TestCSUB.Lock();
			*Data++ = 0xffffffff;
			*Data++ = 0xffffffff;
			*Data++ = 0xffffffff;
			*Data++ = 0xffffffff;
			memset(Data, 0x33, 16384);
			TestCSUB.Unlock();
		}

		WhiteTexture.Create(Device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, EMemLocation::GPU, 1, 1, VK_FORMAT_R8G8B8A8_UNORM);
		Device.SetDebugName(WhiteTexture.Image.Image, VK_OBJECT_TYPE_IMAGE, "WhiteTexture");
		{
			FStagingBuffer* Buffer = GStagingBufferMgr.AcquireBuffer(4, nullptr);
			uint8* Mem = (uint8*)Buffer->Buffer->Lock();
			*Mem += 0xff;
			*Mem += 0xff;
			*Mem += 0xff;
			*Mem += 0xff;
			Buffer->Buffer->Unlock();

			PendingOpsMgr.AddCopyBufferToImage(Buffer, WhiteTexture.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		DefaultNormalMapTexture.Create(Device, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, EMemLocation::GPU, 1, 1, VK_FORMAT_R32G32B32A32_SFLOAT);
		Device.SetDebugName(DefaultNormalMapTexture.Image.Image, VK_OBJECT_TYPE_IMAGE, "DefaultNormalMap");
		{
			FStagingBuffer* Buffer = GStagingBufferMgr.AcquireBuffer(sizeof(FVector4), nullptr);
			FVector4* Mem = (FVector4*)Buffer->Buffer->Lock();
			Mem->x = 0;
			Mem->y = 0;
			Mem->z = 1.0f;
			Mem->w = 0;
			Buffer->Buffer->Unlock();

			PendingOpsMgr.AddCopyBufferToImage(Buffer, DefaultNormalMapTexture.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		GPUTiming.Init(&Device, PendingOpsMgr);
		RecreateDepthBuffer(Device);
	}

	void RecreateDepthBuffer(SVulkan::SDevice& Device)
	{
		DepthBuffer.Destroy();

		int32 Width = 0, Height = 1;
		glfwGetFramebufferSize(Window, &Width, &Height);
		glfwSetFramebufferSizeCallback(Window, ResizeCallback);
		DepthBuffer.Create(Device, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, EMemLocation::GPU, (uint32)Width, (uint32)Height, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
	}

	void Destroy()
	{
		vkDestroySampler(ImGuiFont.Image.Device, LinearMipSampler, nullptr);
		DefaultNormalMapTexture.Destroy();
		DepthBuffer.Destroy();
		GPUTiming.Destroy();

		WhiteTexture.Destroy();

		vkDestroySampler(ImGuiFont.Image.Device, ImGuiFontSampler, nullptr);
		for (int32 Index = 0; Index < NUM_IMGUI_BUFFERS; ++Index)
		{
			ImGuiIB[Index].Destroy();
			ImGuiVB[Index].Destroy();
			ImGuiScaleTranslateUB[Index].Destroy();
		}
		ImGuiFont.Destroy();

		Scene.Destroy();
		TestCSUB.Destroy();
		TestCSBuffer.Destroy();
		ClipVB.Destroy();
		ColorUB.Destroy();

		ImGui_ImplGlfw_Shutdown();
	}

	void SetupImGuiAndResources(SVulkan::SDevice& Device)
	{
		IMGUI_CHECKVERSION();

		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO& IO = ImGui::GetIO();
		ImGui_ImplGlfw_InitForVulkan(Window, true);

		int32 Width = 0, Height = 0;
		unsigned char* Pixels = nullptr;
		IO.Fonts->GetTexDataAsAlpha8(&Pixels, &Width, &Height);

		FStagingBuffer* FontBuffer = GStagingBufferMgr.AcquireBuffer(Width * Height * sizeof(uint32), nullptr);
		uint8* Data = (uint8*)FontBuffer->Buffer->Lock();
		for (int32 Index = 0; Index < Width * Height; ++Index)
		{
			*Data++ = *Pixels;
			*Data++ = *Pixels;
			*Data++ = *Pixels;
			*Data++ = *Pixels;
			++Pixels;
		}
		FontBuffer->Buffer->Unlock();

		ImGuiFont.Create(Device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, EMemLocation::GPU, Width, Height, VK_FORMAT_R8G8B8A8_UNORM);
		Device.SetDebugName(ImGuiFont.Image.Image, "ImGuiFont");

		PendingOpsMgr.AddCopyBufferToImage(FontBuffer, ImGuiFont.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		IO.Fonts->TexID = (void*)ImGuiFont.Image.Image;

		const uint32 ImGuiVertexSize = sizeof(ImDrawVert) * ImGuiMaxVertices;
		for (int32 Index = 0; Index < NUM_IMGUI_BUFFERS; ++Index)
		{
			ImGuiVB[Index].Create(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, ImGuiVertexSize, true);
			static_assert(sizeof(uint16) == sizeof(ImDrawIdx), "");
			ImGuiIB[Index].Create(Device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, ImGuiMaxIndices * sizeof(uint16), true);
			ImGuiScaleTranslateUB[Index].Create(Device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, (16 + 4) * sizeof(float), true);
		}

		{
			VkSamplerCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
			VERIFY_VKRESULT(vkCreateSampler(Device.Device, &Info, nullptr, &ImGuiFontSampler));
		}

		{
			VkSamplerCreateInfo Info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, };
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
			Info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			Info.magFilter = VK_FILTER_LINEAR;
			Info.minFilter = VK_FILTER_LINEAR;
			Info.maxLod = 1.0f;
			VERIFY_VKRESULT(vkCreateSampler(Device.Device, &Info, nullptr, &LinearMipSampler));
		}
	}

	void Update(SVulkan::SDevice& Device)
	{
		++FrameIndex;
		GStagingBufferMgr.Refresh();
		Camera.UpdateMatrix();

		if (LoadingState == ELoadingState::Loading)
		{
			if (IsGLTFLoaderFinished(GLTFLoader))
			{
				TryLoadGLTF(Device);
			}
		}
	}

	void ImGuiNewFrame()
	{
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Setup time step
		double current_time = glfwGetTime();
		Time = current_time;

		ProcessInput();
	}

	void ProcessInput()
	{
		bLMouseButtonHeld = (glfwGetMouseButton(Window, GLFW_MOUSE_BUTTON_1) == GLFW_PRESS);
		bRMouseButtonHeld = (glfwGetMouseButton(Window, GLFW_MOUSE_BUTTON_2) == GLFW_PRESS);
		//bCtrlHeld = bCtrlHeld || (glfwGetKey(Window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(Window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);

		if (glfwGetKey(Window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(Window, true);
		}

		if (ImGui::IsAnyWindowHovered())
		{
			bLMouseButtonHeld = false;
			bRMouseButtonHeld = false;
			return;
		}

		float CameraSpeed = 0.5f * (float)Time;

		if (glfwGetKey(Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
		{
			CameraSpeed *= 2.5f;
		}


		if (glfwGetKey(Window, GLFW_KEY_W) == GLFW_PRESS)
		{
			Camera.Pos += CameraSpeed * Camera.Front;
		}

		if (glfwGetKey(Window, GLFW_KEY_S) == GLFW_PRESS)
		{
			Camera.Pos -= CameraSpeed * Camera.Front;
		}

		if (glfwGetKey(Window, GLFW_KEY_A) == GLFW_PRESS)
		{
			Camera.Pos += FVector3::Cross(Camera.Front, Camera.Up).GetNormalized() * CameraSpeed;
		}

		if (glfwGetKey(Window, GLFW_KEY_D) == GLFW_PRESS)
		{
			Camera.Pos -= FVector3::Cross(Camera.Front, Camera.Up).GetNormalized() * CameraSpeed;
		}
/*
		if (glfwGetKey(Window, GLFW_KEY_UP) == GLFW_PRESS)
		{
			Camera.Pos -= CameraSpeed * Camera.Front;
		}

		if (glfwGetKey(Window, GLFW_KEY_DOWN) == GLFW_PRESS)
		{
			Camera.Pos += CameraSpeed * Camera.Front;
		}
*/
		const float RotSpeed = 1.0f;
		if (glfwGetKey(Window, GLFW_KEY_LEFT) == GLFW_PRESS)
		{
			Camera.Yaw -= RotSpeed;
		}

		if (glfwGetKey(Window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		{
			Camera.Yaw += RotSpeed;
		}

		if (glfwGetKey(Window, GLFW_KEY_PAGE_UP) == GLFW_PRESS)
		{
			Camera.Pos += Camera.Up * CameraSpeed;
		}

		if (glfwGetKey(Window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS)
		{
			Camera.Pos -= Camera.Up * CameraSpeed;
		}
	}

	void DrawDataImGui(ImDrawData* DrawData, SVulkan::FCmdBuffer* CmdBuffer, SVulkan::FFramebuffer* Framebuffer)
	{
		if (DrawData->CmdListsCount > 0)
		{
			uint32 NumVertices = DrawData->TotalVtxCount;
			uint32 NumIndices = DrawData->TotalIdxCount;
			uint32 DestVBOffset = 0;
			uint32 DestIBOffset = 0;
			ImDrawVert* DestVBData = (ImDrawVert*)ImGuiVB[FrameIndex % NUM_IMGUI_BUFFERS].Lock();
			uint16* DestIBData = (uint16*)ImGuiIB[FrameIndex % NUM_IMGUI_BUFFERS].Lock();
			for (int32 Index = 0; Index < DrawData->CmdListsCount; ++Index)
			{
				const ImDrawList* CmdList = DrawData->CmdLists[Index];
				const ImDrawVert* SrcVB = CmdList->VtxBuffer.Data;
				const ImDrawIdx* SrcIB = CmdList->IdxBuffer.Data;

				check(NumVertices + CmdList->VtxBuffer.Size <= ImGuiMaxVertices);
				check(NumIndices + CmdList->IdxBuffer.Size <= ImGuiMaxIndices);

				memcpy(DestIBData, SrcIB, CmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
				memcpy(DestVBData, SrcVB, CmdList->VtxBuffer.Size * sizeof(ImDrawVert));
				
				DestIBData += CmdList->IdxBuffer.Size;
				DestVBData += CmdList->VtxBuffer.Size;
			}

			ImGuiIB[FrameIndex % NUM_IMGUI_BUFFERS].Unlock();
			ImGuiVB[FrameIndex % NUM_IMGUI_BUFFERS].Unlock();

			SVulkan::FGfxPSO* PSO = GPSOCache.GetGfxPSO(ImGUIPSO, FPSOCache::FPSOSecondHandle(ImGUIVertexDecl, EPSODoubleSided));
			{
				{
					float L = DrawData->DisplayPos.x;
					float R = DrawData->DisplayPos.x + DrawData->DisplaySize.x;
					float T = DrawData->DisplayPos.y;
					float B = DrawData->DisplayPos.y + DrawData->DisplaySize.y;

					FMatrix4x4* UBData = (FMatrix4x4*)ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS].Lock();
					UBData->Rows[0].Set(2.0f/(R-L),   0.0f,           0.0f,       0.0f);
					UBData->Rows[1].Set(0.0f,         2.0f/(T-B),     0.0f,       0.0f);
					UBData->Rows[2].Set(0.0f,         0.0f,           0.5f,       0.0f);
					UBData->Rows[3].Set((R+L)/(L-R),  (T+B)/(B-T),    0.5f,       1.0f);
					ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS].Unlock();
				}

				FDescriptorPSOCache Cache(PSO);
				Cache.SetUniformBuffer("CB", ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS]);
				Cache.SetSampler("Sampler", ImGuiFontSampler);
				Cache.SetImage("Font", ImGuiFont, ImGuiFontSampler);
				Cache.UpdateDescriptors(GDescriptorCache, CmdBuffer);
			}

			CmdBuffer->BeginRenderPass(Framebuffer);
			vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PSO->Pipeline);

			{
				VkViewport Viewport;
				Viewport.x = 0;
				Viewport.y = DrawData->DisplaySize.y;
				Viewport.width = DrawData->DisplaySize.x;
				Viewport.height = -DrawData->DisplaySize.y;
				Viewport.minDepth = 0.0f;
				Viewport.maxDepth = 1.0f;
				vkCmdSetViewport(CmdBuffer->CmdBuffer, 0, 1, &Viewport);
			}

			vkCmdBindIndexBuffer(CmdBuffer->CmdBuffer, ImGuiIB[FrameIndex % NUM_IMGUI_BUFFERS].Buffer.Buffer, 0, VK_INDEX_TYPE_UINT16);
			VkDeviceSize Zero = 0;
			vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &ImGuiVB[FrameIndex % NUM_IMGUI_BUFFERS].Buffer.Buffer, &Zero);

			int VertexOffset = 0;
			int IndexOffset = 0;
			ImVec2 DisplayPos = DrawData->DisplayPos;
			for (int n = 0; n < DrawData->CmdListsCount; n++)
			{
				const ImDrawList* CmdList = DrawData->CmdLists[n];
				for (int Index = 0; Index < CmdList->CmdBuffer.Size; Index++)
				{
					const ImDrawCmd* Cmd = &CmdList->CmdBuffer[Index];

					VkRect2D Scissor;
					ZeroMem(Scissor);
					Scissor.offset.x = (int32)(Cmd->ClipRect.x - DisplayPos.x) > 0 ? (int32_t)(Cmd->ClipRect.x - DisplayPos.x) : 0;
					Scissor.offset.y = (int32)(Cmd->ClipRect.y - DisplayPos.y) > 0 ? (int32_t)(Cmd->ClipRect.y - DisplayPos.y) : 0;
					Scissor.extent.width = (uint32)(Cmd->ClipRect.z - Cmd->ClipRect.x);
					Scissor.extent.height = (uint32)(Cmd->ClipRect.w - Cmd->ClipRect.y);
					vkCmdSetScissor(CmdBuffer->CmdBuffer, 0, 1, &Scissor);

					vkCmdDrawIndexed(CmdBuffer->CmdBuffer, Cmd->ElemCount, 1, IndexOffset, VertexOffset, 0);
					IndexOffset += Cmd->ElemCount;
				}
				VertexOffset += CmdList->VtxBuffer.Size;
			}
			CmdBuffer->EndRenderPass();
		}
	}



	FScene Scene;

	bool bUseColorStream = false;
	bool bHasNormals = false;
	bool bHasTexCoords = false;

	std::string LoadedGLTF;
	FPendingOpsManager PendingOpsMgr;

	FGLTFLoader* GLTFLoader = nullptr;

	static void AsyncLoadingThread(FApp* This, std::string Filename)
	{
		This->LoadingState = ELoadingState::BeginLoading;

		This->GLTFLoader = CreateGLTFLoader(Filename.c_str());
		if (This->GLTFLoader)
		{
			This->LoadingState = ELoadingState::Loading;
		}
		else
		{
			This->LoadingState = ELoadingState::Idle;
		}
	}

	std::thread* AsyncLoadingThreadHandle = nullptr;

	void BeginAsyncLoading(const char* Filename)
	{
		check(!AsyncLoadingThreadHandle);
		static bool b = true;
		if (b)
		{
			AsyncLoadingThreadHandle = new std::thread(AsyncLoadingThread, this, std::string(Filename));
		}
		else
		{
			AsyncLoadingThread(this, std::string(Filename));
		}
	}

	static void FixGLTFVertexDecl(SVulkan::FShader* Shader, int32& PrimVertexDeclHandle)
	{
		SpvReflectShaderModule Module;

		SpvReflectResult result = spvReflectCreateShaderModule(Shader->SpirV.size(), Shader->SpirV.data(), &Module);
		check(result == SPV_REFLECT_RESULT_SUCCESS);

		uint32 Count = 0;
		result = spvReflectEnumerateInputVariables(&Module, &Count, nullptr);
		check(result == SPV_REFLECT_RESULT_SUCCESS);

		std::vector<SpvReflectInterfaceVariable*> Variables(Count);
		result = spvReflectEnumerateInputVariables(&Module, &Count, Variables.data());
		check(result == SPV_REFLECT_RESULT_SUCCESS);

		FPSOCache::FVertexDecl NewDecl = GPSOCache.VertexDecls[PrimVertexDeclHandle];

		auto FindSemantic = [&](const char* Name) -> int32
		{
			for (auto* Var : Variables)
			{
				if (strstr(Var->name, Name))
				{
					return (int32)Var->location;
				}
			}

			return -1;
		};

		uint32 Index = 0;
		for (auto& Name : NewDecl.Names)
		{
			int32 Found = FindSemantic(Name.c_str());
			check(Found != -1);
			{
				NewDecl.AttrDescs[Index].location = (uint32)Found;
			}

			++Index;
		}

		spvReflectDestroyShaderModule(&Module);

		PrimVertexDeclHandle = GPSOCache.FindOrAddVertexDecl(NewDecl);
	}

	void TryLoadGLTF(SVulkan::SDevice& Device)
	{
		//double StartTime = glfwGetTime();
		CreateGLTFGfxResources(GLTFLoader, Device, GPSOCache, Scene, PendingOpsMgr, &GStagingBufferMgr);
		LoadingState = ELoadingState::FinishedLoading;
		LoadedGLTF = GetGLTFFilename(GLTFLoader);
		FreeGLTFLoader(GLTFLoader);
		GLTFLoader = nullptr;

		{
			std::stringstream ss;
			ss << "VkTest2 - " << LoadedGLTF;
			ss.flush();
			::glfwSetWindowTitle(Window, ss.str().c_str());
		}

		//if (LoadGLTF(Device, Filename, GPSOCache, Scene, PendingOpsMgr, &GStagingBufferMgr))
		//{
		//	double EndTime = glfwGetTime();
		//	{
		//		std::stringstream ss;
		//		ss << "Loaded " << Filename << " in " << (float)(EndTime - StartTime) << "s\n";
		//		ss.flush();
		//		::OutputDebugStringA(ss.str().c_str());
		//	}
		//}

		FShaderInfo* TestGLTFVS = GShaderLibrary.GetShader("Shaders/TestMesh.hlsl", "TestGLTFVS", FShaderInfo::EStage::Vertex);
		check(TestGLTFVS);

		for (auto& Mesh : Scene.Meshes)
		{
			for (auto& Prim : Mesh.Prims)
			{
				FixGLTFVertexDecl(TestGLTFVS->Shader, Prim.VertexDecl);
			}
		}
	}

	struct : FCamera
	{
		FMatrix4x4 ViewMtx;
		FVector3 FOVNearFar = {35.0f, 1.0f, 3000.0f};

		void UpdateMatrix()
		{
			ViewMtx = Update();
		}
	} Camera;

	FVector4 LightDir = {0, 1, 0, 0};
	FVector4 PointLight = {0, 0, 0, 0};
	//bool bRotateObject = false;
	bool bSkipCull = true;
	bool bForceCull = false;
	bool bShowBounds = false;

	struct FViewUB
	{
		FMatrix4x4 ViewMtx;
		FVector4 CameraPosition;
		FMatrix4x4 ProjMtx;
		FVector4 LightDir;
		FVector4 PointLight;
		FIntVector4 Mode;
		FIntVector4 Mode2;
	};

	struct FObjUB
	{
		FMatrix4x4 ObjMtx;
	};

	bool IsVisible(FScene::FPrim& Prim, FMatrix4x4 ObjToWorldMtx)
	{
		if (bSkipCull)
		{
			return true;
		}

		FVector4 BoundsMin = ObjToWorldMtx.Transform(FVector4(Prim.ObjectSpaceBounds.Min, 1.0f));
		FVector4 BoundsMax = ObjToWorldMtx.Transform(FVector4(Prim.ObjectSpaceBounds.Max, 1.0f));
		FVector4 Min = Camera.ViewMtx.Transform(BoundsMin);
		FVector4 Max = Camera.ViewMtx.Transform(BoundsMax);
		FVector4 Center = (Min + Max) * 0.5f;
		FVector4 R = (Max - Min) * 0.5f;
		float SquareRadius = FVector4::Dot(R, R);
		if (Center.z + sqrtf(SquareRadius) < 0)
		{
			return false;
		}
		return true;
	}

	FStagingBuffer* GetObjUB(SVulkan::FCmdBuffer* CmdBuffer, FMatrix4x4 ObjectMatrix = FMatrix4x4::GetIdentity())
	{
		FObjUB ObjUB;
		ObjUB.ObjMtx = ObjectMatrix;
		FStagingBuffer* ObjBuffer = GStagingBufferMgr.AcquireBuffer(sizeof(ObjUB), CmdBuffer);
		*(FObjUB*)ObjBuffer->Buffer->Lock() = ObjUB;
		ObjBuffer->Buffer->Unlock();
		return ObjBuffer;
	}

	FViewUB GetViewUBStruct()
	{
		int W = 0, H = 1;
		glfwGetWindowSize(Window, &W, &H);
		float FOVRadians = tan(ToRadians(Camera.FOVNearFar.x));

		FViewUB ViewUB;
		ViewUB.Mode = g_vMode;
		ViewUB.Mode2 = g_vMode2;
		ViewUB.LightDir = LightDir.GetNormalized();
		ViewUB.PointLight = PointLight;
		ViewUB.ViewMtx = Camera.ViewMtx;
		ViewUB.CameraPosition = FVector4(Camera.Pos, 1.0f);
		ViewUB.ProjMtx = CalculateProjectionMatrixLH(FOVRadians, (float)W / (float)H, Camera.FOVNearFar.y, Camera.FOVNearFar.z);
		return ViewUB;
	}

	FStagingBuffer* GetViewUB(SVulkan::FCmdBuffer* CmdBuffer)
	{
		FViewUB ViewUB = GetViewUBStruct();
		FStagingBuffer* ViewBuffer = GStagingBufferMgr.AcquireBuffer(sizeof(ViewUB), CmdBuffer);
		*(FViewUB*)ViewBuffer->Buffer->Lock() = ViewUB;
		ViewBuffer->Buffer->Unlock();
		return ViewBuffer;
	}

	void DrawScene(SVulkan::SDevice& Device, SVulkan::FCmdBuffer* CmdBuffer)
	{
		FMarkerScope MarkerScope(&Device, CmdBuffer, "Scene");

/*
		static float DeltaRot = 0;
		float RotateObjectAngle = 0;
		if (bRotateObject)
		{
			RotateObjectAngle = DeltaRot;
			DeltaRot += 2.0f / 60.0f;
		}
		else
		{
			DeltaRot = 0;
		}
*/
		FStagingBuffer* ViewBuffer = GetViewUB(CmdBuffer);

		for (auto& Instance : Scene.Instances)
		{
			std::stringstream ss;
			ss << "InstanceID " << Instance.ID;
			ss.flush();
			FMarkerScope MarkerScope(Device, CmdBuffer, ss.str().c_str());

			auto& Mesh = Scene.Meshes[Instance.Mesh];
			for (auto& Prim : Mesh.Prims)
			{
				std::stringstream ss2;
				ss2 << "PrimID " << Prim.ID;
				ss2.flush();
				FMarkerScope MarkerScope(Device, CmdBuffer, ss2.str().c_str());

				FMatrix4x4 ObjectMatrix = FMatrix4x4::GetIdentity();//FMatrix4x4::GetRotationZ(ToRadians(180));
																	//ObjectMatrix *= FMatrix4x4::GetScale(Instance.Scale);
				ObjectMatrix.Rows[3] = Instance.Pos;
				//float RotateObjectAngle = 0;
				//ObjectMatrix *= FMatrix4x4::GetRotationY(RotateObjectAngle);
				FStagingBuffer* ObjBuffer = GetObjUB(CmdBuffer, ObjectMatrix);

				if (Prim.ID == 96)
				{
					int i = 0;
					++i;
				}

				if (IsVisible(Prim, ObjectMatrix))
				{
					SVulkan::FGfxPSO* PSO = GPSOCache.GetGfxPSO(TestGLTFPSO, 
						FPSOCache::FPSOSecondHandle(Prim.VertexDecl, 
							(Scene.Materials[Prim.Material].bDoubleSided ? EPSODoubleSided : 0) |
							(g_bWireframe ? EPSOWireFrame : 0))
						);
					vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PSO->Pipeline);
					GVulkan.Swapchain.SetViewportAndScissor(CmdBuffer);
					std::vector<VkBuffer> VBs;
	#if SCENE_USE_SINGLE_BUFFERS
					vkCmdBindIndexBuffer(CmdBuffer->CmdBuffer, Prim.IndexBuffer.Buffer.Buffer, 0, Prim.IndexType);
					std::vector<VkDeviceSize> VertexOffsets;
					for (auto VB : Prim.VertexBuffers)
					{
						VBs.push_back(VB.Buffer.Buffer);
						VertexOffsets.push_back(0);
					}
					vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, (uint32)VBs.size(), VBs.data(), VertexOffsets.data());
	#else
					SVulkan::FBuffer& IB = Scene.Buffers[Prim.IndexBuffer].Buffer;
					vkCmdBindIndexBuffer(CmdBuffer->CmdBuffer, IB.Buffer, Prim.IndexOffset, Prim.IndexType);
					for (int VBIndex : Prim.VertexBuffers)
					{
						VBs.push_back(Scene.Buffers[VBIndex].Buffer.Buffer);
					}
					check(VBs.size() == Prim.VertexOffsets.size());
					vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, (uint32)VBs.size(), VBs.data(), Prim.VertexOffsets.data());
	#endif
					{
						FDescriptorPSOCache Cache(PSO);
						Cache.SetUniformBuffer("ViewUB", *ViewBuffer->Buffer);
						Cache.SetUniformBuffer("ObjUB", *ObjBuffer->Buffer);
						Cache.SetSampler("SS", LinearMipSampler);
						Cache.SetImage("BaseTexture", Scene.Textures.empty() || Scene.Materials[Prim.Material].BaseColor == -1 ? WhiteTexture : Scene.Textures[Scene.Materials[Prim.Material].BaseColor].Image, LinearMipSampler);
						Cache.SetImage("NormalTexture", Scene.Textures.empty() || Scene.Materials[Prim.Material].Normal == -1 ? DefaultNormalMapTexture : Scene.Textures[Scene.Materials[Prim.Material].Normal].Image, LinearMipSampler);
						Cache.SetImage("MetallicRoughnessTexture", Scene.Textures.empty() || Scene.Materials[Prim.Material].MetallicRoughness == -1 ? WhiteTexture : Scene.Textures[Scene.Materials[Prim.Material].MetallicRoughness].Image, LinearMipSampler);
						Cache.UpdateDescriptors(GDescriptorCache, CmdBuffer);
					}

					if (!bForceCull)
					{
						vkCmdDrawIndexed(CmdBuffer->CmdBuffer, Prim.NumIndices, 1, 0, 0, 0);
					}
				}

				if (bShowBounds)
				{
					RenderBoundingBox(CmdBuffer, Prim, ViewBuffer, ObjBuffer);
				}
			}
		}

		FObjUB ObjUB;
		ObjUB.ObjMtx = FMatrix4x4::GetIdentity();
		FStagingBuffer* ObjBuffer = GStagingBufferMgr.AcquireBuffer(sizeof(ObjUB), CmdBuffer);
		*(FObjUB*)ObjBuffer->Buffer->Lock() = ObjUB;
		ObjBuffer->Buffer->Unlock();
		RenderPointLight(CmdBuffer, ViewBuffer, ObjBuffer);
	}

	struct FPosOnlyDecl : public FPSOCache::FVertexDecl
	{
		FPosOnlyDecl()
		{
			AddAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0, "POSITION");
			AddBinding(0, sizeof(FVector3));
		}
	};

	FPosOnlyDecl GetPosOnlyDecl()
	{
		static FPosOnlyDecl NewDecl;
		return NewDecl;
	}

	void RenderSphere(SVulkan::FCmdBuffer* CmdBuffer, FStagingBuffer* ViewBuffer, FStagingBuffer* ObjBuffer, FVector3 Pos, float Radius, uint32 Color)
	{
		int NumIndices = 20 * 3;
		FStagingBuffer* VB = GStagingBufferMgr.AcquireBuffer(12 * sizeof(FUnlitVertex), CmdBuffer);
		FStagingBuffer* IB = GStagingBufferMgr.AcquireBuffer(NumIndices * sizeof(uint32), CmdBuffer);
		GenerateIcosahedron(VB, IB, Pos, Radius, Color);
		SVulkan::FGfxPSO* PSO = GPSOCache.GetGfxPSO(UnlitPSO, FPSOCache::FPSOSecondHandle(UnlitVertexDecl, EPSODoubleSided | EPSOWireFrame));

		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PSO->Pipeline);
		VkDeviceSize VBs[1] = { 0 };
		vkCmdBindIndexBuffer(CmdBuffer->CmdBuffer, IB->Buffer->Buffer.Buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &VB->Buffer->Buffer.Buffer, VBs);
		{
			FDescriptorPSOCache Cache(PSO);
			Cache.SetUniformBuffer("ViewUB", *ViewBuffer->Buffer);
			Cache.SetUniformBuffer("ObjUB", *ObjBuffer->Buffer);
			Cache.SetSampler("SS", LinearMipSampler);
			Cache.UpdateDescriptors(GDescriptorCache, CmdBuffer);
		}

		vkCmdDrawIndexed(CmdBuffer->CmdBuffer, NumIndices, 1, 0, 0, 0);
	}

	void RenderPointLight(SVulkan::FCmdBuffer* CmdBuffer, FStagingBuffer* ViewBuffer, FStagingBuffer* ObjBuffer)
	{
		RenderSphere(CmdBuffer, ViewBuffer, ObjBuffer, PointLight.GetVector3(), 10, 0xff0000ff);
	}

	void RenderBoundingBox(SVulkan::FCmdBuffer* CmdBuffer, const FScene::FPrim& Prim, FStagingBuffer* ViewBuffer, FStagingBuffer* ObjBuffer)
	{
		int NumIndices = 12 * 2;
		FStagingBuffer* VB = GStagingBufferMgr.AcquireBuffer(8 * sizeof(FUnlitVertex), CmdBuffer);
		FStagingBuffer* IB = GStagingBufferMgr.AcquireBuffer(NumIndices * sizeof(uint32), CmdBuffer);
		GenerateBoundingBox(VB, IB, Prim.ObjectSpaceBounds);
		SVulkan::FGfxPSO* PSO = GPSOCache.GetGfxPSO(UnlitPSO, FPSOCache::FPSOSecondHandle(UnlitVertexDecl, EPSODoubleSided | EPSOLineList));

		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PSO->Pipeline);
		VkDeviceSize VBs[1] = { 0 };
		vkCmdBindIndexBuffer(CmdBuffer->CmdBuffer, IB->Buffer->Buffer.Buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &VB->Buffer->Buffer.Buffer, VBs);
		{
			FDescriptorPSOCache Cache(PSO);
			Cache.SetUniformBuffer("ViewUB", *ViewBuffer->Buffer);
			Cache.SetUniformBuffer("ObjUB", *ObjBuffer->Buffer);
			Cache.SetSampler("SS", LinearMipSampler);
			Cache.SetImage("BaseTexture", Scene.Textures.empty() || Scene.Materials[Prim.Material].BaseColor == -1 ? WhiteTexture : Scene.Textures[Scene.Materials[Prim.Material].BaseColor].Image, LinearMipSampler);
			Cache.SetImage("NormalTexture", Scene.Textures.empty() || Scene.Materials[Prim.Material].Normal == -1 ? DefaultNormalMapTexture : Scene.Textures[Scene.Materials[Prim.Material].Normal].Image, LinearMipSampler);
			Cache.SetImage("MetallicRoughnessTexture", Scene.Textures.empty() || Scene.Materials[Prim.Material].MetallicRoughness == -1 ? WhiteTexture : Scene.Textures[Scene.Materials[Prim.Material].MetallicRoughness].Image, LinearMipSampler);
			Cache.UpdateDescriptors(GDescriptorCache, CmdBuffer);
		}

		vkCmdDrawIndexed(CmdBuffer->CmdBuffer, NumIndices, 1, 0, 0, 0);
	}

	void RecreateSwapchain(SVulkan::SDevice& Device, SVulkan::FSwapchain& Swapchain)
	{
		int W = 0, H = 0;
		glfwGetFramebufferSize(Window, &W, &H);
		while (W == 0 || H == 0)
		{
			glfwGetFramebufferSize(Window, &W, &H);
			glfwWaitEvents();
		}
		Device.WaitForIdle();

		Swapchain.Recreate(Device, Window);
	}

	void RenderTests(SVulkan::SDevice& Device, SVulkan::FCmdBuffer* CmdBuffer)
	{
		GVulkan.Swapchain.SetViewportAndScissor(CmdBuffer);
		FStagingBuffer* ViewBuffer = GetViewUB(CmdBuffer);
		FStagingBuffer* ObjBuffer = GetObjUB(CmdBuffer);
		float Radius = 10;
		float Dist = 100;
		FVector3 Center(0, 0, 100);

		auto InnerRenderSphere = [&](SVulkan::FCmdBuffer* CmdBuffer, FStagingBuffer* ViewBuffer, FStagingBuffer* ObjBuffer, FVector3 Pos, float Radius, uint32 Color)
		{
			std::stringstream ss;
			ss << "Sphere " << Pos.x << "," << Pos.y << "," << Pos.z;
			ss.flush();
			FMarkerScope MarkerScope(&Device, CmdBuffer, ss.str().c_str());
			RenderSphere(CmdBuffer, ViewBuffer, ObjBuffer, Pos, Radius, Color);
		};

		InnerRenderSphere(CmdBuffer, ViewBuffer, ObjBuffer, Center + FVector3(0, 0, 0), Radius, 0xffffffff);
		InnerRenderSphere(CmdBuffer, ViewBuffer, ObjBuffer, Center + FVector3(-Dist, 0, -Dist), Radius, 0xff0000ff);
		InnerRenderSphere(CmdBuffer, ViewBuffer, ObjBuffer, Center + FVector3(Dist, 0, -Dist), Radius, 0xff00ff00);
		InnerRenderSphere(CmdBuffer, ViewBuffer, ObjBuffer, Center + FVector3(-Dist, 0, Dist), Radius, 0xffff0000);
		InnerRenderSphere(CmdBuffer, ViewBuffer, ObjBuffer, Center + FVector3(Dist, 0, Dist), Radius, 0xff000000);
	}
};
static FApp GApp;

static void ResizeCallback(GLFWwindow*, int w, int h)
{
	GApp.bResizeSwapchain = true;
}

static void ClearColorImage(VkCommandBuffer CmdBuffer, VkImage Image, float Color[4])
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

static void ClearDepthImage(VkCommandBuffer CmdBuffer, VkImage Image, float Depth, uint32 Stencil)
{
	VkClearDepthStencilValue ClearDS;
	ClearDS.depth = Depth;
	ClearDS.stencil = Stencil;

	VkImageSubresourceRange Range;
	ZeroMem(Range);
	Range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	Range.layerCount = 1;
	Range.levelCount = 1;
	vkCmdClearDepthStencilImage(CmdBuffer, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearDS, 1, &Range);
}

static bool GenerateImGuiUI(SVulkan::SDevice& Device, FApp& App, SVulkan::FCmdBuffer* CmdBuffer, SVulkan::FFramebuffer* Framebuffer)
{
	bool bRecompileShaders = false;
	FMarkerScope MarkerScope(Device, CmdBuffer, "ImGUI");
	if (ImGui::Begin("Debug"))
	{
		if (!App.LoadedGLTF.empty())
		{
			ImGui::MenuItem(RCUtils::GetBaseName(App.LoadedGLTF, true).c_str(), nullptr, false, false);
		}
		char s[64];
		sprintf(s, "FPS %3.2f", (float)(1000.0 / App.CpuDelta));
		float Value = (float)(1000.0 / App.CpuDelta);
		ImGui::SliderFloat("FPS", &Value, 0, 60);
		Value = (float)App.CpuDelta;
		ImGui::SliderFloat("CPU", &Value, 0, 66);
		Value = (float)App.GpuDelta;
		ImGui::SliderFloat("GPU", &Value, 0, 66);
		ImGui::InputFloat3("Pos", App.Camera.Pos.Values);
		ImGui::InputFloat2("Yaw/Pitch", App.Camera.Rot.Values);
		ImGui::Checkbox("Skip Culling", &App.bSkipCull);
		ImGui::Checkbox("Force Cull", &App.bForceCull);
		ImGui::Checkbox("Show Bounds (even if culled)", &App.bShowBounds);
		//ImGui::Checkbox("Rotate Object", &App.bRotateObject);
		ImGui::InputFloat3("FOV,Near,Far", App.Camera.FOVNearFar.Values);
		ImGui::InputFloat3("Light Dir", App.LightDir.Values);
		ImGui::InputFloat4("Point Light", App.PointLight.Values);

#define TEXT_ENTRY(Index, String, Enum)		String,
		const char* List[] = {
			VIEW_ENTRY_LIST(TEXT_ENTRY)
		};
#undef TEXT_ENTRY
		ImGui::ListBox("Show mode", &g_vMode.x, List, IM_ARRAYSIZE(List));
		ImGui::Checkbox("Identity World xfrm", (bool*)&g_vMode.w);
		ImGui::Checkbox("Identity Normal xfrm", (bool*)&g_vMode.y);
		ImGui::Checkbox("Lighting only", (bool*)&g_vMode.z);
		ImGui::Checkbox("Transpose tangent basis", (bool*)&g_vMode2.x);
		ImGui::Checkbox("Normalize", (bool*)&g_vMode2.y);
		ImGui::Checkbox("Wireframe", (bool*)&g_bWireframe);
		ImGui::Checkbox("Vertex Lighting Calc", (bool*)&g_vMode2.z);
		const char* Lights[] = {"Point Light", "Directional Light"};
		ImGui::Combo("Light", (int*)&g_vMode2.w, Lights, IM_ARRAYSIZE(Lights));

		if (ImGui::Button("Recompile shaders"))
		{
			bRecompileShaders = true;
		}
	}
	ImGui::End();

	//ImGui::ShowDemoWindow();

	ImGui::Render();

	ImDrawData* DrawData = ImGui::GetDrawData();
	App.DrawDataImGui(DrawData, CmdBuffer, Framebuffer);
	return bRecompileShaders;
}


static double Render(FApp& App)
{
	SVulkan::SDevice& Device = GVulkan.Devices[GVulkan.PhysicalDevice];
	if (!GVulkan.Swapchain.AcquireBackbuffer() || App.bResizeSwapchain)
	{
		App.RecreateSwapchain(Device, GVulkan.Swapchain);
		GRenderTargetCache.Destroy();
		App.RecreateDepthBuffer(Device);

		App.bResizeSwapchain = false;
	}

	Device.RefreshCommandBuffers();
	App.Update(Device);

	SVulkan::FCmdBuffer* CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);
	if (!App.PendingOpsMgr.Ops.empty())
	{
		FMarkerScope MarkerScope(Device, CmdBuffer, "Pending");
		App.PendingOpsMgr.ExecutePendingStagingOps(Device, CmdBuffer);
	}

	App.GPUTiming.BeginTimestamp(CmdBuffer);

	float Width = GVulkan.Swapchain.GetViewport().width;
	float Height = GVulkan.Swapchain.GetViewport().height;

	ImGuiIO& IO = ImGui::GetIO();
	IO.DisplaySize.x = Width;
	IO.DisplaySize.y = Height;
	IO.DeltaTime = App.LastDelta;

	FRenderTargetInfo ColorInfo = GVulkan.Swapchain.GetRenderTargetInfo();
	SVulkan::FFramebuffer* Framebuffer = GRenderTargetCache.GetOrCreateFrameBuffer(ColorInfo, FRenderTargetInfo(App.DepthBuffer.View, App.DepthBuffer.Image.Format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE), (uint32)Width, (uint32)Height);

	App.ImGuiNewFrame();

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0, 
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT);
	Device.TransitionImage(CmdBuffer, App.DepthBuffer.Image.Image,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

	static float F = 0.7f;

	F += 0.005f;

	float ClearColor[4] = {0.0f, abs(sin(F)), abs(cos(F)), 0.0f};
	ClearColorImage(CmdBuffer->CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex], ClearColor);
	ClearDepthImage(CmdBuffer->CmdBuffer, App.DepthBuffer.Image.Image, 1.0f, 0);

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT);
	Device.TransitionImage(CmdBuffer, App.DepthBuffer.Image.Image,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

	CmdBuffer->BeginRenderPass(Framebuffer);
	if (!App.Scene.Meshes.empty())
	{
		App.DrawScene(Device, CmdBuffer);
	}

	App.RenderTests(Device, CmdBuffer);

	CmdBuffer->EndRenderPass();

	if (0)
	{
		FMarkerScope MarkerScope(Device, CmdBuffer, "TestCompute");
		SVulkan::FComputePSO* PSO = GPSOCache.GetComputePSO(App.TestCSPSO);
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, PSO->Pipeline);

		FDescriptorPSOCache Cache(PSO);
		Cache.SetUniformBuffer("CB0", App.TestCSUB);
		Cache.SetTexelBuffer("output", App.TestCSBuffer);
		Cache.UpdateDescriptors(GDescriptorCache, CmdBuffer);
		for (int32 Index = 0; Index < 256; ++Index)
		{
			vkCmdDispatch(CmdBuffer->CmdBuffer, 256, 1, 1);
		}
	}

	App.GPUTiming.EndTimestamp(CmdBuffer);

	bool bRecompileShaders = GenerateImGuiUI(Device, App, CmdBuffer, Framebuffer);

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0,
		VK_IMAGE_ASPECT_COLOR_BIT);
	CmdBuffer->End();

	Device.Submit(Device.PresentQueue, CmdBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, GVulkan.Swapchain.AcquireBackbufferSemaphore, GVulkan.Swapchain.FinalSemaphore);

	if (!GVulkan.Swapchain.Present(Device.PresentQueue, GVulkan.Swapchain.FinalSemaphore))
	{
		App.bResizeSwapchain = true;
	}

	if (bRecompileShaders)
	{
		Device.WaitForIdle();
		if (GShaderLibrary.RecompileShaders())
		{
			GPSOCache.RecompileShaders();
		}
	}

	//Device.WaitForFence(CmdBuffer.Fence, CmdBuffer.LastSubmittedFence);
	//vkDeviceWaitIdle(Device.Device);

	double GpuTimeMS = App.GPUTiming.ReadTimestamp();
	return GpuTimeMS;
}

static void SetupShaders(FApp& App)
{
	FShaderInfo* TestCS = GShaderLibrary.RegisterShader("Shaders/TestCS.hlsl", "TestCS", FShaderInfo::EStage::Compute);
	FShaderInfo* UnlitVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "UnlitVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* RedPS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "RedPS", FShaderInfo::EStage::Pixel);
	FShaderInfo* ColorPS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "ColorPS", FShaderInfo::EStage::Pixel);
	FShaderInfo* UIVS = GShaderLibrary.RegisterShader("Shaders/UI.hlsl", "UIMainVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* UIPS = GShaderLibrary.RegisterShader("Shaders/UI.hlsl", "UIMainPS", FShaderInfo::EStage::Pixel);
	FShaderInfo* TestGLTFVS = GShaderLibrary.RegisterShader("Shaders/TestMesh.hlsl", "TestGLTFVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* TestGLTFPS = GShaderLibrary.RegisterShader("Shaders/TestMesh.hlsl", "TestGLTFPS", FShaderInfo::EStage::Pixel);
	GShaderLibrary.RecompileShaders();

	App.TestCSPSO = GPSOCache.CreateComputePSO("TestCSPSO", TestCS);

	SVulkan::FRenderPass* RenderPass = GRenderTargetCache.GetOrCreateRenderPass(FAttachmentInfo(GVulkan.Swapchain.Format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE), FAttachmentInfo(VK_FORMAT_D32_SFLOAT_S8_UINT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE));

	VkViewport Viewport = GVulkan.Swapchain.GetViewport();
	VkRect2D Scissor = GVulkan.Swapchain.GetScissor();

	{
		FPSOCache::FVertexDecl Decl;
		Decl.AddAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, IM_OFFSETOF(ImDrawVert, pos), "pos");
		Decl.AddAttribute(0, 1, VK_FORMAT_R32G32_SFLOAT, IM_OFFSETOF(ImDrawVert, uv), "uv");
		Decl.AddAttribute(0, 2, VK_FORMAT_R8G8B8A8_UNORM, IM_OFFSETOF(ImDrawVert, col), "col");
		Decl.AddBinding(0, sizeof(ImDrawVert));
		App.ImGUIVertexDecl = GPSOCache.FindOrAddVertexDecl(Decl);
		App.ImGUIPSO = GPSOCache.CreateGfxPSO("ImGUIPSO", UIVS, UIPS, RenderPass);
	}

	{
		FPSOCache::FVertexDecl Decl;
		Decl.AddAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, IM_OFFSETOF(FUnlitVertex, Position), "Position");
		Decl.AddAttribute(0, 1, VK_FORMAT_R8G8B8A8_UNORM, IM_OFFSETOF(FUnlitVertex, Color), "Color");
		Decl.AddBinding(0, sizeof(FUnlitVertex));
		App.UnlitVertexDecl = GPSOCache.FindOrAddVertexDecl(Decl);
		App.UnlitPSO = GPSOCache.CreateGfxPSO("UnlitPSO", UnlitVS, ColorPS, RenderPass);
	}

	{
		App.TestGLTFPSO = GPSOCache.CreateGfxPSO("TestGLTFPSO", TestGLTFVS, TestGLTFPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
			{
				VkPipelineDepthStencilStateCreateInfo* DSInfo = (VkPipelineDepthStencilStateCreateInfo*)GfxPipelineInfo.pDepthStencilState;
				DSInfo->depthTestEnable = VK_TRUE;
				DSInfo->depthWriteEnable = VK_TRUE;
				DSInfo->depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			});
	}
}

static void ErrorCallback(int Error, const char* Msg)
{
	fprintf(stderr, "Glfw Error %d: %s\n", Error, Msg);
}

static void ScrollCallback(GLFWwindow* Window, double XOffset, double YOffset)
{
	if (ImGui::IsAnyWindowHovered())
	{
		return;
	}

	GApp.Camera.FOVNearFar.x -= (float)YOffset;
	GApp.Camera.FOVNearFar.x = Clamp(1.0f, GApp.Camera.FOVNearFar.x, 45.0f);
}

static void MouseCallback(GLFWwindow* Window, double XPos, double YPos)
{
	if (!ImGui::IsAnyWindowHovered() && GApp.bLMouseButtonHeld || GApp.bRMouseButtonHeld)
	{
		if (GApp.Camera.bFirstTime) // initially set to true
		{
			GApp.Camera.LastX = (float)XPos;
			GApp.Camera.LastY = (float)YPos;
			GApp.Camera.bFirstTime = false;
		}

		float XOffset = (float)XPos - GApp.Camera.LastX;
		float YOffset = -((float)YPos - GApp.Camera.LastY);
		GApp.Camera.LastX = (float)XPos;
		GApp.Camera.LastY = (float)YPos;

		if (GApp.bRMouseButtonHeld)
		{
			const float Sensitivity = 0.25f;
			XOffset *= Sensitivity;
			YOffset *= Sensitivity;
			GApp.Camera.Yaw -= XOffset;
			GApp.Camera.Pitch += YOffset;
		}

		if (GApp.bLMouseButtonHeld)
		{
			const float Sensitivity = 1.25f;
			XOffset *= Sensitivity;
			YOffset *= Sensitivity;
			GApp.Camera.Pos += YOffset * GApp.Camera.Front;
			GApp.Camera.Pos += FVector3::Cross(GApp.Camera.Up, GApp.Camera.Front).GetNormalized() * XOffset;
		}
	}
	else
	{
		GApp.Camera.bFirstTime = true;
	}
}

static GLFWwindow* Init(FApp& App)
{
	double Begin = GetTimeInMs();
	glfwSetErrorCallback(ErrorCallback);
	int RC = glfwInit();
	check(RC != 0);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);


	uint32 ResX = RCUtils::FCmdLine::Get().TryGetIntPrefix("-resx=", 1920);
	uint32 ResY = RCUtils::FCmdLine::Get().TryGetIntPrefix("-resy=", 1080);

	{
		App.Camera.Pos = TryGetVector3Prefix("-pos=", App.Camera.Pos);
		App.Camera.Rot = TryGetVector2Prefix("-rot=", App.Camera.Rot);
		App.Camera.FOVNearFar.x = RCUtils::FCmdLine::Get().TryGetFloatPrefix("-fov=", App.Camera.FOVNearFar.x);
		App.Camera.FOVNearFar.y = RCUtils::FCmdLine::Get().TryGetFloatPrefix("-near=", App.Camera.FOVNearFar.y);
		App.Camera.FOVNearFar.z = RCUtils::FCmdLine::Get().TryGetFloatPrefix("-far=", App.Camera.FOVNearFar.z);
	}

	if (RCUtils::FCmdLine::Get().Contains("-nocull"))
	{
		GApp.bSkipCull = true;
	}

	App.LightDir = FVector4(TryGetVector3Prefix("-lightdir=", App.LightDir.GetVector3()), 0);
	App.LightDir = App.LightDir.GetNormalized();

	App.PointLight = TryGetVector4Prefix("-pointlight=", App.PointLight);

	GLFWwindow* Window = glfwCreateWindow(ResX, ResY, "VkTest2", 0, 0);
	App.Camera.Init((float)ResX, (float)ResY);

	glfwHideWindow(Window);
	check(Window);

	if (RCUtils::FCmdLine::Get().Contains("-waitfordebugger"))
	{
		while (!::IsDebuggerPresent())
		{
			::Sleep(100);
		}
//		__debugbreak();
	}

	GVulkan.Init(Window);
	SVulkan::SDevice& Device = GVulkan.Devices[GVulkan.PhysicalDevice];

	GRenderTargetCache.Init(Device.Device);
	GShaderLibrary.Init(Device.Device);
	GPSOCache.Init(&Device);
	GDescriptorCache.Init(&Device);
	GStagingBufferMgr.Init(&Device);

	const char* Filename = nullptr;
	if (RCUtils::FCmdLine::Get().TryGetStringFromPrefix("-gltf=", Filename))
	{
		App.BeginAsyncLoading(Filename);
		//App.TryLoadGLTF(Device, Filename);
	}

	SetupShaders(App);

	App.Create(Device, Window);
	App.SetupImGuiAndResources(Device);

	glfwShowWindow(Window);

	glfwSetCursorPosCallback(Window, MouseCallback);
	glfwSetScrollCallback(Window, ScrollCallback);

	double End = GetTimeInMs();
	double Delta = End - Begin;
	char s[128];
	sprintf(s, "*** INIT Time %f\n", (float)Delta);
	::OutputDebugStringA(s);

	return Window;
}

static void Deinit(FApp& App, GLFWwindow* Window)
{
	GVulkan.DeinitPre();

	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	GStagingBufferMgr.Destroy();

	App.Destroy();

	GDescriptorCache.Destroy();
	GPSOCache.Destroy();
	GShaderLibrary.Destroy();
	GRenderTargetCache.Destroy();

	GVulkan.Deinit();

	glfwDestroyWindow(Window);
	glfwTerminate();
}

int main()
{
	FApp& App = GApp;
	GLFWwindow* Window = Init(App);

	uint32 ExitAfterNFrames = RCUtils::FCmdLine::Get().TryGetIntPrefix("-exitafterframes=", (uint32)-1);

	if (Window)
	{
		if (App.LoadingState == ELoadingState::BeginLoading || App.LoadingState == ELoadingState::Loading)
		{
			::glfwSetWindowTitle(Window, "VkTest2: Loading...");
		}
		else
		{
			::glfwSetWindowTitle(Window, "VkTest2");
		}
	}

	uint32 Frame = 1;
	while (!glfwWindowShouldClose(Window))
	{
		double CpuBegin = glfwGetTime() * 1000.0;

		glfwPollEvents();

		//App.Update();

		App.GpuDelta = Render(App);

		double CpuEnd = glfwGetTime() * 1000.0;

		App.CpuDelta = CpuEnd - CpuBegin;

		App.LastDelta = (float)App.CpuDelta;
		++Frame;

		if (Frame > ExitAfterNFrames)
		{
			glfwSetWindowShouldClose(Window, 1);
		}
	}

	Deinit(App, Window);
}
