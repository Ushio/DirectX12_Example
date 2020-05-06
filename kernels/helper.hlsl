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

int to_ordered(float f) {
	uint b = asuint(f);
	uint s = b & 0x80000000; // sign bit
	int  x = b & 0x7FFFFFFF; // expornent and significand
	return s ? -x : x;
}
float from_ordered(int ordered) {
	if (ordered < 0) {
		uint x = -ordered;
		return asfloat(x | 0x80000000);
	}
	return asfloat(ordered);
}

#endif