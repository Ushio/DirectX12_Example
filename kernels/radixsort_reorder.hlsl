#include "helper.hlsl"

#define ELEMENTS_IN_BLOCK 1024

cbuffer arguments : register(b0, space0)
{
	uint numberOfBlock;
};

RWStructuredBuffer<uint> xs0 : register(u0);
RWStructuredBuffer<uint> xs1 : register(u1);
RWStructuredBuffer<uint> offsetCounter : register(u2);
RWStructuredBuffer<uint> offsetTable : register(u3);

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	uint blockIndex = gID.x;

	if(numberOfBlock <= blockIndex)
	{
		return;
	}

	/*
	column major store
	+------> counters (COUNTERS_IN_BLOCK)
	|(block 0, cnt=0), (block 0, cnt=1)
	|(block 1, cnt=0), (block 1, cnt=1)
	|(block 2, cnt=0), (block 2, cnt=1)
	v
	blocks ( numberOfBlock )
	*/
	uint n = numberOfElement(xs0);
	
	uint valueHead = blockIndex * ELEMENTS_IN_BLOCK;
	for(int i = 0 ; i < ELEMENTS_IN_BLOCK ; ++i)
	{
		uint valueIndex = valueHead + i;
		if( n <= valueIndex)
		{
			return;
		}
		uint value = xs0[valueIndex] & 0xFF;
		uint offsetTableIndex = numberOfBlock * value + blockIndex;

		uint indexOnBlock = offsetCounter[offsetTableIndex];
		offsetCounter[offsetTableIndex] = indexOnBlock + 1;

		uint offset = offsetTable[offsetTableIndex];
		xs1[offset + indexOnBlock] = xs0[valueIndex];
		// xs1[valueIndex] = indexOnBlock;
	}
}
