#include "helper.hlsl"
#include "bvh.h"

RWStructuredBuffer<BinningBuffer> binningBuffer : register(u0);
RWStructuredBuffer<uint> executionCount : register(u1);

/* 
    nExecute( 1, 64 ) = 1
    nExecute( 2, 64 ) = 1
    ..
    nExecute( 64, 64 ) = 1
    nExecute( 65, 64 ) = 2
*/
uint nExecute( uint n, uint batch )
{
	return ( n + batch - 1 ) / batch;
}

[numthreads(64, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
    uint n;
	uint stride;
	binningBuffer.GetDimensions( n, stride );
    if( n <= gID.x )
    {
        return;
    }
    BuildTask task = binningBuffer[gID.x].task;
    uint nPrim = task.geomEnd - task.geomBeg;
    executionCount[gID.x] = nExecute(nPrim, EXECUTION_BATCH_COUNT);
}
