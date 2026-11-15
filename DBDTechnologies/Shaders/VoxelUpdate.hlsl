/* One thread: moves every clipmap window so the camera stays in its centre and keeps the previous origins for the carve pass */
#include "VoxelCommon.hlsli"

RWStructuredBuffer<int4> State : register(u0);

[numthreads(1, 1, 1)]
void main()
{
	const float3 camera = CameraPosition();
	const bool cameraValid = all(abs(camera) < 1e7);

	[unroll]
	for (uint level = 0; level < LEVEL_COUNT; ++level)
	{
		int4 previous = State[level];
		if (Reset != 0 || cameraValid == false)
			previous.w = 0;
		State[LEVEL_COUNT + level] = previous;

		if (cameraValid)
		{
			const int3 origin = (int3)floor(camera / VoxelSize(level)) - (int)(Resolution / 2);
			State[level] = int4(origin, 1);
		}
		else
			State[level] = int4(previous.xyz, 0);
	}
}
