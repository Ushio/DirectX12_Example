#include "helper.hlsl"

// cbuffer arguments : register(b0, space0)
// {
// };
// static const int 

#define ELEMENTS_IN_BLOCK 1024

// 4 stages, uint = [8 bit] [8 bit] [8 bit] [8 bit]
// so buckets wants 256 counters
#define COUNTER_IN_BLOCK 256

RWStructuredBuffer<uint> xs : register(u0);
RWStructuredBuffer<uint> counter : register(u1);

[numthreads(ELEMENTS_IN_BLOCK, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID, uint3 group : SV_GroupID)
{
	if(numberOfElement(xs) <= gID.x)
	{
		return;
	}
	uint bucketStart = group.x * COUNTER_IN_BLOCK;
	uint index = xs[gID.x] & 0xFF;
	InterlockedAdd(counter[bucketStart + index], 1);
}
