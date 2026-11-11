#pragma once
#include "UpscalerInputs.h"






/*
	AMD FSR 2.2.1 on Direct3D 11. ffx_fsr2.cpp from the SDK is used unchanged; its GPU jobs are executed
	by the D3D11 backend in FSR2.cpp. Needs typed UAV loads for R16F, RG32F and R8.
*/
class FSR2
{
private:
	static inline const char* errorReason = "";
	static inline bool unsupported = false;
	static inline void* fsrContext = nullptr;   // FfxFsr2Context, 64 KB
	static inline int contextMaxWidth = 0, contextMaxHeight = 0, contextOutputWidth = 0, contextOutputHeight = 0;
	static inline bool contextDynamic = false;
	static inline int failedKey[5] = { -1, -1, -1, -1, -1 };

	static bool CheckDevice(ID3D11Device* device);
	static bool EnsureContext(ID3D11Device* device, const UpscalerInputs& in);
	static void DestroyContext();


public:
	static bool Execute(ID3D11DeviceContext* context, const UpscalerInputs& inputs);

	static const char* ErrorReason() { return errorReason; }
};
