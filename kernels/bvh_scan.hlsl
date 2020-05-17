#include "helper.hlsl"

cbuffer BinningArgument : register(b0, space0)
{
	int consumeTaskCount;
};
cbuffer BvhScanArgument : register(b1, space0)
{
	int offset;
};

RWStructuredBuffer<uint> scanTable0 : register(u0);
RWStructuredBuffer<uint> scanTable1 : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if( consumeTaskCount <= gID.x )
	{
		return;
	}

	uint value = scanTable0[gID.x];
	int index1 = (int)gID.x - offset;
	if(offset <= gID.x)
	{
		value += scanTable0[gID.x - offset];
	}
	scanTable1[gID.x] = value;
}
