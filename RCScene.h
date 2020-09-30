

#pragma once

#include "../RCUtils/RCUtilsMath.h"

#define SCENE_USE_SINGLE_BUFFERS	1


struct FBoundingBox
{
	FVector3 Min = { FLT_MAX, FLT_MAX, FLT_MAX };
	FVector3 Max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
};

struct FScene
{
	std::vector<FBufferWithMem> Buffers;
	//std::vector<VkVertexInputAttributeDescription> AttrDescs;
	//std::vector<VkVertexInputBindingDescription> BindingDescs;

	struct FPrim
	{
		uint32 ID = ~0;
#if SCENE_USE_SINGLE_BUFFERS
		FBufferWithMem IndexBuffer;
		std::vector<FBufferWithMem> VertexBuffers;
#else
		int IndexBuffer;
		VkDeviceSize IndexOffset;
		std::vector<int> VertexBuffers;
		std::vector<VkDeviceSize> VertexOffsets;
#endif
		uint32 NumIndices = 0;
		VkIndexType IndexType = VK_INDEX_TYPE_UINT32;
		VkPrimitiveTopology PrimType = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;;
		int Material = -1;
		int VertexDecl = -1;

		FBoundingBox ObjectSpaceBounds;
	};

	struct FMesh
	{
		std::vector<FPrim> Prims;
	};

	std::vector<FMesh> Meshes;

	struct FInstance
	{
		uint32 ID;
		FInstance(uint32 InID)
			: ID(InID)
		{
		}

		FVector4 Pos =  {0, 0, 0, 1};
		FVector3 Scale = {1, 1, 1};
		FVector3 Rotation = {0, 0, 0};
		uint32 Mesh = 0;
	};

	std::vector<FInstance> Instances;

	struct FMaterial
	{
		std::string Name;
		int32 BaseColor = -1;
		int32 Normal = -1;
		int32 MetallicRoughness = -1;
		bool bDoubleSided = false;
	};
	std::vector<FMaterial> Materials;

	struct FTexture
	{
		FImageWithMemAndView Image;

		VkImage GetImage()
		{
			return Image.Image.Image;
		}
	};
	std::vector<FTexture> Textures;

	void Destroy()
	{
#if SCENE_USE_SINGLE_BUFFERS
		//std::set<VkBuffer> Deleted;
		for (auto& Mesh : Meshes)
		{
			for (auto& Prim : Mesh.Prims)
			{
				Prim.IndexBuffer.Destroy();
				for (auto& VB : Prim.VertexBuffers)
				{
					// Hack to not refcount
					//if (Deleted.end() == Deleted.find(VB.Buffer.Buffer))
					{
						VB.Destroy();
						//Deleted.insert(VB.Buffer.Buffer);
					}
				}
			}
		}
#endif
		for (auto& Buffer : Buffers)
		{
			Buffer.Destroy();
		}

		for (auto& Texture : Textures)
		{
			Texture.Image.Destroy();
		}
	}
};
