#pragma once
#include "Utilities.h"






/*
	AMD FidelityFX FSR 1 (MIT): EASU — пространственное увеличение, RCAS — резкость.
	Шейдеры — официальный ffx_fsr1.h (SDKs\FSR1), компилируются Visual Studio при сборке.

	RCAS также используется как резкость после DLSS (r.Upscaler.Sharpness).
*/
class FSR
{
public:
	/* Резкость 0..1 -> "стопы" RCAS (0 = максимум). 0 = без резкости */
	static bool IsSharpening(float sharpness) { return sharpness > 0.001f; }

	/*
		Резкость поверх готового кадра в формате RGBA16F (HDR, до тонмаппинга).
		target должен поддерживать UAV (выход TAA/DLSS его поддерживает).
	*/
	static bool Sharpen(ID3D11DeviceContext* context, ID3D11Resource* target, UINT width, UINT height, float sharpness);

	/*
		FSR 1: увеличить input (область inputWidth x inputHeight внутри текстуры textureWidth x textureHeight)
		до области viewport в renderTarget. renderTargetFormat — формат RTV, через который игра пишет в цель.
	*/
	static bool Upscale(ID3D11DeviceContext* context, ID3D11ShaderResourceView* input, UINT inputWidth, UINT inputHeight,
	                    UINT textureWidth, UINT textureHeight, ID3D11Resource* renderTarget, DXGI_FORMAT renderTargetFormat,
	                    const D3D11_VIEWPORT& viewport, float sharpness);


private:
	struct EasuConstants
	{
		uint32_t const0[4];
		uint32_t const1[4];
		uint32_t const2[4];
		uint32_t const3[4];
		uint32_t outputSize[4];
	};

	struct RcasConstants
	{
		uint32_t const0[4];
		uint32_t params[4];      // x = HDR, y = без резкости, z = sRGB
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

	static inline Texture upscaled;   // результат EASU (RGBA16F)
	static inline Texture output;     // результат RCAS в формате цели
	static inline Texture copy;       // копия кадра для резкости после DLSS

	/* UAV на выход DLSS (у UE несколько текстур по очереди) */
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
};
