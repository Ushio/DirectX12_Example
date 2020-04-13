#include "helper.hlsl"

#define ELEMENTS_IN_BLOCK 512

// 4 stages, uint = [8 bit] [8 bit] [8 bit] [8 bit]
// so buckets wants 256 counters
cbuffer arguments : register(b0, space0)
{
	uint numberOfBlock;
	uint elementsInBlock;
	uint iteration; // 0, 1, 2, 3
};

RWStructuredBuffer<uint> xs : register(u0);
RWStructuredBuffer<uint> counter : register(u1);

uint getSortKey(uint x)
{
	uint m = x & (0xFF << (8 * iteration));
	return m >> (8 * iteration);
}

groupshared uint groupCounters[256];

[numthreads(ELEMENTS_IN_BLOCK, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID, uint3 blockIndexSV: SV_GroupID, uint3 indexOnGroup: SV_GroupThreadID)
{
	if(indexOnGroup.x < 256)
	{
		groupCounters[indexOnGroup.x] = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	if(gID.x < numberOfElement(xs))
	{
		/*
		column major store
		+------> counters ( 256 )
		|(block 0, cnt=0), (block 0, cnt=1)
		|(block 1, cnt=0), (block 1, cnt=1)
		|(block 2, cnt=0), (block 2, cnt=1)
		v
		blocks ( numberOfBlock )
		*/
		uint value = getSortKey(xs[gID.x]);
		uint o;
		InterlockedAdd(groupCounters[value], 1, o);
	}

	GroupMemoryBarrierWithGroupSync();

	if(indexOnGroup.x < 256)
	{
		uint key = indexOnGroup.x;
		uint store = numberOfBlock * key + blockIndexSV.x /*block index*/; 
		counter[store] = groupCounters[indexOnGroup.x];
	}
}
