#include "helper.hlsl"
#include "bvh.h"

#define FLT_MAX          3.402823466e+38F        // max value

cbuffer BinningArgument : register(b0, space0)
{
	int consumeTaskCount;
};

RWStructuredBuffer<BuildTask> buildTasks : register(u0);
RWStructuredBuffer<uint> buildTaskRingRange : register(u1);

[numthreads(1, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
    uint ringBufferSize;
    uint stride;
    buildTasks.GetDimensions( ringBufferSize, stride );
    buildTaskRingRange[0] = (buildTaskRingRange[0] + consumeTaskCount) % ringBufferSize;
}
