#include "helper.hlsl"
#include "bvh.h"

uint numberOfElement(RWStructuredBuffer<BvhElement> xs)
{
	uint numStruct;
	uint stride;
	xs.GetDimensions( numStruct, stride );
	return numStruct;
}

RWStructuredBuffer<BuildTask> buildTasksIn : register(u0);
RWStructuredBuffer<BvhElement> bvhElements : register(u1);
RWStructuredBuffer<uint> bvhElementIndices : register(u2);

[numthreads(64, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
    uint nElem = numberOfElement(bvhElements);
    uint iElem = min(gID.x, nElem - 1);

    int3 lower = WaveActiveMin(int3(bvhElements[iElem].lower[0], bvhElements[iElem].lower[1], bvhElements[iElem].lower[2]));
    int3 upper = WaveActiveMax(int3(bvhElements[iElem].upper[0], bvhElements[iElem].upper[1], bvhElements[iElem].upper[2]));
    if( WaveIsFirstLane() ) 
    {
        InterlockedMin(buildTasksIn[0].lower[0], lower.x);
        InterlockedMax(buildTasksIn[0].upper[0], upper.x);
        InterlockedMin(buildTasksIn[0].lower[1], lower.y);
        InterlockedMax(buildTasksIn[0].upper[1], upper.y);
        InterlockedMin(buildTasksIn[0].lower[2], lower.z);
        InterlockedMax(buildTasksIn[0].upper[2], upper.z);
    }

    if( gID.x < nElem )
    {
        bvhElementIndices[gID.x] = gID.x;
    }
}
