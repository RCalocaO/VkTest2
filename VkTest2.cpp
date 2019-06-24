// VkTest2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <GLFW/glfw3.h>

// Fix Windows warning
#undef APIENTRY

#include "RCVulkan.h"

#include "imgui.h"

#include "RCScene.h"


#include "../RCUtils/RCUtilsMath.h"

static SVulkan GVulkan;


static FShaderLibrary GShaderLibrary;

static FRenderTargetCache GRenderTargetCache;

static FPSOCache GPSOCache;

static FDescriptorCache GDescriptorCache;

static FStagingBufferManager GStagingBufferMgr;

static FVector3 g_vMove = {0, 0, 0};
static FVector3 g_vRot = {0, 0, 0};
static int32 g_nMode = 0;

extern bool LoadGLTF(SVulkan::SDevice& Device, const char* Filename, FPSOCache& PSOCache, FScene& Scene, FPendingOpsManager& PendingStagingOps, FStagingBufferManager* StagingMgr);

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

struct FApp
{
	uint32 FrameIndex = 0;
	FImageWithMemAndView DepthBuffer;
	FPSOCache::FPSOHandle PassThroughVSRedPSPSO;
	//FPSOCache::FPSOHandle DataClipVSColorTessPSO;
	FPSOCache::FPSOHandle NoVBClipVSRedPSO;
	FPSOCache::FPSOHandle DataClipVSRedPSO;
	FPSOCache::FPSOHandle DataClipVSColorPSO;
	FPSOCache::FPSOHandle VBClipVSRedPSO;
	FPSOCache::FPSOHandle TestGLTFPSO;
	FBufferWithMemAndView ClipVB;
	FPSOCache::FPSOHandle TestCSPSO;
	FBufferWithMemAndView TestCSBuffer;
	FBufferWithMem TestCSUB;
	FBufferWithMem ColorUB;
	GLFWwindow* Window;
	uint32 ImGuiMaxVertices = 16384 * 3;
	uint32 ImGuiMaxIndices = 16384 * 3;
	FImageWithMemAndView ImGuiFont;
	VkSampler ImGuiFontSampler = VK_NULL_HANDLE;
	VkSampler LinearMipSampler = VK_NULL_HANDLE;
	FImageWithMemAndView WhiteTexture;
	FGPUTiming GPUTiming;
	enum
	{
		NUM_IMGUI_BUFFERS = 3,
	};
	FBufferWithMem ImGuiVB[NUM_IMGUI_BUFFERS];
	FBufferWithMem ImGuiIB[NUM_IMGUI_BUFFERS];
	FBufferWithMem ImGuiScaleTranslateUB[NUM_IMGUI_BUFFERS];
	FPSOCache::FPSOHandle ImGUIPSO;
	int32 ImGUIVertexDecl = -1;
	double Time = 0;
	bool MouseJustPressed[5] = {false, false, false, false, false};
	GLFWcursor* MouseCursors[ImGuiMouseCursor_COUNT] = {0};

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

		GPUTiming.Init(&Device);

		int32 Width = 0, Height = 1;
		glfwGetFramebufferSize(Window, &Width, &Height);
		DepthBuffer.Create(*GPSOCache.Device, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, EMemLocation::GPU, (uint32)Width, (uint32)Height, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
	}

	void Destroy()
	{
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
	}

	void SetupImGuiAndResources(SVulkan::SDevice& Device)
	{
		ImGuiIO& IO = ImGui::GetIO();

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
			ImGuiScaleTranslateUB[Index].Create(Device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, 4 * sizeof(float), true);
		}

		{
			VkSamplerCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
			VERIFY_VKRESULT(vkCreateSampler(Device.Device, &Info, nullptr, &ImGuiFontSampler));
		}

