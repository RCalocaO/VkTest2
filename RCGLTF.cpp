

#include "VkTest2.h"

#ifdef _DEBUG
#pragma optimize( "gt", on)
#endif

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

static int GetOrAddVertexDecl(SVulkan::SDevice& Device, tinygltf::Model& Model, tinygltf::Primitive& GLTFPrim, FScene::FPrim& OutPrim, FPSOCache& PSOCache)
{
	FPSOCache::FVertexDecl VertexDecl;
	uint32 BindingIndex = 0;
	uint32 MaxSize = 0;
	for (auto Pair : GLTFPrim.attributes)
	{
		std::string Name = Pair.first;
		tinygltf::Accessor& Accessor = Model.accessors[Pair.second];

		tinygltf::BufferView& BufferView = Model.bufferViews[Accessor.bufferView];

		auto FixNormalOrPosition = [&](const std::string& InName, uint32 Count, FVector3* Data)
		{
			bool bPosition = InName == "POSITION";
			if (bPosition)
			{
				for (uint32 Index = 0; Index < Count; ++Index)
				{
					OutPrim.ObjectSpaceBounds.Min = FVector3::Min(OutPrim.ObjectSpaceBounds.Min, *Data);
					OutPrim.ObjectSpaceBounds.Max = FVector3::Max(OutPrim.ObjectSpaceBounds.Max, *Data);
					++Data;
				}
			}
		};
#if SCENE_USE_SINGLE_BUFFERS
		VertexDecl.AddAttribute(BindingIndex, BindingIndex, GetFormat(Accessor.componentType, Accessor.type), 0, Name.c_str());

		uint32 Size = (uint32)BufferView.byteLength;
		MaxSize = MaxSize > Size ? MaxSize : Size;
		OutPrim.VertexBuffers.push_back(FBufferWithMem());
		FBufferWithMem& VB = OutPrim.VertexBuffers.back();
		VB.Create(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, Size, true);
		{
			unsigned char* SrcData = (unsigned char*)Model.buffers[BufferView.buffer].data.data();
			SrcData += BufferView.byteOffset + Accessor.byteOffset;
			float* DestData = (float*)VB.Lock();
			memcpy(DestData, SrcData, Size);
			FixNormalOrPosition(Name, (uint32)Accessor.count, (FVector3*)DestData);
			VB.Unlock();
		}
		Device.SetDebugName(VB.Buffer.Buffer, "GLTFVB");
#else
		OutPrim.VertexOffsets.push_back(BufferView.byteOffset + Accessor.byteOffset);
		OutPrim.VertexBuffers.push_back(BufferView.buffer);
		VertexDecl.AddAttribute(BindingIndex, UINT32_MAX, GetFormat(Accessor.componentType, Accessor.type), 0, Name.c_str());
#endif
		uint32 Stride = BufferView.byteStride == 0 ? (uint32)(BufferView.byteLength / Accessor.count) : (uint32)BufferView.byteStride;
		check(Stride <= 256);
		VertexDecl.AddBinding(BindingIndex, Stride);

		++BindingIndex;
	}

	auto AddDummyStream = [&](const char* Semantic, VkFormat Format, uint8* Values, uint8 NumComponents)
	{
		if (GLTFPrim.attributes.find(Semantic) == GLTFPrim.attributes.end())
		{
#if SCENE_USE_SINGLE_BUFFERS
			VertexDecl.AddAttribute(BindingIndex, BindingIndex, Format, 0, Semantic);
/*
			if (Device.bUseVertexDivisor)
			{
				OutPrim.VertexBuffers.push_back(PSOCache.ZeroBuffer);
				VertexDecl.AddBinding(BindingIndex, PSOCache.ZeroBuffer.Size, true);
				VertexDecl.Divisors.push_back({ BindingIndex, 0 });
			}
			else
*/
			{
				OutPrim.VertexBuffers.push_back(FBufferWithMem());
				FBufferWithMem& VB = OutPrim.VertexBuffers.back();
				VB.Create(Device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, MaxSize, true);
				{
					uint8* DestData = (uint8*)VB.Lock();
					check(NumComponents > 0);
					for (uint32 N = 0; N < MaxSize; N += NumComponents)
					{
						for (uint8 C = 0; C < NumComponents; ++C)
						{
							*DestData++ = Values[C];
						}
					}
					VB.Unlock();
				}
				Device.SetDebugName(VB.Buffer.Buffer, "GLTFVB");
				VertexDecl.AddBinding(BindingIndex, 16, true);
			}
			++BindingIndex;
#else
#error
#endif
		}
	};

	uint8 NormalValue[] = {0, 0, 255};
	uint8 TangentValue[] = {0, 255, 0};
	uint8 TexCoordValue[] = {0, 0};
	uint8 ColorValue[] = {255, 255, 255, 255};
	AddDummyStream("NORMAL", VK_FORMAT_R8G8B8_UNORM, NormalValue, 3);
	AddDummyStream("TANGENT", VK_FORMAT_R8G8B8A8_UNORM, TangentValue, 3);
	AddDummyStream("TEXCOORD_0", VK_FORMAT_R8G8_UNORM, TexCoordValue, 2);
	AddDummyStream("COLOR", VK_FORMAT_R8G8B8A8_UNORM, ColorValue, 4);

	return PSOCache.FindOrAddVertexDecl(VertexDecl);
}

