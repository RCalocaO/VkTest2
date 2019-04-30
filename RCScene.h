

#pragma once

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
