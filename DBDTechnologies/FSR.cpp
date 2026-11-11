#include "FSR.h"

/* Байткод шейдеров, его генерирует Visual Studio из Shaders\*.hlsl (FxCompile) */
#include "Shaders/FsrEasu.h"
#include "Shaders/FsrRcas.h"

/* Константы EASU/RCAS считаются официальными функциями FSR 1 (CPU-часть ffx_fsr1.h) */
#include <cmath>
#define A_CPU 1
#include "ffx_a.h"
#include "ffx_fsr1.h"






/* ================================================================== текстуры */

bool FSR::Texture::Ensure(ID3D11Device* device, UINT w, UINT h, DXGI_FORMAT f, bool needSrv, bool needUav)
{
	if (texture && width == w && height == h && format == f)
		return true;
	Release();

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.Format = f;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = (needSrv ? D3D11_BIND_SHADER_RESOURCE : 0) | (needUav ? D3D11_BIND_UNORDERED_ACCESS : 0);

	if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)) ||
	    (needSrv && FAILED(device->CreateShaderResourceView(texture, nullptr, &srv))) ||
	    (needUav && FAILED(device->CreateUnorderedAccessView(texture, nullptr, &uav))))
	{
		Release();
		return false;
	}

	width = w;
	height = h;
	format = f;
	return true;
}

void FSR::Texture::Release()
{
	Utilities::SafeRelease(uav);
	Utilities::SafeRelease(srv);
	Utilities::SafeRelease(texture);
	width = height = 0;
	format = DXGI_FORMAT_UNKNOWN;
}




/* ================================================================== инициализация */

bool FSR::Initialize(ID3D11Device* device)
{
	initTried = true;

	if (FAILED(device->CreateComputeShader(g_FsrEasu, sizeof(g_FsrEasu), nullptr, &easuShader)) ||
	    FAILED(device->CreateComputeShader(g_FsrRcas, sizeof(g_FsrRcas), nullptr, &rcasShader)))
	{
		Utilities::Log::Write("FSR: failed to create shaders");
		return false;
	}

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.ByteWidth = sizeof(EasuConstants);
	if (FAILED(device->CreateBuffer(&bufferDesc, nullptr, &easuConstants)))
		return false;
	bufferDesc.ByteWidth = sizeof(RcasConstants);
	if (FAILED(device->CreateBuffer(&bufferDesc, nullptr, &rcasConstants)))
		return false;

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(device->CreateSamplerState(&samplerDesc, &linearClamp)))
		return false;

	Utilities::Log::Write("FSR 1: shaders ready");
	return true;
}

ID3D11UnorderedAccessView* FSR::TargetUav(ID3D11Device* device, ID3D11Resource* target)
{
	for (int i = 0; i < 4; ++i)
		if (targetResources[i] == target && targetUavs[i])
			return targetUavs[i];

	int i = targetNext;
	targetNext = (targetNext + 1) & 3;
	Utilities::SafeRelease(targetUavs[i]);
	targetResources[i] = nullptr;
	if (FAILED(device->CreateUnorderedAccessView(target, nullptr, &targetUavs[i])))
		return nullptr;
	targetResources[i] = target;
	return targetUavs[i];
}

/* Формат, в который шейдер может писать (UAV), и нужно ли кодировать sRGB вручную */
bool FSR::WritableFormat(ID3D11Device* device, DXGI_FORMAT renderTargetFormat, DXGI_FORMAT& uavFormat, bool& encodeSrgb)
{
	encodeSrgb = false;
	switch (renderTargetFormat)
	{
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			uavFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
			break;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			encodeSrgb = true;
			[[fallthrough]];
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			uavFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			encodeSrgb = true;
			[[fallthrough]];
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			uavFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
			break;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			uavFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
			break;
		case DXGI_FORMAT_R11G11B10_FLOAT:
			uavFormat = DXGI_FORMAT_R11G11B10_FLOAT;
			break;
		default:
			return false;
	}

	UINT support = 0;
	return SUCCEEDED(device->CheckFormatSupport(uavFormat, &support)) && (support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW);
}

