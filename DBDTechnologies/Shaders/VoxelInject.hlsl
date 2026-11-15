/*
	One thread per 2x2 pixel block (the sampled pixel rotates every frame): blends the lit scene color of the visible
	surface into the voxel containing it, on every level whose window contains the point.
	Many pixels share a voxel and only one write survives per frame, so each write moves last frame's color only
	RADIANCE_BLEND of the way: the voxel settles on the average instead of flickering between its pixels.
*/
#include "VoxelCommon.hlsli"

Texture2D<float>  SceneDepth : register(t0);
Texture2D<float4> SceneColor : register(t3);
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

static const float MaxRadiance = 64.0;   // Pre-exposed scene color; clamps sparks and light sources so they do not smear

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

void WriteVoxel(uint level, uint3 texel, float3 radiance)
{
	switch (level)
	{
		case 0: Occupancy0[texel] = 1.0; Radiance0[texel] = radiance; break;
		case 1: Occupancy1[texel] = 1.0; Radiance1[texel] = radiance; break;
		case 2: Occupancy2[texel] = 1.0; Radiance2[texel] = radiance; break;
		default: Occupancy3[texel] = 1.0; Radiance3[texel] = radiance; break;
	}
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	const uint2 sub = uint2(Frame & 1u, (Frame >> 1) & 1u);
	const uint2 local = id.xy * 2u + sub;
	if (any(local >= Size))
		return;
	const uint2 pixel = Offset + local;

	const float deviceDepth = SceneDepth[pixel];
	if (deviceDepth <= 0.0)
		return;   // Sky

	const float3 position = Unproject(pixel, deviceDepth);
	const float3 observed = min(max(SceneColor[pixel].rgb, 0.0), MaxRadiance);
	const int resolution = (int)Resolution;

	[unroll]
	for (uint level = 0; level < LEVEL_COUNT; ++level)
	{
		const int4 window = State[level];
		const int3 cell = (int3)floor(position / VoxelSize(level));
		if (window.w == 0 || any(cell < window.xyz) || any(cell >= window.xyz + resolution))
			continue;
		const uint3 texel = (uint3)PositiveMod(cell, resolution);

		/* A voxel the carve pass left empty (scrolled in, seen through, reset) takes the sample as it is: its old
		   color belongs to another place, or it would fade in from black */
		const float3 history = LoadPreviousRadiance(level, texel);
		const bool fresh = Reset != 0 || LoadOccupancy(level, texel) <= 0.0 || all(history == 0.0);
		const float3 radiance = fresh ? observed : lerp(history, observed, RADIANCE_BLEND);
		WriteVoxel(level, texel, radiance);
	}
}
