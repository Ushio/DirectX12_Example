#include "helper.hlsl"

RWStructuredBuffer<float> src : register(u0);
RWStructuredBuffer<float> dst : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if(numberOfElement(src) <= gID.x)
	{
		return;
	}
	dst[gID.x] = sin(src[gID.x]);
}
