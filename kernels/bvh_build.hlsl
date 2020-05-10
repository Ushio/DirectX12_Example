#include "helper.hlsl"
#include "bvh.h"

#define FLT_MAX          3.402823466e+38F        // max value
#define NUM_THREAD 128

float surfaceArea(int lower[3], int upper[3]) {
    float3 l = float3(from_ordered(lower[0]), from_ordered(lower[1]), from_ordered(lower[2]));
    float3 u = float3(from_ordered(upper[0]), from_ordered(upper[1]), from_ordered(upper[2]));
    float3 size = l - u;
    return dot(size.xyz, size.yzx) * 2.0f;
}

ConsumeStructuredBuffer<BuildTask> buildTasksIn : register(u0);
AppendStructuredBuffer<BuildTask> buildTasksOut : register(u1);
RWStructuredBuffer<BvhElement> bvhElements : register(u2);

// output bvh
RWStructuredBuffer<BvhNode> bvhNodes : register(u3);
RWStructuredBuffer<uint> bvhNodeCounter : register(u4);

// reorder
RWStructuredBuffer<uint> bvhElementIndicesIn : register(u5);
RWStructuredBuffer<uint> bvhElementIndicesOut : register(u6);

// Selected Task
groupshared BuildTask task;

// Binning per axis
groupshared Bin bins[3][BIN_COUNT];
groupshared Bin summedBinsL[3][BIN_COUNT];
groupshared Bin summedBinsR[3][BIN_COUNT];

// Selected Split
groupshared float splitSahMin;
groupshared int splitAxis;
groupshared int splitBinIndexBorder;
groupshared Bin splitBinL;
groupshared Bin splitBinR;

// Reorder
groupshared int splitLCounter;
groupshared int splitRCounter;

void expand( inout Bin bin, Bin otherBin)
{
    for( int axis = 0 ; axis < 3 ; ++axis )
    {
        bin.lower[axis] = min(bin.lower[axis], otherBin.lower[axis]);
        bin.upper[axis] = max(bin.upper[axis], otherBin.upper[axis]);
    }
    bin.nElem += otherBin.nElem;
}

[numthreads(NUM_THREAD, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
    if(localID.x == 0)
    {
        task = buildTasksIn.Consume();
    }

    splitSahMin = FLT_MAX;
    splitAxis = -1;

    // clear bin
    if(localID.x < BIN_COUNT) {
        for(int j = 0 ; j < 3 ; ++j) {
            for(int i = 0 ; i < 3 ; ++i) {
                bins[j][localID.x].lower[i] = to_ordered(+FLT_MAX);
                bins[j][localID.x].upper[i] = to_ordered(-FLT_MAX);
            }
            bins[j][localID.x].nElem = 0;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // store to bin
    float3 lowerBound = float3( from_ordered(task.lower[0]), from_ordered(task.lower[1]), from_ordered(task.lower[2]) );
    float3 upperBound = float3( from_ordered(task.upper[0]), from_ordered(task.upper[1]), from_ordered(task.upper[2]) );
    for(int i = task.geomBeg ; i < task.geomEnd ; i += NUM_THREAD)
    {
        // store bin
        int index = i + localID.x;
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
    }

    GroupMemoryBarrierWithGroupSync();

    // inclusive scan LR for each axis
    if(localID.x < 3)
    {
        int axis = localID.x;

        int i;
        Bin b;
        
        b = bins[axis][0];
        for( i = 0 ; i < BIN_COUNT - 1 ; ++i)
        {
            int index = i;
            summedBinsL[axis][index] = b;
            expand(b, bins[axis][index + 1]);
        }

        b = bins[axis][BIN_COUNT - 1];
        for( i = 0 ; i < BIN_COUNT - 1 ; ++i)
        {
            int r_index = BIN_COUNT - 1 - i;
            summedBinsR[axis][r_index] = b;
            expand(b, bins[axis][r_index - 1]);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    
    if(localID.x == 0)
    {
        for( int axis = 0 ; axis < 3 ; ++axis ) {
            // L [x---]
            // R [-xxx]
            for(i = 0 ; i < BIN_COUNT - 1 ; ++i)
            {
                Bin L = summedBinsL[axis][i];
                Bin R = summedBinsR[axis][i + 1];

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
    }

    splitLCounter = 0;
    splitRCounter = 0;
    GroupMemoryBarrierWithGroupSync();

    float nonSplitSah = SAH_ELEM_COST * (task.geomEnd - task.geomBeg);
    if(splitAxis < 0 || nonSplitSah < splitSahMin )
    {
        // Finish split
        if(localID.x == 0)
        {
            bvhNodes[task.currentNode].geomBeg = task.geomBeg;
            bvhNodes[task.currentNode].geomEnd = task.geomEnd;
        }

        // straight copy indices
        for(int i = task.geomBeg ; i < task.geomEnd ; i += NUM_THREAD)
        {
            int index = i + localID.x;
            if (index < task.geomEnd )
            {
                bvhElementIndicesOut[ index ] = bvhElementIndicesIn[ index ];
            }
        }

        // Terminate
    }
    else
    {
        // Split
        if(localID.x == 0)
        {
            int axis;

            // store child AABB. the child AABBs are already caclulated.
            for(axis = 0 ; axis < 3 ; ++axis)
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
            BuildTask lTask;
            lTask.geomBeg = task.geomBeg;
            lTask.geomEnd = task.geomBeg + splitBinL.nElem;
            lTask.currentNode = childnode;
            for(axis = 0 ; axis < 3 ; ++axis)
            {
                lTask.lower[axis] = splitBinL.lower[axis];
                lTask.upper[axis] = splitBinL.upper[axis];
            }
            buildTasksOut.Append(lTask);

            BuildTask rTask;
            rTask.geomBeg = task.geomBeg + splitBinL.nElem;
            rTask.geomEnd = task.geomEnd;
            rTask.currentNode = childnode + 1;
            for(axis = 0 ; axis < 3 ; ++axis)
            {
                rTask.lower[axis] = splitBinR.lower[axis];
                rTask.upper[axis] = splitBinR.upper[axis];
            }
            buildTasksOut.Append(rTask);
        }

        int nElem = task.geomEnd - task.geomBeg;
        float lowerBound = from_ordered(task.lower[splitAxis]);
        float upperBound = from_ordered(task.upper[splitAxis]);
        for(int i = task.geomBeg ; i < task.geomEnd ; i += NUM_THREAD)
        {
            int index = i + localID.x;
            if (index < task.geomEnd )
            {
                uint iPrim = bvhElementIndicesIn[ index ];
                float x = bvhElements[iPrim].centeroid[splitAxis];
                float location_f = (x - lowerBound) / (upperBound - lowerBound);
                int bin_idx = clamp((int)(location_f * (float)BIN_COUNT), 0, BIN_COUNT - 1);
                
                int to_index;
                if(bin_idx < splitBinIndexBorder)
                {
                    InterlockedAdd(splitLCounter, 1, to_index);
                }
                else
                {
                    int r_index;
                    InterlockedAdd(splitRCounter, 1, r_index);
                    to_index = nElem - r_index - 1;
                }
                to_index += task.geomBeg;

                bvhElementIndicesOut[ to_index ] = iPrim;
            }
        }
    }
}
