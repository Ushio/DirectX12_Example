#include "helper.hlsl"

// cbuffer arguments : register(b0, space0)
// {
// };
//#define ELEMENTS_IN_BLOCK 1024

// // 4 stages, uint = [8 bit] [8 bit] [8 bit] [8 bit]
// // so buckets wants 256 counters
// #define COUNTERS_IN_BLOCK 256

RWStructuredBuffer<uint> offsetTable : register(u0);
RWStructuredBuffer<uint> offsetCounter : register(u1);

RWStructuredBuffer<uint> globalScanTable0 : register(u2);

// we can assume WaveGetLaneCount(), 1, 2, 4, 8 or 32 or 64
[numthreads(128, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if(numberOfElement(offsetTable) <= gID.x)
	{
		return;
	}

	// just clear
	offsetCounter[gID.x] = 0;

	// apply globalScan
	int globalWaveIndex = gID.x / WaveGetLaneCount();
	offsetTable[gID.x] = offsetTable[gID.x] + globalScanTable0[globalWaveIndex];
}
