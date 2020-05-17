#ifndef __BVH_H__
#define __BVH_H__

#define BIN_COUNT 8

#define EXECUTION_BATCH_COUNT 64

#define SAH_AABB_COST 1.0f
#define SAH_ELEM_COST 2.0f

struct BuildTask 
{
    int lower[3];
    int upper[3];
    int geomBeg;
    int geomEnd;
    int currentNode;
};
struct BvhElement 
{
    int lower[3];
    int upper[3];
    float centeroid[3];
};

/*
0 <= geomBeg : Leaf Node. childNode, lowerL, upperL, lowerR, upperR is undefined.
geomBeg < 0  : Branch Node. geomBeg == -1, geomEnd == -1
*/
struct BvhNode
{
    // childNode  : left child
    // childNode+1: right child
    int childNode;

    // AABBs
    float lowerL[3];
    float upperL[3];
    float lowerR[3];
    float upperR[3];

    // Leaf
    int geomBeg;
    int geomEnd;
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
    
    int iProcess;
    Bin bins[3][BIN_COUNT];
    
    int splitAxis;
    int splitBinIndexBorder; // bin_idx < splitBinIndexBorder is left, otherwise right
    
    int splitLCounter;
    int splitRCounter;
};

#endif
