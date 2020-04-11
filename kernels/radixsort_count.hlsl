#include "helper.hlsl"

// 4 stages, uint = [8 bit] [8 bit] [8 bit] [8 bit]
// so buckets wants 256 counters
cbuffer arguments : register(b0, space0)
{
	uint numberOfBlock;
	uint elementsInBlock;
};

RWStructuredBuffer<uint> xs : register(u0);
RWStructuredBuffer<uint> counter : register(u1);

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
	uint value = xs[gID.x] & 0xFF;
	uint store = numberOfBlock * value + gID.x / elementsInBlock;
	uint o;
	InterlockedAdd(counter[store], 1, o);
}
