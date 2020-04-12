#include "helper.hlsl"

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

[numthreads(512, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if(numberOfElement(xs) <= gID.x)
	{
		return;
	}

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
	uint k = elementsInBlock;
	uint store = numberOfBlock * value + gID.x / elementsInBlock /*block index*/; 
	uint o;
	InterlockedAdd(counter[store], 1, o);
}
