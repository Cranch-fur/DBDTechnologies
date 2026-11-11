#include "DLSS.h"
#include <psapi.h>
#include <cwctype>

#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"

#pragma comment(lib, "version.lib")
#pragma comment(lib, "psapi.lib")






static NVSDK_NGX_PerfQuality_Value QualityValue(int renderWidth, int outputWidth)
{
	float ratio = static_cast<float>(renderWidth) / static_cast<float>(outputWidth);
	if (ratio >= 0.99f) return NVSDK_NGX_PerfQuality_Value_DLAA;
	if (ratio >= 0.62f) return NVSDK_NGX_PerfQuality_Value_MaxQuality;
	if (ratio >= 0.55f) return NVSDK_NGX_PerfQuality_Value_Balanced;
	if (ratio >= 0.45f) return NVSDK_NGX_PerfQuality_Value_MaxPerf;
	return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
}




bool DLSS::IsValidPreset(int32_t value)
{
	for (const PresetInfo& preset : presets)
		if (preset.value == value)
			return true;
	return false;
}

const char* DLSS::PresetName(int32_t value)
{
	for (const PresetInfo& preset : presets)
		if (preset.value == value)
			return preset.name;
	return "?";
}

bool DLSS::Initialize(ID3D11Device* d3dDevice)
{
	device = d3dDevice;

	static std::wstring asiDirectory = Utilities::Module::GetOwnDirectory();
	static const wchar_t* searchPaths[] = { asiDirectory.c_str() };

	NVSDK_NGX_FeatureCommonInfo featureInfo = {};
	featureInfo.PathListInfo.Path = searchPaths;
	featureInfo.PathListInfo.Length = 1;
	Utilities::Log::Write("NGX: looking for nvngx_dlss.dll in %ls (then next to the exe)", asiDirectory.c_str());

	std::wstring directory = Utilities::Module::GetDirectory();
	NVSDK_NGX_Result result = NVSDK_NGX_D3D11_Init_with_ProjectID(
		"a7c1e2d4-6b3f-4e58-9d21-0f8b7c6a5e43", NVSDK_NGX_ENGINE_TYPE_UNREAL, "4.27.2", directory.c_str(), device, &featureInfo);
	if (NVSDK_NGX_FAILED(result))
	{
		Utilities::Log::Write("NVSDK_NGX_D3D11_Init failed: 0x%08X (NVIDIA driver missing or too old, or not an NVIDIA GPU)", static_cast<unsigned>(result));
		initErrorReason = "NGX INIT FAILED - NVIDIA DRIVER?";
		return false;
	}

	result = NVSDK_NGX_D3D11_GetCapabilityParameters(&parameters);
	if (NVSDK_NGX_FAILED(result) || parameters == nullptr)
	{
		Utilities::Log::Write("GetCapabilityParameters failed: 0x%08X", static_cast<unsigned>(result));
		initErrorReason = "NGX PARAMETERS FAILED";
		return false;
	}

	int available = 0, needsDriver = 0, initResult = 0;
	parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
	parameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
	parameters->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initResult);
	Utilities::Log::Write("NGX: DLSS available=%d, needs newer driver=%d, feature init result=0x%08X", available, needsDriver,
	                      static_cast<unsigned>(initResult));

	if (available == 0)
	{
		if (needsDriver)
			initErrorReason = "UPDATE NVIDIA DRIVER";
		else if (static_cast<unsigned>(initResult) == static_cast<unsigned>(NVSDK_NGX_Result_FAIL_FeatureNotFound))
			initErrorReason = "NVNGX_DLSS.DLL NOT FOUND";
		else
			initErrorReason = "DLSS NOT AVAILABLE (RTX GPU?)";
		Utilities::Log::Write("DLSS unavailable: %s. Put nvngx_dlss.dll next to the DBDTechnologies module (%ls)", initErrorReason, asiDirectory.c_str());
		return false;
	}
	return true;
}

/* device is AddRef'ed by Execute and released here */
DWORD WINAPI DLSS::InitializeThread(LPVOID d3dDevice)
{
	ID3D11Device* candidate = static_cast<ID3D11Device*>(d3dDevice);
	const DWORD start = GetTickCount();
	const bool ok = Initialize(candidate);
	if (ok)
		Utilities::Log::Write("NGX ready in %lu ms", GetTickCount() - start);
	else
		Utilities::Log::Write("DLSS unavailable - original TAA stays until restart");
	candidate->Release();
	InterlockedExchange(&initState, static_cast<LONG>(ok ? InitState::Ready : InitState::Failed));
	return 0;
}

/* The driver may load its own copy from ProgramData\NVIDIA\NGX instead of the file next to the game */
void DLSS::DescribeLoadedLibrary()
{
	HMODULE modules[1024];
	DWORD needed = 0;
	if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed) == FALSE)
		return;

	for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < _countof(modules); ++i)
	{
		wchar_t path[MAX_PATH] = {};
		if (GetModuleFileNameW(modules[i], path, MAX_PATH) == 0)
			continue;

		std::wstring lower = path;
		for (wchar_t& c : lower)
			c = static_cast<wchar_t>(towlower(c));
		bool isDlss = lower.find(L"nvngx_dlss.dll") != std::wstring::npos ||
		              lower.find(L"\\ngx\\models\\dlss\\") != std::wstring::npos;
		if (isDlss == false)
			continue;

		DWORD handle = 0;
		DWORD size = GetFileVersionInfoSizeW(path, &handle);
		std::vector<uint8_t> data(size ? size : 1);
		VS_FIXEDFILEINFO* info = nullptr;
		UINT length = 0;
		if (size && GetFileVersionInfoW(path, 0, size, data.data()) &&
		    VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) && info)
		{
			snprintf(versionText, sizeof(versionText), "%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
			         HIWORD(info->dwFileVersionLS));
		}
		Utilities::Log::Write("DLSS library: %ls (version %s)", path, versionText[0] ? versionText : "?");
		return;
	}
	Utilities::Log::Write("DLSS library: not found among loaded modules");
}