void FSR::RcasConstantsFor(float sharpness, uint32_t out[4])
{
	/* r.Upscaler.Sharpness 0..1 -> RCAS: 1 = 0 стопов (максимум), около 0 = 2 стопа (слабо) */
	if (sharpness > 1.0f) sharpness = 1.0f;
	float stops = (1.0f - sharpness) * 2.0f;
	FsrRcasCon(reinterpret_cast<AU1*>(out), stops);
}

void FSR::EasuConstantsFor(EasuConstants& c, UINT inputWidth, UINT inputHeight, UINT textureWidth, UINT textureHeight,
                           UINT outputWidth, UINT outputHeight)
{
	FsrEasuCon(reinterpret_cast<AU1*>(c.const0), reinterpret_cast<AU1*>(c.const1), reinterpret_cast<AU1*>(c.const2),
	           reinterpret_cast<AU1*>(c.const3),
	           static_cast<AF1>(inputWidth), static_cast<AF1>(inputHeight),
	           static_cast<AF1>(textureWidth), static_cast<AF1>(textureHeight),
	           static_cast<AF1>(outputWidth), static_cast<AF1>(outputHeight));
	c.outputSize[0] = outputWidth;
	c.outputSize[1] = outputHeight;
	c.outputSize[2] = c.outputSize[3] = 0;
}




/* ================================================================== резкость после DLSS */

