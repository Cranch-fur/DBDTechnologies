#include "MotionVectors.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")






static const char* shaderSource = R"(
cbuffer Constants : register(b0)
{
    row_major float4x4 ClipToPrevClip;
    float2 RenderSize;
    float2 InvRenderSize;
    uint   HasVelocity;
    uint   DecodeMode;
    float2 Pad;
};
Texture2D<float>    DepthTex : register(t0);
Texture2D<float4>   VelTex   : register(t1);
RWTexture2D<float2> OutMV    : register(u0);
RWTexture2D<float>  OutDepth : register(u1);

[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID)
{
    if (p.x >= (uint)RenderSize.x || p.y >= (uint)RenderSize.y) return;
    float d = DepthTex[p];
    OutDepth[p] = d;

    float2 uv  = (float2(p) + 0.5) * InvRenderSize;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

    float2 V;  // Current minus previous position, NDC
    float4 e = HasVelocity ? VelTex[p] : float4(0, 0, 0, 0);
    if (HasVelocity && e.x > 0.0)
    {
        // UE 4.27 EncodeVelocityToTexture
        const float InvDiv = 1.0 / (0.499 * 0.5);
        V = e.xy * InvDiv - (32767.0 / 65535.0) * InvDiv;
        if (DecodeMode == 1) V = (V * abs(V)) * 0.5;
    }
    else
    {
        float4 prev = mul(float4(ndc, d, 1.0), ClipToPrevClip);
        V = ndc - prev.xy / prev.w;
    }
    OutMV[p] = -V * float2(0.5 * RenderSize.x, -0.5 * RenderSize.y);  // Pixels, towards the previous frame
}
)";




bool MotionVectors::Initialize(ID3D11Device* d3dDevice)
{
	initTried = true;
	device = d3dDevice;

	ID3DBlob* code = nullptr;
	ID3DBlob* errors = nullptr;
	HRESULT result = D3DCompile(shaderSource, strlen(shaderSource), "DBDTechnologies_MotionVectors", nullptr, nullptr, "main",
	                            "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
	if (FAILED(result))
	{
		Utilities::Log::Write("motion vector shader error: %s", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
		Utilities::SafeRelease(errors);
		return false;
	}

	result = device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
	Utilities::SafeRelease(code);
	Utilities::SafeRelease(errors);
	if (FAILED(result))
	{
		Utilities::Log::Write("motion vectors: CreateComputeShader failed 0x%08X", static_cast<unsigned>(result));
		return false;
	}

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeof(Constants);
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &constants));
}

bool MotionVectors::EnsureTextures(int width, int height)
{
	if (motionTexture && textureWidth == width && textureHeight == height)
		return true;

	Utilities::SafeRelease(motionUav);
	Utilities::SafeRelease(motionTexture);
	Utilities::SafeRelease(depthUav);
	Utilities::SafeRelease(depthTexture);

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = static_cast<UINT>(width);
	desc.Height = static_cast<UINT>(height);
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

	desc.Format = DXGI_FORMAT_R16G16_FLOAT;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &motionTexture)) ||
	    FAILED(device->CreateUnorderedAccessView(motionTexture, nullptr, &motionUav)))
		return false;

	desc.Format = DXGI_FORMAT_R32_FLOAT;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &depthTexture)) ||
	    FAILED(device->CreateUnorderedAccessView(depthTexture, nullptr, &depthUav)))
		return false;

	textureWidth = width;
	textureHeight = height;
	Utilities::Log::Write("motion/depth textures: %dx%d", width, height);
	return true;
}

bool MotionVectors::Generate(ID3D11DeviceContext* context, const Inputs& in)
{
	if (initTried == false)
	{
		ID3D11Device* d3dDevice = nullptr;
		context->GetDevice(&d3dDevice);
		if (d3dDevice == nullptr)
			return false;
		Initialize(d3dDevice);
		d3dDevice->Release();   // The device outlives the mod
	}
	if (shader == nullptr || constants == nullptr || in.depth == nullptr || EnsureTextures(in.bufferWidth, in.bufferHeight) == false)
		return false;

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;
	Constants c = {};
	memcpy(c.clipToPrevClip, in.clipToPrevClip, sizeof(c.clipToPrevClip));
	c.renderSize[0] = static_cast<float>(in.renderWidth);
	c.renderSize[1] = static_cast<float>(in.renderHeight);
	c.invRenderSize[0] = 1.0f / in.renderWidth;
	c.invRenderSize[1] = 1.0f / in.renderHeight;
	c.hasVelocity = in.velocity ? 1u : 0u;
	c.decodeMode = static_cast<uint32_t>(in.decodeMode);
	memcpy(mapped.pData, &c, sizeof(c));
	context->Unmap(constants, 0);

	ID3D11ShaderResourceView* sources[2] = { in.depth, in.velocity };
	ID3D11UnorderedAccessView* targets[2] = { motionUav, depthUav };

	Utilities::ComputeState::Unbind(context);
	context->CSSetShader(shader, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &constants);
	context->CSSetShaderResources(0, 2, sources);
	context->CSSetUnorderedAccessViews(0, 2, targets, nullptr);
	context->Dispatch(static_cast<UINT>(in.renderWidth + 7) / 8, static_cast<UINT>(in.renderHeight + 7) / 8, 1);
	Utilities::ComputeState::Unbind(context);
	return true;
}