bool DLSS::EnsureFeature(ID3D11DeviceContext* context, const UpscalerInputs& in, int preset)
{
	/* With dynamic resolution the feature is created for the largest input and the real size goes to InRenderSubrectDimensions */
	const int width = in.maxRenderWidth;
	const int height = in.maxRenderHeight;

	if (feature && featureWidth == width && featureHeight == height && featureOutputWidth == in.outputWidth &&
	    featureOutputHeight == in.outputHeight && featurePreset == preset)
		return true;

	if (feature)
	{
		NVSDK_NGX_D3D11_ReleaseFeature(feature);
		feature = nullptr;
	}

	const int key[5] = { width, height, in.outputWidth, in.outputHeight, preset };
	if (memcmp(key, failedKey, sizeof(key)) == 0)
		return false;

	/* The preset is read only at creation time */
	const unsigned value = static_cast<unsigned>(preset);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, value);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, value);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, value);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, value);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, value);
	NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, value);

	NVSDK_NGX_DLSS_Create_Params create = {};
	create.Feature.InWidth = static_cast<unsigned>(width);
	create.Feature.InHeight = static_cast<unsigned>(height);
	create.Feature.InTargetWidth = static_cast<unsigned>(in.outputWidth);
	create.Feature.InTargetHeight = static_cast<unsigned>(in.outputHeight);
	create.Feature.InPerfQualityValue = QualityValue(width, in.outputWidth);
	create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
	                              NVSDK_NGX_DLSS_Feature_Flags_DepthInverted | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
	create.InEnableOutputSubrects = false;

	NVSDK_NGX_Result result = NGX_D3D11_CREATE_DLSS_EXT(context, &feature, parameters, &create);
	if (NVSDK_NGX_FAILED(result) || feature == nullptr)
	{
		Utilities::Log::Write("DLSS create failed %dx%d -> %dx%d preset %s: 0x%08X", width, height, in.outputWidth, in.outputHeight,
		                      PresetName(preset), static_cast<unsigned>(result));
		feature = nullptr;
		memcpy(failedKey, key, sizeof(key));
		errorReason = static_cast<unsigned>(result) == static_cast<unsigned>(NVSDK_NGX_Result_FAIL_FeatureNotFound) ?
		              "NVNGX_DLSS.DLL NOT FOUND" : "DLSS CREATE FAILED";
		return false;
	}

	featureWidth = width;
	featureHeight = height;
	featureOutputWidth = in.outputWidth;
	featureOutputHeight = in.outputHeight;
	featurePreset = preset;

	static bool described = false;
	if (described == false)
	{
		described = true;
		DescribeLoadedLibrary();
	}
	Utilities::Log::Write("DLSS created: %dx%d -> %dx%d, preset %s%s", width, height, in.outputWidth, in.outputHeight, PresetName(preset),
	                      in.dynamicResolution ? " (dynamic resolution: max input)" : "");
	return true;
}

bool DLSS::Execute(ID3D11DeviceContext* context, const UpscalerInputs& in, int preset)
{
	if (initState == static_cast<LONG>(InitState::NotStarted))
	{
		ID3D11Device* d3dDevice = nullptr;
		context->GetDevice(&d3dDevice);
		/* Loading is set before the thread starts, so a fast thread cannot have its result overwritten */
		InterlockedExchange(&initState, static_cast<LONG>(InitState::Loading));
		HANDLE thread = d3dDevice ? CreateThread(nullptr, 0, &InitializeThread, d3dDevice, 0, nullptr) : nullptr;
		if (thread)
			CloseHandle(thread);
		else
		{
			Utilities::SafeRelease(d3dDevice);
			initErrorReason = "NGX INIT THREAD FAILED";
			InterlockedExchange(&initState, static_cast<LONG>(InitState::Failed));
		}
	}

	const InitState state = static_cast<InitState>(initState);
	if (state == InitState::Loading)
	{
		errorReason = "LOADING NGX";
		return false;
	}
	if (state != InitState::Ready)
	{
		errorReason = initErrorReason;
		return false;
	}
	if (EnsureFeature(context, in, preset) == false)
		return false;

	NVSDK_NGX_D3D11_DLSS_Eval_Params evaluate = {};
	evaluate.Feature.pInColor = in.color;
	evaluate.Feature.pInOutput = in.output;
	evaluate.pInDepth = in.depth;
	evaluate.pInMotionVectors = in.motionVectors;
	evaluate.InJitterOffsetX = in.jitterX;
	evaluate.InJitterOffsetY = in.jitterY;
	evaluate.InRenderSubrectDimensions.Width = static_cast<unsigned>(in.renderWidth);
	evaluate.InRenderSubrectDimensions.Height = static_cast<unsigned>(in.renderHeight);
	evaluate.InReset = in.reset ? 1 : 0;
	evaluate.InMVScaleX = 1.0f;
	evaluate.InMVScaleY = 1.0f;

	NVSDK_NGX_Result result = NGX_D3D11_EVALUATE_DLSS_EXT(context, feature, parameters, &evaluate);
	if (NVSDK_NGX_FAILED(result))
	{
		static int errors = 0;
		if (errors++ < 10)
			Utilities::Log::Write("DLSS evaluate failed: 0x%08X", static_cast<unsigned>(result));
		errorReason = "DLSS EVALUATE FAILED";
		return false;
	}
	errorReason = "";
	return true;
}
