#include "helper.hlsl"

RWStructuredBuffer<uint> src : register(u0);
RWStructuredBuffer<float4> dst : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if(numberOfElement(src) <= gID.x)
	{
		return;
	}
	
	uint value = src[gID.x];
	uint r = (value & 0x000000FF);
	uint g = (value & 0x0000FF00) >> 8;
	uint b = (value & 0x00FF0000) >> 16;
	uint a = (value & 0xFF000000) >> 24;

	dst[gID.x] = float4(
		pow( (float)r / 255.0f, 2.2f ),
		pow( (float)g / 255.0f, 2.2f ),
		pow( (float)b / 255.0f, 2.2f ),
		(float)a / 255.0f
	);
}