struct FGLTFLoader
{
	tinygltf::TinyGLTF Loader;
	tinygltf::Model Model;
	std::string Error;
	std::string Warnings;
	std::string Filename;

	std::atomic<bool> bFinishedLoading = false;
};

const char* GetGLTFFilename(FGLTFLoader* Loader)
{
	return Loader ? Loader->Filename.c_str() : nullptr;
}

FGLTFLoader* CreateGLTFLoader(const char* Filename)
{
	FGLTFLoader* Loader = new FGLTFLoader;
	Loader->Filename = Filename;
	//Loader->bFinishedLoading = false;
	if (Loader->Loader.LoadASCIIFromFile(&Loader->Model, &Loader->Error, &Loader->Warnings, Filename))
	{
		Loader->bFinishedLoading = true;
		return Loader;
	}
	
	delete Loader;
	return nullptr;
}

bool IsGLTFLoaderFinished(FGLTFLoader* Loader)
{
	return Loader->bFinishedLoading;
}

void FreeGLTFLoader(FGLTFLoader* Loader)
{
	if (Loader)
	{
		delete Loader;
	}
}

//double GetTimeInMs();

void CreateGLTFGfxResources(FGLTFLoader* Loader, SVulkan::SDevice& Device, FPSOCache& PSOCache, FScene& Scene, FPendingOpsManager& PendingStagingOps, FStagingBufferManager* StagingMgr)
{
	//double Begin = GetTimeInMs();
	//bool bLoaded = Loader->Loader.LoadASCIIFromFile(&Model, &Error, &Warnings, Filename);
	//double End = GetTimeInMs();
	//double Delta = End - Begin;
	//char s[128];
	//sprintf(s, "*** tinyGLTF Time %f\n", (float)Delta);
	//::OutputDebugStringA(s);
	//Begin = GetTimeInMs();
	if (Loader)
	{
		auto FindTextureValueDouble = [](tinygltf::Material& GLTFMaterial, const char* Name, bool bIsAdditional) -> double
		{
			auto& Values = bIsAdditional ? GLTFMaterial.additionalValues : GLTFMaterial.values;
			auto Found = Values.find(Name);
			return Found !=Values.end()
				? Found->second.json_double_value["index"]
				: -1.0;
		};
		auto FindTextureValueBool = [](tinygltf::Material& GLTFMaterial, const char* Name, bool bIsAdditional)
		{
			auto& Values = bIsAdditional ? GLTFMaterial.additionalValues : GLTFMaterial.values;
			auto Found = Values.find(Name);
			return Found != Values.end()
				? Found->second.bool_value
				: false;
		};

		for (tinygltf::Material& GLTFMaterial : Loader->Model.materials)
		{
			FScene::FMaterial Mtl;
			Mtl.Name = GLTFMaterial.name;
			Mtl.BaseColor = (int32)FindTextureValueDouble(GLTFMaterial, "baseColorTexture", false);
			Mtl.Normal = (int32)FindTextureValueDouble(GLTFMaterial, "normalTexture", true);
			Mtl.bDoubleSided = FindTextureValueBool(GLTFMaterial, "doubleSided", true);
			Mtl.MetallicRoughness = (int32)FindTextureValueDouble(GLTFMaterial, "metallicRoughnessTexture", false);

			Scene.Materials.push_back(Mtl);
		}

		for (tinygltf::Mesh& GLTFMesh : Loader->Model.meshes)
		{
			FScene::FMesh Mesh;
			for (tinygltf::Primitive& GLTFPrim : GLTFMesh.primitives)
			{
				FScene::FPrim Prim;
				tinygltf::Accessor& Indices = Loader->Model.accessors[GLTFPrim.indices];
				check(Indices.type == TINYGLTF_TYPE_SCALAR);

				tinygltf::BufferView& IndicesBufferView = Loader->Model.bufferViews[Indices.bufferView];
				Prim.VertexDecl = GetOrAddVertexDecl(Device, Loader->Model, GLTFPrim, Prim, PSOCache);
				Prim.Material = GLTFPrim.material;
				Prim.PrimType = GetPrimType(GLTFPrim.mode);
				Prim.NumIndices = (uint32)IndicesBufferView.byteLength / GetSizeInBytes(Indices.componentType);
#if SCENE_USE_SINGLE_BUFFERS
				uint32 Size = (uint32)IndicesBufferView.byteLength;
				Prim.IndexBuffer.Create(Device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, Size, true);
				{
					unsigned char* SrcData = Loader->Model.buffers[IndicesBufferView.buffer].data.data();
					SrcData += IndicesBufferView.byteOffset + Indices.byteOffset;
					unsigned short* DestData = (unsigned short*)Prim.IndexBuffer.Lock();
					memcpy(DestData, SrcData, Size);
					Prim.IndexBuffer.Unlock();
				}
				Device.SetDebugName(Prim.IndexBuffer.Buffer.Buffer, "GLTFIB");
#else
				Prim.IndexOffset = Indices.byteOffset + IndicesBufferView.byteOffset;
				Prim.IndexBuffer = IndicesBufferView.buffer;
#endif
				Prim.IndexType = GetIndexType(Indices.componentType);

				static uint32 ID = 0;
				Prim.ID = ID;
				++ID;

				Mesh.Prims.push_back(Prim);
			}

			Scene.Meshes.push_back(Mesh);
		}

#if !SCENE_USE_SINGLE_BUFFERS
		//#todo Flip Y
		check(Model.buffers.size() == 1);
		for (tinygltf::Buffer& GLTFBuffer : Loader->Model.buffers)
		{
			FBufferWithMem Buffer;
			uint32 Size = (uint32)GLTFBuffer.data.size();
			Buffer.Create(Device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, EMemLocation::CPU_TO_GPU, Size, true);
			float* Data = (float*)Buffer.Lock();
			memcpy(Data, GLTFBuffer.data.data(), Size);
			Buffer.Unlock();
			Device.SetDebugName(Buffer.Buffer.Buffer, "GLTFBuffer");
			Scene.Buffers.push_back(Buffer);
		}
#endif
		//AddDefaultVertexInputs(Device);

		for (tinygltf::Image& GLTFImage : Loader->Model.images)
		{
			check(!GLTFImage.as_is);
			check(GLTFImage.bufferView == -1);
			check(!GLTFImage.image.empty());
			{
				FScene::FTexture Texture;
				Texture.Image.Create(Device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, EMemLocation::GPU, GLTFImage.width, GLTFImage.height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, GetNumMips(GLTFImage.width, GLTFImage.height));
				Device.SetDebugName(Texture.Image.Image.Image, "GLTFTexture");

				SVulkan::FCmdBuffer* CmdBuffer = Device.BeginCommandBuffer(Device.GfxQueueIndex);

				uint32 Size = GLTFImage.width * GLTFImage.height * sizeof(uint32);
				FStagingBuffer* TempBuffer = StagingMgr->AcquireBuffer(Size, CmdBuffer);

				uint8* Data = (uint8*)TempBuffer->Buffer->Lock();
				memcpy(Data, GLTFImage.image.data(), Size);
				TempBuffer->Buffer->Unlock();

				Device.TransitionImage(CmdBuffer, Texture.GetImage(),
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
				vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, TempBuffer->Buffer->Buffer.Buffer, Texture.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

				// All mips in DST
				uint32 RemainingMips = Texture.Image.Image.NumMips;
				uint32 Width = GLTFImage.width;
				uint32 Height = GLTFImage.height;
				for (uint32 Mip = 1; Mip < Texture.Image.Image.NumMips; ++Mip)
				{
					// Prev mip to SRC
					Device.TransitionImage(CmdBuffer, Texture.GetImage(),
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
						VK_IMAGE_ASPECT_COLOR_BIT, Mip - 1, 1);
					VkImageBlit Region;
					ZeroMem(Region);
					Region.srcOffsets[1].x = Width;
					Region.srcOffsets[1].y = Height;
					Region.srcOffsets[1].z = 1;
					Width = Max(Width >> 1, 1u);
					Height = Max(Height >> 1, 1u);
					Region.dstOffsets[1].x = Width;
					Region.dstOffsets[1].y = Height;
					Region.dstOffsets[1].z = 1;
					Region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					Region.srcSubresource.mipLevel = Mip - 1;
					Region.srcSubresource.layerCount = 1;
					Region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					Region.dstSubresource.mipLevel = Mip;
					Region.dstSubresource.layerCount = 1;
					vkCmdBlitImage(CmdBuffer->CmdBuffer, Texture.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						Texture.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region, VK_FILTER_LINEAR);
/*
					// Test mips cleared to different colors
					VkImageSubresourceRange Range = {};
					Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					Range.baseMipLevel = Mip;
					Range.layerCount = 1;
					Range.levelCount = 1;
					VkClearColorValue Color;
					Color.float32[0] = (Mip & 4) ? 1 : 0;
					Color.float32[1] = (Mip & 2) ? 1 : 0;
					Color.float32[2] = (Mip & 1) ? 1 : 0;
					Color.float32[3] = 1;
					vkCmdClearColorImage(CmdBuffer->CmdBuffer, Texture.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						&Color, 1, &Range);
*/
					// Prev mip to DST
					Device.TransitionImage(CmdBuffer, Texture.GetImage(),
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
						VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
						VK_IMAGE_ASPECT_COLOR_BIT, Mip - 1, 1);
				}

				// All mips to READ
				Device.TransitionImage(CmdBuffer, Texture.GetImage(),
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT);

				CmdBuffer->End();
				Device.Submit(Device.GfxQueue, CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_NULL_HANDLE, VK_NULL_HANDLE);

				//StagingMgr->ReleaseBuffer(TempBuffer);

				Scene.Textures.push_back(Texture);
			}
		}

		for (tinygltf::Node Node : Loader->Model.nodes)
		{
			if (Node.mesh != -1)
			{
				static uint32 ID = 0;
				FScene::FInstance Instance(ID++);
				Instance.Mesh = Node.mesh;
				if (Node.translation.size() != 0)
				{
					check(Node.translation.size() == 3);
					Instance.Pos.Set((float)Node.translation[0], (float)Node.translation[1], (float)Node.translation[2], 1);
				}

				if (Node.scale.size() != 0)
				{
					check(Node.scale.size() == 3);
					Instance.Scale.Set((float)Node.scale[0], (float)Node.scale[1], (float)Node.scale[2]);
				}

				if (Node.rotation.size() != 0)
				{
					check(Node.rotation.size() == 3);
					Instance.Rotation.Set((float)Node.rotation[0], (float)Node.rotation[1], (float)Node.rotation[2]);
				}
				Scene.Instances.push_back(Instance);
			}
		}

		if (Loader->Model.nodes.size() == 0)
		{
			for (int32 Index = 0; Index < (int32)Scene.Meshes.size(); ++Index)
			{
				static uint32 ID = 0;
				FScene::FInstance Instance(ID++);
				Instance.Mesh = Index;
				Scene.Instances.push_back(Instance);
			}
		}
	}

	//End = GetTimeInMs();
	//Delta = End - Begin;
	//sprintf(s, "*** RC GLTF Time %f\n", (float)Delta);
	//::OutputDebugStringA(s);

	//return true;
}
