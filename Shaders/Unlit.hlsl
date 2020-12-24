#define HLSL	1

#include "ShaderDefines.h"

struct FVSUnlitIn
{
	float3 Position : POSITION;
	float4 Color : COLOR;
};

struct FPSUnlit
{
	float4 ClipPos : SV_POSITION;
	float4 Color : COLOR;
};

FPSUnlit UnlitVS(FVSUnlitIn In)
{
	FPSUnlit Out = (FPSUnlit)0;

	float4x4 WorldMtx = ObjMtx;
	bool bIdentityWorld = Mode.w != 0;
	if (bIdentityWorld)
	{
		WorldMtx = float4x4(float4(1, 0, 0, 0), float4(0, 1, 0, 0), float4(0, 0, 1, 0), float4(0, 0, 0, 1));
	}

	float4 ObjPos = float4(In.Position, 1);
	float4 WorldPos = mul(WorldMtx, ObjPos);
	float4 ViewPos = mul(ViewMtx, WorldPos);
	Out.ClipPos = mul(ProjectionMtx, ViewPos);
	Out.Color = In.Color;

	return Out;
}

float4 ColorPS(FPSUnlit In)
{
	return In.Color;
}

float4 RedPS() : SV_Target0
{
	return float4(1, 0, 0, 1);
}
