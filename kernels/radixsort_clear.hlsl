#include "helper.hlsl"

RWStructuredBuffer<uint> counter : register(u0);
RWStructuredBuffer<uint> offsetCounter : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if(numberOfElement(counter) <= gID.x)
	{
		return;
	}

	counter[gID.x] = 0;
	offsetCounter[gID.x] = 0;
}
