#pragma once
#include "UpscalerInputs.h"

struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;






/*
	NVIDIA DLSS Super Resolution through NGX (D3D11).
	nvngx_dlss.dll is searched next to the mod module (.dll / .asi) first, then next to the game executable.
*/
class DLSS
{
private:
	struct PresetInfo
	{
		int32_t value;
		const char* name;
	};

	/* nvsdk_ngx_defs.h (SDK 310.9): A-D were removed, G/H/I/N/O are reserved */
	static inline const PresetInfo presets[7] =
	{
		{ 0,  "DEFAULT" },
		{ 10, "J" },
		{ 11, "K" },
		{ 12, "L" },
		{ 13, "M" },
		{ 5,  "E" },
		{ 6,  "F" },
	};

	enum class InitState : LONG
	{
		NotStarted,
		Loading,
		Ready,
		Failed,
	};

	static inline ID3D11Device* device = nullptr;
	static inline volatile LONG initState = static_cast<LONG>(InitState::NotStarted);
	static inline NVSDK_NGX_Parameter* parameters = nullptr;
	static inline NVSDK_NGX_Handle* feature = nullptr;
	static inline int featureWidth = 0, featureHeight = 0, featureOutputWidth = 0, featureOutputHeight = 0, featurePreset = -1;
	static inline int failedKey[5] = { -1, -1, -1, -1, -1 };

	static inline const char* errorReason = "";
	static inline const char* initErrorReason = "";
	static inline char versionText[32] = "";

	static bool Initialize(ID3D11Device* d3dDevice);
	static DWORD WINAPI InitializeThread(LPVOID d3dDevice);
	static bool EnsureFeature(ID3D11DeviceContext* context, const UpscalerInputs& in, int preset);
	static void DescribeLoadedLibrary();


public:
	static bool Execute(ID3D11DeviceContext* context, const UpscalerInputs& inputs, int preset);

	static bool IsValidPreset(int32_t value);
	static const char* PresetName(int32_t value);

	/* Preset of the feature that is currently created, -1 before the first frame */
	static int ActivePreset() { return featurePreset; }

	/* NGX init loads the driver part of DLSS and takes a few seconds; it runs in the background and TAA stays meanwhile */
	static bool IsLoading() { return initState == static_cast<LONG>(InitState::Loading); }

	static const char* VersionText() { return versionText; }
	static const char* ErrorReason() { return errorReason; }
};
