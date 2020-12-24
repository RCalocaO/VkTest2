#define HLSL	1

#include "ShaderDefines.h"

#define CONST_ENTRY(Index, String, Enum)	static const int Enum = Index;
VIEW_ENTRY_LIST(CONST_ENTRY)
#undef CONST_ENTRY

SamplerState SS : register(s2);
Texture2D BaseTexture : register(t3);
Texture2D NormalTexture : register(t4);
Texture2D MetallicRoughnessTexture : register(t5);

struct FGLTFVS
{
	// Use GLTF semantic names for easier binding
	float3 POSITION : POSITION;
	float3 NORMAL : NORMAL;
	float4 TANGENT : TANGENT;
	float2 TEXCOORD_0 : TEXCOORD_0;
	float4 COLOR_0 : COLOR;
};

struct FGLTFPS
{
	float4 ClipPos : SV_POSITION;
	float3 Normal : NORMAL;
	float3 Tangent : TANGENT;
	float3 BiTangent : BITANGENT;
	float2 UV0 : TEXCOORD0;
	float4 Color : COLOR;
	float4 WorldPos : WORLD_POS;
	float4 ViewPos : VIEW_POS;
	float4 LightDir : LIGHTDIR;
float4 TEST0 : TEST0;
float4 TEST1 : TEST1;
float4 TEST2 : TEST2;
float4 TEST3 : TEST3;
};

float4 CalculatePointLight(float3 VertexViewPos)
{
#if 1
	float3 LightPosCamSpace = PointLightWS.xyz - CameraPosition.xyz;
	float4 LightDir;
	LightDir.xyz = -normalize(LightPosCamSpace - VertexViewPos);
	LightDir.w = 1;//1 / (1 + PointLightWS.w * sqrt(dot(LightPosCamSpace - VertexViewPos, LightPosCamSpace - VertexViewPos)));
	return LightDir;
#else
	float3 LightPosCam = PointLightWS.xyz - VertexViewPos;
	float4 LightDir;
	LightDir.xyz = -normalize(LightPosCam - ViewMtx[3].xyz);
	LightDir.w = 1 / (1 + PointLightWS.w * sqrt(dot(LightPosCam, LightPosCam)));
	return LightDir;
#endif
}

FGLTFPS TestGLTFVS(FGLTFVS In)
{
	float4x4 WorldMtx = ObjMtx;
	bool bIdentityWorld = Mode.w != 0;
	if (bIdentityWorld)
	{
		WorldMtx = float4x4(float4(1, 0, 0, 0), float4(0, 1, 0, 0), float4(0, 0, 1, 0), float4(0, 0, 0, 1));
	}

	FGLTFPS Out = (FGLTFPS)0;
	float4 ObjPos = float4(In.POSITION, 1);
	Out.WorldPos = mul(WorldMtx, ObjPos);
	Out.ViewPos = mul(ViewMtx, Out.WorldPos);
	Out.ClipPos = mul(ProjectionMtx, Out.ViewPos);

	float3 vN = /*normalize*/(mul((float3x3)WorldMtx, In.NORMAL));
	bool bNormalize = Mode2.y != 0;
	if (bNormalize)
	{
		vN = normalize(vN);
	}
	float3 vT = 0;//normalize(mul((float3x3)WorldMtx, In.TANGENT.xyz));
	float3 vB = 0;//normalize(mul((float3x3)WorldMtx, In.TANGENT.w * cross(vN, vT)));

	Out.Tangent =	0;//float3(vT.x, vB.x, vN.x);
	Out.BiTangent = 0;//float3(vT.y, vB.y, vN.y);
	Out.Normal =	0;//float3(vT.z, vB.z, vN.z);

	// View normals
	Out.Normal = mul((float3x3)WorldMtx, In.NORMAL);
	Out.Normal = mul((float3x3)ViewMtx, Out.Normal);
	if (bNormalize)
	{
		Out.Normal = normalize(Out.Normal);
	}

	// View normals
	Out.Tangent = mul((float3x3)WorldMtx, In.TANGENT.xyz);
	Out.Tangent = mul((float3x3)ViewMtx, Out.Tangent);
	if (bNormalize)
	{
		Out.Tangent = normalize(Out.Tangent);
	}

	Out.BiTangent = mul((float3x3)WorldMtx, In.TANGENT.w * cross(In.NORMAL.xyz, In.TANGENT.xyz));
	Out.BiTangent = mul((float3x3)ViewMtx, Out.BiTangent);
	if (bNormalize)
	{
		Out.BiTangent = normalize(Out.BiTangent);
	}

	Out.UV0 = In.TEXCOORD_0;
	Out.Color = In.COLOR_0;

	bool bIsDirLight = Mode2.w != 0;
	if (bIsDirLight)
	{
		Out.LightDir.xyz = mul((float3x3)ViewMtx, mul((float3x3)WorldMtx, DirLightWS.xyz));
	}
	else
	{
		// Point light
		bool bVertexLighting = Mode2.z != 0;
		if (bVertexLighting)
		{
			Out.LightDir = CalculatePointLight(Out.ViewPos.xyz);
		}
	}

	//float3 CamNorm = Out.Normal;
	//float CosAngIncidence = dot(CamNorm, Out.LightDir);
	//Out.TEST1 = float4(CamNorm.xyz, 0);
	//Out.TEST2 = float4(Out.LightDir.xyz, CosAngIncidence);
	//Out.TEST0 = clamp(CosAngIncidence, 0, 1);

	return Out;
}


