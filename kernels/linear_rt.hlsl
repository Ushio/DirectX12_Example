#include "helper.hlsl"

#define FLT_MAX         3.402823466e+38

cbuffer arguments : register(b0, space0)
{
	int cb_width;
	int cb_height;
	int cb_pad0; int cb_pad1;
	float4x4 cb_inverseVP;
};

RWStructuredBuffer<uint> colorRGBXBuffer : register(u0);
RWStructuredBuffer<float3> vertexBuffer : register(u1);
RWStructuredBuffer<uint> indexBuffer : register(u2);

float3 homogeneous(float4 p)
{
	return float3(p.x, p.y, p.z) / p.w;
}
void shoot(out float3 ro, out float3 rd, int imageWidth, int imageHeight, int x, int y, float4x4 inverseVP)
{
	float xf = 2.0f * (float)(x - (float)imageWidth * 0.5f) / (float)imageWidth;
	float yf = -2.0f * (float)(y - (float)imageHeight * 0.5f) / (float)imageHeight;
	ro = homogeneous( mul( float4( xf, yf, -1.0f /*near*/, 1.0f ), inverseVP ) );
	rd = homogeneous( mul( float4( xf, yf, +1.0f /*far */, 1.0f ), inverseVP ) ) - ro;
	rd = normalize(rd);
}

float4 intersect_none() {
	return float4(-1.0f, 0.0f, 0.0f, 0.0f);
}
/*
    x  : intersected t. -1 is no-intersected
    yzw: un-normalized normal
*/
float4 intersect_sphere(float3 ro, float3 rd, float3 o, float r) 
{
    float A = dot(rd, rd);
    float3 S = ro - o;
    float3 SxRD = cross(S, rd);
    float D = A * r * r - dot(SxRD, SxRD);

    if (D < 0.0f) {
        return intersect_none();
    }

    float B = dot(S, rd);
    float sqrt_d = sqrt(D);
    float t0 = (-B - sqrt_d) / A;
    if (0.0f < t0) {
        float3 n = (rd * t0 + S);
		return float4(t0, n);
    }

    float t1 = (-B + sqrt_d) / A;
    if (0.0f < t1) {
        float3 n = (rd * t1 + S);
		return float4(t1, n);
    }
    return intersect_none();
}
/* 
 tmin must be initialized.
*/
inline bool intersect_ray_triangle(float3 ro, float3 rd, float3 v0, float3 v1, float3 v2, inout float tmin, out float2 uv)
{
	const float kEpsilon = 1.0e-8;

	float3 v0v1 = v1 - v0;
	float3 v0v2 = v2 - v0;
	float3 pvec = cross(rd, v0v2);
	float det = dot(v0v1, pvec);

	if (abs(det) < kEpsilon) {
		return false;
	}

	float invDet = 1.0f / det;

	float3 tvec = ro - v0;
	float u = dot(tvec, pvec) * invDet;
	if (u < 0.0f || u > 1.0f) {
		return false;
	}

	float3 qvec = cross(tvec, v0v1);
	float v = dot(rd, qvec) * invDet;
	if (v < 0.0f || u + v > 1.0f) {
		return false;
	}

	float t = dot(v0v2, qvec) * invDet;

	if( t < 0.0f ) {
		return false;
	}
	if( tmin < t) {
		return false;
	}
	tmin = t;
	uv = float2(u, v);
	return true;

    // Branch Less Ver
	// float3 v0v1 = v1 - v0;
	// float3 v0v2 = v2 - v0;
	// float3 pvec = cross(rd, v0v2);
	// float det = dot(v0v1, pvec);
	// float3 tvec = ro - v0;
	// float3 qvec = cross(tvec, v0v1);

	// float invDet = 1.0f / det;
	// float u = dot(tvec, pvec) * invDet;
	// float v = dot(rd, qvec) * invDet;
	// float t = dot(v0v2, qvec) * invDet;

	// const float kEpsilon = 1.0e-8;
	// if (kEpsilon < abs(det) && 0.0f < u && 0.0f < v && u + v < 1.0f && 0.0f < t & t < tmin) {
	// 	tmin = t;
	// 	uv = float2(u, v);
	// 	return true;
	// }
	// return false;
}

float compMin(float3 v){
	return min(min(v.x, v.y), v.z);
}
float compMax(float3 v){
	return max(max(v.x, v.y), v.z);
}
bool slabs(float3 p0, float3 p1, float3 ro, float3 one_over_rd, float farclip_t) {
	float3 t0 = (p0 - ro) * one_over_rd;
	float3 t1 = (p1 - ro) * one_over_rd;

	// t0 = select(t0, -t1, isnan(t0));
	// t1 = select(t1, -t0, isnan(t1));

	float3 tmin = min(t0, t1), tmax = max(t0, t1);
	float region_min = compMax(tmin);
	float region_max = compMin(tmax);
	
	return region_min <= region_max && 0.0f <= region_max && region_min <= farclip_t;
}

