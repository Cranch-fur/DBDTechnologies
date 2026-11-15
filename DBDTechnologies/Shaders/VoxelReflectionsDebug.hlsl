/*
	Debug views written into the engine SSR output (r.VoxelReflections.Debug 1..5).
	The reflection composite multiplies this by the surface reflectance, so values are scaled up by Scale.
	Alpha 1 = full SSR coverage: reflection captures and the sky light are not added on top.
*/
#include "VoxelCommon.hlsli"

Texture2D<float>  SceneDepth : register(t0);   // Reversed Z, R32 view of the D32S8 depth buffer
Texture2D<float4> GBufferA   : register(t1);   // World normal * 0.5 + 0.5
Texture2D<float4> GBufferB   : register(t2);   // Metallic, specular, roughness, shading model

RWTexture2D<float4> Output : register(u0);

static const float AssumedNearPlane = 10.0;   // cm; only used for the depth bands
static const float BandLength = 500.0;        // cm

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Size.x || id.y >= Size.y)
		return;
	const uint2 pixel = id.xy + Offset;
	const float deviceDepth = SceneDepth[pixel];

	float3 color = float3(1.0, 0.0, 1.0);
	if (Mode == 2)
	{
		color = GBufferA[pixel].rgb;
	}
	else if (Mode == 3)
	{
		const float viewDepth = AssumedNearPlane / max(deviceDepth, 1e-7);
		const float band = frac(viewDepth / BandLength);
		color = deviceDepth <= 0.0 ? float3(0.0, 0.3, 1.0) : float3(band, band * band, 1.0 - band);
	}
	else if (Mode == 4)
	{
		color = GBufferB[pixel].bbb;
	}
	else if (Mode == 5)
	{
		/* World grid from the engine View uniform buffer: 1 m cells, X red, Y green, Z blue lines.
		   The lines must stay glued to surfaces while the camera moves; otherwise the buffer slot or layout is wrong */
		const float3 position = Unproject(pixel, max(deviceDepth, 1e-6));
		const float3 cell = frac(position / 100.0);
		const float3 lines = step(cell, 0.04);
		const float3 camera = CameraPosition();
		const bool valid = all(abs(position) < 1e7) && all(abs(camera) < 1e7);
		color = valid ? 0.05 + lines : float3(1.0, 0.0, 0.0) * frac(Frame / 30.0);
	}

	Output[id.xy] = float4(color * Scale, 1.0);
}
