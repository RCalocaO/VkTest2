// VkTest2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <GLFW/glfw3.h>

// Fix Windows warning
#undef APIENTRY

#include "RCVulkan.h"

#include "imgui.h"


#if USE_CGLTF
#include "../cgltf/cgltf.h"
#endif

#if USE_TINY_GLTF
#pragma warning(push)
#pragma warning(disable:4267)
#include "../tinygltf/tiny_gltf.h"
#pragma warning(pop)
#endif

#include "../RCUtils/RCUtilsMath.h"

#pragma comment(lib, "glfw3.lib")

static SVulkan GVulkan;


static FShaderLibrary GShaderLibrary;

static FRenderTargetCache GRenderTargetCache;

static FPSOCache GPSOCache;

static FDescriptorCache GDescriptorCache;

static FStagingBufferManager GStagingBufferMgr;

struct FApp
{
	uint32 FrameIndex = 0;
	//SVulkan::FGfxPSO DataClipVSColorTessPSO;
	SVulkan::FGfxPSO NoVBClipVSRedPSO;
	SVulkan::FGfxPSO DataClipVSRedPSO;
	SVulkan::FGfxPSO DataClipVSColorPSO;
	SVulkan::FGfxPSO VBClipVSRedPSO;
	SVulkan::FGfxPSO TestGLTFPSO;
	FBufferWithMemAndView ClipVB;
	FBufferWithMem StagingClipVB;
	FShaderInfo* TestCS = nullptr;
	SVulkan::FComputePSO TestCSPSO;
	FBufferWithMemAndView TestCSBuffer;
	FBufferWithMem TestCSUB;
	FBufferWithMem ColorUB;
	GLFWwindow* Window;
	uint32 ImGuiMaxVertices = 16384 * 3;
	uint32 ImGuiMaxIndices = 16384 * 3;
	FImageWithMemAndView ImGuiFont;
	VkSampler ImGuiFontSampler = VK_NULL_HANDLE;
	FImageWithMemAndView WhiteTexture;
	enum
	{
		NUM_IMGUI_BUFFERS = 3,
	};
	FBufferWithMem ImGuiVB[NUM_IMGUI_BUFFERS];
	FBufferWithMem ImGuiIB[NUM_IMGUI_BUFFERS];
	FBufferWithMem ImGuiScaleTranslateUB[NUM_IMGUI_BUFFERS];
	SVulkan::FGfxPSO ImGUIPSO;
	double Time = 0;
	bool MouseJustPressed[5] = {false, false, false, false, false};
	GLFWcursor* MouseCursors[ImGuiMouseCursor_COUNT] = {0};

	float LastDelta = 1.0f / 60.0f;

	void Create(SVulkan::SDevice& Device, GLFWwindow* InWindow)
	{
		Window = InWindow;

		// Dummy stuff
		uint32 ClipVBSize = 3 * 4 * sizeof(float);
		ClipVB.Create(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ClipVBSize, VK_FORMAT_R32G32B32A32_SFLOAT);

		{
			StagingClipVB.Create(Device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ClipVBSize);
			float* Data = (float*)StagingClipVB.Lock();
			*Data++ = 0;		*Data++ = -0.5f;	*Data++ = 1; *Data++ = 1;
			*Data++ = 0.5f;		*Data++ = 0.5f;		*Data++ = 1; *Data++ = 1;
			*Data++ = -0.5f;	*Data++ = 0.5f;		*Data++ = 1; *Data++ = 1;
			StagingClipVB.Unlock();
		}

		ColorUB.Create(Device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 4 * sizeof(float));
		{
			float* Data = (float*)ColorUB.Lock();
			*Data++ = 0.0f;
			*Data++ = 1.0f;
			*Data++ = 0.0f;
			*Data++ = 1.0f;
			ColorUB.Unlock();
		}

		TestCSBuffer.Create(Device, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 256 * 256 * 4 * sizeof(float),  VK_FORMAT_R32G32B32A32_SFLOAT);

		TestCSUB.Create(Device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 4 * sizeof(uint32) + 16 * 1024);
		{
			uint32* Data = (uint32*)TestCSUB.Lock();
			*Data++ = 0xffffffff;
			*Data++ = 0xffffffff;
			*Data++ = 0xffffffff;
			*Data++ = 0xffffffff;
			memset(Data, 0x33, 16384);
			TestCSUB.Unlock();
		}

		WhiteTexture.Create(Device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 4, 4, VK_FORMAT_R8G8B8A8_UNORM);
	}

	void SetupFirstTime(SVulkan::SDevice& Device, SVulkan::FCmdBuffer* CmdBuffer)
	{
		VkBufferCopy Region;
		ZeroMem(Region);
		Region.size = ClipVB.Size;
		vkCmdCopyBuffer(CmdBuffer->CmdBuffer, StagingClipVB.Buffer.Buffer, ClipVB.Buffer.Buffer, 1, &Region);

		{
			FBufferWithMem* Buffer = GStagingBufferMgr.AcquireBuffer(CmdBuffer, 4);
			uint8* Mem = (uint8*)Buffer->Lock();
			*Mem += 0xff;
			*Mem += 0xff;
			*Mem += 0xff;
			*Mem += 0xff;
			Buffer->Unlock();
			VkBufferImageCopy Region;
			ZeroMem(Region);
			Region.imageExtent.width = 1;
			Region.imageExtent.height = 1;
			Region.imageExtent.depth = 1;
			Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			Region.imageSubresource.layerCount = 1;
			vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, Buffer->Buffer.Buffer, WhiteTexture.Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
		}

		SetupImGuiAndResources(Device);
	}

	void Destroy()
	{
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
		StagingClipVB.Destroy();
		ClipVB.Destroy();
		ColorUB.Destroy();
	}

