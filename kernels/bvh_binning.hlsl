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
RWStructuredBuffer<BvhElement> bvhElements : register(u5);

groupshared int iBinningBuffer;
groupshared int iExecutionOnTask;

// Binning per axis
groupshared Bin bins[3][BIN_COUNT];

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

        // clear bin
        uint lower = to_ordered(+FLT_MAX);
        uint upper = to_ordered(-FLT_MAX);
        if(localID.x < BIN_COUNT) {
            for(int j = 0 ; j < 3 ; ++j) {
                for(int i = 0 ; i < 3 ; ++i) {
                    bins[j][localID.x].lower[i] = lower;
                    bins[j][localID.x].upper[i] = upper;
                }
                bins[j][localID.x].nElem = 0;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        // finish, no any tasks
        if( iBinningBuffer < 0 )
        {
            break;
        }
        
        // store to bins local
        BuildTask task = binningBuffer[iBinningBuffer].task;

        float3 lowerBound = float3( from_ordered(task.lower[0]), from_ordered(task.lower[1]), from_ordered(task.lower[2]) );
        float3 upperBound = float3( from_ordered(task.upper[0]), from_ordered(task.upper[1]), from_ordered(task.upper[2]) );

        int index = task.geomBeg + iExecutionOnTask * EXECUTION_BATCH_COUNT + localID.x;
        if( index < task.geomEnd)
        {
            uint iPrim = bvhElementIndicesIn[ index ];
            BvhElement element = bvhElements[iPrim];
            float3 x = float3(element.centeroid[0], element.centeroid[1], element.centeroid[2]);
            float3 location_f = (x - lowerBound) / (upperBound - lowerBound);
            int3 bin_idx = clamp((int3)(location_f * (float)BIN_COUNT), int3(0, 0, 0), int3(BIN_COUNT - 1, BIN_COUNT - 1, BIN_COUNT - 1));
            
            // update bin AABB
            for(int d = 0 ; d < 3 ; ++d)
            {
                int shift_d = (d + localID.x) % 3; // avoid conflict
                InterlockedMin(bins[0][bin_idx.x].lower[shift_d], bvhElements[iPrim].lower[shift_d]);
                InterlockedMax(bins[0][bin_idx.x].upper[shift_d], bvhElements[iPrim].upper[shift_d]);
                InterlockedMin(bins[1][bin_idx.y].lower[shift_d], bvhElements[iPrim].lower[shift_d]);
                InterlockedMax(bins[1][bin_idx.y].upper[shift_d], bvhElements[iPrim].upper[shift_d]);
                InterlockedMin(bins[2][bin_idx.z].lower[shift_d], bvhElements[iPrim].lower[shift_d]);
                InterlockedMax(bins[2][bin_idx.z].upper[shift_d], bvhElements[iPrim].upper[shift_d]);
            }

            InterlockedAdd(bins[0][bin_idx.x].nElem, 1);
            InterlockedAdd(bins[1][bin_idx.y].nElem, 1);
            InterlockedAdd(bins[2][bin_idx.z].nElem, 1);
        }

        GroupMemoryBarrierWithGroupSync();

        // store global
        if(localID.x < BIN_COUNT) {
            for(int j = 0 ; j < 3 ; ++j) {
                for(int i = 0 ; i < 3 ; ++i) {
                    InterlockedMin(binningBuffer[iBinningBuffer].bins[j][localID.x].lower[i], bins[j][localID.x].lower[i]);
                    InterlockedMax(binningBuffer[iBinningBuffer].bins[j][localID.x].upper[i], bins[j][localID.x].upper[i]);
                }
                InterlockedAdd(binningBuffer[iBinningBuffer].bins[j][localID.x].nElem, bins[j][localID.x].nElem);
            }
        }
    }
}
