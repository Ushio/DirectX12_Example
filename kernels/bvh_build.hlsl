#include "helper.hlsl"
#include "bvh.h"

#define BIN_COUNT 16
#define NUM_THREAD 128

#define FLT_MAX          3.402823466e+38F        // max value

#define SAH_AABB_COST 1.0f
#define SAH_ELEM_COST 2.0f

float surfaceArea(int lower[3], int upper[3]) {
    float3 l = float3(from_ordered(lower[0]), from_ordered(lower[1]), from_ordered(lower[2]));
    float3 u = float3(from_ordered(upper[0]), from_ordered(upper[1]), from_ordered(upper[2]));
    float3 size = l - u;
    return dot(size.xyz, size.yzx) * 2.0f;
}

uint numberOfElement(RWStructuredBuffer<BvhElement> xs)
{
	uint numStruct;
	uint stride;
	xs.GetDimensions( numStruct, stride );
	return numStruct;
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

struct Bin {
    // bin AABB
    int lower[3];
    int upper[3];

    // element counter
    int nElem;
};

// Selected Task
groupshared BuildTask task;

// Binning
groupshared Bin bins[BIN_COUNT];
groupshared Bin summedBinsL[BIN_COUNT];
groupshared Bin summedBinsR[BIN_COUNT];

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

    for( int axis = 0 ; axis < 3 ; ++axis )
    {
        // clear bin
        if(localID.x < BIN_COUNT) {
            for(int i = 0 ; i < 3 ; ++i) {
                bins[localID.x].lower[i] = to_ordered(+FLT_MAX);
                bins[localID.x].upper[i] = to_ordered(-FLT_MAX);
            }
            bins[localID.x].nElem = 0;
        }
        GroupMemoryBarrierWithGroupSync();

        // store to bin
        float lowerBound = from_ordered(task.lower[axis]);
        float upperBound = from_ordered(task.upper[axis]);
        for(int i = task.geomBeg ; i < task.geomEnd ; i += NUM_THREAD)
        {
            // store bin
            int index = i + localID.x;
            if( index < task.geomEnd)
            {
                uint iPrim = bvhElementIndicesIn[ index ];
                float x = bvhElements[iPrim].centeroid[axis];
                float location_f = (x - lowerBound) / (upperBound - lowerBound);
                int bin_idx = clamp((int)(location_f * (float)BIN_COUNT), 0, BIN_COUNT - 1);
                
                // update bin AABB
                InterlockedMin(bins[bin_idx].lower[0], bvhElements[iPrim].lower[0]);
                InterlockedMax(bins[bin_idx].upper[0], bvhElements[iPrim].upper[0]);
                InterlockedMin(bins[bin_idx].lower[1], bvhElements[iPrim].lower[1]);
                InterlockedMax(bins[bin_idx].upper[1], bvhElements[iPrim].upper[1]);
                InterlockedMin(bins[bin_idx].lower[2], bvhElements[iPrim].lower[2]);
                InterlockedMax(bins[bin_idx].upper[2], bvhElements[iPrim].upper[2]);
                InterlockedAdd(bins[bin_idx].nElem, 1);
            }
        }

        GroupMemoryBarrierWithGroupSync();
        if(localID.x == 0)
        {
            int i;
            Bin b;

            // inclusive scan LR
            b = bins[0];
            for( i = 0 ; i < BIN_COUNT - 1 ; ++i)
            {
                int index = i;
                summedBinsL[index] = b;
                expand(b, bins[index + 1]);
            }

            b = bins[BIN_COUNT - 1];
            for( i = 0 ; i < BIN_COUNT - 1 ; ++i)
            {
                int r_index = BIN_COUNT - 1 - i;
                summedBinsR[r_index] = b;
                expand(b, bins[r_index - 1]);
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
    }

    splitLCounter = 0;
    splitRCounter = 0;
    GroupMemoryBarrierWithGroupSync();

    if(splitAxis < 0)
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
