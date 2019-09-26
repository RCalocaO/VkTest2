cbuffer ViewUB : register(b0)
{
	float4x4 ViewMtx;
	float4x4 ProjectionMtx;
	float4 LightDirWS;
	int4 Mode;
	int4 Mode2;
};

cbuffer ObjUB : register(b1)
{
	float4x4 ObjMtx;
};

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
	float4 Pos : SV_POSITION;
	float3 Normal : NORMAL;
	float3 Tangent : TANGENT;
	float3 BiTangent : BITANGENT;
	float2 UV0 : TEXCOORD0;
	float4 Color : COLOR;
};

FGLTFPS TestGLTFVS(FGLTFVS In)
{
	float4x4 WorldMtx = ObjMtx;
	bool bIdentityWorld = Mode.w != 0;
	if (bIdentityWorld)
	{
		WorldMtx = float4x4(float4(1, 0, 0, 0), float4(0, 1, 0, 0), float4(0, 0, 1, 0), float4(0, 0, 0, 1));
	}

	FGLTFPS Out = (FGLTFPS)0;
	float4 Pos = float4(In.POSITION, 1);
	Pos = mul(WorldMtx, Pos);
	Pos = mul(ViewMtx, Pos);
	Out.Pos = mul(ProjectionMtx, Pos);

	float3 vN = normalize(mul((float3x3)WorldMtx, In.NORMAL));
	float3 vT = normalize(mul((float3x3)WorldMtx, In.TANGENT.xyz));
	float3 vB = normalize(mul((float3x3)WorldMtx, In.TANGENT.w * cross(vN, vT)));

	Out.Tangent =	float3(vT.x, vB.x, vN.x);
	Out.BiTangent = float3(vT.y, vB.y, vN.y);
	Out.Normal =	float3(vT.z, vB.z, vN.z);
	Out.UV0 = In.TEXCOORD_0;
	Out.Color = In.COLOR_0;
	return Out;
}

float4 TestGLTFPS(FGLTFPS In) : SV_Target0
{
	float4 Diffuse = BaseTexture.Sample(SS, In.UV0);
	float3 vNormalMap = NormalTexture.Sample(SS, In.UV0).xyz * 2 - 1;
	float4 MetallicRoughness = MetallicRoughnessTexture.Sample(SS, In.UV0);

	bool bIdentityNormalBasis = Mode.y != 0;
	bool bLightingOnly = Mode.z != 0;

	float3x3 mTangentBasis = bIdentityNormalBasis ? float3x3(float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1)) : transpose(float3x3(In.Tangent, In.BiTangent, In.Normal));
	bool bTransposeTangentBasis = Mode2.x != 0;
	bool bNormalize = Mode2.y != 0;
	if (bTransposeTangentBasis)
	{
		mTangentBasis = transpose(mTangentBasis);
	}

	if (Mode.x == 0 || Mode.x == 1)
	{
		if (Diffuse.a < 1)
		{
			discard;
		}
	}

	float3 LightDir = mul(mTangentBasis, LightDirWS);

	if (Mode.x == 1)
	{
		return Diffuse;
	}
	else if (Mode.x == 2)
	{
		return float4(vNormalMap, 1);
	}
	else if (Mode.x == 3)
	{
		return float4(In.Normal * 0.5 + 0.5, 1);
	}
	else if (Mode.x == 4)
	{
		vNormalMap = mul(mTangentBasis, vNormalMap);
		if (bNormalize)
		{
			vNormalMap = normalize(vNormalMap);
		}
		return float4(vNormalMap * 0.5 + 0.5, 1);
	}
	else if (Mode.x == 5)
	{
		return float4(In.Tangent * 0.5 + 0.5, 1);
	}
	else if (Mode.x == 6)
	{
		float L = max(0, dot(In.Normal, -LightDir));
		return (bLightingOnly ? float4(1, 1, 1, 1) : Diffuse) * float4(L, L, L, 1);
	}
	else if (Mode.x == 7)
	{
		return float4(In.BiTangent * 0.5 + 0.5, 1);
	}
	else if (Mode.x == 8)
	{
		return MetallicRoughness.g;
	}
	else if (Mode.x == 9)
	{
		return MetallicRoughness.b;
	}

	vNormalMap = mul(mTangentBasis, vNormalMap);
	if (bNormalize)
	{
		vNormalMap = normalize(vNormalMap);
	}
	float L = max(0, dot(vNormalMap, -LightDir));
	return (bLightingOnly ? float4(1, 1, 1, 1) : Diffuse) * float4(L, L, L, 1);
}