	void SetupImGuiAndResources(SVulkan::SDevice& Device)
	{
		ImGuiIO& IO = ImGui::GetIO();

		int32 Width = 0, Height = 0;
		unsigned char* Pixels = nullptr;
		IO.Fonts->GetTexDataAsAlpha8(&Pixels, &Width, &Height);

		SVulkan::FCmdBuffer* CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);

		FBufferWithMem* FontBuffer = GStagingBufferMgr.AcquireBuffer(CmdBuffer, Width * Height * sizeof(uint32));
		uint8* Data = (uint8*)FontBuffer->Lock();
		for (int32 Index = 0; Index < Width * Height; ++Index)
		{
			*Data++ = *Pixels;
			*Data++ = *Pixels;
			*Data++ = *Pixels;
			*Data++ = *Pixels;
			++Pixels;
		}
		FontBuffer->Unlock();

		ImGuiFont.Create(Device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Width, Height, VK_FORMAT_R8G8B8A8_UNORM);

		Device.TransitionImage(CmdBuffer, ImGuiFont.Image.Image,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT);

		VkBufferImageCopy Region;
		ZeroMem(Region);
		Region.imageExtent.width = Width;
		Region.imageExtent.height = Height;
		Region.imageExtent.depth = 1;
		Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Region.imageSubresource.layerCount = 1;
		vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, FontBuffer->Buffer.Buffer, ImGuiFont.Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

		Device.TransitionImage(CmdBuffer, ImGuiFont.Image.Image,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT);

		CmdBuffer->End();
		Device.Submit(Device.GfxQueue, CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_NULL_HANDLE, VK_NULL_HANDLE);

		IO.Fonts->TexID = (void*)ImGuiFont.Image.Image;

		GStagingBufferMgr.ReleaseBuffer(FontBuffer);

		const uint32 ImGuiVertexSize = sizeof(ImDrawVert) * ImGuiMaxVertices;
		for (int32 Index = 0; Index < NUM_IMGUI_BUFFERS; ++Index)
		{
			ImGuiVB[Index].Create(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ImGuiVertexSize);

			static_assert(sizeof(uint16) == sizeof(ImDrawIdx), "");
			ImGuiIB[Index].Create(Device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ImGuiMaxIndices * sizeof(uint16));

			ImGuiScaleTranslateUB[Index].Create(Device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 4 * sizeof(float));
		}

