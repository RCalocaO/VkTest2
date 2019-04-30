

#include "RCVulkan.h"
#include "VkTest2.h"
#include "RCScene.h"

#define	USE_TINY_GLTF			0
#define	USE_CGLTF				1


#if USE_TINY_GLTF
#pragma warning(push)
#pragma warning(disable:4267)
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../tinygltf/tiny_gltf.h"
#pragma warning(pop)
#endif

#if USE_CGLTF
#define CGLTF_IMPLEMENTATION
#include "../cgltf/cgltf.h"
#endif


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

int GetOrAddVertexDecl(cgltf_data* Data, cgltf_primitive& GLTFPrim, FScene::FPrim& OutPrim, std::vector<FVertexBindings>& OutVertexDecls)
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

	OutVertexDecls.push_back(VertexDecl);

	return 0;
}

#endif

bool LoadGLTF(SVulkan::SDevice& Device, const char* Filename, std::vector<FVertexBindings>& VertexDecls, FScene& Scene)
{
#if USE_CGLTF
	cgltf_options Options;
	ZeroMem(Options);
	cgltf_data* SrcData = nullptr;
	cgltf_result Result = cgltf_parse_file(&Options, Filename, &SrcData);
	if (Result == cgltf_result_success)
	{
		Result = cgltf_load_buffers(&Options, SrcData, nullptr);
		if (Result == cgltf_result_success)
		{
			for (cgltf_size BufferIndex = 0; BufferIndex < SrcData->buffers_count; ++BufferIndex)
			{
				cgltf_buffer& GLTFBuffer = SrcData->buffers[BufferIndex];
				FBufferWithMem Buffer;
				uint32 Size = (uint32)GLTFBuffer.size;
#if USE_VMA
				Buffer.Create(Device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, Size);
#else
				Buffer.Create(Device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, Size);
#endif
				float* Data = (float*)Buffer.Lock();
				memcpy(Data, GLTFBuffer.data, Size);
				Buffer.Unlock();
				Scene.Buffers.push_back(Buffer);
			}

			for (cgltf_size MeshIndex = 0; MeshIndex < SrcData->meshes_count; ++MeshIndex)
			{
				cgltf_mesh& GLTFMesh = SrcData->meshes[MeshIndex];
				FScene::FMesh Mesh;
				for (cgltf_size PrimIndex = 0; PrimIndex < GLTFMesh.primitives_count; ++PrimIndex)
				{
					cgltf_primitive& GLTFPrim = GLTFMesh.primitives[PrimIndex];
					FScene::FPrim Prim;
					cgltf_accessor& Indices = *GLTFPrim.indices;
					check(Indices.type == cgltf_type_scalar);
					cgltf_buffer_view& IndicesBufferView = *Indices.buffer_view;
					Prim.VertexDecl = GetOrAddVertexDecl(SrcData, GLTFPrim, Prim, VertexDecls);
					//Prim.Material = GLTFPrim.material;
					Prim.PrimType = GetPrimType(GLTFPrim.type);
					Prim.IndexOffset = Indices.offset + IndicesBufferView.offset;
					Prim.NumIndices = (uint32)IndicesBufferView.size / GetSizeInBytes(Indices.component_type);
					Prim.IndexBuffer = FindBuffer(SrcData, IndicesBufferView.buffer);
					Prim.IndexType = GetIndexType(Indices.component_type);
					Mesh.Prims.push_back(Prim);
				}

				Scene.Meshes.push_back(Mesh);
			}

			//AddDefaultVertexInputs(Device);

			for (cgltf_size ImageIndex = 0; ImageIndex < SrcData->images_count; ++ImageIndex)
			{
				cgltf_image& GLTFImage = SrcData->images[ImageIndex];

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

		cgltf_free(SrcData);
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
	}
#endif

	return true;
}
