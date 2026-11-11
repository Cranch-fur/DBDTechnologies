/* AMD FSR 1 RCAS, also used as the sharpening pass of every upscaler */

#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

cbuffer Constants : register(b0)
{
    uint4 Const0;
    uint4 Params;      // x = HDR input, y = passthrough, z = encode sRGB, w = undo the EASU HDR curve
    uint4 OutputSize;  // xy = size
};

Texture2D<float4>   InputTexture  : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

/* RCAS expects 0..1, HDR scene color goes through a reversible curve */
float3 Compress(float3 c)   { return c / (1.0 + max(max(c.r, c.g), c.b)); }
float3 Decompress(float3 c) { return c / max(1.0 - max(max(c.r, c.g), c.b), 1.0 / 65504.0); }

#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p)
{
    AF4 c = InputTexture.Load(int3(p, 0));
    if (Params.x != 0)
        c.rgb = Compress(max(c.rgb, 0.0));
    return c;
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}

#include "ffx_fsr1.h"

float3 LinearToSrgb(float3 c)
{
    c = saturate(c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutputSize.x || id.y >= OutputSize.y)
        return;

    float4 source = InputTexture.Load(int3(id.xy, 0));
    float3 color = source.rgb;

    if (Params.y == 0)
    {
        FsrRcasF(color.r, color.g, color.b, id.xy, Const0);
        if (Params.x != 0)
            color = Decompress(color);
    }

    if (Params.w != 0)
        color = color / max(1.0 - abs(color), 1.0 / 65504.0);

    if (Params.z != 0)
        color = LinearToSrgb(color);

    OutputTexture[id.xy] = float4(color, source.a);
}