		{
			VkSamplerCreateInfo Info;
			ZeroVulkanMem(Info, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
			VERIFY_VKRESULT(vkCreateSampler(Device.Device, &Info, nullptr, &ImGuiFontSampler));
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
			return;

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

			{
				VkWriteDescriptorSet DescriptorWrites[3];
				ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
				//DescriptorWrites.pNext = NULL;
				//DescriptorWrites.dstSet = 0;  // dstSet is ignored by the extension
				DescriptorWrites[0].descriptorCount = 1;
				DescriptorWrites[0].descriptorType = (VkDescriptorType)ImGUIPSO.VS[0]->bindings[0]->descriptor_type;
				VkDescriptorBufferInfo BInfo;
				ZeroMem(BInfo);
				BInfo.buffer = ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS].Buffer.Buffer;
				BInfo.range = ImGuiScaleTranslateUB[FrameIndex % NUM_IMGUI_BUFFERS].Size;
				DescriptorWrites[0].pBufferInfo = &BInfo;
				DescriptorWrites[0].dstBinding = ImGUIPSO.VS[0]->bindings[0]->binding;

				VkDescriptorImageInfo IInfo;
				ZeroMem(IInfo);
				IInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				IInfo.imageView = ImGuiFont.View;
				IInfo.sampler = ImGuiFontSampler;

				ZeroVulkanMem(DescriptorWrites[1], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
				DescriptorWrites[1].descriptorCount = 1;
				DescriptorWrites[1].descriptorType = (VkDescriptorType)ImGUIPSO.PS[0]->bindings[0]->descriptor_type;
				DescriptorWrites[1].pImageInfo = &IInfo;
				DescriptorWrites[1].dstBinding = ImGUIPSO.PS[0]->bindings[0]->binding;

				ZeroVulkanMem(DescriptorWrites[2], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
				DescriptorWrites[2].descriptorCount = 1;
				DescriptorWrites[2].descriptorType = (VkDescriptorType)ImGUIPSO.PS[0]->bindings[1]->descriptor_type;
				DescriptorWrites[2].pImageInfo = &IInfo;
				DescriptorWrites[2].dstBinding = ImGUIPSO.PS[0]->bindings[1]->binding;

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
				GDescriptorCache.UpdateDescriptors(CmdBuffer, 3, DescriptorWrites, ImGUIPSO);
			}

			CmdBuffer->BeginRenderPass(Framebuffer);
			vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ImGUIPSO.Pipeline);

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



	struct FScene
	{
		std::vector<FBufferWithMem> Buffers;
		std::vector<FImageWithMemAndView> Images;
		//std::vector<VkVertexInputAttributeDescription> AttrDescs;
		//std::vector<VkVertexInputBindingDescription> BindingDescs;

		struct FPrim
		{
			VkDeviceSize IndexOffset;
			uint32 NumIndices;
			int IndexBuffer;
			std::vector<int> VertexBuffers;
			std::vector<VkDeviceSize> VertexOffsets;
			VkIndexType IndexType;
			VkPrimitiveTopology PrimType;
			int Material;
			int VertexDecl;
		};

		struct FMesh
		{
			std::vector<FPrim> Prims;
		};

		std::vector<FMesh> Meshes;

		void Destroy()
		{
			for (auto& Buffer : Buffers)
			{
				Buffer.Destroy();
			}

			for (auto& Image : Images)
			{
				Image.Destroy();
			}
		}
	};
	FScene Scene;

#if USE_TINY_GLTF
	static VkFormat GetFormat(int GLTFComponentType, int GLTFType)
	{
		if (GLTFComponentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
		{
			switch (GLTFType)
			{
			case TINYGLTF_COMPONENT_TYPE_FLOAT:
				return VK_FORMAT_R32_SFLOAT;
			case TINYGLTF_TYPE_VEC2:
				return VK_FORMAT_R32G32_SFLOAT;
			case TINYGLTF_TYPE_VEC3:
				return VK_FORMAT_R32G32B32_SFLOAT;
			case TINYGLTF_TYPE_VEC4:
				return VK_FORMAT_R32G32B32A32_SFLOAT;
			default:
				check(0);
				break;
			}
		}
		check(0);
		return VK_FORMAT_UNDEFINED;
	}
#endif

	struct FVertexBindings
	{
		std::vector<VkVertexInputAttributeDescription> AttrDescs;
		std::vector<VkVertexInputBindingDescription> BindingDescs;
		std::vector<std::string> Names;
	};
	std::vector<FVertexBindings> VertexDecls;
	bool bUseColorStream = false;
	bool bHasNormals = false;
	bool bHasTexCoords = false;

#if USE_TINY_GLTF
	int GetOrAddVertexDecl(tinygltf::Model& Model, tinygltf::Primitive& GLTFPrim, FScene::FPrim& OutPrim)
	{
		FVertexBindings VertexDecl;
		uint32 BindingIndex = 0;
		for (auto Pair : GLTFPrim.attributes)
		{
			std::string Name = Pair.first;
			tinygltf::Accessor& Accessor = Model.accessors[Pair.second];

			tinygltf::BufferView& BufferView = Model.bufferViews[Accessor.bufferView];
			OutPrim.VertexOffsets.push_back(BufferView.byteOffset + Accessor.byteOffset);
			OutPrim.VertexBuffers.push_back(BufferView.buffer);

			VkVertexInputAttributeDescription AttrDesc;
			ZeroMem(AttrDesc);
			AttrDesc.binding = BindingIndex;
			AttrDesc.format = GetFormat(Accessor.componentType, Accessor.type);
			AttrDesc.location = BindingIndex;
			AttrDesc.offset = 0;
			VertexDecl.AttrDescs.push_back(AttrDesc);

			VkVertexInputBindingDescription BindingDesc;
			ZeroMem(BindingDesc);
			BindingDesc.binding = BindingIndex;
			BindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			BindingDesc.stride = (uint32)BufferView.byteStride;
			VertexDecl.BindingDescs.push_back(BindingDesc);

			VertexDecl.Names.push_back(Name);

			++BindingIndex;
		}

		bUseColorStream = (GLTFPrim.attributes.find("COLOR_0") != GLTFPrim.attributes.end());
		bHasNormals = (GLTFPrim.attributes.find("NORMAL") != GLTFPrim.attributes.end());
		bHasTexCoords = (GLTFPrim.attributes.find("TEXCOORD_0") != GLTFPrim.attributes.end());

		VertexDecls.push_back(VertexDecl);

		return 0;
	}
#endif

#if USE_CGLTF
	int GetOrAddVertexDecl(cgltf_data* Data, cgltf_primitive& GLTFPrim, FScene::FPrim& OutPrim)
	{
		FVertexBindings VertexDecl;
		uint32 BindingIndex = 0;
		for (cgltf_size Index = 0; Index < GLTFPrim.attributes_count; ++Index)
		{
			cgltf_attribute& Attribute = GLTFPrim.attributes[Index];
			std::string Name = Attribute.name;
			cgltf_accessor& Accessor = *Attribute.data;
			cgltf_buffer_view& BufferView = *Accessor.buffer_view;
			OutPrim.VertexOffsets.push_back(BufferView.offset + Accessor.offset);
			OutPrim.VertexBuffers.push_back(FindBuffer(Data, BufferView.buffer));

			VkVertexInputAttributeDescription AttrDesc;
			ZeroMem(AttrDesc);
			AttrDesc.binding = BindingIndex;
			AttrDesc.format = GetFormat(Accessor.component_type, Accessor.type);
			AttrDesc.location = BindingIndex;
			AttrDesc.offset = 0;
			VertexDecl.AttrDescs.push_back(AttrDesc);

			VkVertexInputBindingDescription BindingDesc;
			ZeroMem(BindingDesc);
			BindingDesc.binding = BindingIndex;
			BindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			BindingDesc.stride = (uint32)BufferView.stride;
			VertexDecl.BindingDescs.push_back(BindingDesc);

			VertexDecl.Names.push_back(Name);

			++BindingIndex;
		}

/*
		bUseColorStream = (GLTFPrim.attributes.find("COLOR_0") != GLTFPrim.attributes.end());
		bHasNormals = (GLTFPrim.attributes.find("NORMAL") != GLTFPrim.attributes.end());
		bHasTexCoords = (GLTFPrim.attributes.find("TEXCOORD_0") != GLTFPrim.attributes.end());
*/

		VertexDecls.push_back(VertexDecl);

		return 0;
	}

	static inline VkPrimitiveTopology GetPrimType(cgltf_primitive_type GLTFMode)
	{
		switch (GLTFMode)
		{
		case cgltf_primitive_type_points:
			return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case cgltf_primitive_type_lines:
			return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case cgltf_primitive_type_line_strip:
			return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		case cgltf_primitive_type_triangles:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case cgltf_primitive_type_triangle_strip:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case cgltf_primitive_type_triangle_fan:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
		default:
			check(0);
			break;
		}

		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}

	static inline uint32 GetSizeInBytes(cgltf_component_type GLTFComponentType)
	{
		switch (GLTFComponentType)
		{
		case cgltf_component_type_r_8:
		case cgltf_component_type_r_8u:
			return 1;
		case cgltf_component_type_r_16:
		case cgltf_component_type_r_16u:
			return 2;
		case cgltf_component_type_r_32f:
		case cgltf_component_type_r_32u:
			return 4;
		default:
			check(0);
			break;
		}

		return 0;
	}

	static inline VkIndexType GetIndexType(cgltf_component_type GLTFComponentType)
	{
		if (GLTFComponentType == cgltf_component_type_r_16u)
		{
			return VK_INDEX_TYPE_UINT16;
		}
		else if (GLTFComponentType == cgltf_component_type_r_32u)
		{
			return VK_INDEX_TYPE_UINT32;
		}

		check(0);
		return VK_INDEX_TYPE_UINT32;
	}

	static VkFormat GetFormat(cgltf_component_type GLTFComponentType, cgltf_type GLTFType)
	{
		if (GLTFComponentType == cgltf_component_type_r_32f)
		{
			switch (GLTFType)
			{
			case cgltf_type_scalar:
				return VK_FORMAT_R32_SFLOAT;
			case cgltf_type_vec2:
				return VK_FORMAT_R32G32_SFLOAT;
			case cgltf_type_vec3:
				return VK_FORMAT_R32G32B32_SFLOAT;
			case cgltf_type_vec4:
				return VK_FORMAT_R32G32B32A32_SFLOAT;
			default:
				check(0);
				break;
			}
		}

		check(0);
		return VK_FORMAT_UNDEFINED;
	}

	int32 FindBuffer(cgltf_data* Data, cgltf_buffer* Buffer)
	{
		for (cgltf_size BufferIndex = 0; BufferIndex < Data->buffers_count; ++BufferIndex)
		{
			cgltf_buffer& GLTFBuffer = Data->buffers[BufferIndex];
			if (Buffer->size == GLTFBuffer.size)
			{
				if (memcmp(GLTFBuffer.data, Buffer->data, GLTFBuffer.size) == 0)
				{
					return (int32)BufferIndex;
				}
			}
		}

		check(0);
		return -1;
	};


#endif

	void AddDefaultVertexInputs(SVulkan::SDevice& Device)
	{
/*
		auto AddDefaultStream = [&](const char* Name, VkFormat Format, uint32 Stride)
		{
			VkVertexInputAttributeDescription AttrDesc;
			ZeroMem(AttrDesc);
			AttrDesc.binding = BindingIndex;
			AttrDesc.format = Format;
			AttrDesc.location = OutPrim.VertexBuffers.size();
			AttrDesc.offset = 0;
			VertexDecl.AttrDescs.push_back(AttrDesc);

			VkVertexInputBindingDescription BindingDesc;
			ZeroMem(BindingDesc);
			BindingDesc.binding = BindingIndex;
			BindingDesc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
			BindingDesc.stride = Stride;
			VertexDecl.BindingDescs.push_back(BindingDesc);

			VertexDecl.Names.push_back(Name);

			OutPrim.VertexOffsets.push_back(0);
			OutPrim.VertexBuffers.push_back(Scene.Buffers.size());
			FBufferWithMem DefaultBuffer;
			DefaultBuffer.Create(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Stride);
			Scene.Buffers.push_back(DefaultBuffer);

			++BindingIndex;
		};

		if (!bUseColorStream)
		{
			AddDefaultStream("COLOR_0", VK_FORMAT_R8G8B8A8_UNORM, 4);
		}
		if (!bHasNormals)
		{
			AddDefaultStream("NORMAL", VK_FORMAT_R32G32B32_SFLOAT, 3 * 4);
		}
		if (!bHasTexCoords)
		{
			AddDefaultStream("TEXCOORD_0", VK_FORMAT_R32G32_SFLOAT, 2 * 4);
		}*/
	}

#if USE_TINY_GLTF
	static inline VkIndexType GetIndexType(int GLTFComponentType)
	{
		if (GLTFComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
		{
			return VK_INDEX_TYPE_UINT16;
		}
		else if (GLTFComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
		{
			return VK_INDEX_TYPE_UINT32;
		}

		check(0);
		return VK_INDEX_TYPE_UINT32;
	}

	static inline uint32 GetSizeInBytes(int GLTFComponentType)
	{
		switch (GLTFComponentType)
		{
		case TINYGLTF_COMPONENT_TYPE_BYTE:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			return 1;
		case TINYGLTF_COMPONENT_TYPE_SHORT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			return 2;
		case TINYGLTF_COMPONENT_TYPE_INT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
			return 4;
		case TINYGLTF_COMPONENT_TYPE_DOUBLE:
			return 8;

		default:
			check(0);
			break;
		}

		return 0;
	}

	static inline VkPrimitiveTopology GetPrimType(int GLTFMode)
	{
		switch (GLTFMode)
		{
		case TINYGLTF_MODE_POINTS:
			return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case TINYGLTF_MODE_LINE:
			return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case TINYGLTF_MODE_LINE_LOOP:
			return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		case TINYGLTF_MODE_TRIANGLES:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case TINYGLTF_MODE_TRIANGLE_STRIP:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case TINYGLTF_MODE_TRIANGLE_FAN:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
		default:
			check(0);
			break;
		}

		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}
#endif

	std::string LoadedGLTF;
	void TryLoadGLTF(SVulkan::SDevice& Device, const char* Filename)
	{
#if USE_CGLTF
		cgltf_options Options;
		ZeroMem(Options);
		cgltf_data* Data = nullptr;
		cgltf_result Result = cgltf_parse_file(&Options, Filename, &Data);
		if (Result == cgltf_result_success)
		{
			Result = cgltf_load_buffers(&Options, Data, nullptr);
			if (Result == cgltf_result_success)
			{
				for (cgltf_size BufferIndex = 0; BufferIndex < Data->buffers_count; ++BufferIndex)
				{
					cgltf_buffer& GLTFBuffer = Data->buffers[BufferIndex];
					FBufferWithMem Buffer;
					uint32 Size = (uint32)GLTFBuffer.size;
					Buffer.Create(Device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, Size);
					float* Data = (float*)Buffer.Lock();
					memcpy(Data, GLTFBuffer.data, Size);
					Buffer.Unlock();
					Scene.Buffers.push_back(Buffer);
				}

				for (cgltf_size MeshIndex = 0; MeshIndex < Data->meshes_count; ++MeshIndex)
				{
					cgltf_mesh& GLTFMesh = Data->meshes[MeshIndex];
					FScene::FMesh Mesh;
					for (cgltf_size PrimIndex = 0; PrimIndex < GLTFMesh.primitives_count; ++PrimIndex)
					{
						cgltf_primitive& GLTFPrim = GLTFMesh.primitives[PrimIndex];
						FScene::FPrim Prim;
						cgltf_accessor& Indices = *GLTFPrim.indices;
						check(Indices.type == cgltf_type_scalar);
						cgltf_buffer_view& IndicesBufferView = *Indices.buffer_view;
						Prim.VertexDecl = GetOrAddVertexDecl(Data, GLTFPrim, Prim);
						//Prim.Material = GLTFPrim.material;
						Prim.PrimType = GetPrimType(GLTFPrim.type);
						Prim.IndexOffset = Indices.offset + IndicesBufferView.offset;
						Prim.NumIndices = (uint32)IndicesBufferView.size / GetSizeInBytes(Indices.component_type);
						Prim.IndexBuffer = FindBuffer(Data, IndicesBufferView.buffer);
						Prim.IndexType = GetIndexType(Indices.component_type);
						Mesh.Prims.push_back(Prim);
					}

					Scene.Meshes.push_back(Mesh);
				}

				AddDefaultVertexInputs(Device);

				for (cgltf_size ImageIndex = 0; ImageIndex < Data->images_count; ++ImageIndex)
				{
					cgltf_image& GLTFImage = Data->images[ImageIndex];

					/*
									check(!GLTFImage.as_is);
									check(GLTFImage.bufferView == -1);
									check(!GLTFImage.image.empty());
									{
										FImageWithMemAndView Image;
										Image.Create(Device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, GLTFImage.width, GLTFImage.height, VK_FORMAT_R8G8B8A8_UNORM);

										SVulkan::FCmdBuffer* CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);

										uint32 Size = GLTFImage.width * GLTFImage.height * sizeof(uint32);
										FBufferWithMem* TempBuffer = GStagingBufferMgr.AcquireBuffer(CmdBuffer, Size);

										uint8* Data = (uint8*)TempBuffer->Lock();
										memcpy(Data, GLTFImage.image.data(), Size);
										TempBuffer->Unlock();

										Device.TransitionImage(CmdBuffer, Image.Image.Image,
											VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0,
											VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
											VK_IMAGE_ASPECT_COLOR_BIT);

										VkBufferImageCopy Region;
										ZeroMem(Region);
										Region.imageExtent.width = GLTFImage.width;
										Region.imageExtent.height = GLTFImage.height;
										Region.imageExtent.depth = 1;
										Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
										Region.imageSubresource.layerCount = 1;
										vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, TempBuffer->Buffer.Buffer, Image.Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

										Device.TransitionImage(CmdBuffer, Image.Image.Image,
											VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
											VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
											VK_IMAGE_ASPECT_COLOR_BIT);

										CmdBuffer->End();
										Device.Submit(Device.GfxQueue, CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_NULL_HANDLE, VK_NULL_HANDLE);

										GStagingBufferMgr.ReleaseBuffer(TempBuffer);

										Scene.Images.push_back(Image);
									}
					*/
				}
			}

			LoadedGLTF = Filename;

			cgltf_free(Data);
		}
#endif
#if USE_TINY_GLTF
		tinygltf::TinyGLTF Loader;
		tinygltf::Model Model;
		std::string Error;
		std::string Warnings;
		if (Loader.LoadASCIIFromFile(&Model, &Error, &Warnings, Filename))
		{
			for (tinygltf::Mesh& GLTFMesh : Model.meshes)
			{
				FScene::FMesh Mesh;
				for (tinygltf::Primitive& GLTFPrim : GLTFMesh.primitives)
				{
					FScene::FPrim Prim;

					tinygltf::Accessor& Indices = Model.accessors[GLTFPrim.indices];
					check(Indices.type == TINYGLTF_TYPE_SCALAR);

					tinygltf::BufferView& IndicesBufferView = Model.bufferViews[Indices.bufferView];

					Prim.VertexDecl = GetOrAddVertexDecl(Model, GLTFPrim, Prim);
					Prim.Material = GLTFPrim.material;
					Prim.PrimType = GetPrimType(GLTFPrim.mode);
					Prim.IndexOffset = Indices.byteOffset + IndicesBufferView.byteOffset;
					Prim.NumIndices = (uint32)IndicesBufferView.byteLength / GetSizeInBytes(Indices.componentType);
					Prim.IndexBuffer = IndicesBufferView.buffer;
					Prim.IndexType = GetIndexType(Indices.componentType);

					Mesh.Prims.push_back(Prim);
				}

				Scene.Meshes.push_back(Mesh);
			}

			for (tinygltf::Buffer& GLTFBuffer : Model.buffers)
			{
				FBufferWithMem Buffer;
				uint32 Size = (uint32)GLTFBuffer.data.size();
				Buffer.Create(Device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, Size);
				float* Data = (float*)Buffer.Lock();
				memcpy(Data, GLTFBuffer.data.data(), Size);
				Buffer.Unlock();
				Scene.Buffers.push_back(Buffer);
			}

			AddDefaultVertexInputs(Device);

			for (tinygltf::Image& GLTFImage : Model.images)
			{
				check(!GLTFImage.as_is);
				check(GLTFImage.bufferView == -1);
				check(!GLTFImage.image.empty());
				{
					FImageWithMemAndView Image;
					Image.Create(Device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, GLTFImage.width, GLTFImage.height, VK_FORMAT_R8G8B8A8_UNORM);

					SVulkan::FCmdBuffer* CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);

					uint32 Size = GLTFImage.width * GLTFImage.height * sizeof(uint32);
					FBufferWithMem* TempBuffer = GStagingBufferMgr.AcquireBuffer(CmdBuffer, Size);

					uint8* Data = (uint8*)TempBuffer->Lock();
					memcpy(Data, GLTFImage.image.data(), Size);
					TempBuffer->Unlock();

					Device.TransitionImage(CmdBuffer, Image.Image.Image,
						VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0,
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
						VK_IMAGE_ASPECT_COLOR_BIT);

					VkBufferImageCopy Region;
					ZeroMem(Region);
					Region.imageExtent.width = GLTFImage.width;
					Region.imageExtent.height = GLTFImage.height;
					Region.imageExtent.depth = 1;
					Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					Region.imageSubresource.layerCount = 1;
					vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, TempBuffer->Buffer.Buffer, Image.Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

					Device.TransitionImage(CmdBuffer, Image.Image.Image,
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_ASPECT_COLOR_BIT);

					CmdBuffer->End();
					Device.Submit(Device.GfxQueue, CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_NULL_HANDLE, VK_NULL_HANDLE);

					GStagingBufferMgr.ReleaseBuffer(TempBuffer);

					Scene.Images.push_back(Image);
				}
			}

			LoadedGLTF = Filename;
		}
#endif
	}

	void DrawScene(SVulkan::FCmdBuffer* CmdBuffer)
	{
		for (auto& Mesh : Scene.Meshes)
		{
			for (auto& Prim : Mesh.Prims)
			{
				//GetPSO(Prim.VertexDecl);

				SVulkan::FBuffer& IB = Scene.Buffers[Prim.IndexBuffer].Buffer;
				std::vector<VkBuffer> VBs;
				for (auto VBIndex : Prim.VertexBuffers)
				{
					VBs.push_back(Scene.Buffers[VBIndex].Buffer.Buffer);
				}
				check(VBs.size() == Prim.VertexOffsets.size());
				vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, TestGLTFPSO.Pipeline);
				vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, (uint32)VBs.size(), VBs.data(), Prim.VertexOffsets.data());
				vkCmdBindIndexBuffer(CmdBuffer->CmdBuffer, IB.Buffer, Prim.IndexOffset, Prim.IndexType);

				{
					VkDescriptorImageInfo ImageInfo[2];
					ZeroMem(ImageInfo);

					ImageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					ImageInfo[0].sampler = ImGuiFontSampler;

					ImageInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					ImageInfo[1].imageView = Scene.Images.empty() ? WhiteTexture.View : Scene.Images[0].View;
					ImageInfo[1].sampler = ImGuiFontSampler;

					VkWriteDescriptorSet DescriptorWrites[2];
					ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
					DescriptorWrites[0].descriptorCount = 1;
					DescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
					DescriptorWrites[0].pImageInfo = &ImageInfo[0];
					DescriptorWrites[0].dstBinding = TestGLTFPSO.PS[0]->bindings[0]->binding;

					ZeroVulkanMem(DescriptorWrites[1], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
					DescriptorWrites[1].descriptorCount = 1;
					DescriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					DescriptorWrites[1].pImageInfo = &ImageInfo[1];
					DescriptorWrites[1].dstBinding = TestGLTFPSO.PS[0]->bindings[1]->binding;

					GDescriptorCache.UpdateDescriptors(CmdBuffer, 2, &DescriptorWrites[0], TestGLTFPSO);
				}


				vkCmdDrawIndexed(CmdBuffer->CmdBuffer, Prim.NumIndices, 1, 0, 0, 0);
			}
		}
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

static double Render(FApp& App)
{
	SVulkan::SDevice& Device = GVulkan.Devices[GVulkan.PhysicalDevice];
	GVulkan.Swapchain.AcquireBackbuffer();

	Device.RefreshCommandBuffers();
	App.Update();

	SVulkan::FCmdBuffer* CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);
	Device.BeginTimestamp(CmdBuffer);

	ImGuiIO& IO = ImGui::GetIO();
	IO.DisplaySize.x = GVulkan.Swapchain.GetViewport().width;
	IO.DisplaySize.y = GVulkan.Swapchain.GetViewport().height;
	IO.DeltaTime = App.LastDelta;

	SVulkan::FFramebuffer* Framebuffer = GRenderTargetCache.GetOrCreateFrameBuffer(&GVulkan.Swapchain, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);

	static bool bFirst = true;
	if (bFirst)
	{
		App.SetupFirstTime(Device, CmdBuffer);
		bFirst = false;
	}

	App.ImGuiNewFrame();

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, 0, 
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT);

	static float F = 0;
	F += 0.025f;
	float ClearColor[4] = {0.0f, abs(sin(F)), abs(cos(F)), 0.0f};
	ClearImage(CmdBuffer->CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex], ClearColor);

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT);

	CmdBuffer->BeginRenderPass(Framebuffer);
	if (0)
	{
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.NoVBClipVSRedPSO.Pipeline);
	}
	/*
	else if (1)
	{
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.DataClipVSColorTessPSO.Pipeline);

		VkWriteDescriptorSet DescriptorWrites[2];
		ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[0].descriptorCount = 1;
		DescriptorWrites[0].descriptorType = (VkDescriptorType)App.DataClipVSColorPSO.VS[0]->bindings[0]->descriptor_type;
		DescriptorWrites[0].pTexelBufferView = &App.ClipVB.View;
		//DescriptorWrites.pBufferInfo = &info.uniform_data.buffer_info;  // populated by init_uniform_buffer()
		DescriptorWrites[0].dstBinding = App.DataClipVSRedPSO.VS[0]->bindings[0]->binding;

		ZeroVulkanMem(DescriptorWrites[1], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[1].descriptorCount = 1;
		DescriptorWrites[1].descriptorType = (VkDescriptorType)App.DataClipVSColorPSO.PS[0]->bindings[0]->descriptor_type;
		VkDescriptorBufferInfo BufferInfo;
		ZeroMem(BufferInfo);
		BufferInfo.buffer = App.ColorUB.Buffer.Buffer;
		BufferInfo.range =  App.ColorUB.Size;
		DescriptorWrites[1].pBufferInfo = &BufferInfo;
		DescriptorWrites[1].dstBinding = App.DataClipVSColorPSO.PS[0]->bindings[0]->binding;

		GDescriptorCache.UpdateDescriptors(CmdBuffer, 2, DescriptorWrites, App.DataClipVSColorPSO);
	}
	*/
	else if (1)
	{
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.DataClipVSColorPSO.Pipeline);

