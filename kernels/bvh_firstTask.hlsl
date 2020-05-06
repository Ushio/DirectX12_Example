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
RWStructuredBuffer<BuildTask> buildTasksCounter : register(u1); //is necessary?
RWStructuredBuffer<BvhElement> bvhElements : register(u2);

[numthreads(64, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
    uint nElem = numberOfElement(bvhElements);
    uint iElem = min(gID.x, nElem - 1);

    int3 lower = WaveActiveMin(int3(bvhElements[iElem].lower[0], bvhElements[iElem].lower[1], bvhElements[iElem].lower[2]));
    int3 upper = WaveActiveMax(int3(bvhElements[iElem].upper[0], bvhElements[iElem].upper[1], bvhElements[iElem].upper[2]));
    if( WaveIsFirstLane() ) 
    {
        InterlockedMin(bvhElements[iElem].lower[0], lower.x);
        InterlockedMax(bvhElements[iElem].upper[0], upper.x);
        InterlockedMin(bvhElements[iElem].lower[1], lower.y);
        InterlockedMax(bvhElements[iElem].upper[1], upper.y);
        InterlockedMin(bvhElements[iElem].lower[2], lower.z);
        InterlockedMax(bvhElements[iElem].upper[2], upper.z);
    }
}
