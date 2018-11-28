// VkTest2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <GLFW/glfw3.h>

// Fix Windows warning
#undef APIENTRY

#include "RCVulkan.h"

#include "imgui.h"


#pragma comment(lib, "glfw3.lib")

static SVulkan GVulkan;


static FShaderLibrary GShaderLibrary;

static FRenderTargetCache GRenderTargetCache;

static FPSOCache GPSOCache;

static FDescriptorCache GDescriptorCache;

struct FApp
{
	SVulkan::FGfxPSO NoVBClipVSRedPSO;
	SVulkan::FGfxPSO DataClipVSRedPSO;
	SVulkan::FGfxPSO DataClipVSColorPSO;
	SVulkan::FGfxPSO VBClipVSRedPSO;
	FBufferWithMem ClipVB;
	VkBufferView ClipVBView;
	FBufferWithMem StagingClipVB;
	FShaderInfo* TestCS = nullptr;
	SVulkan::FComputePSO TestCSPSO;
	FBufferWithMem TestCSBuffer;
	FBufferWithMem TestCSUB;
	FBufferWithMem ColorUB;
	VkBufferView TestCSBufferView;

	const uint32 ImGuiMaxVertices = 1024 * 3;
	const uint32 ImGuiMaxIndices = 1024 * 3;
	FImageWithMem ImGuiFont;
	FBufferWithMem ImGuiVB;
	FBufferWithMem ImGuiIB;
	SVulkan::FGfxPSO ImGUIPSO;

	float LastDelta = 1.0f / 60.0f;

	void Create(SVulkan::SDevice& Device)
	{
		// Dummy stuff
		uint32 ClipVBSize = 3 * 4 * sizeof(float);
		ClipVB.Create(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ClipVBSize);

		{
			StagingClipVB.Create(Device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ClipVBSize);
			float* Data = (float*)StagingClipVB.Lock();
			*Data++ = 0;		*Data++ = -0.5f;	*Data++ = 1; *Data++ = 1;
			*Data++ = -0.5f;	*Data++ = 0.5f;		*Data++ = 1; *Data++ = 1;
			*Data++ = 0.5f;		*Data++ = 0.5f;		*Data++ = 1; *Data++ = 1;
			StagingClipVB.Unlock();
		}

		ClipVBView = Device.CreateBufferView(ClipVB.Buffer, VK_FORMAT_R32G32B32A32_SFLOAT, ClipVBSize);

		ColorUB.Create(Device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 4 * sizeof(float));
		{
			float* Data = (float*)ColorUB.Lock();
			*Data++ = 0.0f;
			*Data++ = 1.0f;
			*Data++ = 0.0f;
			*Data++ = 1.0f;
			ColorUB.Unlock();
		}

		TestCSBuffer.Create(Device, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 256 * 256 * 4 * sizeof(float));
		TestCSBufferView = Device.CreateBufferView(TestCSBuffer.Buffer, VK_FORMAT_R32G32B32A32_SFLOAT, TestCSBuffer.Size / 4);

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
	}

	void Destroy()
	{
		ImGuiIB.Destroy();
		ImGuiVB.Destroy();
		ImGuiFont.Destroy();

		TestCSUB.Destroy();
		vkDestroyBufferView(TestCSBuffer.Buffer.Device, TestCSBufferView, nullptr);
		TestCSBufferView = VK_NULL_HANDLE;
		TestCSBuffer.Destroy();
		vkDestroyBufferView(ClipVB.Buffer.Device, ClipVBView, nullptr);
		ClipVBView = VK_NULL_HANDLE;
		StagingClipVB.Destroy();
		ClipVB.Destroy();
		ColorUB.Destroy();
	}