		VkWriteDescriptorSet DescriptorWrites[2];
		ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[0].descriptorCount = 1;
		DescriptorWrites[0].descriptorType = (VkDescriptorType)App.DataClipVSColorPSO.VS[0]->bindings[0]->descriptor_type;
		DescriptorWrites[0].pTexelBufferView = &App.ClipVB.View;
		//DescriptorWrites.pBufferInfo = &info.uniform_data.buffer_info;  // populated by init_uniform_buffer()
		DescriptorWrites[0].dstBinding = App.DataClipVSRedPSO.VS[0]->bindings[0]->binding;

		ZeroVulkanMem(DescriptorWrites[1], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[1].descriptorCount = 1;
		DescriptorWrites[1].descriptorType = (VkDescriptorType)App.DataClipVSColorPSO.PS[0]->bindings[0]->descriptor_type;
		VkDescriptorBufferInfo BufferInfo;
		ZeroMem(BufferInfo);
		BufferInfo.buffer = App.ColorUB.Buffer.Buffer;
		BufferInfo.range =  App.ColorUB.Size;
		DescriptorWrites[1].pBufferInfo = &BufferInfo;
		DescriptorWrites[1].dstBinding = App.DataClipVSColorPSO.PS[0]->bindings[0]->binding;

		GDescriptorCache.UpdateDescriptors(CmdBuffer, 2, DescriptorWrites, App.DataClipVSColorPSO);
	}
	else if (1)
	{
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.DataClipVSRedPSO.Pipeline);

