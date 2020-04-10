#ifndef __HELPER__
#define __HELPER__

uint numberOfElement(RWStructuredBuffer<uint> xs)
{
	uint numStruct;
	uint stride;
	xs.GetDimensions( numStruct, stride );
	return numStruct;
}

uint numberOfElement(RWStructuredBuffer<float> xs)
{
	uint numStruct;
	uint stride;
	xs.GetDimensions( numStruct, stride );
	return numStruct;
}

uint numberOfElement(RWStructuredBuffer<float4> xs)
{
	uint numStruct;
	uint stride;
	xs.GetDimensions( numStruct, stride );
	return numStruct;
}

#endif