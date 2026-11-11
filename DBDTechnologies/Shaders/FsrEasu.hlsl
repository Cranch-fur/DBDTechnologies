/* AMD FSR 1 EASU */

#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

cbuffer Constants : register(b0)
{
    uint4 Const0;
    uint4 Const1;
    uint4 Const2;
    uint4 Const3;
    uint4 OutputSize;   // xy = output size, z = linear HDR input
};

Texture2D<float4>   InputTexture  : register(t0);
SamplerState        LinearClamp   : register(s0);
RWTexture2D<float4> OutputTexture : register(u0);

/* EASU expects 0..1. Linear HDR goes through x / (1 + |x|) per channel because Gather returns one channel; RCAS undoes it */
AF4 Compress4(AF4 v) { return OutputSize.z != 0 ? v / (1.0 + abs(v)) : v; }

#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return Compress4(InputTexture.GatherRed(LinearClamp, p, int2(0, 0))); }
AF4 FsrEasuGF(AF2 p) { return Compress4(InputTexture.GatherGreen(LinearClamp, p, int2(0, 0))); }
AF4 FsrEasuBF(AF2 p) { return Compress4(InputTexture.GatherBlue(LinearClamp, p, int2(0, 0))); }

#include "ffx_fsr1.h"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutputSize.x || id.y >= OutputSize.y)
        return;

    AF3 color;
    FsrEasuF(color, id.xy, Const0, Const1, Const2, Const3);
    OutputTexture[id.xy] = AF4(color, 1.0);
}
