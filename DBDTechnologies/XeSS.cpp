#include "XeSS.h"
#include <dxgi.h>
#include <cstdlib>

#include "xess/xess_d3d11.h"

#pragma comment(lib, "dxgi.lib")






/* Resolved with GetProcAddress, so libxess_dx11.lib is not linked and a missing DLL does not stop the game */
using GetVersion_t = decltype(&xessGetVersion);
using CreateContext_t = decltype(&xessD3D11CreateContext);
using Init_t = decltype(&xessD3D11Init);
using Execute_t = decltype(&xessD3D11Execute);
using DestroyContext_t = decltype(&xessDestroyContext);
using GetOptimalInputResolution_t = decltype(&xessGetOptimalInputResolution);
using SetVelocityScale_t = decltype(&xessSetVelocityScale);
using SetJitterScale_t = decltype(&xessSetJitterScale);
using SetLoggingCallback_t = decltype(&xessSetLoggingCallback);

template <class T>
static T Function(void* pointer)
{
	return reinterpret_cast<T>(pointer);
}

static void LogCallback(const char* message, xess_logging_level_t level)
{
	static int count = 0;
	if (message && count++ < 50)
		Utilities::Log::Write("XeSS [%d]: %s", static_cast<int>(level), message);
}

struct QualityInfo
{
	xess_quality_settings_t value;
	const char* name;
};

/* Ordered by decreasing input resolution */
static const QualityInfo qualities[] =
{
	{ XESS_QUALITY_SETTING_AA, "NATIVE AA" },
	{ XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, "ULTRA QUALITY PLUS" },
	{ XESS_QUALITY_SETTING_ULTRA_QUALITY, "ULTRA QUALITY" },
	{ XESS_QUALITY_SETTING_QUALITY, "QUALITY" },
	{ XESS_QUALITY_SETTING_BALANCED, "BALANCED" },
	{ XESS_QUALITY_SETTING_PERFORMANCE, "PERFORMANCE" },
	{ XESS_QUALITY_SETTING_ULTRA_PERFORMANCE, "ULTRA PERF" },
};




const char* XeSS::ResultName(int result)
{
	switch (result)
	{
	case XESS_RESULT_SUCCESS: return "success";
	case XESS_RESULT_WARNING_NONEXISTING_FOLDER: return "warning: folder does not exist";
	case XESS_RESULT_WARNING_OLD_DRIVER: return "warning: old driver";
	case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE: return "unsupported device";
	case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER: return "unsupported driver";
	case XESS_RESULT_ERROR_UNINITIALIZED: return "uninitialized";
	case XESS_RESULT_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY: return "out of video memory";
	case XESS_RESULT_ERROR_DEVICE: return "device error";
	case XESS_RESULT_ERROR_NOT_IMPLEMENTED: return "not implemented";
	case XESS_RESULT_ERROR_INVALID_CONTEXT: return "invalid context";
	case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS: return "operation in progress";
	case XESS_RESULT_ERROR_UNSUPPORTED: return "unsupported";
	case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY: return "can't load library";
	case XESS_RESULT_ERROR_WRONG_CALL_ORDER: return "wrong call order";
	default: return "unknown error";
	}
}

