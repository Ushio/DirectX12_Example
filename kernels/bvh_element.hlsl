#include "helper.hlsl"

struct BvhElement 
{
    int lower[3];
    int upper[3];
    float centeroid[3];
};
uint numberOfElement(RWStructuredBuffer<BvhElement> xs)
{
	uint numStruct;
	uint stride;
	xs.GetDimensions( numStruct, stride );
	return numStruct;
}

RWStructuredBuffer<float3> vertexBuffer : register(u0);
RWStructuredBuffer<uint> indexBuffer : register(u1);
RWStructuredBuffer<BvhElement> bvhElements : register(u2);

[numthreads(64, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
	if(numberOfElement(bvhElements) <= gID.x) {
		return;
	}
    uint iPrim = gID.x;
    uint index = iPrim * 3;
    float3 v0 = vertexBuffer[indexBuffer[index]];
    float3 v1 = vertexBuffer[indexBuffer[index+1]];
    float3 v2 = vertexBuffer[indexBuffer[index+2]];

    float3 lower = min(min(v0, v1), v2);
    float3 upper = max(max(v0, v1), v2);
    float3 centeroid = (v0 + v1 + v2) / 3.0f;

    BvhElement e;
    e.lower[0] = to_ordered(lower.x);
    e.lower[1] = to_ordered(lower.y);
    e.lower[2] = to_ordered(lower.z);
    e.upper[0] = to_ordered(upper.x);
    e.upper[1] = to_ordered(upper.y);
    e.upper[2] = to_ordered(upper.z);
    e.centeroid[0] = centeroid.x;
    e.centeroid[1] = centeroid.y;
    e.centeroid[2] = centeroid.z;
    bvhElements[gID.x] = e;
}
