#ifndef __BVH_H__
#define __BVH_H__

#define BIN_COUNT 8

#define EXECUTION_BATCH_COUNT 64

#define SAH_AABB_COST 1.0f
#define SAH_ELEM_COST 2.0f

#if defined(_MSC_VER)
#include <stdint.h>
using uint = uint32_t;
#endif

struct BuildTask 
{
    int lower[3];
    int upper[3];
    int geomBeg;
    int geomEnd;
    int parentNode;
    int childOrder;
};
struct BvhElement 
{
    int lower[3];
    int upper[3];
    float centeroid[3];
};

/*
indexL[0], indexR[0] 0x80000000 bit
    0: branch
    1: leaf

    if indexL[0] or indexR[0] is child, the value is child node.
    otherwise, indexL[0] indexL[1] or indexR[0] indexR[1] indicates geomBeg, geomEnd
*/
struct BvhNode
{
    // AABBs
    float lowerL[3];
    float upperL[3];
    float lowerR[3];
    float upperR[3];

    // childs or geoms
    uint indexL[2];
    uint indexR[2];
};

struct Bin {
    // bin AABB
    int lower[3];
    int upper[3];

    // element counter
    int nElem;
};

struct BinningBuffer
{
    BuildTask task;
    
    Bin bins[3][BIN_COUNT];
    
    int splitAxis;
    int splitBinIndexBorder; // bin_idx < splitBinIndexBorder is left, otherwise right

    int splitLCounter;
    int splitRCounter;
};

#endif