		{
			VkSamplerCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
			Info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			Info.magFilter = VK_FILTER_LINEAR;
			Info.minFilter = VK_FILTER_LINEAR;
			VERIFY_VKRESULT(vkCreateSampler(Device.Device, &Info, nullptr, &LinearMipSampler));
		}
	}

	void Update()
	{
		++FrameIndex;
		GStagingBufferMgr.Refresh();
	}

	void ImGuiNewFrame()
	{
		ImGuiIO& io = ImGui::GetIO();
		IM_ASSERT(io.Fonts->IsBuilt());     // Font atlas needs to be built, call renderer _NewFrame() function e.g. ImGui_ImplOpenGL3_NewFrame() 

			// Setup display size
		int w, h;
		int display_w, display_h;
		glfwGetWindowSize(Window, &w, &h);
		glfwGetFramebufferSize(Window, &display_w, &display_h);
		io.DisplaySize = ImVec2((float)w, (float)h);
		io.DisplayFramebufferScale = ImVec2(w > 0 ? ((float)display_w / w) : 0, h > 0 ? ((float)display_h / h) : 0);

		// Setup time step
		double current_time = glfwGetTime();
		io.DeltaTime = Time > 0.0 ? (float)(current_time - Time) : (float)(1.0f/60.0f);
		Time = current_time;

		UpdateMousePosAndButtons();
		UpdateMouseCursor();

		ImGui::NewFrame();
	}

	void UpdateMousePosAndButtons()
	{
		ImGuiIO& io = ImGui::GetIO();
		for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++)
		{
			// If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
			io.MouseDown[i] = MouseJustPressed[i] || glfwGetMouseButton(Window, i) != 0;
			MouseJustPressed[i] = false;
		}

		// Update mouse position
		const ImVec2 mouse_pos_backup = io.MousePos;
		io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
		const bool focused = glfwGetWindowAttrib(Window, GLFW_FOCUSED) != 0;
		if (focused)
		{
			if (io.WantSetMousePos)
			{
				glfwSetCursorPos(Window, (double)mouse_pos_backup.x, (double)mouse_pos_backup.y);
			}
			else
			{
				double mouse_x, mouse_y;
				glfwGetCursorPos(Window, &mouse_x, &mouse_y);
				io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);
			}
		}
	}

	void UpdateMouseCursor()
	{
		ImGuiIO& io = ImGui::GetIO();
		if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) || glfwGetInputMode(Window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
		{
			return;
		}

		ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
		if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
		{
			// Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
			glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
		}
		else
		{
			// Show OS mouse cursor
			// FIXME-PLATFORM: Unfocused windows seems to fail changing the mouse cursor with GLFW 3.2, but 3.3 works here.
			glfwSetCursor(Window, MouseCursors[imgui_cursor] ? MouseCursors[imgui_cursor] : MouseCursors[ImGuiMouseCursor_Arrow]);
			glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}

	void DrawDataImGui(ImDrawData* DrawData, SVulkan::FSwapchain& Swapchain, SVulkan::FCmdBuffer* CmdBuffer, SVulkan::FFramebuffer* Framebuffer)
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
				
				//vkCmdUpdateBuffer(CmdBuffer->CmdBuffer, ImGuiIB[FrameIndex % NUM_IMGUI_BUFFERS].Buffer.Buffer, DestIBOffset * sizeof(ImDrawIdx), Align<uint32>(CmdList->IdxBuffer.Size * sizeof(ImDrawIdx), 4), SrcIB);
				//vkCmdUpdateBuffer(CmdBuffer->CmdBuffer, ImGuiVB[FrameIndex % NUM_IMGUI_BUFFERS].Buffer.Buffer, DestVBOffset * sizeof(ImDrawVert), Align<uint32>(CmdList->VtxBuffer.Size * sizeof(ImDrawVert), 4), SrcVB);

				//DestIBOffset += CmdList->IdxBuffer.Size;
				//DestVBOffset += CmdList->VtxBuffer.Size;
				DestIBData += CmdList->IdxBuffer.Size;
				DestVBData += CmdList->VtxBuffer.Size;
			}

			//VkBufferMemoryBarrier BufferBarriers[2];
			//ZeroVulkanMem(BufferBarriers[0], VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
			//BufferBarriers[0].buffer = ImGuiVB[FrameIndex % NUM_IMGUI_BUFFERS].Buffer.Buffer;
			//BufferBarriers[0].size = DestVBOffset * sizeof(ImDrawVert);
			//BufferBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			//BufferBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			//ZeroVulkanMem(BufferBarriers[1], VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
			//BufferBarriers[1].buffer = ImGuiIB[FrameIndex % NUM_IMGUI_BUFFERS].Buffer.Buffer;
			//BufferBarriers[1].size = DestIBOffset * sizeof(ImDrawIdx);
			//BufferBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			//BufferBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			//vkCmdPipelineBarrier(CmdBuffer->CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 2, BufferBarriers, 0, nullptr);

			ImGuiIB[FrameIndex % NUM_IMGUI_BUFFERS].Unlock();
			ImGuiVB[FrameIndex % NUM_IMGUI_BUFFERS].Unlock();

			SVulkan::FGfxPSO* PSO = GPSOCache.GetGfxPSO(ImGUIPSO, ImGUIVertexDecl);
			{
				VkWriteDescriptorSet DescriptorWrites[3];
				ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
				//DescriptorWrites.pNext = NULL;
				//DescriptorWrites.dstSet = 0;  // dstSet is ignored by the extension
				DescriptorWrites[0].descriptorCount = 1;
				DescriptorWrites[0].descriptorType = (VkDescriptorType)PSO->Reflection[EShaderStages::Vertex]->bindings[0]->descriptor_type;
				VkDescriptorBufferInfo BInfo;
				ZeroMem(BInfo);
				BInfo.buffer = ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS].Buffer.Buffer;
				BInfo.range = ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS].Size;
				DescriptorWrites[0].pBufferInfo = &BInfo;
				DescriptorWrites[0].dstBinding = PSO->Reflection[EShaderStages::Vertex]->bindings[0]->binding;

				VkDescriptorImageInfo IInfo;
				ZeroMem(IInfo);
				IInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				IInfo.imageView = ImGuiFont.View;
				IInfo.sampler = ImGuiFontSampler;

				ZeroVulkanMem(DescriptorWrites[1], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
				DescriptorWrites[1].descriptorCount = 1;
				DescriptorWrites[1].descriptorType = (VkDescriptorType)PSO->Reflection[EShaderStages::Pixel]->bindings[0]->descriptor_type;
				DescriptorWrites[1].pImageInfo = &IInfo;
				DescriptorWrites[1].dstBinding = PSO->Reflection[EShaderStages::Pixel]->bindings[0]->binding;

				ZeroVulkanMem(DescriptorWrites[2], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
				DescriptorWrites[2].descriptorCount = 1;
				DescriptorWrites[2].descriptorType = (VkDescriptorType)PSO->Reflection[EShaderStages::Pixel]->bindings[1]->descriptor_type;
				DescriptorWrites[2].pImageInfo = &IInfo;
				DescriptorWrites[2].dstBinding = PSO->Reflection[EShaderStages::Pixel]->bindings[1]->binding;

				{
					float* ScaleTranslate = (float*)ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS].Lock();
					FVector2 Scale = {2.0f / DrawData->DisplaySize.x, 2.0f / DrawData->DisplaySize.y};
					FVector2 Translate = { -1.0f - DrawData->DisplayPos.x * Scale.x, -1.0f - DrawData->DisplayPos.y * Scale.y};
					//float ScaleTranslate[4];
					ScaleTranslate[0] = Scale.x;
					ScaleTranslate[1] = Scale.y;
					ScaleTranslate[2] = Translate.x;
					ScaleTranslate[3] = Translate.y;
					ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS].Unlock();
					//vkCmdUpdateBuffer(CmdBuffer->CmdBuffer, ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS].Buffer.Buffer, 0, sizeof(ScaleTranslate), &ScaleTranslate);

					//VkBufferMemoryBarrier BufferBarrier;
					//ZeroVulkanMem(BufferBarrier, VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
					//BufferBarrier.buffer = ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS].Buffer.Buffer;
					//BufferBarrier.size = sizeof(ScaleTranslate);
					//BufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
					//BufferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
					//vkCmdPipelineBarrier(CmdBuffer->CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &BufferBarrier, 0, nullptr);
				}
				GDescriptorCache.UpdateDescriptors(CmdBuffer, 3, DescriptorWrites, PSO);
			}

			CmdBuffer->BeginRenderPass(Framebuffer);
			vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PSO->Pipeline);

			{
				VkViewport Viewport;
				Viewport.x = 0;
				Viewport.y = 0;
				Viewport.width = DrawData->DisplaySize.x;
				Viewport.height = DrawData->DisplaySize.y;
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
					Scissor.extent.height = (uint32)(Cmd->ClipRect.w - Cmd->ClipRect.y + 1);
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

	void TryLoadGLTF(SVulkan::SDevice& Device, const char* Filename)
	{
		if (LoadGLTF(Device, Filename, GPSOCache, Scene, PendingOpsMgr, &GStagingBufferMgr))
		{
			LoadedGLTF = Filename;
		}
	}

	struct
	{
		FMatrix4x4 ViewMtx;
		FVector3 Pos = {0, 0, 0};
		FVector3 Rot ={0, 0, 0};
		FVector3 FOVNearFar = {35.0f, 100.0f, 3000.0f};
	} Camera;

	void UpdateCameraMatrices(const FVector3& DeltaPos, const FVector3& DeltaRot)
	{
		Camera.Rot += DeltaRot;
		Camera.Pos += DeltaPos;
		Camera.ViewMtx = FMatrix4x4::GetRotationY(ToRadians(Camera.Rot.y));
		Camera.ViewMtx.Rows[3] += Camera.Pos;
	}

	void DrawScene(SVulkan::FCmdBuffer* CmdBuffer)
	{
		int W = 0, H = 1;
		glfwGetWindowSize(Window, &W, &H);
		float FOVRadians = tan(ToRadians(Camera.FOVNearFar.x));

		for (auto& Instance : Scene.Instances)
		{
			auto& Mesh = Scene.Meshes[Instance.Mesh];
			for (auto& Prim : Mesh.Prims)
			{
				struct FUB
				{
					FMatrix4x4 ViewMtx;
					FMatrix4x4 ProjMtx;
					FMatrix4x4 WorldMtx;
					FIntVector4 Mode;
				} UB;
				UB.WorldMtx = FMatrix4x4::GetIdentity();
				UB.WorldMtx = FMatrix4x4::GetRotationZ(ToRadians(180));
				UB.WorldMtx.Rows[3] = Instance.Pos;
				UB.Mode.Set(g_nMode, 0, 0, 0);
				UB.ViewMtx = Camera.ViewMtx;
				UB.ProjMtx = CalculateProjectionMatrix(FOVRadians, (float)W / (float)H, Camera.FOVNearFar.y, Camera.FOVNearFar.z);
				FStagingBuffer* Buffer = GStagingBufferMgr.AcquireBuffer(sizeof(UB), CmdBuffer);
				*(FUB*)Buffer->Buffer->Lock() = UB;
				Buffer->Buffer->Unlock();

				SVulkan::FGfxPSO* PSO = GPSOCache.GetGfxPSO(TestGLTFPSO, Prim.VertexDecl);
				vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PSO->Pipeline);
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
				for (auto VBIndex : Prim.VertexBuffers)
				{
					VBs.push_back(Scene.Buffers[VBIndex].Buffer.Buffer);
				}
				check(VBs.size() == Prim.VertexOffsets.size());
				vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, (uint32)VBs.size(), VBs.data(), Prim.VertexOffsets.data());
#endif
				{
					VkDescriptorImageInfo ImageInfo[3];
					ZeroMem(ImageInfo);

					ImageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					ImageInfo[0].sampler = LinearMipSampler;

					ImageInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					ImageInfo[1].imageView = Scene.Textures.empty() ? WhiteTexture.View : Scene.Textures[Scene.Materials[Prim.Material].BaseColor].Image.View;
					ImageInfo[1].sampler = LinearMipSampler;

					ImageInfo[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					ImageInfo[2].imageView = Scene.Textures.empty() ? WhiteTexture.View : Scene.Textures[Scene.Materials[Prim.Material].Normal].Image.View;
					ImageInfo[2].sampler = LinearMipSampler;

					VkDescriptorBufferInfo BufferInfo;
					ZeroMem(BufferInfo);
					BufferInfo.buffer = Buffer->Buffer->Buffer.Buffer;
					BufferInfo.range = Buffer->Buffer->Size;

					VkWriteDescriptorSet DescriptorWrites[4];
					ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
					DescriptorWrites[0].descriptorCount = 1;
					DescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					DescriptorWrites[0].pBufferInfo = &BufferInfo;
					DescriptorWrites[0].dstBinding = PSO->Reflection[EShaderStages::Vertex]->bindings[0]->binding;

					ZeroVulkanMem(DescriptorWrites[1], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
					DescriptorWrites[1].descriptorCount = 1;
					DescriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
					DescriptorWrites[1].pImageInfo = &ImageInfo[0];
					DescriptorWrites[1].dstBinding = PSO->Reflection[EShaderStages::Pixel]->bindings[1]->binding;

					ZeroVulkanMem(DescriptorWrites[2], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
					DescriptorWrites[2].descriptorCount = 1;
					DescriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					DescriptorWrites[2].pImageInfo = &ImageInfo[1];
					DescriptorWrites[2].dstBinding = PSO->Reflection[EShaderStages::Pixel]->bindings[2]->binding;

					ZeroVulkanMem(DescriptorWrites[3], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
					DescriptorWrites[3].descriptorCount = 1;
					DescriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					DescriptorWrites[3].pImageInfo = &ImageInfo[2];
					DescriptorWrites[3].dstBinding = PSO->Reflection[EShaderStages::Pixel]->bindings[3]->binding;

					GDescriptorCache.UpdateDescriptors(CmdBuffer, 4, &DescriptorWrites[0], PSO);
				}


				vkCmdDrawIndexed(CmdBuffer->CmdBuffer, Prim.NumIndices, 1, 0, 0, 0);
			}
		}
	}
};

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

static void UpdateInput(FApp& App)
{
	App.UpdateCameraMatrices(g_vMove, g_vRot);

	g_vMove = FVector3::GetZero();
	g_vRot = FVector3::GetZero();
}

static double Render(FApp& App)
{
	SVulkan::SDevice& Device = GVulkan.Devices[GVulkan.PhysicalDevice];
	GVulkan.Swapchain.AcquireBackbuffer();

	Device.RefreshCommandBuffers();
	App.Update();

	SVulkan::FCmdBuffer* CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);
	App.GPUTiming.BeginTimestamp(CmdBuffer);

	float Width = GVulkan.Swapchain.GetViewport().width;
	float Height = GVulkan.Swapchain.GetViewport().height;

	ImGuiIO& IO = ImGui::GetIO();
	IO.DisplaySize.x = Width;
	IO.DisplaySize.y = Height;
	IO.DeltaTime = App.LastDelta;

	FRenderTargetInfo ColorInfo = GVulkan.Swapchain.GetRenderTargetInfo();
	SVulkan::FFramebuffer* Framebuffer = GRenderTargetCache.GetOrCreateFrameBuffer(ColorInfo, FRenderTargetInfo(App.DepthBuffer.View, App.DepthBuffer.Image.Format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE), (uint32)Width, (uint32)Height);

	App.PendingOpsMgr.ExecutePendingStagingOps(Device, CmdBuffer);

	App.ImGuiNewFrame();

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0, 
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT);
	Device.TransitionImage(CmdBuffer, App.DepthBuffer.Image.Image,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

	static float F = 0;
	F += 0.025f;
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
	if (0)
	{
		//vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.NoVBClipVSRedPSO.Pipeline);
	}
	else if (0)
	{
		//vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.PassThroughVSRedPSPSO.Pipeline);
	}
	else if (1)
	{
		SVulkan::FGfxPSO* PSO = GPSOCache.GetGfxPSO(App.DataClipVSColorPSO);

		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PSO->Pipeline);

		VkWriteDescriptorSet DescriptorWrites[2];
		ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[0].descriptorCount = 1;
		DescriptorWrites[0].descriptorType = (VkDescriptorType)PSO->Reflection[EShaderStages::Vertex]->bindings[0]->descriptor_type;
		DescriptorWrites[0].pTexelBufferView = &App.ClipVB.View;
		//DescriptorWrites.pBufferInfo = &info.uniform_data.buffer_info;  // populated by init_uniform_buffer()
		DescriptorWrites[0].dstBinding = PSO->Reflection[EShaderStages::Vertex]->bindings[0]->binding;

		ZeroVulkanMem(DescriptorWrites[1], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[1].descriptorCount = 1;
		DescriptorWrites[1].descriptorType = (VkDescriptorType)PSO->Reflection[EShaderStages::Pixel]->bindings[0]->descriptor_type;
		VkDescriptorBufferInfo BufferInfo;
		ZeroMem(BufferInfo);
		BufferInfo.buffer = App.ColorUB.Buffer.Buffer;
		BufferInfo.range =  App.ColorUB.Size;
		DescriptorWrites[1].pBufferInfo = &BufferInfo;
		DescriptorWrites[1].dstBinding = PSO->Reflection[EShaderStages::Pixel]->bindings[0]->binding;

		GDescriptorCache.UpdateDescriptors(CmdBuffer, 2, DescriptorWrites, PSO);
	}
/*
	else if (1)
	{
		SVulkan::FGfxPSO* PSO = GPSOCache.GetPSO(App.DataClipVSRedPSO);
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.DataClipVSRedPSO.Pipeline);

		VkWriteDescriptorSet DescriptorWrites;
		ZeroVulkanMem(DescriptorWrites, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		//DescriptorWrites.pNext = NULL;
		//DescriptorWrites.dstSet = 0;  // dstSet is ignored by the extension
		DescriptorWrites.descriptorCount = 1;
		DescriptorWrites.descriptorType = (VkDescriptorType)App.DataClipVSRedPSO.Reflection[EShaderStages::Vertex]->bindings[0]->descriptor_type;
		DescriptorWrites.pTexelBufferView = &App.ClipVB.View;
		DescriptorWrites.dstBinding = App.DataClipVSRedPSO.Reflection[EShaderStages::Vertex]->bindings[0]->binding;

		GDescriptorCache.UpdateDescriptors(CmdBuffer, 1, &DescriptorWrites, App.DataClipVSRedPSO);
	}
	else if (1)
	{
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.VBClipVSRedPSO.Pipeline);
		VkDeviceSize Offsets = 0;
		vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &App.ClipVB.Buffer.Buffer, &Offsets);
	}
*/
	GVulkan.Swapchain.SetViewportAndScissor(CmdBuffer);

	vkCmdDraw(CmdBuffer->CmdBuffer, 3, 1, 0, 0);

	if (!App.Scene.Meshes.empty())
	{
		App.DrawScene(CmdBuffer);
	}

	CmdBuffer->EndRenderPass();

	{
		SVulkan::FComputePSO* PSO = GPSOCache.GetComputePSO(App.TestCSPSO);
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, PSO->Pipeline);
		GVulkan.Swapchain.SetViewportAndScissor(CmdBuffer);

		VkWriteDescriptorSet DescriptorWrites[2];
		ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[0].descriptorCount = 1;
		DescriptorWrites[0].descriptorType = (VkDescriptorType)PSO->Reflection->bindings[0]->descriptor_type;
		DescriptorWrites[0].pTexelBufferView = &App.TestCSBuffer.View;
		DescriptorWrites[0].dstBinding = PSO->Reflection->bindings[0]->binding;

		ZeroVulkanMem(DescriptorWrites[1], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[1].descriptorCount = 1;
		DescriptorWrites[1].descriptorType = (VkDescriptorType)PSO->Reflection->bindings[1]->descriptor_type;
		VkDescriptorBufferInfo BufferInfo;
		ZeroMem(BufferInfo);
		BufferInfo.buffer = App.TestCSUB.Buffer.Buffer;
		BufferInfo.range =  App.TestCSUB.Size;
		DescriptorWrites[1].pBufferInfo = &BufferInfo;
		DescriptorWrites[1].dstBinding = PSO->Reflection->bindings[1]->binding;

		GDescriptorCache.UpdateDescriptors(CmdBuffer, 2, DescriptorWrites, PSO);

		for (int32 Index = 0; Index < 256; ++Index)
		{
			vkCmdDispatch(CmdBuffer->CmdBuffer, 256, 1, 1);
		}
	}

	App.GPUTiming.EndTimestamp(CmdBuffer);

#if 0
	if (ImGui::BeginMainMenuBar())
	{
		{
			char s[64];
			sprintf(s, "FPS %3.2f", (float)(1000.0 / App.CpuDelta));
			ImGui::MenuItem(s, nullptr, false, false);
			sprintf(s, "CPU %3.2fms", (float)App.CpuDelta);
			ImGui::MenuItem(s, nullptr, false, false);
			sprintf(s, "GPU %3.2fms", (float)App.GpuDelta);
			ImGui::MenuItem(s, nullptr, false, false);
			if (!App.LoadedGLTF.empty())
			{
				ImGui::MenuItem(App.LoadedGLTF.c_str(), nullptr, false, false);
			}
		}
/*
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Open", "Ctrl+O"))
			{
			}
			ImGui::EndMenu();
		}
*/

		ImGui::EndMainMenuBar();
	}
#endif
	if (ImGui::Begin("Test"))
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
		ImGui::InputFloat3("Rot", App.Camera.Rot.Values);
		ImGui::InputFloat3("FOV,Near,Far", App.Camera.FOVNearFar.Values);

		const char* List[] = {"Base", "Vertex Color", "Normals", "Base * Vertex", "NormalMap"};
		ImGui::ListBox("Show mode", &g_nMode, List, 4);
	}
	ImGui::End();

	//ImGui::ShowDemoWindow();

	ImGui::Render();

	ImDrawData* DrawData = ImGui::GetDrawData();
	App.DrawDataImGui(DrawData, GVulkan.Swapchain, CmdBuffer, Framebuffer);

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0,
		VK_IMAGE_ASPECT_COLOR_BIT);
	CmdBuffer->End();

	Device.Submit(Device.PresentQueue, CmdBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, GVulkan.Swapchain.AcquireBackbufferSemaphore, GVulkan.Swapchain.FinalSemaphore);

	GVulkan.Swapchain.Present(Device.PresentQueue, GVulkan.Swapchain.FinalSemaphore);
	//Device.WaitForFence(CmdBuffer.Fence, CmdBuffer.LastSubmittedFence);
	;vkDeviceWaitIdle(Device.Device);

	double GpuTimeMS = App.GPUTiming.ReadTimestamp();
	return GpuTimeMS;
}


