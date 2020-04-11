#include "helper.hlsl"

RWStructuredBuffer<uint> counter : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if(numberOfElement(counter) <= gID.x)
	{
		return;
	}

	counter[gID.x] = 0;
	// counter[gID.x] = gID.x;
}
