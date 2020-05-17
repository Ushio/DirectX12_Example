#include "helper.hlsl"
#include "bvh.h"

#define FLT_MAX          3.402823466e+38F        // max value

cbuffer BinningArgument : register(b0, space0)
{
	int consumeTaskCount;
};

RWStructuredBuffer<uint> executionCount : register(u0);
RWStructuredBuffer<uint> executionTable : register(u1);
RWStructuredBuffer<uint> executionIterator : register(u2);

RWStructuredBuffer<BinningBuffer> binningBuffer : register(u3);

RWStructuredBuffer<uint> bvhElementIndicesIn : register(u4);
RWStructuredBuffer<uint> bvhElementIndicesOut : register(u5);
RWStructuredBuffer<BvhElement> bvhElements : register(u6);

groupshared int iBinningBuffer;
groupshared int iExecutionOnTask;

[numthreads(EXECUTION_BATCH_COUNT, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
    for(;;)
    {
        if(localID.x == 0)
        {
            uint nExecution = executionCount[consumeTaskCount - 1] + executionTable[consumeTaskCount - 1];
            uint iExecution;
            InterlockedAdd(executionIterator[0], 1, iExecution);
            if(nExecution <= iExecution)
            {
                iBinningBuffer = -1; // done
            }
            else
            {
                // Find an assigned bin
                int beg = 0;
                int end = consumeTaskCount;
                while( end - beg != 1 )
                {
                    int mid = (beg + end) / 2;
                    if( iExecution < executionTable[mid] )
                    {
                        end = mid;
                    }
                    else
                    {
                        beg = mid;
                    }
                }
                iBinningBuffer = beg;
                iExecutionOnTask = iExecution - executionTable[beg];
            }
        }

        GroupMemoryBarrierWithGroupSync();

        // finish, no any tasks
        if( iBinningBuffer < 0 )
        {
            break;
        }
        
        // store to bins local
        int splitAxis = binningBuffer[iBinningBuffer].splitAxis;
        BuildTask task = binningBuffer[iBinningBuffer].task;
        int nElem = task.geomEnd - task.geomBeg;

        if( splitAxis < 0 )
        {
            int index = task.geomBeg + iExecutionOnTask * EXECUTION_BATCH_COUNT + localID.x;
            if( index < task.geomEnd )
            {
                bvhElementIndicesOut[ index ] = bvhElementIndicesIn[ index ];
            }
        }
        else
        {
            int splitBinIndexBorder = binningBuffer[iBinningBuffer].splitBinIndexBorder;
            float lowerBound = from_ordered(task.lower[splitAxis]);
            float upperBound = from_ordered(task.upper[splitAxis]);
            int index = task.geomBeg + iExecutionOnTask * EXECUTION_BATCH_COUNT + localID.x;
            if( index < task.geomEnd)
            {
                uint iPrim = bvhElementIndicesIn[ index ];
                float x = bvhElements[iPrim].centeroid[splitAxis];
                float location_f = (x - lowerBound) / (upperBound - lowerBound);
                int bin_idx = clamp((int)(location_f * (float)BIN_COUNT), 0, BIN_COUNT - 1);
                
                int to_index;
                if(bin_idx < splitBinIndexBorder)
                {
                    InterlockedAdd(binningBuffer[iBinningBuffer].splitLCounter, 1, to_index);
                }
                else
                {
                    int r_index;
                    InterlockedAdd(binningBuffer[iBinningBuffer].splitRCounter, 1, r_index);
                    to_index = nElem - r_index - 1;
                }
                to_index += task.geomBeg;

                bvhElementIndicesOut[ to_index ] = iPrim;
            }

            // bool addL = false;
            // bool addR = false;
            // uint iPrim = 0;
            // if( index < task.geomEnd )
            // {
            //     iPrim = bvhElementIndicesIn[ index ];
            //     float x = bvhElements[iPrim].centeroid[splitAxis];
            //     float location_f = (x - lowerBound) / (upperBound - lowerBound);
            //     int bin_idx = clamp((int)(location_f * (float)BIN_COUNT), 0, BIN_COUNT - 1);
                
            //     if(bin_idx < splitBinIndexBorder)
            //     {
            //         addL = true;
            //     }
            //     else
            //     {
            //         addR = true;
            //     }
            // }
            // int lidx = WavePrefixCountBits(addL);
            // int ridx = WavePrefixCountBits(addR);
            // int ln = WaveActiveCountBits(addL);
            // int rn = WaveActiveCountBits(addR);
            // int lBase = 0;
            // int rBase = 0;
            // if( WaveIsFirstLane() )
            // {
            //     InterlockedAdd(binningBuffer[iBinningBuffer].splitLCounter, ln, lBase);
            //     InterlockedAdd(binningBuffer[iBinningBuffer].splitRCounter, rn, rBase);
            // }
            // lBase = WaveReadLaneFirst(lBase);
            // rBase = WaveReadLaneFirst(rBase);
            // if( addL )
            // {
            //     int to_index = task.geomBeg + lBase + lidx;
            //     bvhElementIndicesOut[ to_index ] = iPrim;
            // }
            // else if( addR )
            // {
            //     int to_index = task.geomBeg + nElem - (rBase + ridx) - 1;
            //     bvhElementIndicesOut[ to_index ] = iPrim;
            // }
        }
    }
}