static void CharCallback(GLFWwindow* Window, unsigned int Char)
{
	ImGuiIO& io = ImGui::GetIO();
	io.AddInputCharacter(Char);
}

static void KeyCallback(GLFWwindow* Window, int Key, int Scancode, int Action, int Mods)
{
	ImGuiIO& io = ImGui::GetIO();

	auto IsModifierKey = [](int Key)
	{
		switch (Key)
		{
		case GLFW_KEY_LEFT_CONTROL:
		case GLFW_KEY_RIGHT_CONTROL:
		case GLFW_KEY_LEFT_SHIFT:
		case GLFW_KEY_RIGHT_SHIFT:
		case GLFW_KEY_LEFT_ALT:
		case GLFW_KEY_RIGHT_ALT:
		case GLFW_KEY_LEFT_SUPER:
		case GLFW_KEY_RIGHT_SUPER:
			break;
		default:
			return false;
		}

		return true;
	};

	if (IsModifierKey(Key) || io.WantCaptureKeyboard)
	{
		if (Action == GLFW_PRESS)
		{
			io.KeysDown[Key] = true;
		}
		if (Action == GLFW_RELEASE)
		{
			io.KeysDown[Key] = false;
		}

		// Modifiers are not reliable across systems
		io.KeyCtrl = io.KeysDown[GLFW_KEY_LEFT_CONTROL] || io.KeysDown[GLFW_KEY_RIGHT_CONTROL];
		io.KeyShift = io.KeysDown[GLFW_KEY_LEFT_SHIFT] || io.KeysDown[GLFW_KEY_RIGHT_SHIFT];
		io.KeyAlt = io.KeysDown[GLFW_KEY_LEFT_ALT] || io.KeysDown[GLFW_KEY_RIGHT_ALT];
		io.KeySuper = io.KeysDown[GLFW_KEY_LEFT_SUPER] || io.KeysDown[GLFW_KEY_RIGHT_SUPER];
	}
	else
	{
		if (Action == GLFW_PRESS || Action == GLFW_REPEAT)
		{
			float Delta = io.KeyShift ? 5.0f : 1.0f;
			switch (Key)
			{
			case GLFW_KEY_LEFT_SHIFT:
			case GLFW_KEY_RIGHT_SHIFT:
				break;
			case GLFW_KEY_RIGHT:
				g_vRot.y += Delta;
				break;
			case GLFW_KEY_LEFT:
				g_vRot.y -= Delta;
				break;
			case GLFW_KEY_PAGE_UP:
				g_vMove.y += Delta;
				break;
			case GLFW_KEY_PAGE_DOWN:
				g_vMove.y -= Delta;
				break;
			case GLFW_KEY_W:
				g_vMove.z += Delta;
				break;
			case GLFW_KEY_S:
				g_vMove.z -= Delta;
				break;
			case GLFW_KEY_A:
				g_vMove.x += Delta;
				break;
			case GLFW_KEY_D:
				g_vMove.x -= Delta;
				break;
			default:
				break;
			}
		}
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

static void SetupShaders(FApp& App)
{
	FShaderInfo* TestCS = GShaderLibrary.RegisterShader("Shaders/TestCS.hlsl", "TestCS", FShaderInfo::EStage::Compute);
	FShaderInfo* VBClipVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "VBClipVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* NoVBClipVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "MainNoVBClipVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* DataClipVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "MainBufferClipVS", FShaderInfo::EStage::Vertex);
	//FShaderInfo* DataClipHS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "MainBufferClipHS", FShaderInfo::EStage::Hull);
	//FShaderInfo* DataClipDS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "MainBufferClipDS", FShaderInfo::EStage::Domain);
	FShaderInfo* RedPS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "RedPS", FShaderInfo::EStage::Pixel);
	FShaderInfo* ColorPS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "ColorPS", FShaderInfo::EStage::Pixel);
	FShaderInfo* UIVS = GShaderLibrary.RegisterShader("Shaders/UI.hlsl", "UIMainVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* UIPS = GShaderLibrary.RegisterShader("Shaders/UI.hlsl", "UIMainPS", FShaderInfo::EStage::Pixel);
	FShaderInfo* TestGLTFVS = GShaderLibrary.RegisterShader("Shaders/TestMesh.hlsl", "TestGLTFVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* TestGLTFPS = GShaderLibrary.RegisterShader("Shaders/TestMesh.hlsl", "TestGLTFPS", FShaderInfo::EStage::Pixel);
	FShaderInfo* PassThroughVS = GShaderLibrary.RegisterShader("Shaders/PassThroughVS.hlsl", "MainVS", FShaderInfo::EStage::Vertex);
	GShaderLibrary.RecompileShaders();

	App.TestCSPSO = GPSOCache.CreateComputePSO("TestCSPSO", TestCS);

	SVulkan::FRenderPass* RenderPass = GRenderTargetCache.GetOrCreateRenderPass(FAttachmentInfo(GVulkan.Swapchain.Format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE), FAttachmentInfo(VK_FORMAT_D32_SFLOAT_S8_UINT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE));

	VkViewport Viewport = GVulkan.Swapchain.GetViewport();
	VkRect2D Scissor = GVulkan.Swapchain.GetScissor();

	App.DataClipVSColorPSO = GPSOCache.CreateGfxPSO("DataClipVSColorPSO", DataClipVS, ColorPS, RenderPass);
//	App.DataClipVSColorTessPSO = GPSOCache.CreateGfxPSO(DataClipVS, DataClipHS, DataClipDS, ColorPS, RenderPass);
	App.NoVBClipVSRedPSO = GPSOCache.CreateGfxPSO("NoVBClipVSRedPSO", NoVBClipVS, RedPS, RenderPass);
	App.DataClipVSRedPSO = GPSOCache.CreateGfxPSO("DataClipVSRedPSO", DataClipVS, RedPS, RenderPass);
	App.PassThroughVSRedPSPSO = GPSOCache.CreateGfxPSO("PassThroughVSRedPSPSO", PassThroughVS, RedPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
	{
		VkPipelineRasterizationStateCreateInfo* RasterizerInfo = (VkPipelineRasterizationStateCreateInfo*)GfxPipelineInfo.pRasterizationState;
		RasterizerInfo->cullMode = VK_CULL_MODE_NONE;
	});

	{
		//FPSOCache::FVertexDecl Decl;
		//Decl.AddAttribute(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0, "Position");
		//Decl.AddBinding(0, 4 * sizeof(float));
		//int32 VertexDeclHandle = GPSOCache.FindOrAddVertexDecl(Decl);
		//App.VBClipVSRedPSO = GPSOCache.CreateGfxPSO("VBClipVSRedPSO", VBClipVS, RedPS, RenderPass,  VertexDeclHandle);
	}

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
		App.TestGLTFPSO = GPSOCache.CreateGfxPSO("TestGLTFPSO", TestGLTFVS, TestGLTFPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
		{
			VkPipelineDepthStencilStateCreateInfo* DSInfo = (VkPipelineDepthStencilStateCreateInfo*)GfxPipelineInfo.pDepthStencilState;
			DSInfo->depthTestEnable = VK_TRUE;
			DSInfo->depthWriteEnable = VK_TRUE;
			DSInfo->depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		});
		for (auto& Mesh : App.Scene.Meshes)
		{
			for (auto& Prim : Mesh.Prims)
			{
				FixGLTFVertexDecl(TestGLTFVS->Shader, Prim.VertexDecl);
			}
		}
	}
}

