

#pragma once

#include "../RCUtils/RCUtilsMath.h"

#define SCENE_USE_SINGLE_BUFFERS	1


struct FScene
{
	std::vector<FBufferWithMem> Buffers;
	std::vector<FImageWithMemAndView> Images;
	//std::vector<VkVertexInputAttributeDescription> AttrDescs;
	//std::vector<VkVertexInputBindingDescription> BindingDescs;

	struct FPrim
	{
#if SCENE_USE_SINGLE_BUFFERS
		FBufferWithMem IndexBuffer;
		std::vector<FBufferWithMem> VertexBuffers;
#else
		int IndexBuffer;
		VkDeviceSize IndexOffset;
		std::vector<int> VertexBuffers;
		std::vector<VkDeviceSize> VertexOffsets;
#endif
		uint32 NumIndices;
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

	struct FInstance
	{
		FVector4 Pos =  {0, 0, 0, 1};
		uint32 Mesh = 0;
	};

	std::vector<FInstance> Instances;

	void Destroy()
	{
#if SCENE_USE_SINGLE_BUFFERS
		for (auto& Mesh : Meshes)
		{
			for (auto& Prim : Mesh.Prims)
			{
				Prim.IndexBuffer.Destroy();
				for (auto& VB : Prim.VertexBuffers)
				{
//					VB.Destroy();
				}
			}
		}
#endif
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
