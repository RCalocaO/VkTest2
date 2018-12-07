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

struct FVSIn
{
	float3 Position : POSITION;
	float4 Color : COLOR;
	float2 UVs : TEXCOORD0;
};

struct FVSOut
{
	float4 Pos : SV_POSITION;
	float4 Color : COLOR;
	float2 UVs : TEXCOORD0;
};

float4 VBClipVS(in uint VertexID : SV_VertexID, in float4 Pos : POSITION) : SV_Position
{
	return Pos;
}

float4 MainNoVBClipVS(in uint VertexID : SV_VertexID) : SV_Position
{
	float3 Pos[3] = { float3(0, -0.5, 1), float3(-0.5, 0.5, 1), float3(0.5, 0.5, 1)};
	return float4(Pos[VertexID % 3], 1);
}

Buffer<float4> Pos : register(b8);
float4 MainBufferClipVS(in uint VertexID : SV_VertexID) : SV_Position
{
	return Pos[VertexID % 3];
}

float4 RedPS() : SV_Target0
{
	return float4(1, 0, 0, 1);
}

float4 Color;
float4 ColorPS() : SV_Target0
{
	return Color;
}

FVSOut MainVS(FVSIn In)
{
	FVSOut Out;
	float4 Position = mul(ObjMtx, float4(In.Position.xyz, 1.0));
	Position = mul(ViewMtx, Position);

	Out.UVs = In.UVs;

	Out.Color = In.Color * Tint;

	Out.Pos = mul(ProjectionMtx, Position);
	return Out;
}

float4 MainColorPS(FVSOut In)
{
	return In.Color;
}

SamplerState SS : register(s2);
Texture2D Tex : register(t3);

float4 MainTexPS(FVSOut In)
{
	return Tex.Sample(SS, In.UVs) * In.Color;
}



struct FGLTFVS
{
	float3 Pos : POSITION;
	float3 Normal : NORMAL;
};

struct FGLTFPS
{
	float4 Pos : SV_POSITION;
	float3 Normal : NORMAL;
};

FGLTFPS TestGLTFVS(FGLTFVS In)
{
	FGLTFPS Out = (FGLTFPS)0;
	Out.Pos = float4(In.Pos, 1);
	Out.Normal = In.Normal;
	return Out;
}

float4 TestGLTFPS(FGLTFPS In) : SV_Target0
{
	return float4(In.Normal, 1);
}
