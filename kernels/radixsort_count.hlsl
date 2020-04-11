#include "helper.hlsl"

// cbuffer arguments : register(b0, space0)
// {
// };
// static const int 

#define BLOCK_SIZE 1024

RWStructuredBuffer<uint> xs : register(u0);
RWStructuredBuffer<uint> counter : register(u1);

[numthreads(BLOCK_SIZE, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID, uint3 group : SV_GroupID)
{
	if(numberOfElement(xs) <= gID.x)
	{
		return;
	}
	xs[gID.x] = counter[group.x];
}
