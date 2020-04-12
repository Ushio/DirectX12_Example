#include "helper.hlsl"

#define ELEMENTS_IN_BLOCK 1024

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
RWStructuredBuffer<uint> offsetTable : register(u2);

uint getSortKey(uint x)
{
	uint m = x & (0xFF << (8 * iteration));
	return m >> (8 * iteration);
}

// slow...
// groupshared uint blockxs[256];  // [item id]
// groupshared uint offsetxs[256]; // [item id]
// groupshared uint counters[256]; // [key]

// [numthreads(256, 1, 1)]
// void main(uint3 gID : SV_DispatchThreadID, uint3 blockIndex: SV_GroupID, uint3 indexOnSubblock: SV_GroupThreadID)
// {
// 	uint n = numberOfElement(xs0);

// 	counters[indexOnSubblock.x] = 0;

// 	for(int i = 0 ; i < 4 ; ++i)
// 	{
// 		// load to shared memory
// 		uint xsIndex = blockIndex.x * ELEMENTS_IN_BLOCK + 256 * i + indexOnSubblock.x;
// 		if( xsIndex < n )
// 		{
// 			blockxs[indexOnSubblock.x] = xs0[xsIndex];
// 		}
		
// 		GroupMemoryBarrierWithGroupSync();

// 		// count. single thread
// 		if(indexOnSubblock.x == 0)
// 		{
// 			// e.g. blockIndex.x == 2, i == 1
// 			// 
// 			// remove this offset
// 			// < ------------------------------------------ > 
// 			// < -------------------------------------------|-----n >
// 			// [ELEMENTS_IN_BLOCK] [ELEMENTS_IN_BLOCK], [256], [256 / my sub block]
// 			uint blockxsCount = min(n - (blockIndex.x * ELEMENTS_IN_BLOCK + i * 256), 256);

// 			// calculate local offset per element
// 			for(int j = 0 ; j < blockxsCount ; ++j)
// 			{
// 				uint value = blockxs[j];
// 				uint key = getSortKey(value);
// 				uint cnt = counters[key];
// 				offsetxs[j] = cnt;
// 				counters[key] = cnt + 1;
// 			}
// 		}

// 		GroupMemoryBarrierWithGroupSync();

// 		if( xsIndex < n )
// 		{
// 			uint value = blockxs[indexOnSubblock.x];
// 			uint offsetTableIndex = numberOfBlock * getSortKey(value) + blockIndex.x;
// 			uint offset = offsetTable[offsetTableIndex];

// 			// combine block offset, local offset
// 			uint toIndex = offset + offsetxs[indexOnSubblock.x];
// 			xs1[toIndex] = value;
// 		}
// 	}
// }

groupshared uint counters[256]; // [key]

[numthreads(64, 1, 1)]
void main(uint3 blockIndexSV: SV_GroupID, uint3 indexOnGroup: SV_GroupThreadID)
{
	for(int j = 0 ; j < 4 ; ++j)
	{
		counters[64 * j + indexOnGroup.x] = 0;
	}
	GroupMemoryBarrierWithGroupSync();
	
	uint blockIndex = blockIndexSV.x;
	if(indexOnGroup.x != 0)
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

		uint value = xs0[valueIndex];
		uint key = getSortKey(value);
		uint offsetTableIndex = numberOfBlock * key + blockIndex;

		uint indexOnBlock = counters[key];
		counters[key] = indexOnBlock + 1;

		uint offset = offsetTable[offsetTableIndex];
		xs1[offset + indexOnBlock] = value;
	}
}