		VkWriteDescriptorSet DescriptorWrites;
		ZeroVulkanMem(DescriptorWrites, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		//DescriptorWrites.pNext = NULL;
		//DescriptorWrites.dstSet = 0;  // dstSet is ignored by the extension
		DescriptorWrites.descriptorCount = 1;
		DescriptorWrites.descriptorType = (VkDescriptorType)App.DataClipVSRedPSO.VS[0]->bindings[0]->descriptor_type;
		DescriptorWrites.pTexelBufferView = &App.ClipVB.View;
		DescriptorWrites.dstBinding = App.DataClipVSRedPSO.VS[0]->bindings[0]->binding;

		GDescriptorCache.UpdateDescriptors(CmdBuffer, 1, &DescriptorWrites, App.DataClipVSRedPSO);
	}
	else if (1)
	{
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.VBClipVSRedPSO.Pipeline);
		VkDeviceSize Offsets = 0;
		vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &App.ClipVB.Buffer.Buffer, &Offsets);
	}
	GVulkan.Swapchain.SetViewportAndScissor(CmdBuffer);
	vkCmdDraw(CmdBuffer->CmdBuffer, 3, 1, 0, 0);

	if (!App.Scene.Meshes.empty())
	{
		App.DrawScene(CmdBuffer);
	}

	CmdBuffer->EndRenderPass();

	{
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, App.TestCSPSO.Pipeline);
		GVulkan.Swapchain.SetViewportAndScissor(CmdBuffer);

		VkWriteDescriptorSet DescriptorWrites[2];
		ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[0].descriptorCount = 1;
		DescriptorWrites[0].descriptorType = (VkDescriptorType)App.TestCSPSO.CS[0]->bindings[0]->descriptor_type;
		DescriptorWrites[0].pTexelBufferView = &App.TestCSBuffer.View;
		DescriptorWrites[0].dstBinding = App.TestCSPSO.CS[0]->bindings[0]->binding;

		ZeroVulkanMem(DescriptorWrites[1], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[1].descriptorCount = 1;
		DescriptorWrites[1].descriptorType = (VkDescriptorType)App.TestCSPSO.CS[0]->bindings[1]->descriptor_type;
		VkDescriptorBufferInfo BufferInfo;
		ZeroMem(BufferInfo);
		BufferInfo.buffer = App.TestCSUB.Buffer.Buffer;
		BufferInfo.range =  App.TestCSUB.Size;
		DescriptorWrites[1].pBufferInfo = &BufferInfo;
		DescriptorWrites[1].dstBinding = App.TestCSPSO.CS[0]->bindings[1]->binding;

		GDescriptorCache.UpdateDescriptors(CmdBuffer, 2, DescriptorWrites, App.TestCSPSO);

		for (int32 Index = 0; Index < 256; ++Index)
		{
			vkCmdDispatch(CmdBuffer->CmdBuffer, 256, 1, 1);
		}
	}

	Device.EndTimestamp(CmdBuffer);

	{
		if (ImGui::Begin("Hello, world!"))
		{
			if (ImGui::Button("File..."))
			{
				ImGui::OpenPopup("OpenFilePopup");
			}
			if (ImGui::BeginPopup("OpenFilePopup"))
			{
				//ShowExampleMenuFile();
				ImGui::EndPopup();
			}

			ImGui::End();
		}

		ImGui::ShowDemoWindow();
	}

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

	double GpuTimeMS = Device.ReadTimestamp();
	return GpuTimeMS;
}


