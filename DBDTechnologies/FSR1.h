#pragma once
#include "Utilities.h"






/*
	AMD FSR 1: EASU upscaling and RCAS sharpening (ffx_fsr1.h, shaders compiled by FxCompile).
	RCAS is also the r.Upscaler.Sharpness pass after DLSS and XeSS.
*/
class FSR1
{
private:
	struct EasuConstants
	{
		uint32_t const0[4];
		uint32_t const1[4];
		uint32_t const2[4];
		uint32_t const3[4];
		uint32_t outputSize[4];   // xy = size, z = linear HDR input
	};

	struct RcasConstants
	{
		uint32_t const0[4];
		uint32_t params[4];       // x = HDR, y = passthrough, z = encode sRGB, w = undo EASU HDR curve
		uint32_t outputSize[4];
	};

	struct Texture
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		ID3D11UnorderedAccessView* uav = nullptr;
		UINT width = 0, height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

		bool Ensure(ID3D11Device* device, UINT w, UINT h, DXGI_FORMAT f, bool needSrv, bool needUav);
		void Release();
	};

	static inline bool initTried = false;
	static inline bool ready = false;
	static inline ID3D11ComputeShader* easuShader = nullptr;
	static inline ID3D11ComputeShader* rcasShader = nullptr;
	static inline ID3D11Buffer* easuConstants = nullptr;
	static inline ID3D11Buffer* rcasConstants = nullptr;
	static inline ID3D11SamplerState* linearClamp = nullptr;

	static inline Texture upscaled;   // EASU result, RGBA16F
	static inline Texture output;     // RCAS result in the render target format
	static inline Texture copy;       // Source copy for sharpening in place

	/* The engine cycles through a few output textures */
	static inline ID3D11Resource* targetResources[4] = {};
	static inline ID3D11UnorderedAccessView* targetUavs[4] = {};
	static inline int targetNext = 0;

	static inline Utilities::ComputeState savedState;

	static bool Initialize(ID3D11Device* device);
	static ID3D11UnorderedAccessView* TargetUav(ID3D11Device* device, ID3D11Resource* target);
	static bool WritableFormat(ID3D11Device* device, DXGI_FORMAT renderTargetFormat, DXGI_FORMAT& uavFormat, bool& encodeSrgb);
	static void RcasConstantsFor(float sharpness, uint32_t out[4]);
	static void EasuConstantsFor(EasuConstants& c, UINT inputWidth, UINT inputHeight, UINT textureWidth, UINT textureHeight,
	                             UINT outputWidth, UINT outputHeight);


public:
	static bool IsSharpening(float sharpness) { return sharpness > 0.001f; }

	/* In-place sharpening of an RGBA16F HDR image; target must support UAV */
	static bool Sharpen(ID3D11DeviceContext* context, ID3D11Resource* target, UINT width, UINT height, float sharpness);

	/* Upscales inputWidth x inputHeight (inside a textureWidth x textureHeight texture) into the viewport of renderTarget */
	static bool Upscale(ID3D11DeviceContext* context, ID3D11ShaderResourceView* input, UINT inputWidth, UINT inputHeight,
	                    UINT textureWidth, UINT textureHeight, ID3D11Resource* renderTarget, DXGI_FORMAT renderTargetFormat,
	                    const D3D11_VIEWPORT& viewport, float sharpness);
};