static void ErrorCallback(int Error, const char* Msg)
{
	fprintf(stderr, "Glfw Error %d: %s\n", Error, Msg);
}

static GLFWwindow* Init(FApp& App)
{
	glfwSetErrorCallback(ErrorCallback);
	int RC = glfwInit();
	check(RC != 0);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	uint32 ResX = RCUtils::FCmdLine::Get().TryGetIntPrefix("-resx=", 1920);
	uint32 ResY = RCUtils::FCmdLine::Get().TryGetIntPrefix("-resy=", 1080);

	g_vMove = TryGetVector3Prefix("-pos=", FVector3::GetZero());
	g_vRot = TryGetVector3Prefix("-rot=", FVector3::GetZero());

	GLFWwindow* Window = glfwCreateWindow(ResX, ResY, "VkTest2", 0, 0);
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

	glfwSetKeyCallback(Window, KeyCallback);
	glfwSetCharCallback(Window, CharCallback);

	const char* Filename = nullptr;
	if (RCUtils::FCmdLine::Get().TryGetStringFromPrefix("-gltf=", Filename))
	{
		App.TryLoadGLTF(Device, Filename);
	}

	SetupShaders(App);

	App.Create(Device, Window);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& IO = ImGui::GetIO();
	//IO.ImeWindowHandle = Window;

	App.SetupImGuiAndResources(Device);
	glfwShowWindow(Window);

	return Window;
}

