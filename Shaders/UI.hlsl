
struct FVSIn
{
	float2 Pos : POSITION;
	float2 UVs : TEXCOORD0;
	float4 Color : COLOR;
};

struct FVSOut
{
	float4 Pos : SV_POSITION;
	float4 Color : COLOR;
	float2 UVs : TEXCOORD0;
};

cbuffer CB
{
	float2 Scale;
	float2 Translate;
}

FVSOut UIMainVS(in FVSIn In) : SV_Position
{
	FVSOut Out;
	Out.Pos = float4(In.Pos.xy * Scale + Translate, 0, 1);
	Out.Color = In.Color;
	Out.UVs = In.UVs;
	return Out;
}

float4 UIMainPS(FVSOut In) : SV_Target0
{
	return In.Color;
}
