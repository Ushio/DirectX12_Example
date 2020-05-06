#include "helper.hlsl"

struct BuildTask 
{
    int lower[3];
    int upper[3];
    int geomBeg;
    int geomEnd;
};
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
RWStructuredBuffer<BuildTask> buildTasksIn : register(u0);
RWStructuredBuffer<BuildTask> buildTasksCounter : register(u1);
AppendStructuredBuffer<BuildTask> buildTasksOut : register(u2);
RWStructuredBuffer<BvhElement> bvhElements : register(u3);

struct Bin {
    // bin AABB
    int lower[3];
    int upper[3];
};
groupshared Bin bins[16];

[numthreads(64, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
    uint nElem = numberOfElement(bvhElements);
    
    if(localID.x < 16) {
        for(int i = 0 ; i < 3 ; ++i) {
            bins[localID.x].lower[i] = 2147483647;
            bins[localID.x].upper[i] = -2147483648;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    float3 lowerBound = float3(-10.0f, -10.0f, -10.0f);
    float3 upperBound = float3(+10.0f, +10.0f, +10.0f);

    for(int i = 0 ; i < nElem ; i += 64)
    {
        uint iPrim = min(i + localID.x, nElem);
        float x = bvhElements[iPrim].centeroid[0];
        float location_f = (x - lowerBound.x) / (upperBound.x - lowerBound.x);
        int3 bin_idx = clamp((int3)(location_f * 16.0f), int3(0, 0, 0), int3(16, 16, 16));
        
        // update bin AABB
        InterlockedMin(bins[bin_idx.x].lower[0], bvhElements[iPrim].lower[0]);
        InterlockedMax(bins[bin_idx.x].upper[0], bvhElements[iPrim].upper[0]);
        InterlockedMin(bins[bin_idx.y].lower[1], bvhElements[iPrim].lower[1]);
        InterlockedMax(bins[bin_idx.y].upper[1], bvhElements[iPrim].upper[1]);
        InterlockedMin(bins[bin_idx.z].lower[2], bvhElements[iPrim].lower[2]);
        InterlockedMax(bins[bin_idx.z].upper[2], bvhElements[iPrim].upper[2]);
    }
    
    BuildTask task;
    task.geomBeg = 0;
    task.geomEnd = gID.x;
    buildTasksOut.Append(task);
}