float4 TestGLTFPS(FGLTFPS In) : SV_Target0
{
#if 0
	//float3 LightDir = normalize(In.LightDir);
	//float L = max(0, dot(In.Normal, LightDir));
	return float4(In.TEST0.xyz, 1);
#else
	float4 Diffuse = BaseTexture.Sample(SS, In.UV0);
	if (Diffuse.a < 1)
	{
		discard;
	}

	float3 vNormalMap = NormalTexture.Sample(SS, In.UV0).xyz * 2 - 1;
	float4 MetallicRoughness = MetallicRoughnessTexture.Sample(SS, In.UV0);

	bool bIdentityNormalBasis = Mode.y != 0;
	bool bLightingOnly = Mode.z != 0;

	float3x3 mTangentBasis = bIdentityNormalBasis ? float3x3(float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1)) : transpose(float3x3(In.Tangent, In.BiTangent, In.Normal));
	bool bTransposeTangentBasis = Mode2.x != 0;
	bool bNormalize = Mode2.y != 0;
	bool bVertexLighting = Mode2.z != 0;
	if (bTransposeTangentBasis)
	{
		mTangentBasis = transpose(mTangentBasis);
	}

	float4 LightDir = In.LightDir;

	bool bIsDirLight = Mode2.w != 0;

	if (!bIsDirLight && !bVertexLighting)
	{
		// Point light
		LightDir = CalculatePointLight(In.ViewPos.xyz);
	}

	LightDir.xyz = mul(mTangentBasis, normalize(LightDir.xyz));

	if (Mode.x == MODE_SHOWTEX_DIFFUSE)
	{
		return Diffuse;
	}
	else if (Mode.x == MODE_SHOWTEX_NORMALMAP)
	{
		return float4((vNormalMap + 1) * 0.5, 1);
	}
	else if (Mode.x == MODE_SHOW_VERTEX_NORMALS)
	{
		return float4(In.Normal * 0.5 + 0.5, 1);
	}
	else if (Mode.x == MODE_SHOW_PIXEL_NORMALS)
	{
		vNormalMap = mul(mTangentBasis, vNormalMap);
		if (bNormalize)
		{
			vNormalMap = normalize(vNormalMap);
		}
		return float4(vNormalMap * 0.5 + 0.5, 1);
	}
	else if (Mode.x == MODE_SHOW_VERTEX_TANGENT)
	{
		return float4(In.Tangent * 0.5 + 0.5, 1);
	}
	else if (Mode.x == MODE_VERTEX_NORMAL_LIT)
	{
		float L = max(0, dot(In.Normal, -LightDir.xyz));
		return (bLightingOnly ? float4(1, 1, 1, 1) : Diffuse) * float4(L, L, L, 1);
	}
	else if (Mode.x == MODE_SHOW_VERTEX_BITANGENT)
	{
		return float4(In.BiTangent * 0.5 + 0.5, 1);
	}
	else if (Mode.x == MODE_SHOW_ROUGHNESS)
	{
		return MetallicRoughness.g;
	}
	else if (Mode.x == MODE_SHOW_METALLIC)
	{
		return MetallicRoughness.b;
	}

	vNormalMap = mul(mTangentBasis, vNormalMap);
	if (bNormalize)
	{
		vNormalMap = normalize(vNormalMap);
	}

	float L = max(0, dot(vNormalMap, -LightDir));
	if (!bIsDirLight)
	{
		float Attenuation = LightDir.w;
		L *= Attenuation;
	}
	return (bLightingOnly ? float4(1, 1, 1, 1) : Diffuse) * float4(L, L, L, 1);
#endif
}
