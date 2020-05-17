#include "helper.hlsl"
#include "bvh.h"

#define FLT_MAX          3.402823466e+38F        // max value

cbuffer BinningArgument : register(b0, space0)
{
	int consumeTaskCount;
};

void expand( inout Bin bin, Bin otherBin)
{
    for( int axis = 0 ; axis < 3 ; ++axis )
    {
        bin.lower[axis] = min(bin.lower[axis], otherBin.lower[axis]);
        bin.upper[axis] = max(bin.upper[axis], otherBin.upper[axis]);
    }
    bin.nElem += otherBin.nElem;
}
float surfaceArea(int lower[3], int upper[3]) {
    float3 l = float3(from_ordered(lower[0]), from_ordered(lower[1]), from_ordered(lower[2]));
    float3 u = float3(from_ordered(upper[0]), from_ordered(upper[1]), from_ordered(upper[2]));
    float3 size = l - u;
    return dot(size.xyz, size.yzx) * 2.0f;
}

RWStructuredBuffer<BuildTask> buildTasksOut : register(u0);
RWStructuredBuffer<uint> buildTaskRingRangeOut : register(u1);

RWStructuredBuffer<BinningBuffer> binningBuffer : register(u2);

// output bvh
RWStructuredBuffer<BvhNode> bvhNodes : register(u3);
RWStructuredBuffer<uint> bvhNodeCounter : register(u4);

RWStructuredBuffer<uint> debugBuffer : register(u5);

groupshared uint s_taskCounter;
groupshared uint s_taskCounterBase;

[numthreads(64, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
    if(consumeTaskCount <= gID.x)
    {
        return;
    }

    int iBinningBuffer = gID.x;
    BinningBuffer bBuf = binningBuffer[iBinningBuffer];
    BuildTask task = bBuf.task;

    float splitSahMin = SAH_ELEM_COST * (task.geomEnd - task.geomBeg); // non split SAH
    int splitAxis = -1;
    int splitBinIndexBorder;
    Bin splitBinL;
    Bin splitBinR;

    for( int axis = 0 ; axis < 3 ; ++axis ) {
        Bin summedBinsL[BIN_COUNT];
        Bin summedBinsR[BIN_COUNT];

        int i;
        Bin b;

        b = bBuf.bins[axis][0];
        for( i = 0 ; i < BIN_COUNT - 1 ; ++i)
        {
            int index = i;
            summedBinsL[index] = b;
            expand(b, bBuf.bins[axis][index + 1]);
        }

        b = bBuf.bins[axis][BIN_COUNT - 1];
        for( i = 0 ; i < BIN_COUNT - 1 ; ++i)
        {
            int r_index = BIN_COUNT - 1 - i;
            summedBinsR[r_index] = b;
            expand(b, bBuf.bins[axis][r_index - 1]);
        }

        // L [x---]
        // R [-xxx]
        for(i = 0 ; i < BIN_COUNT - 1 ; ++i)
        {
            Bin L = summedBinsL[i];
            Bin R = summedBinsR[i + 1];

            if(0 == L.nElem || 0 == R.nElem) {
                continue;
            }

            // Compute SAH
            float saP = surfaceArea(task.lower, task.upper);
            float saL = surfaceArea(L.lower, L.upper);
            float saR = surfaceArea(R.lower, R.upper);
            float sah = 
                SAH_AABB_COST * 2.0f 
                + (saL / saP) * SAH_ELEM_COST * L.nElem
                + (saR / saP) * SAH_ELEM_COST * R.nElem;

            if( sah < splitSahMin )
            {
                splitSahMin = sah;
                splitAxis = axis;
                splitBinIndexBorder = i + 1;
                splitBinL = L;
                splitBinR = R;
            }
        }
    }

    binningBuffer[iBinningBuffer].splitAxis = splitAxis;
    binningBuffer[iBinningBuffer].splitBinIndexBorder = splitBinIndexBorder;

    if( localID.x == 0 )
    {
        s_taskCounter = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    uint lrTaskIndexLocal = 0;
    if(splitAxis < 0)
    {
        // NOP
    }
    else
    {
        InterlockedAdd( s_taskCounter, 2, lrTaskIndexLocal );
    }

    GroupMemoryBarrierWithGroupSync();

    uint nRingBuffer;
    uint stride;
    buildTasksOut.GetDimensions( nRingBuffer, stride );

    if( localID.x == 0 )
    {
        uint expect;
        uint newValue;
        uint curValue;
        for(;;)
        {
            expect = buildTaskRingRangeOut[1];
            newValue = (expect + s_taskCounter) % nRingBuffer;
            InterlockedCompareExchange( buildTaskRingRangeOut[1], expect, newValue, curValue );

            if(expect == curValue)
            {
                break;
            }
        }
        s_taskCounterBase = curValue;
    }

    GroupMemoryBarrierWithGroupSync();

    if(splitAxis < 0 || task.geomEnd - task.geomBeg <= 1)
    {
        // no split
        bvhNodes[task.currentNode].geomBeg = task.geomBeg;
        bvhNodes[task.currentNode].geomEnd = task.geomEnd;

        debugBuffer[gID.x] = -1;
    }
    else 
    {
        // store child AABB. the child AABBs are already caclulated.
        for(int axis = 0 ; axis < 3 ; ++axis)
        {
            bvhNodes[task.currentNode].lowerL[axis] = from_ordered(splitBinL.lower[axis]);
            bvhNodes[task.currentNode].upperL[axis] = from_ordered(splitBinL.upper[axis]);
            bvhNodes[task.currentNode].lowerR[axis] = from_ordered(splitBinR.lower[axis]);
            bvhNodes[task.currentNode].upperR[axis] = from_ordered(splitBinR.upper[axis]);
        }
        bvhNodes[task.currentNode].geomBeg = -1;
        bvhNodes[task.currentNode].geomEnd = -1;

        // allocate child nodes
        uint childnode;
        InterlockedAdd(bvhNodeCounter[0], 2, childnode);
        bvhNodes[task.currentNode].childNode = childnode;

        // add child task
        uint lrTaskIndex = s_taskCounterBase + lrTaskIndexLocal;
        uint lTaskIndex = lrTaskIndex % nRingBuffer;
        uint rTaskIndex = (lrTaskIndex + 1) % nRingBuffer;

        BuildTask lTask;
        lTask.geomBeg = task.geomBeg;
        lTask.geomEnd = task.geomBeg + splitBinL.nElem;
        lTask.currentNode = childnode;
        for(axis = 0 ; axis < 3 ; ++axis)
        {
            lTask.lower[axis] = splitBinL.lower[axis];
            lTask.upper[axis] = splitBinL.upper[axis];
        }
        buildTasksOut[lTaskIndex] = lTask;
        buildTasksOut[lTaskIndex].currentNode = childnode;
        // buildTasksOut.Append(lTask);

        BuildTask rTask;
        rTask.geomBeg = task.geomBeg + splitBinL.nElem;
        rTask.geomEnd = task.geomEnd;
        rTask.currentNode = childnode + 1;
        for(axis = 0 ; axis < 3 ; ++axis)
        {
            rTask.lower[axis] = splitBinR.lower[axis];
            rTask.upper[axis] = splitBinR.upper[axis];
        }
        buildTasksOut[rTaskIndex] = rTask;

        debugBuffer[gID.x] = lrTaskIndex;
        // buildTasksOut.Append(rTask);
    }
}
