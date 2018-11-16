

groupshared float dummyLDS[256];

RWBuffer<float> output : register(u0);

struct LoadConstantsWithArray
{
	uint elementsMask;		// Runtime address mask. Needed to prevent compiler combining narrow raw buffer loads from single thread.
	uint writeIndex;		// Runtime write mask. Always 0xffffffff (= never write). But the compiler doesn't know this :)
	uint readStartAddress;
	uint padding;

	float4 benchmarkArray[1024];	// 16 KB test array (fits inside L1$)
};

cbuffer CB0 : register(b8)
{
	LoadConstantsWithArray loadConstants;
};

[numthreads(256,1,1)]
void TestCS(uint3 tid : SV_DispatchThreadID, uint gix : SV_GroupIndex)
{
	float4 value = 0;
	uint htid = gix;
	[loop]
	for (int i = 0; i < 256; ++i)
	{
		// Mask with runtime constant to prevent unwanted compiler optimizations
		uint elemIdx = (htid + i) | loadConstants.elementsMask;
		value += loadConstants.benchmarkArray[elemIdx].xyzw;
	}

	dummyLDS[gix] = value.x + value.y + value.z + value.w;
	GroupMemoryBarrierWithGroupSync();

	[branch]
	if (loadConstants.writeIndex != 0xffffffff)
	{
		output[tid.x + tid.y] = dummyLDS[loadConstants.writeIndex];
	}
}
