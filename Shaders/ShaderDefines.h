// Common file between C++ & hlsl

#ifndef HLSL
#define HLSL	0
#endif


#define VIEW_ENTRY_LIST(ENTRY)	\
ENTRY(0, "Default (Normal Mapping Lit)",	MODE_NORMAL_MAP_LIT) \
ENTRY(1, "Show Vertex Normals",				MODE_SHOW_VERTEX_NORMALS) \
ENTRY(2, "Show Vertex Tangents",			MODE_SHOW_VERTEX_TANGENT) \
ENTRY(3, "Show Vertex Binormals",			MODE_SHOW_VERTEX_BITANGENT) \
ENTRY(4, "Vertex Normal Lit",				MODE_VERTEX_NORMAL_LIT) \
ENTRY(5, "Diffuse Texture",					MODE_SHOWTEX_DIFFUSE) \
ENTRY(6, "NormalMap Texture",				MODE_SHOWTEX_NORMALMAP) \
ENTRY(7, "Show Pixel Normals",				MODE_SHOW_PIXEL_NORMALS) \
ENTRY(8, "Show Roughness",					MODE_SHOW_ROUGHNESS) \
ENTRY(9, "Show Metallic",					MODE_SHOW_METALLIC)
