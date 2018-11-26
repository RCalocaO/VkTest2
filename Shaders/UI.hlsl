
struct FVSIn
{
	float2 Pos : POSITION;
	float2 UVs : TEXCOORD0;
	uint Color : COLOR;
};

struct FVSOut
{
	float4 Pos : SV_POSITION;
	float4 Color : COLOR;
	float2 UVs : TEXCOORD0;
};

FVSOut UIMainVS(in FVSIn In) : SV_Position
{
	FVSOut Out;
	Out.Pos = float4(In.Pos.xy, 0, 1);
	Out.Color = float4(1, 0, 0, 1);
	Out.UVs = In.UVs;
	return Out;
}

float4 UIMainPS(FVSOut In) : SV_Target0
{
	return In.Color;
}