	void SetupImGui(SVulkan::SDevice& Device)
	{
		ImGuiIO& IO = ImGui::GetIO();

		int32 Width = 0, Height = 0;
		unsigned char* Pixels = nullptr;
		IO.Fonts->GetTexDataAsAlpha8(&Pixels, &Width, &Height);

		FBufferWithMem FontBuffer;
		FontBuffer.Create(Device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Width * Height * sizeof(uint32));

		uint8* Data = (uint8*)FontBuffer.Lock();
		for (int32 Index = 0; Index < Width * Height; ++Index)
		{
			*Data++ = *Pixels;
			*Data++ = *Pixels;
			*Data++ = *Pixels;
			*Data++ = *Pixels;
			++Pixels;
		}
		FontBuffer.Unlock();

		ImGuiFont.Create(Device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Width, Height);

		SVulkan::FCmdBuffer* CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);

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
		vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, FontBuffer.Buffer.Buffer, ImGuiFont.Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

		CmdBuffer->End();
		Device.Submit(Device.GfxQueue, CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_NULL_HANDLE, VK_NULL_HANDLE);
		CmdBuffer->Fence.Wait(20 * 1000);

		IO.Fonts->TexID = (void*)ImGuiFont.Image.Image;

		FontBuffer.Destroy();

		const uint32 ImGuiVertexSize = sizeof(ImDrawVert) * ImGuiMaxVertices;
		ImGuiVB.Create(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ImGuiVertexSize);

		static_assert(sizeof(uint16) == sizeof(ImDrawIdx), "");
		ImGuiIB.Create(Device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ImGuiMaxIndices * sizeof(uint16));
	}

	void DrawDataImGui(ImDrawData* DrawData, SVulkan::FCmdBuffer* CmdBuffer, SVulkan::FFramebuffer* Framebuffer)
	{
		if (DrawData->CmdListsCount > 0)
		{
			uint32 NumVertices = 0;
			uint32 NumIndices = 0;
			ImDrawVert* DestVBData = (ImDrawVert*)ImGuiVB.Lock();
			uint16* DestIBData = (uint16*)ImGuiIB.Lock();
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

				NumVertices += CmdList->VtxBuffer.Size;
				NumIndices += CmdList->IdxBuffer.Size;
			}

			ImGuiIB.Unlock();
			ImGuiVB.Unlock();

			CmdBuffer->BeginRenderPass(Framebuffer);
			vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ImGUIPSO.Pipeline);
			vkCmdBindIndexBuffer(CmdBuffer->CmdBuffer, ImGuiIB.Buffer.Buffer, 0, VK_INDEX_TYPE_UINT16);
			VkDeviceSize Zero = 0;
			vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &ImGuiVB.Buffer.Buffer, &Zero);

			int VertexOffset = 0;
			int IndexOffset = 0;
			for (int n = 0; n < DrawData->CmdListsCount; n++)
			{
				const ImDrawList* CmdList = DrawData->CmdLists[n];
				for (int Index = 0; Index < CmdList->CmdBuffer.Size; Index++)
				{
					const ImDrawCmd* Cmd = &CmdList->CmdBuffer[Index];
					vkCmdDrawIndexed(CmdBuffer->CmdBuffer, Cmd->ElemCount, 1, IndexOffset, VertexOffset, 0);
					IndexOffset += Cmd->ElemCount;
				}
				VertexOffset += CmdList->VtxBuffer.Size;
			}
			CmdBuffer->EndRenderPass();
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
		VkBufferCopy Region;
		ZeroMem(Region);
		Region.size = App.ClipVB.Size;
		vkCmdCopyBuffer(CmdBuffer->CmdBuffer, App.StagingClipVB.Buffer.Buffer, App.ClipVB.Buffer.Buffer, 1, &Region);
		bFirst = false;

		App.SetupImGui(Device);
	}

	ImGui::NewFrame();

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
	else if (1)
	{
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.DataClipVSColorPSO.Pipeline);

		VkWriteDescriptorSet DescriptorWrites[2];
		ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[0].descriptorCount = 1;
		DescriptorWrites[0].descriptorType = (VkDescriptorType)App.DataClipVSColorPSO.VS[0]->bindings[0]->descriptor_type;
		DescriptorWrites[0].pTexelBufferView = &App.ClipVBView;
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
		DescriptorWrites.pTexelBufferView = &App.ClipVBView;
		DescriptorWrites.dstBinding = App.DataClipVSRedPSO.VS[0]->bindings[0]->binding;

		GDescriptorCache.UpdateDescriptors(CmdBuffer, 1, &DescriptorWrites, App.DataClipVSRedPSO);
	}
	else if (1)
	{
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, App.VBClipVSRedPSO.Pipeline);
		VkDeviceSize Offsets = 0;
		vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &App.ClipVB.Buffer.Buffer, &Offsets);
	}
	vkCmdDraw(CmdBuffer->CmdBuffer, 3, 1, 0, 0);

	CmdBuffer->EndRenderPass();

	{
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, App.TestCSPSO.Pipeline);

		VkWriteDescriptorSet DescriptorWrites[2];
		ZeroVulkanMem(DescriptorWrites[0], VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
		DescriptorWrites[0].descriptorCount = 1;
		DescriptorWrites[0].descriptorType = (VkDescriptorType)App.TestCSPSO.CS[0]->bindings[0]->descriptor_type;
		DescriptorWrites[0].pTexelBufferView = &App.TestCSBufferView;
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

	if (ImGui::Begin("Hello, world!"))
	{
		ImGui::Text("test...");
		ImGui::End();
	}

	ImGui::Render();

	ImDrawData* DrawData = ImGui::GetDrawData();
	App.DrawDataImGui(DrawData, CmdBuffer, Framebuffer);

	Device.TransitionImage(CmdBuffer, GVulkan.Swapchain.Images[GVulkan.Swapchain.ImageIndex],
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0,
		VK_IMAGE_ASPECT_COLOR_BIT);
	CmdBuffer->End();


	Device.Submit(Device.PresentQueue, CmdBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, GVulkan.Swapchain.AcquireBackbufferSemaphore, GVulkan.Swapchain.FinalSemaphore);

	GVulkan.Swapchain.Present(Device.PresentQueue, GVulkan.Swapchain.FinalSemaphore);
	//Device.WaitForFence(CmdBuffer.Fence, CmdBuffer.LastSubmittedFence);

	double GpuTimeMS = Device.ReadTimestamp();
	return GpuTimeMS;
}


