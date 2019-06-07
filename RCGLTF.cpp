

#include "VkTest2.h"
#include "RCVulkan.h"
#include "RCScene.h"


#pragma warning(push)
#pragma warning(disable:4267)
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../tinygltf/tiny_gltf.h"
#pragma warning(pop)


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

int GetOrAddVertexDecl(tinygltf::Model& Model, tinygltf::Primitive& GLTFPrim, FScene::FPrim& OutPrim, std::vector<FPSOCache::FVertexDecl>& OutVertexDecls)
{
	FPSOCache::FVertexDecl VertexDecl;
	uint32 BindingIndex = 0;
	for (auto Pair : GLTFPrim.attributes)
	{
		std::string Name = Pair.first;
		tinygltf::Accessor& Accessor = Model.accessors[Pair.second];

		tinygltf::BufferView& BufferView = Model.bufferViews[Accessor.bufferView];
		OutPrim.VertexOffsets.push_back(BufferView.byteOffset + Accessor.byteOffset);
		OutPrim.VertexBuffers.push_back(BufferView.buffer);

		VertexDecl.AddAttribute(BindingIndex, BindingIndex, GetFormat(Accessor.componentType, Accessor.type), 0, Name.c_str());
		VertexDecl.AddStream(BindingIndex, (uint32)BufferView.byteStride);

		++BindingIndex;
	}

/*
	bUseColorStream = (GLTFPrim.attributes.find("COLOR_0") != GLTFPrim.attributes.end());
	bHasNormals = (GLTFPrim.attributes.find("NORMAL") != GLTFPrim.attributes.end());
	bHasTexCoords = (GLTFPrim.attributes.find("TEXCOORD_0") != GLTFPrim.attributes.end());
*/

	OutVertexDecls.push_back(VertexDecl);

	return 0;
}


bool LoadGLTF(SVulkan::SDevice& Device, const char* Filename, std::vector<FPSOCache::FVertexDecl>& VertexDecls, FScene& Scene, FPendingOpsManager& PendingStagingOps, FStagingBufferManager* StagingMgr)
{
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

				Prim.VertexDecl = GetOrAddVertexDecl(Model, GLTFPrim, Prim, VertexDecls);
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
			Buffer.Create(Device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, Size, true);
			float* Data = (float*)Buffer.Lock();
			memcpy(Data, GLTFBuffer.data.data(), Size);
			Buffer.Unlock();
			Scene.Buffers.push_back(Buffer);
		}

		//AddDefaultVertexInputs(Device);

		for (tinygltf::Image& GLTFImage : Model.images)
		{
			check(!GLTFImage.as_is);
			check(GLTFImage.bufferView == -1);
			check(!GLTFImage.image.empty());
			{
				FImageWithMemAndView Image;
				Image.Create(Device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, EMemLocation::GPU, GLTFImage.width, GLTFImage.height, VK_FORMAT_R8G8B8A8_UNORM);

				SVulkan::FCmdBuffer* CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);

				uint32 Size = GLTFImage.width * GLTFImage.height * sizeof(uint32);
				FStagingBuffer* TempBuffer = StagingMgr->AcquireBuffer(Size, CmdBuffer);

				uint8* Data = (uint8*)TempBuffer->Buffer->Lock();
				memcpy(Data, GLTFImage.image.data(), Size);
				TempBuffer->Buffer->Unlock();

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
				vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, TempBuffer->Buffer->Buffer.Buffer, Image.Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

				Device.TransitionImage(CmdBuffer, Image.Image.Image,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT);

				CmdBuffer->End();
				Device.Submit(Device.GfxQueue, CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_NULL_HANDLE, VK_NULL_HANDLE);

				//StagingMgr->ReleaseBuffer(TempBuffer);

				Scene.Images.push_back(Image);
			}
		}
	}

	return true;
}
