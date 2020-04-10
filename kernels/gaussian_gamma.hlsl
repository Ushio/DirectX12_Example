#include "helper.hlsl"

RWStructuredBuffer<float4> src : register(u0);
RWStructuredBuffer<uint> dst : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if(numberOfElement(src) <= gID.x)
	{
		return;
	}

	float4 color = src[gID.x];
	float4 appliedGamma = float4(
		pow( (float)color.r, 1.0f / 2.2f ),
		pow( (float)color.g, 1.0f / 2.2f ),
		pow( (float)color.b, 1.0f / 2.2f ),
		color.a
	);
	int4 quantized = int4(appliedGamma * 255.0f + float4(0.5f, 0.5f, 0.5f, 0.5f));
	quantized = clamp( quantized, int4( 0, 0, 0, 0), int4( 255, 255, 255, 255) );
	uint value = 
		quantized.r       | 
		quantized.g << 8  |
		quantized.b << 16 |
		quantized.a << 24;

	dst[gID.x] = value;
}
