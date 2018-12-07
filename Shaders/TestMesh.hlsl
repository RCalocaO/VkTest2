cbuffer ViewUB : register(b0)
{
	float4x4 ViewMtx;
	float4x4 ProjectionMtx;
};

cbuffer ObjUB : register(b1)
{
	float4x4 ObjMtx;
	float4 Tint;
};

SamplerState SS : register(s2);
Texture2D Tex : register(t3);




struct FGLTFVS
{
	// Use GLTF semantic names for easier binding
	float3 POSITION : POSITION;
	float3 NORMAL : NORMAL;
	//float4 Tangent : TANGENT;
	float2 TEXCOORD_0 : TEXCOORD_0;
	//float2 UV1 : TEXCOORD1;
	//float4 Color : COLOR;
};

struct FGLTFPS
{
	float4 Pos : SV_POSITION;
	float3 Normal : NORMAL;
	float2 UV0 : TEXCOORD0;
	//float2 UV1 : TEXCOORD1;
	//float4 Color : COLOR;
};

FGLTFPS TestGLTFVS(FGLTFVS In)
{
	FGLTFPS Out = (FGLTFPS)0;
	Out.Pos = float4(In.POSITION, 1);
	Out.Normal = In.NORMAL;
	Out.UV0 = In.TEXCOORD_0;
	//Out.UV1 = In.UV1;
	//Out.Color = In.Color;
	return Out;
}

float4 TestGLTFPS(FGLTFPS In) : SV_Target0
{
	return float4(In.UV0, 0, 1);
}