static void Deinit(FApp& App, GLFWwindow* Window)
{
	GVulkan.DeinitPre();

	ImGui::DestroyContext();

	GStagingBufferMgr.Destroy();

	App.Destroy();

	GDescriptorCache.Destroy();
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

	uint32 ExitAfterNFrames = RCUtils::FCmdLine::Get().TryGetIntPrefix("-exitafterframes=", (uint32)-1);

	uint32 Frame = 0;
	while (!glfwWindowShouldClose(Window))
	{
		double CpuBegin = glfwGetTime() * 1000.0;

		glfwPollEvents();

		UpdateInput(App);

		App.GpuDelta = Render(App);

		double CpuEnd = glfwGetTime() * 1000.0;

		App.CpuDelta = CpuEnd - CpuBegin;

/*
		{
			std::stringstream ss;
			ss << "VkTest2 CPU: " << CpuDelta << " ms,  GPU: " << GpuDelta << " ms";
			if (!App.LoadedGLTF.empty())
			{
				ss << " - " << App.LoadedGLTF;
			}
			ss.flush();
			::glfwSetWindowTitle(Window, ss.str().c_str());
		}
*/
		App.LastDelta = (float)App.CpuDelta;
		++Frame;

		if (Frame > ExitAfterNFrames)
		{
			glfwSetWindowShouldClose(Window, 1);
		}
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
