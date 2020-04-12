#include "helper.hlsl"


// 4 stages, uint = [8 bit] [8 bit] [8 bit] [8 bit]
// so buckets wants 256 counters
cbuffer arguments : register(b0, space0)
{
	uint numberOfBlock;
	uint elementsInBlock;
	uint iteration; // 0, 1, 2, 3
};

RWStructuredBuffer<uint> xs0 : register(u0);
RWStructuredBuffer<uint> xs1 : register(u1);
RWStructuredBuffer<uint> offsetCounter : register(u2);
RWStructuredBuffer<uint> offsetTable : register(u3);

uint getSortKey(uint x)
{
	uint m = x & (0xFF << (8 * iteration));
	return m >> (8 * iteration);
}

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
	+------> counters ( 256 )
	|(block 0, cnt=0), (block 0, cnt=1)
	|(block 1, cnt=0), (block 1, cnt=1)
	|(block 2, cnt=0), (block 2, cnt=1)
	v
	blocks ( numberOfBlock )
	*/
	uint n = numberOfElement(xs0);
	
	uint valueHead = blockIndex * elementsInBlock;
	for(int i = 0 ; i < elementsInBlock ; ++i)
	{
		uint valueIndex = valueHead + i;
		if( n <= valueIndex)
		{
			return;
		}
		uint value = getSortKey(xs0[valueIndex]);
		uint offsetTableIndex = numberOfBlock * value + blockIndex;

		uint indexOnBlock = offsetCounter[offsetTableIndex];
		offsetCounter[offsetTableIndex] = indexOnBlock + 1;

		uint offset = offsetTable[offsetTableIndex];
		xs1[offset + indexOnBlock] = xs0[valueIndex];
		// xs1[valueIndex] = indexOnBlock;
	}
}
