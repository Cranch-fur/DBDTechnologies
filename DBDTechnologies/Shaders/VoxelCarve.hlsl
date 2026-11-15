/*
	One thread per voxel of every level (dispatch z covers Resolution * LEVEL_COUNT).
	1. Copies last frame's radiance into this frame's volume (the two alternate).
	2. Clears voxels that just scrolled into the window (or all of them after a reset).
	3. Fades voxels the camera can see through: even the closest depth inside the voxel's on-screen footprint lies
	   more than 1.5 voxels behind it, so whatever was there has moved or never existed (no trails behind characters).
	   Testing the whole footprint, not one pixel, keeps object edges from being erased and refilled every frame.
*/
#include "VoxelCommon.hlsli"

Texture2D<float> SceneDepth : register(t0);
Texture3D<float3> PreviousRadiance0 : register(t4);
Texture3D<float3> PreviousRadiance1 : register(t5);
Texture3D<float3> PreviousRadiance2 : register(t6);
Texture3D<float3> PreviousRadiance3 : register(t7);
StructuredBuffer<int4> State : register(t8);

RWTexture3D<float>  Occupancy0 : register(u0);
RWTexture3D<float>  Occupancy1 : register(u1);
RWTexture3D<float>  Occupancy2 : register(u2);
RWTexture3D<float>  Occupancy3 : register(u3);
RWTexture3D<float3> Radiance0  : register(u4);
RWTexture3D<float3> Radiance1  : register(u5);
RWTexture3D<float3> Radiance2  : register(u6);
RWTexture3D<float3> Radiance3  : register(u7);

float LoadOccupancy(uint level, uint3 texel)
{
	switch (level)
	{
		case 0: return Occupancy0[texel];
		case 1: return Occupancy1[texel];
		case 2: return Occupancy2[texel];
		default: return Occupancy3[texel];
	}
}

float3 LoadPreviousRadiance(uint level, uint3 texel)
{
	const int4 location = int4(texel, 0);
	switch (level)
	{
		case 0: return PreviousRadiance0.Load(location);
		case 1: return PreviousRadiance1.Load(location);
		case 2: return PreviousRadiance2.Load(location);
		default: return PreviousRadiance3.Load(location);
	}
}

void StoreVoxel(uint level, uint3 texel, float occupancy, float3 radiance)
{
	switch (level)
	{
		case 0: Occupancy0[texel] = occupancy; Radiance0[texel] = radiance; break;
		case 1: Occupancy1[texel] = occupancy; Radiance1[texel] = radiance; break;
		case 2: Occupancy2[texel] = occupancy; Radiance2[texel] = radiance; break;
		default: Occupancy3[texel] = occupancy; Radiance3[texel] = radiance; break;
	}
}

float2 ToNdc(float3 position)
{
	const float4 clip = mul(float4(position, 1.0), View_WorldToClip);
	return clip.xy / clip.w;
}

/* True when the depth buffer sees more than 1.5 voxels past the voxel everywhere in its footprint */
bool SeenThrough(float3 centre, float voxelSize)
{
	const float4 clip = mul(float4(centre, 1.0), View_WorldToClip);
	if (clip.w <= voxelSize)
		return false;   // At or behind the camera
	const float2 ndc = clip.xy / clip.w;
	if (any(abs(ndc) >= 1.0))
		return false;   // Off screen: nothing is known

	/* Footprint radius: the largest screen offset of the voxel's half extents */
	const float halfSize = 0.5 * voxelSize;
	const float2 dx = abs(ToNdc(centre + float3(halfSize, 0.0, 0.0)) - ndc);
	const float2 dy = abs(ToNdc(centre + float3(0.0, halfSize, 0.0)) - ndc);
	const float2 dz = abs(ToNdc(centre + float3(0.0, 0.0, halfSize)) - ndc);
	const float2 radius = max(max(dx, dy), dz) * 0.5 * (float2)Size;   // Pixels

	const float2 middle = (ndc * float2(0.5, -0.5) + 0.5) * (float2)Size;
	const float2 corners[5] = { float2(0.0, 0.0), float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0), float2(1.0, 1.0) };

	/* Reversed Z: the closest surface has the largest device depth (sky = 0) */
	float closest = 0.0;
	[unroll]
	for (uint i = 0; i < 5; ++i)
	{
		const float2 at = clamp(middle + corners[i] * radius, 0.0, (float2)Size - 1.0);
		closest = max(closest, SceneDepth[Offset + (uint2)at]);
	}

	/* clip.z is the near plane for every point, clip.w the view depth: scene view depth = near / device depth */
	const float sceneViewDepth = closest > 0.0 ? clip.z / closest : 1e30;
	return sceneViewDepth - clip.w > 1.5 * voxelSize;
}

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
	const uint level = id.z / Resolution;
	if (level >= LEVEL_COUNT || id.x >= Resolution || id.y >= Resolution)
		return;
	const uint3 texel = uint3(id.xy, id.z % Resolution);
	const int resolution = (int)Resolution;

	const int4 current = State[level];
	const int4 previous = State[LEVEL_COUNT + level];

	/* World cell stored in this texel for the current window */
	const int3 cell = current.xyz + PositiveMod((int3)texel - current.xyz, resolution);
	const bool wasInside = current.w != 0 && previous.w != 0 && all(cell >= previous.xyz) && all(cell < previous.xyz + resolution);
	if (wasInside == false)
	{
		StoreVoxel(level, texel, 0.0, 0.0);
		return;
	}

	float occupancy = LoadOccupancy(level, texel);
	float3 radiance = LoadPreviousRadiance(level, texel);

	const float voxelSize = VoxelSize(level);
	if (occupancy > 0.0 && SeenThrough(((float3)cell + 0.5) * voxelSize, voxelSize))
	{
		occupancy *= CarveRate;
		if (occupancy < 0.05)
		{
			occupancy = 0.0;
			radiance = 0.0;
		}
	}
	StoreVoxel(level, texel, occupancy, radiance);
}
