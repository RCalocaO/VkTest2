
#include "pch.h"

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

#if USE_VMA
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include "../VulkanMemoryAllocator/src/vk_mem_alloc.h"
#endif
