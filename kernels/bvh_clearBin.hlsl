#include "helper.hlsl"
#include "bvh.h"

#define FLT_MAX          3.402823466e+38F        // max value

cbuffer BinningArgument : register(b0, space0)
{
	int consumeTaskCount;
};

RWStructuredBuffer<BinningBuffer> binningBuffer : register(u0);

RWStructuredBuffer<BuildTask> buildTasks : register(u1);
RWStructuredBuffer<uint> buildTaskRingRange : register(u2);

[numthreads(64, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
    uint nRingBuffer;
	uint stride;
	buildTasks.GetDimensions( nRingBuffer, stride );

    uint nBinningBuffer;
    binningBuffer.GetDimensions( nBinningBuffer, stride );

    uint iGlobal = ( buildTaskRingRange[0] + gID.x ) % nRingBuffer;

    if( consumeTaskCount <= gID.x )
    {
        return;
    }

    int lower = to_ordered(+FLT_MAX);
    int upper = to_ordered(-FLT_MAX);
    BinningBuffer buffer;
    buffer.task = buildTasks[iGlobal];
    for(int k = 0 ; k < BIN_COUNT ; ++k) {
        for(int j = 0 ; j < 3 ; ++j) {
            for(int i = 0 ; i < 3 ; ++i) {
                buffer.bins[j][k].lower[i] = lower;
                buffer.bins[j][k].upper[i] = upper;
            }
            buffer.bins[j][k].nElem = 0;
        }
    }
    buffer.splitLCounter = 0;
    buffer.splitRCounter = 0;
    binningBuffer[gID.x] = buffer;
}