static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
}

static void FixGLTFVertexDecl(FApp::FVertexBindings& VertexDecl, SVulkan::FShader* Shader)
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
	for (auto& Name : VertexDecl.Names)
	{
		int32 Found = FindSemantic(Name.c_str());
		check(Found != -1);
		{
			VertexDecl.AttrDescs[Index].location = (uint32)Found;
		}

		++Index;
	}

	spvReflectDestroyShaderModule(&Module);
}

static void SetupShaders(FApp& App)
{
	App.TestCS = GShaderLibrary.RegisterShader("Shaders/TestCS.hlsl", "TestCS", FShaderInfo::EStage::Compute);
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
	GShaderLibrary.RecompileShaders();

	App.TestCSPSO = GPSOCache.CreateComputePSO(App.TestCS);

	SVulkan::FRenderPass* RenderPass = GRenderTargetCache.GetOrCreateRenderPass(GVulkan.Swapchain.Format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);

	VkViewport Viewport = GVulkan.Swapchain.GetViewport();
	VkRect2D Scissor = GVulkan.Swapchain.GetScissor();

	App.DataClipVSColorPSO = GPSOCache.CreateGfxPSO(DataClipVS, ColorPS, RenderPass);
//	App.DataClipVSColorTessPSO = GPSOCache.CreateGfxPSO(DataClipVS, DataClipHS, DataClipDS, ColorPS, RenderPass);
	App.NoVBClipVSRedPSO = GPSOCache.CreateGfxPSO(NoVBClipVS, RedPS, RenderPass);
	App.DataClipVSRedPSO = GPSOCache.CreateGfxPSO(DataClipVS, RedPS, RenderPass);

	{
		VkVertexInputAttributeDescription VertexAttrDesc;
		ZeroMem(VertexAttrDesc);
		VertexAttrDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		VkVertexInputBindingDescription VertexBindDesc;
		ZeroMem(VertexBindDesc);
		VertexBindDesc.stride = 4 * sizeof(float);
		App.VBClipVSRedPSO = GPSOCache.CreateGfxPSO(VBClipVS, RedPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
		{
			VkPipelineVertexInputStateCreateInfo* VertexInputInfo = (VkPipelineVertexInputStateCreateInfo*)GfxPipelineInfo.pVertexInputState;
			VertexInputInfo->vertexAttributeDescriptionCount = 1;
			VertexInputInfo->pVertexAttributeDescriptions = &VertexAttrDesc;
			VertexInputInfo->vertexBindingDescriptionCount = 1;
			VertexInputInfo->pVertexBindingDescriptions = &VertexBindDesc;
		});
	}

	{
		VkVertexInputAttributeDescription VertexAttrDesc[3];
		ZeroMem(VertexAttrDesc);
		VertexAttrDesc[0].format = VK_FORMAT_R32G32_SFLOAT;
		VertexAttrDesc[1].offset = IM_OFFSETOF(ImDrawVert, pos);
		VertexAttrDesc[1].format = VK_FORMAT_R32G32_SFLOAT;
		VertexAttrDesc[1].offset = IM_OFFSETOF(ImDrawVert, uv);
		VertexAttrDesc[1].location = 1;
		VertexAttrDesc[2].format = VK_FORMAT_R8G8B8A8_UNORM;
		VertexAttrDesc[2].offset = IM_OFFSETOF(ImDrawVert, col);
		VertexAttrDesc[2].location = 2;

		VkVertexInputBindingDescription VertexBindDesc[1];
		ZeroMem(VertexBindDesc);
		VertexBindDesc[0].stride = sizeof(ImDrawVert);

		App.ImGUIPSO = GPSOCache.CreateGfxPSO(UIVS, UIPS, RenderPass, [&](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
		{
			VkPipelineVertexInputStateCreateInfo* VertexInputInfo = (VkPipelineVertexInputStateCreateInfo*)GfxPipelineInfo.pVertexInputState;
			VertexInputInfo->vertexAttributeDescriptionCount = 3;
			VertexInputInfo->pVertexAttributeDescriptions = VertexAttrDesc;
			VertexInputInfo->vertexBindingDescriptionCount = 1;
			VertexInputInfo->pVertexBindingDescriptions = VertexBindDesc;
		});
	}

	App.TestGLTFPSO = GPSOCache.CreateGfxPSO(TestGLTFVS, TestGLTFPS, RenderPass, [&](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
	{
		if (!App.VertexDecls.empty())
		{
			check(App.VertexDecls.size() == 1);
			FixGLTFVertexDecl(App.VertexDecls[0], TestGLTFVS->Shader);
			VkPipelineVertexInputStateCreateInfo* VertexInputInfo = (VkPipelineVertexInputStateCreateInfo*)GfxPipelineInfo.pVertexInputState;
			VertexInputInfo->vertexAttributeDescriptionCount = (uint32)App.VertexDecls[0].AttrDescs.size();
			VertexInputInfo->pVertexAttributeDescriptions = App.VertexDecls[0].AttrDescs.data();
			VertexInputInfo->vertexBindingDescriptionCount = (uint32)App.VertexDecls[0].BindingDescs.size();
			VertexInputInfo->pVertexBindingDescriptions = App.VertexDecls[0].BindingDescs.data();
		}
	});
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

	GLFWwindow* Window = glfwCreateWindow(ResX, ResY, "VkTest2", 0, 0);
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
	while (!glfwWindowShouldClose(Window))
	{
		double CpuBegin = glfwGetTime() * 1000.0;

		glfwPollEvents();

		double GpuDelta = Render(App);

		double CpuEnd = glfwGetTime() * 1000.0;

		double CpuDelta = CpuEnd - CpuBegin;

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
		App.LastDelta = (float)CpuDelta;
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
