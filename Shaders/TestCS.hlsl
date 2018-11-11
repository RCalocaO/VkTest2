
RWBuffer<float4> Data;

[maxthreads(1,1,1)]
void WriteTriCS(uint Id : SV_DispatchId)
{
	float3 Pos[3] = { float3(0, -0.5, 1), float3(-0.5, 0.5, 1), float3(0.5, 0.5, 1)};
	Data[Id % 3] = float4(Pos[Id % 3], 1);
}
