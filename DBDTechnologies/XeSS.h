#pragma once
#include "UpscalerInputs.h"

struct _xess_context_handle_t;






/*
	Intel XeSS through libxess_dx11.dll, loaded at runtime (the game keeps working without it).
	Intel documents the D3D11 version as Intel GPU only.
*/
class XeSS
{
private:
	using Context = _xess_context_handle_t*;

	static inline bool initTried = false;
	static inline bool ready = false;
	static inline HMODULE library = nullptr;
	static inline Context context = nullptr;
	static inline ID3D11Device* device = nullptr;

	static inline int initOutputWidth = 0, initOutputHeight = 0, initQuality = -1;
	static inline int initMinWidth = 0, initMinHeight = 0, initMaxWidth = 0, initMaxHeight = 0;   // Input range of the current quality
	static inline int failedKey[3] = { -1, -1, -1 };

	static inline const char* errorReason = "";
	static inline char versionText[24] = "";
	static inline const char* qualityText = "";

	static inline void* pGetVersion = nullptr;
	static inline void* pCreateContext = nullptr;
	static inline void* pInit = nullptr;
	static inline void* pExecute = nullptr;
	static inline void* pDestroyContext = nullptr;
	static inline void* pGetOptimalInputResolution = nullptr;
	static inline void* pSetVelocityScale = nullptr;
	static inline void* pSetJitterScale = nullptr;
	static inline void* pSetLoggingCallback = nullptr;

	static bool Initialize(ID3D11Device* d3dDevice);
	static bool IsIntelGpu(ID3D11Device* d3dDevice, unsigned& vendorId);
	static bool LoadLibraryAndFunctions();
	static int ChooseQuality(int renderWidth, int renderHeight, int outputWidth, int outputHeight, int range[4]);
	static bool EnsureInitialized(int renderWidth, int renderHeight, int outputWidth, int outputHeight);
	static const char* ResultName(int result);


public:
	/* XeSS records into the immediate context of the device, which is the context the engine uses */
	static bool Execute(ID3D11DeviceContext* d3dContext, const UpscalerInputs& inputs);

	static const char* ErrorReason() { return errorReason; }
	static const char* VersionText() { return versionText; }
	static const char* QualityText() { return qualityText; }
};
