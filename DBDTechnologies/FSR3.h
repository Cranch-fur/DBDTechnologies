#pragma once
#include "UpscalerInputs.h"






/*
	AMD FSR 3.1 upscaler on Direct3D 11. ffx_fsr3upscaler.cpp from FidelityFX SDK 2.x is used unchanged; its GPU jobs
	are executed by the D3D11 backend in FSR3.cpp. Works on feature level 11.0 (8 UAVs per pass) and needs typed UAV loads
	for R8, RG16F and RGBA32F.
*/
class FSR3
{
private:
	static inline const char* errorReason = "";
	static inline bool unsupported = false;
	static inline void* fsrContext = nullptr;   // FfxFsr3UpscalerContext, 512 KB
	static inline int contextMaxWidth = 0, contextMaxHeight = 0, contextOutputWidth = 0, contextOutputHeight = 0;
	static inline bool contextDynamic = false;
	static inline int failedKey[5] = { -1, -1, -1, -1, -1 };

	/* Shared with frame generation in the SDK, owned by the application */
	static inline ID3D11Texture2D* dilatedDepth = nullptr;
	static inline ID3D11Texture2D* dilatedMotionVectors = nullptr;
	static inline ID3D11Texture2D* reconstructedPreviousDepth = nullptr;

	static bool CheckDevice(ID3D11Device* device);
	static bool CreateSharedTextures(ID3D11Device* device, int width, int height);
	static bool EnsureContext(ID3D11Device* device, const UpscalerInputs& in);
	static void DestroyContext();


public:
	static bool Execute(ID3D11DeviceContext* context, const UpscalerInputs& inputs);

	static const char* ErrorReason() { return errorReason; }
};
