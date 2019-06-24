cbuffer ViewUB : register(b0)
{
	float4x4 ViewMtx;
	float4x4 ProjectionMtx;
	float4x4 WorldMtx;
	int4 Mode;
};

SamplerState SS : register(s2);
Texture2D Base : register(t3);
Texture2D Normal : register(t4);

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
	float4 Pos : SV_POSITION;
	float3 Normal : NORMAL;
	float3 Tangent : TANGENT;
	float2 UV0 : TEXCOORD0;
	float4 Color : COLOR;
};

FGLTFPS TestGLTFVS(FGLTFVS In)
{
	FGLTFPS Out = (FGLTFPS)0;
	float4 Pos = mul(WorldMtx, float4(In.POSITION, 1));
	Pos = mul(ViewMtx, Pos);
	Out.Pos = mul(ProjectionMtx, Pos);
	Out.Normal = mul((float3x3)ViewMtx, In.NORMAL);
	Out.Tangent = In.TANGENT;
	Out.UV0 = In.TEXCOORD_0;
	Out.Color = In.COLOR_0;
	return Out;
}

float4 TestGLTFPS(FGLTFPS In) : SV_Target0
{
	if (Mode.x == 0)
	{
		return float4(Base.Sample(SS, In.UV0).xyz, 1);
	}
	else if (Mode.x == 1)
	{
		return float4(In.Color.xyz, 1);
	}
	else if (Mode.x == 2)
	{
		return float4(In.Normal * 0.5 + 0.5, 1);
	}
	else if (Mode.x == 3)
	{
		return float4(Base.Sample(SS, In.UV0).xyz, 1) * In.Color;
	}

	return float4(Normal.Sample(SS, In.UV0).xyz, 1);
}
