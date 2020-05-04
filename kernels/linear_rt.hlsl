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
	// if (kEpsilon < fabs(det) && 0.0f < u && 0.0f < v && u + v < 1.0f && 0.0f < t & t < *tmin) {
	// 	*tmin = t;
	// 	*uv = (float2)(u, v);
	// 	return true;
	// }
	// return false;
}

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	if(numberOfElement(colorRGBXBuffer) <= gID.x)
	{
		return;
	}

	int x = gID.x % cb_width;
	int y = gID.x / cb_width;

	float3 ro;
	float3 rd;
	shoot(ro, rd, cb_width, cb_height, x, y, cb_inverseVP);

	// float4 isect = float4(-1.0f, 0.0f, 0.0f, 0.0f);
	float4 isect = intersect_sphere(ro, rd, float3(1.0f, 1.0f, 0.0f), 1.0f);

	int indexCount = numberOfElement(indexBuffer);
	float tmin = isect.x < 0.0f ? FLT_MAX : isect.x;

	float2 uv;
	for(int i = 0 ; i < indexCount ; i += 3)
	{
		float3 v0 = vertexBuffer[indexBuffer[i]];
		float3 v1 = vertexBuffer[indexBuffer[i+1]];
		float3 v2 = vertexBuffer[indexBuffer[i+2]];
		if(intersect_ray_triangle(ro, rd, v0, v1, v2, tmin, uv))
		{
			float3 n = cross(v1 - v0, v2 - v0);
			isect = float4(tmin, -n /* index buffer stored as CW */);
		}
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