static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
}

static void SetupShaders(FApp& App)
{
	App.TestCS = GShaderLibrary.RegisterShader("Shaders/TestCS.hlsl", "TestCS", FShaderInfo::EStage::Compute);
	FShaderInfo* VBClipVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "VBClipVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* NoVBClipVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "MainNoVBClipVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* DataClipVS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "MainBufferClipVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* RedPS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "RedPS", FShaderInfo::EStage::Pixel);
	FShaderInfo* ColorPS = GShaderLibrary.RegisterShader("Shaders/Unlit.hlsl", "ColorPS", FShaderInfo::EStage::Pixel);
	FShaderInfo* UIVS = GShaderLibrary.RegisterShader("Shaders/UI.hlsl", "UIMainVS", FShaderInfo::EStage::Vertex);
	FShaderInfo* UIPS = GShaderLibrary.RegisterShader("Shaders/UI.hlsl", "UIMainPS", FShaderInfo::EStage::Pixel);
	GShaderLibrary.RecompileShaders();

	App.TestCSPSO = GPSOCache.CreateComputePSO(App.TestCS);

	SVulkan::FRenderPass* RenderPass = GRenderTargetCache.GetOrCreateRenderPass(GVulkan.Swapchain.Format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);

	VkViewport Viewport = GVulkan.Swapchain.GetViewport();
	VkRect2D Scissor = GVulkan.Swapchain.GetScissor();

	App.DataClipVSColorPSO = GPSOCache.CreateGfxPSO(DataClipVS, ColorPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
	{
		VkPipelineViewportStateCreateInfo* ViewportInfo = (VkPipelineViewportStateCreateInfo*)GfxPipelineInfo.pViewportState;
		ViewportInfo->viewportCount = 1;
		ViewportInfo->pViewports = &Viewport;
		ViewportInfo->scissorCount = 1;
		ViewportInfo->pScissors = &Scissor;
	});

	App.NoVBClipVSRedPSO = GPSOCache.CreateGfxPSO(NoVBClipVS, RedPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
	{
		VkPipelineViewportStateCreateInfo* ViewportInfo = (VkPipelineViewportStateCreateInfo*)GfxPipelineInfo.pViewportState;
		ViewportInfo->viewportCount = 1;
		ViewportInfo->pViewports = &Viewport;
		ViewportInfo->scissorCount = 1;
		ViewportInfo->pScissors = &Scissor;
	});

	App.DataClipVSRedPSO = GPSOCache.CreateGfxPSO(DataClipVS, RedPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
	{
		VkPipelineViewportStateCreateInfo* ViewportInfo = (VkPipelineViewportStateCreateInfo*)GfxPipelineInfo.pViewportState;
		ViewportInfo->viewportCount = 1;
		ViewportInfo->pViewports = &Viewport;
		ViewportInfo->scissorCount = 1;
		ViewportInfo->pScissors = &Scissor;
	});

	{
		VkVertexInputAttributeDescription VertexAttrDesc;
		ZeroMem(VertexAttrDesc);
		VertexAttrDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		VkVertexInputBindingDescription VertexBindDesc;
		ZeroMem(VertexBindDesc);
		VertexBindDesc.stride = 4 * sizeof(float);
		App.VBClipVSRedPSO = GPSOCache.CreateGfxPSO(VBClipVS, RedPS, RenderPass, [=](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
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

	{
		VkVertexInputAttributeDescription VertexAttrDesc[3];
		ZeroMem(VertexAttrDesc);
		VertexAttrDesc[0].format = VK_FORMAT_R32G32_SFLOAT;
		VertexAttrDesc[1].format = VK_FORMAT_R32G32_SFLOAT;
		VertexAttrDesc[1].offset = 2 * sizeof(float);
		VertexAttrDesc[1].location = 1;
		VertexAttrDesc[2].format = VK_FORMAT_R32_UINT;
		VertexAttrDesc[2].offset = VertexAttrDesc[1].offset + 2 * sizeof(float);
		VertexAttrDesc[2].location = 2;

		VkVertexInputBindingDescription VertexBindDesc[1];
		ZeroMem(VertexBindDesc);
		VertexBindDesc[0].stride = 2 * sizeof(float) + sizeof(uint32);

		App.ImGUIPSO = GPSOCache.CreateGfxPSO(UIVS, UIPS, RenderPass, [&](VkGraphicsPipelineCreateInfo& GfxPipelineInfo)
		{
			VkPipelineViewportStateCreateInfo* ViewportInfo = (VkPipelineViewportStateCreateInfo*)GfxPipelineInfo.pViewportState;
			ViewportInfo->viewportCount = 1;
			ViewportInfo->pViewports = &Viewport;
			ViewportInfo->scissorCount = 1;
			ViewportInfo->pScissors = &Scissor;

			VkPipelineVertexInputStateCreateInfo* VertexInputInfo = (VkPipelineVertexInputStateCreateInfo*)GfxPipelineInfo.pVertexInputState;
			VertexInputInfo->vertexAttributeDescriptionCount = 3;
			VertexInputInfo->pVertexAttributeDescriptions = VertexAttrDesc;
			VertexInputInfo->vertexBindingDescriptionCount = 1;
			VertexInputInfo->pVertexBindingDescriptions = VertexBindDesc;
		});
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
	GPSOCache.Init(&Device);
	GDescriptorCache.Init(&Device);

	glfwSetKeyCallback(Window, KeyCallback);

	SetupShaders(App);

	App.Create(Device);


	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& IO = ImGui::GetIO();
//	IO.ImeWindowHandle = ::Gethand;

	return Window;
}

static void Deinit(FApp& App, GLFWwindow* Window)
{
	GVulkan.DeinitPre();

	ImGui::DestroyContext();

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
