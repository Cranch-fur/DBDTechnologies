/*
	Shared declarations of the voxel reflection passes.

	Voxel cache: LEVEL_COUNT nested clipmaps around the camera, Resolution^3 voxels each, voxel size doubling per level.
	A world cell c is stored at texel (c mod Resolution), so the window scrolls with the camera without copying
	(toroidal addressing) and a wrap sampler handles the lookup. Per level:
	  Occupancy  R32F        0 = empty .. 1 = surface seen recently
	  Radiance   R11G11B10F  lit scene color seen by the camera, 0 where empty. Two volumes alternate every frame:
	                         the carve pass copies last frame's into this frame's, the inject pass blends new
	                         samples into it (D3D11 cannot read a typed UAV of this format, hence the copy)

	State buffer (int4): [L] = window origin of level L in cells + valid flag, [LEVEL_COUNT + L] = previous frame's origin.
*/

#define LEVEL_COUNT 4

/* Share of a new sample in a voxel's color: many pixels land in one voxel, so colors are averaged over frames */
#define RADIANCE_BLEND 0.2

cbuffer Constants : register(b0)
{
	uint2 Offset;          // View rectangle origin in the engine textures
	uint2 Size;            // View rectangle size
	uint  Mode;            // Debug mode (r.VoxelReflections.Debug)
	float Scale;           // Debug brightness
	uint  Resolution;      // Voxels per clipmap axis
	uint  Reset;           // 1 = forget the cache this frame
	float VoxelSize0;      // cm, finest level
	float MaxRoughness;
	float MaxDistance;     // cm
	float CarveRate;       // Occupancy multiplier when the depth buffer sees through a voxel
	uint  Frame;
	float3 Padding;
};

/* First members of FViewUniformShaderParameters (UE 4.27), the engine's View uniform buffer bound by the SSR pass */
cbuffer View : register(b1)
{
	row_major float4x4 View_TranslatedWorldToClip;
	row_major float4x4 View_WorldToClip;
	row_major float4x4 View_ClipToWorld;
};

float VoxelSize(uint level)
{
	return VoxelSize0 * (float)(1u << level);
}

int3 PositiveMod(int3 value, int modulus)
{
	return ((value % modulus) + modulus) % modulus;
}

/* Reversed-Z infinite projection: the camera is the world point whose clip position is (0, 0, near, 0) */
float3 CameraPosition()
{
	const float4 h = mul(float4(0.0, 0.0, 1.0, 0.0), View_ClipToWorld);
	return h.xyz / h.w;
}

/* Pixel inside the view rectangle + device depth -> world position */
float3 Unproject(uint2 pixel, float deviceDepth)
{
	const float2 uv = ((float2)(pixel - Offset) + 0.5) / (float2)Size;
	const float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, deviceDepth, 1.0);
	const float4 h = mul(clip, View_ClipToWorld);
	return h.xyz / h.w;
}