bool FSR::Sharpen(ID3D11DeviceContext* context, ID3D11Resource* target, UINT width, UINT height, float sharpness)
{
	if (IsSharpening(sharpness) == false || target == nullptr)
		return false;

	ID3D11Device* device = nullptr;
	context->GetDevice(&device);
	if (device == nullptr)
		return false;
	if (initTried == false)
		ready = Initialize(device);

	ID3D11UnorderedAccessView* targetUav = ready ? TargetUav(device, target) : nullptr;
	bool ok = targetUav && copy.Ensure(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true, false);
	device->Release();
	if (ok == false)
		return false;

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(rcasConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;
	RcasConstants c = {};
	RcasConstantsFor(sharpness, c.const0);
	c.params[0] = 1;   /* HDR */
	c.outputSize[0] = width;
	c.outputSize[1] = height;
	memcpy(mapped.pData, &c, sizeof(c));
	context->Unmap(rcasConstants, 0);

	D3D11_BOX box = { 0, 0, 0, width, height, 1 };
	context->CopySubresourceRegion(copy.texture, 0, 0, 0, 0, target, 0, &box);

	savedState.Save(context);
	Utilities::ComputeState::Unbind(context);
	context->CSSetShader(rcasShader, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &rcasConstants);
	context->CSSetShaderResources(0, 1, &copy.srv);
	context->CSSetUnorderedAccessViews(0, 1, &targetUav, nullptr);
	context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
	Utilities::ComputeState::Unbind(context);
	savedState.Restore(context);
	return true;
}




/* ================================================================== FSR 1 */

bool FSR::Upscale(ID3D11DeviceContext* context, ID3D11ShaderResourceView* input, UINT inputWidth, UINT inputHeight,
                  UINT textureWidth, UINT textureHeight, ID3D11Resource* renderTarget, DXGI_FORMAT renderTargetFormat,
                  const D3D11_VIEWPORT& viewport, float sharpness)
{
	if (input == nullptr || renderTarget == nullptr)
		return false;

	const UINT outputX = static_cast<UINT>(viewport.TopLeftX);
	const UINT outputY = static_cast<UINT>(viewport.TopLeftY);
	const UINT outputWidth = static_cast<UINT>(viewport.Width);
	const UINT outputHeight = static_cast<UINT>(viewport.Height);
	if (outputWidth == 0 || outputHeight == 0)
		return false;

	ID3D11Device* device = nullptr;
	context->GetDevice(&device);
	if (device == nullptr)
		return false;
	if (initTried == false)
		ready = Initialize(device);

	DXGI_FORMAT finalFormat = DXGI_FORMAT_UNKNOWN;
	bool encodeSrgb = false;

	/*
		Цель с плавающей точкой (при HDR-выводе scRGB): кадр уже после тонмаппинга, но линейный и может быть > 1.
		EASU/RCAS рассчитаны на 0..1, поэтому значения проходят через обратимую кривую (для 0..1 она тоже безвредна).
		HDR10 (PQ) лежит в 10-битной цели и уже "перцептивный" — с ним FSR 1 работает как с обычным SDR.
	*/
	const bool linearHdr = renderTargetFormat == DXGI_FORMAT_R16G16B16A16_FLOAT || renderTargetFormat == DXGI_FORMAT_R16G16B16A16_TYPELESS ||
	                       renderTargetFormat == DXGI_FORMAT_R11G11B10_FLOAT;
	static bool hdrLogged = false;
	if (linearHdr && hdrLogged == false)
	{
		hdrLogged = true;
		Utilities::Log::Write("FSR 1: floating point target (format %u) - linear HDR mode", static_cast<unsigned>(renderTargetFormat));
	}
	bool ok = ready && WritableFormat(device, renderTargetFormat, finalFormat, encodeSrgb) &&
	          upscaled.Ensure(device, outputWidth, outputHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, true, true) &&
	          output.Ensure(device, outputWidth, outputHeight, finalFormat, false, true);
	device->Release();
	if (ok == false)
	{
		static bool warned = false;
		if (warned == false)
		{
			warned = true;
			Utilities::Log::Write("FSR 1: render target format %u is not supported - spatial upscale left to the game",
			                      static_cast<unsigned>(renderTargetFormat));
		}
		return false;
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(easuConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;
	EasuConstants easu = {};
	EasuConstantsFor(easu, inputWidth, inputHeight, textureWidth, textureHeight, outputWidth, outputHeight);
	easu.outputSize[2] = linearHdr ? 1u : 0u;
	memcpy(mapped.pData, &easu, sizeof(easu));
	context->Unmap(easuConstants, 0);

	if (FAILED(context->Map(rcasConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;
	RcasConstants rcas = {};
	RcasConstantsFor(sharpness, rcas.const0);
	rcas.params[1] = IsSharpening(sharpness) ? 0u : 1u;   /* без резкости — просто перенос в формат цели */
	rcas.params[2] = encodeSrgb ? 1u : 0u;
	rcas.params[3] = linearHdr ? 1u : 0u;
	rcas.outputSize[0] = outputWidth;
	rcas.outputSize[1] = outputHeight;
	memcpy(mapped.pData, &rcas, sizeof(rcas));
	context->Unmap(rcasConstants, 0);

	savedState.Save(context);
	Utilities::ComputeState::Unbind(context);

	/* 1) EASU: input -> upscaled */
	context->CSSetShader(easuShader, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &easuConstants);
	context->CSSetShaderResources(0, 1, &input);
	context->CSSetSamplers(0, 1, &linearClamp);
	context->CSSetUnorderedAccessViews(0, 1, &upscaled.uav, nullptr);
	context->Dispatch((outputWidth + 7) / 8, (outputHeight + 7) / 8, 1);
	Utilities::ComputeState::Unbind(context);

	/* 2) RCAS: upscaled -> output (в формате цели) */
	context->CSSetShader(rcasShader, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &rcasConstants);
	context->CSSetShaderResources(0, 1, &upscaled.srv);
	context->CSSetUnorderedAccessViews(0, 1, &output.uav, nullptr);
	context->Dispatch((outputWidth + 7) / 8, (outputHeight + 7) / 8, 1);
	Utilities::ComputeState::Unbind(context);

	savedState.Restore(context);

	/* 3) в цель игры, в область viewport */
	D3D11_BOX box = { 0, 0, 0, outputWidth, outputHeight, 1 };
	context->CopySubresourceRegion(renderTarget, 0, outputX, outputY, 0, output.texture, 0, &box);
	return true;
}
