

#pragma once

#define SCENE_USE_SINGLE_BUFFERS	1


struct FVertexBindings
{
	std::vector<VkVertexInputAttributeDescription> AttrDescs;
	std::vector<VkVertexInputBindingDescription> BindingDescs;
	std::vector<std::string> Names;
};


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
#else
		int IndexBuffer;
		VkDeviceSize IndexOffset;
#endif
		std::vector<int> VertexBuffers;
		std::vector<VkDeviceSize> VertexOffsets;
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
					//VB.Destroy();
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