bool XeSS::IsIntelGpu(ID3D11Device* d3dDevice, unsigned& vendorId)
{
	vendorId = 0;
	IDXGIDevice* dxgiDevice = nullptr;
	IDXGIAdapter* adapter = nullptr;
	if (SUCCEEDED(d3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
	    SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
	{
		DXGI_ADAPTER_DESC desc = {};
		if (SUCCEEDED(adapter->GetDesc(&desc)))
		{
			vendorId = desc.VendorId;
			Utilities::Log::Write("XeSS: GPU \"%ls\" (vendor 0x%04X)", desc.Description, desc.VendorId);
		}
	}
	Utilities::SafeRelease(adapter);
	Utilities::SafeRelease(dxgiDevice);
	return vendorId == 0x8086;
}

bool XeSS::LoadLibraryAndFunctions()
{
	const std::wstring candidates[] =
	{
		Utilities::Module::GetOwnDirectory() + L"libxess_dx11.dll",
		Utilities::Module::GetDirectory() + L"libxess_dx11.dll",
	};
	for (const std::wstring& path : candidates)
	{
		if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
			continue;
		library = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (library)
		{
			Utilities::Log::Write("XeSS: loaded %ls", path.c_str());
			break;
		}
		Utilities::Log::Write("XeSS: LoadLibrary(%ls) failed, error %lu", path.c_str(), GetLastError());
	}
	if (library == nullptr)
	{
		Utilities::Log::Write("XeSS: libxess_dx11.dll not found. Put it next to the DBDTechnologies module (%ls)",
		                      Utilities::Module::GetOwnDirectory().c_str());
		errorReason = "LIBXESS_DX11.DLL NOT FOUND";
		return false;
	}

	struct Entry
	{
		const char* name;
		void** pointer;
		bool required;
	};
	const Entry entries[] =
	{
		{ "xessGetVersion", &pGetVersion, false },
		{ "xessD3D11CreateContext", &pCreateContext, true },
		{ "xessD3D11Init", &pInit, true },
		{ "xessD3D11Execute", &pExecute, true },
		{ "xessDestroyContext", &pDestroyContext, true },
		{ "xessGetOptimalInputResolution", &pGetOptimalInputResolution, true },
		{ "xessSetVelocityScale", &pSetVelocityScale, false },
		{ "xessSetJitterScale", &pSetJitterScale, false },
		{ "xessSetLoggingCallback", &pSetLoggingCallback, false },
	};
	for (const Entry& entry : entries)
	{
		*entry.pointer = reinterpret_cast<void*>(GetProcAddress(library, entry.name));
		if (*entry.pointer == nullptr && entry.required)
		{
			Utilities::Log::Write("XeSS: %s not found in libxess_dx11.dll (too old?)", entry.name);
			errorReason = "LIBXESS_DX11.DLL TOO OLD";
			return false;
		}
	}

	if (pGetVersion)
	{
		xess_version_t version = {};
		if (Function<GetVersion_t>(pGetVersion)(&version) == XESS_RESULT_SUCCESS)
			snprintf(versionText, sizeof(versionText), "%u.%u.%u", version.major, version.minor, version.patch);
	}
	Utilities::Log::Write("XeSS: library version %s", versionText[0] ? versionText : "?");
	return true;
}

bool XeSS::Initialize(ID3D11Device* d3dDevice)
{
	if (initTried)
		return ready;
	initTried = true;
	device = d3dDevice;

	unsigned vendorId = 0;
	if (IsIntelGpu(d3dDevice, vendorId) == false)
	{
		Utilities::Log::Write("XeSS: the D3D11 version of XeSS works only on Intel GPUs (this GPU: vendor 0x%04X) - XeSS unavailable",
		                      vendorId);
		errorReason = vendorId == 0x10DE ? "XESS DX11: INTEL GPU ONLY (NVIDIA GPU)" :
		              vendorId == 0x1002 ? "XESS DX11: INTEL GPU ONLY (AMD GPU)" : "XESS DX11: INTEL GPU ONLY";
		return false;
	}

	if (LoadLibraryAndFunctions() == false)
		return false;

	xess_result_t result = Function<CreateContext_t>(pCreateContext)(d3dDevice, &context);
	if (result != XESS_RESULT_SUCCESS || context == nullptr)
	{
		Utilities::Log::Write("XeSS: xessD3D11CreateContext failed: %d (%s)", result, ResultName(result));
		errorReason = result == XESS_RESULT_ERROR_UNSUPPORTED_DRIVER ? "XESS: UPDATE INTEL DRIVER" :
		              result == XESS_RESULT_ERROR_UNSUPPORTED_DEVICE ? "XESS: GPU NOT SUPPORTED" : "XESS CONTEXT FAILED";
		context = nullptr;
		return false;
	}

	if (pSetLoggingCallback)
		Function<SetLoggingCallback_t>(pSetLoggingCallback)(context, XESS_LOGGING_LEVEL_WARNING, &LogCallback);

	Utilities::Log::Write("XeSS: context created");
	ready = true;
	return true;
}




/* Quality whose input range contains the current input, closest to its optimal resolution */
int XeSS::ChooseQuality(int renderWidth, int renderHeight, int outputWidth, int outputHeight, int range[4])
{
	xess_2d_t output = { static_cast<uint32_t>(outputWidth), static_cast<uint32_t>(outputHeight) };
	int best = -1;
	long bestDistance = 0;

	for (int i = 0; i < static_cast<int>(_countof(qualities)); ++i)
	{
		xess_2d_t optimal = {}, minimum = {}, maximum = {};
		xess_result_t result = Function<GetOptimalInputResolution_t>(pGetOptimalInputResolution)(
			context, &output, qualities[i].value, &optimal, &minimum, &maximum);
		if (result != XESS_RESULT_SUCCESS)
			continue;

		bool inside = renderWidth >= static_cast<int>(minimum.x) && renderWidth <= static_cast<int>(maximum.x) &&
		              renderHeight >= static_cast<int>(minimum.y) && renderHeight <= static_cast<int>(maximum.y);
		long distance = std::labs(static_cast<long>(optimal.x) - renderWidth);
		if (inside && (best < 0 || distance < bestDistance))
		{
			best = i;
			bestDistance = distance;
			range[0] = static_cast<int>(minimum.x);
			range[1] = static_cast<int>(minimum.y);
			range[2] = static_cast<int>(maximum.x);
			range[3] = static_cast<int>(maximum.y);
		}
	}
	return best;
}

bool XeSS::EnsureInitialized(int renderWidth, int renderHeight, int outputWidth, int outputHeight)
{
	/* Keeps the current quality while the input stays in its range: xessD3D11Init blocks and resets the history */
	if (initQuality >= 0 && initOutputWidth == outputWidth && initOutputHeight == outputHeight &&
	    renderWidth >= initMinWidth && renderWidth <= initMaxWidth && renderHeight >= initMinHeight && renderHeight <= initMaxHeight)
		return true;

	int range[4] = {};
	int quality = ChooseQuality(renderWidth, renderHeight, outputWidth, outputHeight, range);
	if (quality < 0)
	{
		static int warned = 0;
		if (warned++ < 3)
			Utilities::Log::Write("XeSS: input %dx%d is outside every XeSS quality range for output %dx%d (try r.ScreenPercentage 33..100)",
			                      renderWidth, renderHeight, outputWidth, outputHeight);
		errorReason = "XESS: SCREEN PERCENTAGE OUT OF RANGE";
		return false;
	}

	if (initQuality == quality && initOutputWidth == outputWidth && initOutputHeight == outputHeight)
		return true;

	const int key[3] = { outputWidth, outputHeight, quality };
	if (memcmp(key, failedKey, sizeof(key)) == 0)
		return false;

	/* Color is HDR and motion vectors are at render resolution by default */
	xess_d3d11_init_params_t params = {};
	params.outputResolution.x = static_cast<uint32_t>(outputWidth);
	params.outputResolution.y = static_cast<uint32_t>(outputHeight);
	params.qualitySetting = qualities[quality].value;
	params.initFlags = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;

	/* Intel requires pending GPU work to be finished before re-initialization */
	ID3D11DeviceContext* immediate = nullptr;
	device->GetImmediateContext(&immediate);
	if (immediate)
	{
		immediate->Flush();
		immediate->Release();
	}

	xess_result_t result = Function<Init_t>(pInit)(context, &params);
	if (result != XESS_RESULT_SUCCESS && result != XESS_RESULT_WARNING_OLD_DRIVER)
	{
		Utilities::Log::Write("XeSS: xessD3D11Init %dx%d %s failed: %d (%s)", outputWidth, outputHeight, qualities[quality].name,
		                      result, ResultName(result));
		memcpy(failedKey, key, sizeof(key));
		initQuality = -1;
		errorReason = "XESS INIT FAILED";
		return false;
	}
	if (result == XESS_RESULT_WARNING_OLD_DRIVER)
		Utilities::Log::Write("XeSS: the Intel driver is old - quality or speed may be worse");

	if (pSetVelocityScale)
		Function<SetVelocityScale_t>(pSetVelocityScale)(context, 1.0f, 1.0f);
	if (pSetJitterScale)
		Function<SetJitterScale_t>(pSetJitterScale)(context, 1.0f, 1.0f);

	initQuality = quality;
	initMinWidth = range[0];
	initMinHeight = range[1];
	initMaxWidth = range[2];
	initMaxHeight = range[3];
	initOutputWidth = outputWidth;
	initOutputHeight = outputHeight;
	qualityText = qualities[quality].name;
	errorReason = "";
	Utilities::Log::Write("XeSS initialized: %dx%d -> %dx%d, %s (input range %dx%d .. %dx%d)", renderWidth, renderHeight, outputWidth,
	                      outputHeight, qualityText, range[0], range[1], range[2], range[3]);
	return true;
}

bool XeSS::Execute(ID3D11DeviceContext* d3dContext, const UpscalerInputs& in)
{
	if (initTried == false)
	{
		ID3D11Device* d3dDevice = nullptr;
		d3dContext->GetDevice(&d3dDevice);
		if (d3dDevice)
		{
			Initialize(d3dDevice);
			d3dDevice->Release();
		}
	}
	if (ready == false || context == nullptr)
		return false;
	if (in.color == nullptr || in.motionVectors == nullptr || in.depth == nullptr || in.output == nullptr)
	{
		errorReason = "XESS: NO INPUTS";
		return false;
	}
	if (EnsureInitialized(in.renderWidth, in.renderHeight, in.outputWidth, in.outputHeight) == false)
		return false;

	xess_d3d11_execute_params_t params = {};
	params.pColorTexture = in.color;
	params.pVelocityTexture = in.motionVectors;
	params.pDepthTexture = in.depth;
	params.pOutputTexture = in.output;
	params.jitterOffsetX = in.jitterX;
	params.jitterOffsetY = in.jitterY;
	params.exposureScale = 1.0f;
	params.resetHistory = in.reset ? 1u : 0u;
	params.inputWidth = static_cast<uint32_t>(in.renderWidth);
	params.inputHeight = static_cast<uint32_t>(in.renderHeight);

	xess_result_t result = Function<Execute_t>(pExecute)(context, &params);
	if (result != XESS_RESULT_SUCCESS)
	{
		static int errors = 0;
		if (errors++ < 10)
			Utilities::Log::Write("XeSS: xessD3D11Execute failed: %d (%s)", result, ResultName(result));
		errorReason = "XESS EXECUTE FAILED";
		return false;
	}
	return true;
}
