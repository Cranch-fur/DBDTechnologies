/*
	One thread per pixel: cone-marches the reflection ray through the voxel clipmaps.
	Output (premultiplied, like the engine SSR): rgb = reflected radiance, a = coverage. Where a < 1 the engine adds
	its reflection captures and sky light on top, so rays that leave the cache fall back to the old reflections.

	Mode 6 marches the camera rays instead (r.VoxelReflections.Debug 6): shows what the voxel cache contains.
*/
#include "VoxelCommon.hlsli"

Texture2D<float>  SceneDepth : register(t0);
Texture2D<float4> GBufferA   : register(t1);   // World normal * 0.5 + 0.5
Texture2D<float4> GBufferB   : register(t2);   // Metallic, specular, roughness, shading model

Texture3D<float3> Radiance0  : register(t4);   // This frame's radiance volumes, 0 where empty
Texture3D<float3> Radiance1  : register(t5);
Texture3D<float3> Radiance2  : register(t6);
Texture3D<float3> Radiance3  : register(t7);
StructuredBuffer<int4> State : register(t8);
Texture3D<float>  Occupancy0 : register(t9);
Texture3D<float>  Occupancy1 : register(t10);
Texture3D<float>  Occupancy2 : register(t11);
Texture3D<float>  Occupancy3 : register(t12);

SamplerState LinearWrap : register(s0);

RWTexture2D<float4> Output : register(u0);

static const uint MaxSteps = 160;
static const float StopCoverage = 0.97;

bool InsideWindow(float3 position, uint level)
{
	const float3 cell = position / VoxelSize(level);
	const float3 origin = (float3)State[level].xyz;
	return State[level].w != 0 && all(cell >= origin + 1.0) && all(cell <= origin + (float)Resolution - 1.0);
}

/* rgb = radiance, a = occupancy */
float4 SampleVoxels(uint level, float3 position)
{
	const float3 uvw = position / (VoxelSize(level) * (float)Resolution);
	float3 radiance;
	float occupancy;
	switch (level)
	{
		case 0: radiance = Radiance0.SampleLevel(LinearWrap, uvw, 0); occupancy = Occupancy0.SampleLevel(LinearWrap, uvw, 0); break;
		case 1: radiance = Radiance1.SampleLevel(LinearWrap, uvw, 0); occupancy = Occupancy1.SampleLevel(LinearWrap, uvw, 0); break;
		case 2: radiance = Radiance2.SampleLevel(LinearWrap, uvw, 0); occupancy = Occupancy2.SampleLevel(LinearWrap, uvw, 0); break;
		default: radiance = Radiance3.SampleLevel(LinearWrap, uvw, 0); occupancy = Occupancy3.SampleLevel(LinearWrap, uvw, 0); break;
	}
	/* Empty voxels hold 0, so the filtered radiance is weighted by the filtered occupancy: divide it out */
	return float4(radiance / max(occupancy, 1e-3), occupancy);
}

/* Front-to-back cone march; coneTan = tan(half angle) */
float4 Trace(float3 origin, float3 direction, float coneTan, float maxDistance)
{
	float3 color = 0.0;
	float coverage = 0.0;
	float travelled = VoxelSize0;

	[loop]
	for (uint i = 0; i < MaxSteps && travelled < maxDistance && coverage < StopCoverage; ++i)
	{
		const float3 position = origin + direction * travelled;
		const float diameter = max(VoxelSize0, 2.0 * travelled * coneTan);

		uint level = (uint)clamp(ceil(log2(diameter / VoxelSize0)), 0.0, (float)(LEVEL_COUNT - 1));
		[loop]
		while (level < LEVEL_COUNT && InsideWindow(position, level) == false)
			++level;
		if (level >= LEVEL_COUNT)
			break;   // Left the cache

		const float voxelSize = VoxelSize(level);
		const float stepLength = max(0.5 * diameter, 0.5 * voxelSize);
		const float4 voxel = SampleVoxels(level, position);

		/* Occupancy is stored per voxel; correct it for the step length */
		const float alpha = 1.0 - pow(saturate(1.0 - voxel.a), stepLength / voxelSize);
		color += (1.0 - coverage) * alpha * voxel.rgb;
		coverage += (1.0 - coverage) * alpha;
		travelled += stepLength;
	}
	return float4(color, coverage);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Size.x || id.y >= Size.y)
		return;
	const uint2 pixel = Offset + id.xy;
	const float3 camera = CameraPosition();

	if (Mode == 6)
	{
		const float3 direction = normalize(Unproject(pixel, 0.5) - camera);
		const float4 result = Trace(camera, direction, 0.001, 1e6);
		Output[id.xy] = float4(result.rgb * Scale, 1.0);
		return;
	}

	const float deviceDepth = SceneDepth[pixel];
	if (deviceDepth <= 0.0)
	{
		Output[id.xy] = 0.0;
		return;
	}

	const float roughness = GBufferB[pixel].b;
	const float fade = saturate((MaxRoughness - roughness) / 0.1);
	if (fade <= 0.0)
	{
		Output[id.xy] = 0.0;
		return;
	}

	const float3 position = Unproject(pixel, deviceDepth);
	const float3 normal = normalize(GBufferA[pixel].rgb * 2.0 - 1.0);
	const float3 toCamera = normalize(camera - position);
	const float3 direction = reflect(-toCamera, normal);

	/* Start two voxels above the surface so the ray does not hit the voxel of the surface itself */
	const float3 origin = position + normal * (2.0 * VoxelSize0);
	const float coneTan = clamp(roughness * roughness, 0.01, 0.5);

	Output[id.xy] = Trace(origin, direction, coneTan, MaxDistance) * fade;
}
