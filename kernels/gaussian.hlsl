#include "helper.hlsl"

RWStructuredBuffer<float4> src : register(u0);
RWStructuredBuffer<float4> dst : register(u1);
RWStructuredBuffer<float> kernel : register(u2);

cbuffer arguments : register(b0, space0)
{
	int width;
	int height;
	int sample_dx;
	int sample_dy;
};

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if(numberOfElement(src) <= gID.x)
	{
		return;
	}

	uint x = gID.x % width;
	uint y = gID.x / width;

	int kn = numberOfElement(kernel);
	     
	float4 value = float4(0.0f, 0.0f, 0.0f, 0.0f);
	for(int i = -kn + 1 ; i < kn ; ++i)
	{
		float w = kernel[abs(i)];

		int sx = clamp(x + sample_dx * i, 0, width - 1);
		int sy = clamp(y + sample_dy * i, 0, height - 1);
		value += w * src[sy * width + sx];
	}

	dst[gID.x] = value;
}
