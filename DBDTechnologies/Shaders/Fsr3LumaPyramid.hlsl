/*
	ffx_fsr3upscaler_luma_pyramid_pass.hlsl binds 9 UAVs, feature level 11.0 allows 8. This pass only touches mip 5 of the
	SPD chain, so mips 0-4 are left unbound and the pyramid accessors work on mip 5 alone.
*/
#include "Fsr3Options.hlsli"

#define FSR3UPSCALER_BIND_SRV_CURRENT_LUMA          0
#define FSR3UPSCALER_BIND_SRV_FARTHEST_DEPTH        1

#define FSR3UPSCALER_BIND_UAV_SPD_GLOBAL_ATOMIC     0
#define FSR3UPSCALER_BIND_UAV_FRAME_INFO            1
#define FSR3UPSCALER_BIND_UAV_FARTHEST_DEPTH_MIP1   2

#define FSR3UPSCALER_BIND_CB_FSR3UPSCALER           0
#define FSR3UPSCALER_BIND_CB_SPD                    1

#include "fsr3upscaler/ffx_fsr3upscaler_callbacks_hlsl.h"
#include "fsr3upscaler/ffx_fsr3upscaler_common.h"

/* The name maps it to SPD mip 5 in ffx_fsr3upscaler.cpp */
globallycoherent RWTexture2D<FfxFloat32x2> rw_spd_mip5 : register(u3);

FfxFloat32x2 RWLoadPyramid(FfxInt32x2 iPxPos, FfxUInt32 index)
{
    return rw_spd_mip5[iPxPos];
}

void StorePyramid(FfxInt32x2 iPxPos, FfxFloat32x2 outValue, FfxUInt32 index)
{
    rw_spd_mip5[iPxPos] = outValue;
}

#include "fsr3upscaler/ffx_fsr3upscaler_luma_pyramid.h"

[numthreads(256, 1, 1)]
void CS(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
    ComputeAutoExposure(WorkGroupId, LocalThreadIndex);
}
