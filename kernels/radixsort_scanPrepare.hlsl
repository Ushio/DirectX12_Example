#include "helper.hlsl"

RWStructuredBuffer<uint> counter : register(u0);
RWStructuredBuffer<uint> globalScanTable0 : register(u1);

[numthreads(128, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if( numberOfElement(counter) <= gID.x )
	{
		return;
	}
	globalScanTable0[gID.x] = gID.x == 0 ? 0 : counter[gID.x - 1];
	// globalScanTable0[gID.x] = gID.x == 0 ? 0 : 1;
}