#define NUM_THREAD 64
groupshared float3 localVertices[NUM_THREAD * 3];

[numthreads(NUM_THREAD, 1, 1)]
void main( uint3 gID : SV_DispatchThreadID, uint3 localID: SV_GroupThreadID )
{
	int sharedBaseIndex = WaveGetLaneCount() * (localID.x / WaveGetLaneCount()) * 3;

	int x = gID.x % cb_width;
	int y = gID.x / cb_width;

	float3 ro;
	float3 rd;
	shoot(ro, rd, cb_width, cb_height, x, y, cb_inverseVP);

	float4 isect = float4(-1.0f, 0.0f, 0.0f, 0.0f);
	// float4 isect = intersect_sphere(ro, rd, float3(1.0f, 1.0f, 0.0f), 1.0f);

	int indexCount = numberOfElement(indexBuffer);
	int primCount = indexCount / 3;

	float tmin = isect.x < 0.0f ? FLT_MAX : isect.x;
	float2 uv;
	
	float3 one_over_rd = float3(1.0f, 1.0f, 1.0f) / rd;

	int iPrim = 0;
	while(iPrim < primCount)
	{
		// load WaveGetLaneCount() prims
		int index = min(
			iPrim + WaveGetLaneIndex(),
			primCount - 1
		) * 3;
		float3 v0 = vertexBuffer[indexBuffer[index]];
		float3 v1 = vertexBuffer[indexBuffer[index+1]];
		float3 v2 = vertexBuffer[indexBuffer[index+2]];
		
		float3 lower = min(min(v0, v1), v2);
		float3 upper = max(max(v0, v1), v2);
		float3 waveLower = WaveActiveMin(lower);
		float3 waveUpper = WaveActiveMax(upper);
		bool skipOK = slabs(waveLower, waveUpper, ro, one_over_rd, tmin) == false;

		// check wave because all threads have to use the same iPrim
		if( WaveActiveAllTrue(skipOK) )
		{
			// Go next block
			iPrim += WaveGetLaneCount();
			continue;
		}

		// store LDS
		int storeBaseIndex = sharedBaseIndex + WaveGetLaneIndex() * 3;
		localVertices[storeBaseIndex]     = v0;
		localVertices[storeBaseIndex + 1] = v1;
		localVertices[storeBaseIndex + 2] = v2;

		GroupMemoryBarrierWithGroupSync();

		int n = min(iPrim + WaveGetLaneCount(), primCount) - iPrim;
		for(int i = 0 ; i < n ; ++i)
		{
			int baseIndex = sharedBaseIndex + i * 3;
			float3 v0 = localVertices[baseIndex];
			float3 v1 = localVertices[baseIndex+1];
			float3 v2 = localVertices[baseIndex+2];

			if(intersect_ray_triangle(ro, rd, v0, v1, v2, tmin, uv))
			{
				float3 n = cross(v1 - v0, v2 - v0);
				isect = float4(tmin, -n /* index buffer stored as CW */);
			}
		}
		
		iPrim += WaveGetLaneCount();
	}

	// for(int iPrim = 0 ; iPrim < primCount ; iPrim++)
	// {
	// 	int index = iPrim * 3;
	// 	float3 v0 = vertexBuffer[indexBuffer[index]];
	// 	float3 v1 = vertexBuffer[indexBuffer[index+1]];
	// 	float3 v2 = vertexBuffer[indexBuffer[index+2]];

	// 	if(intersect_ray_triangle(ro, rd, v0, v1, v2, tmin, uv))
	// 	{
	// 		float3 n = cross(v1 - v0, v2 - v0);
	// 		isect = float4(tmin, -n /* index buffer stored as CW */);
	// 	}
	// }

	if(numberOfElement(colorRGBXBuffer) <= gID.x) {
		return;
	}
	
	float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);
	if(0.0f < isect.x)
	{
		color = float4((normalize(isect.yzw) + float3(1.0f, 1.0f, 1.0f)) * 0.5f, 1.0f);
	}

	// float4 color = (x / 10 + y / 10) % 2 == 0 ? float4(0.2f, 0.2f, 0.2f, 1.0f) : float4(0.8f, 0.8f, 0.8f, 1.0f);
	int4 quantized = int4(color * 255.0f + float4(0.5f, 0.5f, 0.5f, 0.5f));
	quantized = clamp( quantized, int4(0, 0, 0, 0), int4( 255, 255, 255, 255) );
	uint value = 
		quantized.r       | 
		quantized.g << 8  |
		quantized.b << 16 |
		quantized.a << 24;
	colorRGBXBuffer[gID.x] = value;
}
