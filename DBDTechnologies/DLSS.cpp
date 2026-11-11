#include "DLSS.h"
#include <d3dcompiler.h>
#include <psapi.h>
#include <cwctype>

#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "psapi.lib")






static const uint8_t SIGNATURE_ADDPASSES[] = { 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40 };
static const uint8_t SIGNATURE_DISPATCH[] = { 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
static const uint8_t SIGNATURE_END_DRAWING[] = { 0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 };
static const uint8_t SIGNATURE_DRAW[] = { 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
static const uint8_t SIGNATURE_DRAW_INDEXED[] = { 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x4C };


/* Векторы движения для DLSS: объекты — из буфера скорости UE, камера — из глубины и матриц */
static const char* motionShaderSource = R"(
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

    float2 V;  // UE: current minus previous position, NDC
    float4 e = HasVelocity ? VelTex[p] : float4(0, 0, 0, 0);
    if (HasVelocity && e.x > 0.0)
    {
        const float InvDiv = 1.0 / (0.499 * 0.5);
        V = e.xy * InvDiv - (32767.0 / 65535.0) * InvDiv;
        if (DecodeMode == 1) V = (V * abs(V)) * 0.5;
    }
    else
    {
        float4 prev = mul(float4(ndc, d, 1.0), ClipToPrevClip);
        V = ndc - prev.xy / prev.w;
    }
    // DLSS: vector to the previous-frame position, in render-resolution pixels
    OutMV[p] = -V * float2(0.5 * RenderSize.x, -0.5 * RenderSize.y);
}
)";

struct MotionConstants
{
	float clipToPrevClip[16];
	float renderSize[2];
	float invRenderSize[2];
	uint32_t hasVelocity;
	uint32_t decodeMode;
	float pad[2];
};




/* ================================================================== настройки и пресеты */

bool DLSS::IsValidPreset(int32_t value)
{
	for (const PresetInfo& p : presets)
		if (p.value == value)
			return true;
	return false;
}

const char* DLSS::PresetName(int32_t value)
{
	for (const PresetInfo& p : presets)
		if (p.value == value)
			return p.name;
	return "?";
}

const char* DLSS::QualityName(int renderWidth, int outputWidth)
{
	float ratio = outputWidth > 0 ? static_cast<float>(renderWidth) / static_cast<float>(outputWidth) : 1.0f;
	if (ratio >= 0.99f) return "DLAA";
	if (ratio >= 0.62f) return "QUALITY";
	if (ratio >= 0.55f) return "BALANCED";
	if (ratio >= 0.45f) return "PERFORMANCE";
	return "ULTRA PERF";
}

static NVSDK_NGX_PerfQuality_Value QualityValue(int renderWidth, int outputWidth)
{
	float ratio = static_cast<float>(renderWidth) / static_cast<float>(outputWidth);
	if (ratio >= 0.99f) return NVSDK_NGX_PerfQuality_Value_DLAA;
	if (ratio >= 0.62f) return NVSDK_NGX_PerfQuality_Value_MaxQuality;
	if (ratio >= 0.55f) return NVSDK_NGX_PerfQuality_Value_Balanced;
	if (ratio >= 0.45f) return NVSDK_NGX_PerfQuality_Value_MaxPerf;
	return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
}

const char* DLSS::SharpnessText()
{
	static char text[24];
	if (FSR::IsSharpening(settings.sharpness))
		snprintf(text, sizeof(text), "  SHARP %.2f", settings.sharpness);
	else
		text[0] = 0;
	return text;
}

/* Проверка значений после консоли: неверное значение -> по умолчанию */
void DLSS::ApplySettingChanges()
{
	static Settings last = { -1, -1, -1, -1, 0, -1.0f, -1 };
	static int32_t lastLowLatency = Reflex::mode;

	if (settings.method < MethodTAA || settings.method > MethodFSR2)
	{
		Utilities::Log::Write("r.Upscaler.Method %d is not supported (0 = TAA, 1 = DLSS, 2 = FSR 1, 3 = XeSS, 4 = FSR 2) -> 0", settings.method);
		settings.method = MethodTAA;
	}
	if (IsValidPreset(settings.preset) == false)
	{
		Utilities::Log::Write("r.DLSS.Preset %d does not exist -> Default", settings.preset);
		settings.preset = 0;
	}
	if (settings.jitterMode < 0 || settings.jitterMode > 3)
	{
		Utilities::Log::Write("r.Upscaler.JitterMode %d is out of range 0..3 -> 0", settings.jitterMode);
		settings.jitterMode = 0;
	}
	if (settings.velocityDecode != 0 && settings.velocityDecode != 1)
	{
		Utilities::Log::Write("r.Upscaler.VelocityDecode %d is not supported (0 or 1) -> 0", settings.velocityDecode);
		settings.velocityDecode = 0;
	}
	if (std::isfinite(settings.sharpness) == false || settings.sharpness < 0.0f || settings.sharpness > 1.0f)
	{
		float clamped = std::isfinite(settings.sharpness) ? (settings.sharpness < 0.0f ? 0.0f : 1.0f) : 0.0f;
		Utilities::Log::Write("r.Upscaler.Sharpness %.3f is out of range 0..1 -> %.1f", settings.sharpness, clamped);
		settings.sharpness = clamped;
	}
	if (settings.overlay != 0 && settings.overlay != 1)
		settings.overlay = 1;
	if (settings.targetFps != 0 && (settings.targetFps < 20 || settings.targetFps > 500))
	{
		Utilities::Log::Write("r.Upscaler.TargetFPS %d is out of range (0 = off, 20..500) -> 0", settings.targetFps);
		settings.targetFps = 0;
	}
	if (settings.minResolution < 25 || settings.minResolution > 100)
	{
		int clamped = settings.minResolution < 25 ? 25 : 100;
		Utilities::Log::Write("r.Upscaler.MinResolution %d is out of range 25..100 -> %d", settings.minResolution, clamped);
		settings.minResolution = clamped;
	}

	Reflex::ValidateMode();
	if (Reflex::mode != lastLowLatency)
	{
		Utilities::Log::Write("r.LowLatency.Mode %d", Reflex::mode);
		lastLowLatency = Reflex::mode;
	}

	if (settings.resetRequest)
	{
		settings.resetRequest = 0;
		InterlockedExchange(&forceReset, 1);
		Utilities::Log::Write("history reset");
	}

	if (last.method != -1)
	{
		static const char* methodNames[] = { "TAA (original)", "DLSS", "FSR 1", "XeSS", "FSR 2" };
		if (settings.method != last.method)
			Utilities::Log::Write("upscaler: %s", methodNames[settings.method]);
		if (settings.preset != last.preset)
			Utilities::Log::Write("DLSS preset %s", PresetName(settings.preset));
		if (settings.jitterMode != last.jitterMode)
			Utilities::Log::Write("jitter mode %d", settings.jitterMode);
		if (settings.overlay != last.overlay)
			Utilities::Log::Write("overlay %s", settings.overlay ? "shown" : "hidden");
		if (settings.sharpness != last.sharpness)
			Utilities::Log::Write("sharpness %.2f", settings.sharpness);
		if (settings.targetFps != last.targetFps || settings.minResolution != last.minResolution)
			Utilities::Log::Write("target FPS %d, min resolution %d%%", settings.targetFps, settings.minResolution);
		if (settings.method != last.method || settings.jitterMode != last.jitterMode)
			InterlockedExchange(&forceReset, 1);
	}
	last = settings;
}




/* ================================================================== консоль движка */

void DLSS::RegisterConsoleVariables()
{
	using RegisterIntRef_t = void* (__fastcall*)(void* self, const wchar_t* name, int32_t* reference, const wchar_t* help, uint32_t flags);
	using RegisterFloatRef_t = void* (__fastcall*)(void* self, const wchar_t* name, float* reference, const wchar_t* help, uint32_t flags);

	void* manager = Utilities::Memory::Read<void*>(reinterpret_cast<void*>(base + RVA_CONSOLE_SINGLETON));
	uintptr_t vtable = Utilities::Memory::Read<uintptr_t>(manager);
	if (manager == nullptr || vtable != base + RVA_CONSOLE_VTABLE)
	{
		Utilities::Log::Write("console manager not found (%p, vtable %p) - console variables unavailable", manager, reinterpret_cast<void*>(vtable));
		return;
	}
	auto registerIntRef = reinterpret_cast<RegisterIntRef_t>(Utilities::Memory::Read<uintptr_t>(reinterpret_cast<void*>(vtable), SLOT_REGISTER_INT_REF * 8));
	auto registerFloatRef = reinterpret_cast<RegisterFloatRef_t>(Utilities::Memory::Read<uintptr_t>(reinterpret_cast<void*>(vtable), SLOT_REGISTER_FLOAT_REF * 8));

	struct Variable
	{
		const wchar_t* name;
		int32_t* intReference;
		float* floatReference;
		const wchar_t* help;
	};
	const Variable variables[] =
	{
		/* общие для любого апскейлера */
		{ L"r.Upscaler.Method", &settings.method, nullptr,
		  L"Upscaler instead of the engine one: 0 = TAA (original, default), 1 = DLSS (replaces TAA/TAAU, needs r.TemporalAA.Upsampling 1), "
		  L"2 = FSR 1 (replaces the spatial upscale, needs r.TemporalAA.Upsampling 0), "
		  L"3 = XeSS (replaces TAA/TAAU, D3D11 version works on Intel GPUs only), "
		  L"4 = FSR 2.2 (replaces TAA/TAAU, any GPU, needs r.TemporalAA.Upsampling 1). Unsupported value -> 0." },
		{ L"r.Upscaler.Sharpness", nullptr, &settings.sharpness,
		  L"Sharpening after the upscaler (AMD RCAS), 0.0 = off (default) .. 1.0 = maximum. Out of range -> clamped." },
		{ L"r.Upscaler.Overlay", &settings.overlay, nullptr,
		  L"Status text in the top-left corner: 1 = show, 0 = hide (default)." },
		{ L"r.Upscaler.JitterMode", &settings.jitterMode, nullptr,
		  L"DLSS/XeSS jitter sign mode 0..3. Pick the one with the most stable image at a low r.ScreenPercentage." },
		{ L"r.Upscaler.VelocityDecode", &settings.velocityDecode, nullptr,
		  L"UE velocity buffer decoding for DLSS/XeSS motion vectors: 0 = linear (4.27), 1 = quadratic." },
		{ L"r.Upscaler.Reset", &settings.resetRequest, nullptr,
		  L"1 = reset upscaler history. Goes back to 0 by itself." },
		{ L"r.Upscaler.TargetFPS", &settings.targetFps, nullptr,
		  L"Dynamic resolution: 0 = off (default), 20..500 = lower the resolution when the GPU cannot hold this FPS. "
		  L"Sets the engine r.DynamicRes.* variables (between r.Upscaler.MinResolution and 100%). Needs r.TemporalAA.Upsampling 1." },
		{ L"r.Upscaler.MinResolution", &settings.minResolution, nullptr,
		  L"Lowest resolution for r.Upscaler.TargetFPS, percent of the screen: 25..100, default 50." },

		/* только DLSS */
		{ L"r.DLSS.Preset", &settings.preset, nullptr,
		  L"DLSS preset: 0 = Default, 10 = J, 11 = K (DLSS 4), 12 = L, 13 = M (DLSS 4.5), 5 = E, 6 = F. Unknown value -> Default." },

		/* снижение задержки (сейчас: NVIDIA Reflex; позже: AMD Anti-Lag 2) */
		{ L"r.LowLatency.Mode", &Reflex::mode, nullptr,
		  L"Low latency mode: 0 = off, 1 = on, 2 = on + boost. Currently NVIDIA Reflex (NVIDIA GPUs only). Unknown value -> 0." },
	};

	int registered = 0;
	for (const Variable& variable : variables)
	{
		void* cvar = variable.floatReference ?
		             registerFloatRef(manager, variable.name, variable.floatReference, variable.help, 0) :
		             registerIntRef(manager, variable.name, variable.intReference, variable.help, 0);
		uintptr_t cvarVtable = Utilities::Memory::Read<uintptr_t>(cvar);
		uintptr_t expected = base + (variable.floatReference ? RVA_CVAR_FLOAT_REF_VTABLE : RVA_CVAR_INT_REF_VTABLE);
		if (cvar && cvarVtable == expected)
			++registered;
		else
			Utilities::Log::Write("console: %ls - unexpected result (%p, vtable %p)", variable.name, cvar, reinterpret_cast<void*>(cvarVtable));
	}
	Utilities::Log::Write("console: %d of %d variables registered (r.Upscaler.*, r.DLSS.*, r.LowLatency.*)", registered,
	                      static_cast<int>(_countof(variables)));

	/* переменные движка для динамического разрешения и HDR */
	engine.operationMode = FindEngineVariable(L"r.DynamicRes.OperationMode");
	engine.frameTimeBudget = FindEngineVariable(L"r.DynamicRes.FrameTimeBudget");
	engine.minScreenPercentage = FindEngineVariable(L"r.DynamicRes.MinScreenPercentage");
	engine.maxScreenPercentage = FindEngineVariable(L"r.DynamicRes.MaxScreenPercentage");
	hdrOutputCvar = FindEngineVariable(L"r.HDR.EnableHDROutput");
	Utilities::Log::Write("engine variables: r.DynamicRes.* %s, r.HDR.EnableHDROutput %s",
	                      engine.operationMode && engine.frameTimeBudget && engine.minScreenPercentage && engine.maxScreenPercentage ?
	                      "found" : "NOT found (r.Upscaler.TargetFPS unavailable)", hdrOutputCvar ? "found" : "not found");
	InterlockedExchange(&consoleReady, 1);
}




/* ================================================================== переменные движка */

void* DLSS::FindEngineVariable(const wchar_t* name)
{
	using Find_t = void* (__fastcall*)(void* self, const wchar_t* name, bool trackFrequentCalls);
	void* manager = Utilities::Memory::Read<void*>(reinterpret_cast<void*>(base + RVA_CONSOLE_SINGLETON));
	uintptr_t vtable = Utilities::Memory::Read<uintptr_t>(manager);
	if (manager == nullptr || vtable != base + RVA_CONSOLE_VTABLE)
		return nullptr;
	auto find = reinterpret_cast<Find_t>(Utilities::Memory::Read<uintptr_t>(reinterpret_cast<void*>(vtable), SLOT_FIND_CONSOLE_VARIABLE * 8));
	return find ? find(manager, name, false) : nullptr;
}

int DLSS::GetEngineInt(void* cvar, int fallback)
{
	using GetInt_t = int32_t(__fastcall*)(void* self);
	uintptr_t vtable = Utilities::Memory::Read<uintptr_t>(cvar);
	uintptr_t function = vtable ? Utilities::Memory::Read<uintptr_t>(reinterpret_cast<void*>(vtable), SLOT_CVAR_GET_INT * 8) : 0;
	/* защита: функция должна быть внутри exe */
	if (function < base || function >= base + 0x10000000)
		return fallback;
	return reinterpret_cast<GetInt_t>(function)(cvar);
}

void DLSS::SetEngineValue(void* cvar, const wchar_t* value)
{
	using Set_t = void(__fastcall*)(void* self, const wchar_t* value, uint32_t setBy);
	uintptr_t vtable = Utilities::Memory::Read<uintptr_t>(cvar);
	uintptr_t function = vtable ? Utilities::Memory::Read<uintptr_t>(reinterpret_cast<void*>(vtable), SLOT_CVAR_SET * 8) : 0;
	if (function < base || function >= base + 0x10000000)
		return;
	reinterpret_cast<Set_t>(function)(cvar, value, ECVF_SET_BY_CONSOLE);
}

/*
	Игровой поток, начало кадра. Переменные движка меняем только здесь (из потока рендера нельзя).
	r.Upscaler.TargetFPS > 0  ->  r.DynamicRes.OperationMode 2 (всегда включено),
	                              FrameTimeBudget = 1000 / FPS, Min = r.Upscaler.MinResolution, Max = 100.
	Движок сам меряет время GPU и подбирает процент; моду остаётся не сбрасывать историю при каждой смене.
*/
void DLSS::GameThreadTick()
{
	if (consoleReady == 0)
		return;

	int32_t fps = settings.targetFps;
	int32_t minimum = settings.minResolution;
	if (fps != 0 && (fps < 20 || fps > 500))
		fps = 0;
	if (minimum < 25) minimum = 25;
	if (minimum > 100) minimum = 100;

	const bool available = engine.operationMode && engine.frameTimeBudget && engine.minScreenPercentage && engine.maxScreenPercentage;
	static int32_t appliedFps = 0, appliedMinimum = -1;
	if (available && (fps != appliedFps || (fps != 0 && minimum != appliedMinimum)))
	{
		wchar_t text[32];
		if (fps != 0)
		{
			swprintf_s(text, L"%d", minimum);
			SetEngineValue(engine.minScreenPercentage, text);
			SetEngineValue(engine.maxScreenPercentage, L"100");
			swprintf_s(text, L"%.2f", 1000.0 / fps);
			SetEngineValue(engine.frameTimeBudget, text);
			SetEngineValue(engine.operationMode, L"2");
			Utilities::Log::Write("dynamic resolution on: target %d FPS (%.2f ms), %d%%..100%%", fps, 1000.0 / fps, minimum);
		}
		else
		{
			SetEngineValue(engine.operationMode, L"0");
			Utilities::Log::Write("dynamic resolution off");
		}
		appliedFps = fps;
		appliedMinimum = minimum;
	}

	/* включено ли динамическое разрешение вообще (нами или вручную через r.DynamicRes.OperationMode) */
	LONG active = engine.operationMode && GetEngineInt(engine.operationMode, 0) != 0 ? 1 : 0;
	if (InterlockedExchange(&dynamicRes, active) != active)
		Utilities::Log::Write("engine dynamic resolution: %s", active ? "active" : "off");
}

const char* DLSS::DynamicText()
{
	return dynamicRes ? "  DYN" : "";
}

/*
	FP16 back buffer = scRGB (так UE выводит HDR в scRGB). 10-битный back buffer у UE 4.27 бывает и в SDR,
	поэтому HDR10 (PQ) определяем по r.HDR.EnableHDROutput.
*/
Overlay::HdrMode DLSS::DetectHdrMode(DXGI_FORMAT format)
{
	if (format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R16G16B16A16_TYPELESS)
		return Overlay::HdrMode::scRGB;
	if (hdrOutputCvar && GetEngineInt(hdrOutputCvar, 0) != 0 &&
	    (format == DXGI_FORMAT_R10G10B10A2_UNORM || format == DXGI_FORMAT_R10G10B10A2_TYPELESS))
		return Overlay::HdrMode::PQ;
	return Overlay::HdrMode::SDR;
}




/* ================================================================== надпись */

void DLSS::ReportStatus(Overlay::State state, const char* format, ...)
{
	char text[Overlay::MaxChars + 1];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	Overlay::SetStatus(state, text);
}




/* ================================================================== перехват 1: AddPasses */

ID3D11Resource* DLSS::NativeOf(void* rdgTexture)
{
	void* rhiTexture = Utilities::Memory::Read<void*>(rdgTexture, OFF_RDG_RHI);
	return Utilities::Memory::Read<ID3D11Resource*>(rhiTexture, OFF_RHI_NATIVE);
}

void __fastcall DLSS::HookAddPasses(const void* self, void* graphBuilder, const void* view, const void* passInputs,
                                    void** outTexture, IntRect* outRect, void** outHalfTexture, IntRect* outHalfRect)
{
	originalAddPasses(self, graphBuilder, view, passInputs, outTexture, outRect, outHalfTexture, outHalfRect);

	LONG number = InterlockedIncrement(&frameNumber);
	if (number == 1)
		RegisterConsoleVariables();   /* к первому кадру консоль движка уже создана */
	ApplySettingChanges();

	using Utilities::Memory;
	using Utilities::Matrix4;

	FrameData f;
	f.outputTexture = Memory::Read<void*>(outTexture);
	f.colorTexture = Memory::Read<void*>(passInputs, OFF_INPUT_COLOR);
	f.velocityTexture = Memory::Read<void*>(passInputs, OFF_INPUT_VELOCITY);
	IntRect output = Memory::Read<IntRect>(outRect);
	IntRect viewRect = Memory::Read<IntRect>(view, OFF_VIEW_RECT);
	f.outputWidth = output.maxX - output.minX;
	f.outputHeight = output.maxY - output.minY;
	f.renderWidth = viewRect.maxX - viewRect.minX;
	f.renderHeight = viewRect.maxY - viewRect.minY;
	f.colorWidth = Memory::Read<int>(f.colorTexture, OFF_RDG_EXTENT);
	f.colorHeight = Memory::Read<int>(f.colorTexture, OFF_RDG_EXTENT + 4);

	bool sane = f.outputTexture && f.colorTexture && f.outputWidth > 0 && f.outputHeight > 0 &&
	            f.renderWidth > 0 && f.renderHeight > 0 && f.renderWidth <= f.colorWidth && f.renderHeight <= f.colorHeight &&
	            viewRect.minX == 0 && viewRect.minY == 0;

	const uint8_t* matrices = reinterpret_cast<const uint8_t*>(view) + OFF_VIEW_MATRICES;
	Matrix4 projection, projectionNoAA, viewMatrix;
	sane = sane && Matrix4::ReadFloat(matrices + OFF_MATRIX_PROJ, projection) &&
	       Matrix4::ReadFloat(matrices + OFF_MATRIX_PROJ_NOAA, projectionNoAA) &&
	       Matrix4::ReadFloat(matrices + OFF_MATRIX_VIEW, viewMatrix) &&
	       projection.m[11] == 1.0 && projection.m[15] == 0.0;

	if (sane)
	{
		/* jitter: UE кладёт в ProjectionMatrix M[2][0] = 2*jx/W, M[2][1] = -2*jy/H */
		float jitterX = static_cast<float>(projection.m[8] * f.renderWidth * 0.5);
		float jitterY = static_cast<float>(-projection.m[9] * f.renderHeight * 0.5);
		f.jitterX = (settings.jitterMode & 1) ? -jitterX : jitterX;
		f.jitterY = (settings.jitterMode & 2) ? -jitterY : jitterY;

		/* обратная бесконечная проекция UE: M[3][2] = ближняя плоскость, M[1][1] = 1 / tan(FOV_Y / 2) */
		if (projectionNoAA.m[14] > 0.0)
			f.cameraNear = static_cast<float>(projectionNoAA.m[14]);
		if (projectionNoAA.m[5] > 0.0)
			f.fovY = static_cast<float>(2.0 * atan(1.0 / projectionNoAA.m[5]));

		Matrix4 viewProjection = Matrix4::Multiply(viewMatrix, projectionNoAA);
		Matrix4 inverseViewProjection;
		/* при динамическом разрешении вход меняется постоянно — история сбрасывается только при смене буфера или выхода */
		const bool renderChanged = f.renderWidth != lastRenderWidth || f.renderHeight != lastRenderHeight;
		bool sizeChanged = f.outputWidth != lastOutputWidth || f.outputHeight != lastOutputHeight ||
		                   f.colorWidth != lastColorWidth || f.colorHeight != lastColorHeight ||
		                   (renderChanged && dynamicRes == 0);

		if (havePrevious && sizeChanged == false && Matrix4::Inverse(viewProjection, inverseViewProjection))
		{
			Matrix4 clipToPrevClip = Matrix4::Multiply(inverseViewProjection, previousViewProjection);
			for (int i = 0; i < 16; ++i)
				f.clipToPrevClip[i] = static_cast<float>(clipToPrevClip.m[i]);
		}
		else
		{
			for (int i = 0; i < 16; ++i)
				f.clipToPrevClip[i] = (i % 5 == 0) ? 1.0f : 0.0f;
			f.reset = true;
		}

		if (sizeChanged)
			Utilities::Log::Write("frame %ld: input %dx%d (buffer %dx%d) -> output %dx%d (%.1f%%)%s", number, f.renderWidth, f.renderHeight,
			                      f.colorWidth, f.colorHeight, f.outputWidth, f.outputHeight, 100.0 * f.renderWidth / f.outputWidth,
			                      dynamicRes ? ", dynamic resolution" : "");

		previousViewProjection = viewProjection;
		havePrevious = true;
		lastRenderWidth = f.renderWidth;
		lastRenderHeight = f.renderHeight;
		lastOutputWidth = f.outputWidth;
		lastOutputHeight = f.outputHeight;
		lastColorWidth = f.colorWidth;
		lastColorHeight = f.colorHeight;
		f.valid = true;
	}
	else
	{
		havePrevious = false;
	}

	EnterCriticalSection(&frameLock);
	frame = f;
	LeaveCriticalSection(&frameLock);
}




/* ================================================================== DLSS */

bool DLSS::InitializeShader()
{
	ID3DBlob* code = nullptr;
	ID3DBlob* errors = nullptr;
	HRESULT result = D3DCompile(motionShaderSource, strlen(motionShaderSource), "DBDTechnologies_MotionVectors", nullptr, nullptr, "main",
	                            "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
	if (FAILED(result))
	{
		Utilities::Log::Write("motion vector shader error: %s", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
		Utilities::SafeRelease(errors);
		return false;
	}

	result = device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &motionShader);
	Utilities::SafeRelease(code);
	Utilities::SafeRelease(errors);
	if (FAILED(result))
	{
		Utilities::Log::Write("CreateComputeShader failed 0x%08X", static_cast<unsigned>(result));
		return false;
	}

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeof(MotionConstants);
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &motionConstants));
}

bool DLSS::InitializeNgx()
{
	/*
		Где NGX ищет nvngx_dlss.dll:
		  1) папка, где лежит DBDTechnologies.asi (передаём сами через PathListInfo),
		  2) папка exe (Binaries\Win64) — путь NGX по умолчанию.
		Если в драйвере NVIDIA включена подмена DLSS (NVIDIA App), он может взять свою копию.
	*/
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
		errorReason = "NGX INIT FAILED - NVIDIA DRIVER?";
		return false;
	}

	result = NVSDK_NGX_D3D11_GetCapabilityParameters(&ngxParameters);
	if (NVSDK_NGX_FAILED(result) || ngxParameters == nullptr)
	{
		Utilities::Log::Write("GetCapabilityParameters failed: 0x%08X", static_cast<unsigned>(result));
		errorReason = "NGX PARAMETERS FAILED";
		return false;
	}

	int available = 0, needsDriver = 0, initResult = 0;
	ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
	ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
	ngxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initResult);
	Utilities::Log::Write("NGX: DLSS available=%d, needs newer driver=%d, feature init result=0x%08X", available, needsDriver,
	                      static_cast<unsigned>(initResult));

	if (available == 0)
	{
		if (needsDriver)
			errorReason = "UPDATE NVIDIA DRIVER";
		else if (static_cast<unsigned>(initResult) == static_cast<unsigned>(NVSDK_NGX_Result_FAIL_FeatureNotFound))
			errorReason = "NVNGX_DLSS.DLL NOT FOUND";
		else
			errorReason = "DLSS NOT AVAILABLE (RTX GPU?)";
		Utilities::Log::Write("DLSS unavailable: %s. Put nvngx_dlss.dll next to DBDTechnologies.asi (%ls)", errorReason, asiDirectory.c_str());
		return false;
	}
	return true;
}

/* Какую копию DLSS реально загрузил NGX: файл рядом с модом/игрой или копию драйвера (ProgramData\NVIDIA\NGX) */
void DLSS::DescribeLoadedDll()
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
			snprintf(dllVersion, sizeof(dllVersion), "%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
			         HIWORD(info->dwFileVersionLS));
		}
		Utilities::Log::Write("DLSS library: %ls (version %s)", path, dllVersion[0] ? dllVersion : "?");
		return;
	}
	Utilities::Log::Write("DLSS library: not found among loaded modules");
}

bool DLSS::EnsureTextures(int width, int height)
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

bool DLSS::EnsureFeature(ID3D11DeviceContext* context, const FrameData& f)
{
	int preset = settings.preset;

	/*
		Обычно DLSS создаётся ровно под вход. При динамическом разрешении — под максимальный вход (буфер сцены,
		но не больше выхода), а реальный размер каждого кадра идёт в InRenderSubrectDimensions: без пересоздания.
	*/
	int createWidth = f.renderWidth, createHeight = f.renderHeight;
	if (dynamicRes)
	{
		createWidth = f.colorWidth < f.outputWidth ? f.colorWidth : f.outputWidth;
		createHeight = f.colorHeight < f.outputHeight ? f.colorHeight : f.outputHeight;
		if (createWidth < f.renderWidth) createWidth = f.renderWidth;
		if (createHeight < f.renderHeight) createHeight = f.renderHeight;
	}

	if (dlssHandle && dlssRenderWidth == createWidth && dlssRenderHeight == createHeight &&
	    dlssOutputWidth == f.outputWidth && dlssOutputHeight == f.outputHeight && dlssPreset == preset)
		return true;

	if (dlssHandle)
	{
		NVSDK_NGX_D3D11_ReleaseFeature(dlssHandle);
		dlssHandle = nullptr;
	}

	/* не пересоздавать каждый кадр с теми же параметрами, если уже не получилось */
	const int key[5] = { createWidth, createHeight, f.outputWidth, f.outputHeight, preset };
	if (memcmp(key, failedKey, sizeof(key)) == 0)
		return false;

	/* пресет читается при создании, поэтому при смене пресета DLSS пересоздаётся */
	const unsigned value = static_cast<unsigned>(preset);
	NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, value);
	NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, value);
	NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, value);
	NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, value);
	NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, value);
	NVSDK_NGX_Parameter_SetUI(ngxParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, value);

	NVSDK_NGX_DLSS_Create_Params create = {};
	create.Feature.InWidth = static_cast<unsigned>(createWidth);
	create.Feature.InHeight = static_cast<unsigned>(createHeight);
	create.Feature.InTargetWidth = static_cast<unsigned>(f.outputWidth);
	create.Feature.InTargetHeight = static_cast<unsigned>(f.outputHeight);
	create.Feature.InPerfQualityValue = QualityValue(createWidth, f.outputWidth);
	create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
	                              NVSDK_NGX_DLSS_Feature_Flags_DepthInverted | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
	create.InEnableOutputSubrects = false;

	NVSDK_NGX_Result result = NGX_D3D11_CREATE_DLSS_EXT(context, &dlssHandle, ngxParameters, &create);
	if (NVSDK_NGX_FAILED(result) || dlssHandle == nullptr)
	{
		Utilities::Log::Write("DLSS create failed %dx%d -> %dx%d preset %s: 0x%08X", createWidth, createHeight,
		                      f.outputWidth, f.outputHeight, PresetName(preset), static_cast<unsigned>(result));
		dlssHandle = nullptr;
		memcpy(failedKey, key, sizeof(key));
		errorReason = static_cast<unsigned>(result) == static_cast<unsigned>(NVSDK_NGX_Result_FAIL_FeatureNotFound) ?
		              "NVNGX_DLSS.DLL NOT FOUND" : "DLSS CREATE FAILED";
		return false;
	}

	dlssRenderWidth = createWidth;
	dlssRenderHeight = createHeight;
	dlssOutputWidth = f.outputWidth;
	dlssOutputHeight = f.outputHeight;
	dlssPreset = preset;
	errorReason = "";

	static bool described = false;
	if (described == false)
	{
		described = true;
		DescribeLoadedDll();
	}
	Utilities::Log::Write("DLSS created: %dx%d -> %dx%d, %s, preset %s%s", createWidth, createHeight, f.outputWidth,
	                      f.outputHeight, QualityName(createWidth, f.outputWidth), PresetName(preset),
	                      dynamicRes ? " (dynamic resolution: max input)" : "");
	return true;
}

bool DLSS::RunTemporal(ID3D11DeviceContext* context, ID3D11Resource* output, const FrameData& f, LONG number, int32_t method)
{
	const bool useXess = method == MethodXeSS;
	const bool useFsr2 = method == MethodFSR2;
	const bool useDlss = useXess == false && useFsr2 == false;
	const char* name = useXess ? "XeSS" : useFsr2 ? "FSR 2" : "DLSS";

	if (device == nullptr)
	{
		context->GetDevice(&device);
		Utilities::Log::Write("ID3D11Device %p, feature level 0x%X", device, static_cast<unsigned>(device->GetFeatureLevel()));
	}
	if (motionShader == nullptr)
	{
		static bool shaderFailed = false;
		if (shaderFailed || InitializeShader() == false)
		{
			shaderFailed = true;
			errorReason = "SHADER ERROR";
			return false;
		}
	}

	if (useXess)
	{
		if (XeSS::Initialize(device) == false)
		{
			errorReason = XeSS::ErrorReason();
			return false;
		}
	}
	else if (useDlss)
	{
		static const char* dlssReason = "";   /* причина остаётся и после переключения на XeSS и обратно */
		if (dlssFailed)
		{
			errorReason = dlssReason;
			return false;
		}
		if (ngxReady == false)
		{
			ngxReady = InitializeNgx();
			if (ngxReady == false)
			{
				dlssFailed = true;
				dlssReason = errorReason;
				Utilities::Log::Write("DLSS unavailable - original TAA stays until restart");
				return false;
			}
		}
	}

	/* входы TAA: t2 = глубина (R32-view), t3 = скорость; цвет — из FRDGTexture */
	ID3D11ShaderResourceView* bound[4] = {};
	context->CSGetShaderResources(0, 4, bound);
	ID3D11Resource* color = NativeOf(f.colorTexture);
	ID3D11ShaderResourceView* depthSrv = bound[2];
	ID3D11ShaderResourceView* velocitySrv = bound[3];

	int velocityWidth = Utilities::Memory::Read<int>(f.velocityTexture, OFF_RDG_EXTENT);
	bool hasVelocity = velocitySrv && velocityWidth > 1;   /* 1x1 = заглушка, движущихся объектов нет */

	bool ok = color && depthSrv && EnsureTextures(f.colorWidth, f.colorHeight) && (useDlss == false || EnsureFeature(context, f));
	if (ok == false && (color == nullptr || depthSrv == nullptr))
		errorReason = "NO TAA INPUTS";
	if (ok == false)
	{
		static int warned = 0;
		if (warned++ < 5)
			Utilities::Log::Write("frame %ld: %s skipped (color %p, depth %p)", number, name, color, depthSrv);
	}

	if (ok)
	{
		savedState.Save(context);

		/* 1) векторы движения + глубина (общие для DLSS и XeSS) */
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(context->Map(motionConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			MotionConstants c = {};
			memcpy(c.clipToPrevClip, f.clipToPrevClip, sizeof(c.clipToPrevClip));
			c.renderSize[0] = static_cast<float>(f.renderWidth);
			c.renderSize[1] = static_cast<float>(f.renderHeight);
			c.invRenderSize[0] = 1.0f / f.renderWidth;
			c.invRenderSize[1] = 1.0f / f.renderHeight;
			c.hasVelocity = hasVelocity ? 1u : 0u;
			c.decodeMode = static_cast<uint32_t>(settings.velocityDecode);
			memcpy(mapped.pData, &c, sizeof(c));
			context->Unmap(motionConstants, 0);
		}

		ID3D11ShaderResourceView* inputs[2] = { depthSrv, hasVelocity ? velocitySrv : nullptr };
		ID3D11UnorderedAccessView* outputs[2] = { motionUav, depthUav };

		Utilities::ComputeState::Unbind(context);   /* освободить выход TAA для апскейлера */
		context->CSSetShader(motionShader, nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &motionConstants);
		context->CSSetShaderResources(0, 2, inputs);
		context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
		context->Dispatch(static_cast<UINT>(f.renderWidth + 7) / 8, static_cast<UINT>(f.renderHeight + 7) / 8, 1);
		Utilities::ComputeState::Unbind(context);

		/* 2) сам апскейлер */
		bool reset = f.reset || InterlockedExchange(&forceReset, 0) != 0;
		if (useXess)
		{
			XeSS::Inputs in;
			in.color = color;
			in.motionVectors = motionTexture;
			in.depth = depthTexture;
			in.output = output;
			in.renderWidth = f.renderWidth;
			in.renderHeight = f.renderHeight;
			in.outputWidth = f.outputWidth;
			in.outputHeight = f.outputHeight;
			in.jitterX = f.jitterX;
			in.jitterY = f.jitterY;
			in.reset = reset;
			ok = XeSS::Execute(in);
			if (ok == false)
				errorReason = XeSS::ErrorReason();
		}
		else if (useFsr2)
		{
			/* время кадра для FSR 2 (поток рендера) */
			static LARGE_INTEGER frequency = {}, previous = {};
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			if (frequency.QuadPart == 0)
				QueryPerformanceFrequency(&frequency);
			float frameMs = previous.QuadPart ? static_cast<float>((now.QuadPart - previous.QuadPart) * 1000.0 / frequency.QuadPart) : 16.6f;
			previous = now;

			FSR2::Inputs in;
			in.color = color;
			in.motionVectors = motionTexture;
			in.depth = depthTexture;
			in.output = output;
			in.renderWidth = f.renderWidth;
			in.renderHeight = f.renderHeight;
			in.maxRenderWidth = f.renderWidth;
			in.maxRenderHeight = f.renderHeight;
			if (dynamicRes)
			{
				/* как у DLSS: контекст под максимальный вход, без пересоздания при каждой смене */
				in.maxRenderWidth = f.colorWidth < f.outputWidth ? f.colorWidth : f.outputWidth;
				in.maxRenderHeight = f.colorHeight < f.outputHeight ? f.colorHeight : f.outputHeight;
				if (in.maxRenderWidth < f.renderWidth) in.maxRenderWidth = f.renderWidth;
				if (in.maxRenderHeight < f.renderHeight) in.maxRenderHeight = f.renderHeight;
			}
			in.outputWidth = f.outputWidth;
			in.outputHeight = f.outputHeight;
			in.jitterX = f.jitterX;
			in.jitterY = f.jitterY;
			in.cameraNear = f.cameraNear;
			in.fovY = f.fovY;
			in.sharpness = FSR::IsSharpening(settings.sharpness) ? settings.sharpness : 0.0f;   /* своя RCAS внутри FSR 2 */
			in.frameTimeMs = frameMs < 1.0f ? 1.0f : frameMs;
			in.reset = reset;
			in.dynamicResolution = dynamicRes != 0;
			ok = FSR2::Execute(context, in);
			if (ok == false)
				errorReason = FSR2::ErrorReason();
		}
		else
		{
			NVSDK_NGX_D3D11_DLSS_Eval_Params evaluate = {};
			evaluate.Feature.pInColor = color;
			evaluate.Feature.pInOutput = output;
			evaluate.pInDepth = depthTexture;
			evaluate.pInMotionVectors = motionTexture;
			evaluate.InJitterOffsetX = f.jitterX;
			evaluate.InJitterOffsetY = f.jitterY;
			evaluate.InRenderSubrectDimensions.Width = static_cast<unsigned>(f.renderWidth);
			evaluate.InRenderSubrectDimensions.Height = static_cast<unsigned>(f.renderHeight);
			evaluate.InReset = reset ? 1 : 0;
			evaluate.InMVScaleX = 1.0f;
			evaluate.InMVScaleY = 1.0f;
			NVSDK_NGX_Result result = NGX_D3D11_EVALUATE_DLSS_EXT(context, dlssHandle, ngxParameters, &evaluate);
			if (NVSDK_NGX_FAILED(result))
			{
				static int errors = 0;
				if (errors++ < 10)
					Utilities::Log::Write("DLSS evaluate failed: 0x%08X", static_cast<unsigned>(result));
				errorReason = "DLSS EVALUATE FAILED";
				ok = false;
			}
		}

		savedState.Restore(context);

		if (ok)
		{
			/* 3) резкость (AMD RCAS) поверх результата; у FSR 2 она своя, внутри */
			if (useFsr2 == false)
				FSR::Sharpen(context, output, static_cast<UINT>(f.outputWidth), static_cast<UINT>(f.outputHeight), settings.sharpness);

			static bool first[3] = { true, true, true };
			const int which = useXess ? 1 : useFsr2 ? 2 : 0;
			if (first[which])
			{
				first[which] = false;
				Utilities::Log::Write("frame %ld: first %s frame done", number, name);
			}
		}
	}

	for (ID3D11ShaderResourceView* srv : bound)
		if (srv)
			srv->Release();
	return ok;
}




/* ================================================================== перехват 2: Dispatch (TAA -> DLSS) */

void __fastcall DLSS::HookDispatch(void* context, UINT x, UINT y, UINT z)
{
	FrameData f;
	EnterCriticalSection(&frameLock);
	f = frame;
	LeaveCriticalSection(&frameLock);

	if (f.valid)
	{
		ID3D11DeviceContext* d3dContext = Utilities::Memory::Read<ID3D11DeviceContext*>(context, OFF_CONTEXT_D3D11);
		ID3D11Resource* output = NativeOf(f.outputTexture);

		if (d3dContext && output)
		{
			ID3D11UnorderedAccessView* uav = nullptr;
			d3dContext->CSGetUnorderedAccessViews(0, 1, &uav);
			ID3D11Resource* boundOutput = nullptr;
			if (uav)
			{
				uav->GetResource(&boundOutput);
				uav->Release();
			}
			if (boundOutput)
				boundOutput->Release();   /* сравниваем только адрес */

			if (boundOutput == output)
			{
				/* это TAA текущего кадра */
				EnterCriticalSection(&frameLock);
				frame.valid = false;
				LeaveCriticalSection(&frameLock);

				const bool upsampling = f.renderWidth < f.outputWidth;

				if (settings.method == MethodDLSS)
				{
					if (RunTemporal(d3dContext, output, f, frameNumber, MethodDLSS))
					{
						ReportStatus(Overlay::State::Active, "DLSS %s  %s  PRESET %s  %dX%d > %dX%d  J%d%s%s%s",
						             dllVersion[0] ? dllVersion : "ON", QualityName(f.renderWidth, f.outputWidth),
						             PresetName(dlssPreset >= 0 ? dlssPreset : settings.preset), f.renderWidth, f.renderHeight,
						             f.outputWidth, f.outputHeight, settings.jitterMode, SharpnessText(), DynamicText(),
						             (upsampling == false && backBufferWidth > static_cast<UINT>(f.outputWidth)) ? "  (UPSAMPLING OFF)" : "");
						return;
					}
					originalDispatch(context, x, y, z);
					ReportStatus(Overlay::State::Error, "DLSS ERROR: %s (TAA)", errorReason[0] ? errorReason : "SEE DBDTECHNOLOGIES.LOG");
					return;
				}

				if (settings.method == MethodFSR2)
				{
					if (RunTemporal(d3dContext, output, f, frameNumber, MethodFSR2))
					{
						const char* quality = QualityName(f.renderWidth, f.outputWidth);
						ReportStatus(Overlay::State::Active, "FSR 2.2  %s  %dX%d > %dX%d  J%d%s%s%s",
						             strcmp(quality, "DLAA") == 0 ? "NATIVE AA" : quality, f.renderWidth, f.renderHeight,
						             f.outputWidth, f.outputHeight, settings.jitterMode, SharpnessText(), DynamicText(),
						             (upsampling == false && backBufferWidth > static_cast<UINT>(f.outputWidth)) ? "  (UPSAMPLING OFF)" : "");
						return;
					}
					originalDispatch(context, x, y, z);
					ReportStatus(Overlay::State::Error, "%s (TAA)", errorReason[0] ? errorReason : "FSR 2 ERROR - SEE DBDTECHNOLOGIES.LOG");
					return;
				}

				if (settings.method == MethodXeSS)
				{
					if (RunTemporal(d3dContext, output, f, frameNumber, MethodXeSS))
					{
						ReportStatus(Overlay::State::Active, "XESS %s  %s  %dX%d > %dX%d  J%d%s%s%s",
						             XeSS::VersionText()[0] ? XeSS::VersionText() : "ON", XeSS::QualityText(), f.renderWidth,
						             f.renderHeight, f.outputWidth, f.outputHeight, settings.jitterMode, SharpnessText(), DynamicText(),
						             (upsampling == false && backBufferWidth > static_cast<UINT>(f.outputWidth)) ? "  (UPSAMPLING OFF)" : "");
						return;
					}
					originalDispatch(context, x, y, z);
					ReportStatus(Overlay::State::Error, "%s (TAA)", errorReason[0] ? errorReason : "XESS ERROR - SEE DBDTECHNOLOGIES.LOG");
					return;
				}

				originalDispatch(context, x, y, z);

				if (settings.method == MethodFSR1)
				{
					if (upsampling)
					{
						/* TAAU уже растянул кадр — FSR 1 негде работать */
						ReportStatus(Overlay::State::Error, "FSR 1 NEEDS R.TEMPORALAA.UPSAMPLING 0 (TAAU)");
					}
					else
					{
						/* ждём пространственное растягивание движка в этом кадре */
						upscaleWait.armed = true;
						upscaleWait.renderWidth = f.renderWidth;
						upscaleWait.renderHeight = f.renderHeight;
						upscaleWait.colorWidth = f.colorWidth;
						upscaleWait.colorHeight = f.colorHeight;
						ReportStatus(Overlay::State::Disabled, "FSR 1: 100%% RESOLUTION - NOTHING TO UPSCALE (TAA)");
					}
				}
				else
				{
					/*
						r.Upscaler.Method 0: работает то, что встроено в движок.
						r.TemporalAA.Upsampling 1 и вход меньше выхода = TAAU (TSR в UE 4.27 нет — он появился в UE5).
						TAA без TAAU сглаживает в разрешении входа; растягивает потом (если нужно) простой фильтр движка.
					*/
					if (upsampling)
						ReportStatus(Overlay::State::Disabled, "TAAU (ENGINE)  %dX%d > %dX%d%s", f.renderWidth, f.renderHeight,
						             f.outputWidth, f.outputHeight, DynamicText());
					else if (backBufferWidth > static_cast<UINT>(f.outputWidth))
						ReportStatus(Overlay::State::Disabled, "TAA (ENGINE)  %dX%d > %uX%u SPATIAL", f.renderWidth, f.renderHeight,
						             backBufferWidth, backBufferHeight);
					else
						ReportStatus(Overlay::State::Disabled, "TAA (ENGINE)  %dX%d NATIVE", f.renderWidth, f.renderHeight);
				}
				return;
			}
		}
	}

	originalDispatch(context, x, y, z);
}




/* ================================================================== перехват 3: отрисовка (растягивание -> FSR 1) */

/*
	Ищем отрисовку FUpscalePS: цель — полноразмерная (больше входа), а один из входов пиксельного шейдера —
	текстура размером с буфер сцены, где лежит кадр в уменьшенном разрешении. true = заменено FSR 1.
*/
bool DLSS::TryFsr1(void* context)
{
	if (upscaleWait.armed == false || settings.method != MethodFSR1)
		return false;

	ID3D11DeviceContext* d3dContext = Utilities::Memory::Read<ID3D11DeviceContext*>(context, OFF_CONTEXT_D3D11);
	if (d3dContext == nullptr)
		return false;

	UINT viewportCount = 1;
	D3D11_VIEWPORT viewport = {};
	d3dContext->RSGetViewports(&viewportCount, &viewport);
	if (viewportCount == 0 || viewport.Width <= upscaleWait.renderWidth * 1.01f || viewport.Height <= upscaleWait.renderHeight * 1.01f)
		return false;

	ID3D11RenderTargetView* rtv = nullptr;
	d3dContext->OMGetRenderTargets(1, &rtv, nullptr);
	if (rtv == nullptr)
		return false;
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtv->GetDesc(&rtvDesc);
	ID3D11Resource* renderTarget = nullptr;
	rtv->GetResource(&renderTarget);
	rtv->Release();

	/* вход: текстура размером с буфер сцены (colorWidth x colorHeight) */
	ID3D11ShaderResourceView* srvs[8] = {};
	d3dContext->PSGetShaderResources(0, 8, srvs);
	ID3D11ShaderResourceView* input = nullptr;
	for (ID3D11ShaderResourceView* srv : srvs)
	{
		if (srv == nullptr || input)
			continue;
		ID3D11Resource* resource = nullptr;
		srv->GetResource(&resource);
		D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
		if (resource)
			resource->GetType(&dimension);
		if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
		{
			D3D11_TEXTURE2D_DESC desc;
			static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
			if (static_cast<int>(desc.Width) == upscaleWait.colorWidth && static_cast<int>(desc.Height) == upscaleWait.colorHeight)
				input = srv;
		}
		Utilities::SafeRelease(resource);
	}

	bool done = false;
	if (input && renderTarget)
	{
		/* найдено: дальше в этом кадре не ищем, даже если FSR 1 не сможет */
		upscaleWait.armed = false;
		done = FSR::Upscale(d3dContext, input, static_cast<UINT>(upscaleWait.renderWidth), static_cast<UINT>(upscaleWait.renderHeight),
		                    static_cast<UINT>(upscaleWait.colorWidth), static_cast<UINT>(upscaleWait.colorHeight), renderTarget,
		                    rtvDesc.Format, viewport, settings.sharpness);

		if (done)
			ReportStatus(Overlay::State::Active, "FSR 1  %s  %dX%d > %.0fX%.0f%s", QualityName(upscaleWait.renderWidth, static_cast<int>(viewport.Width)),
			             upscaleWait.renderWidth, upscaleWait.renderHeight, viewport.Width, viewport.Height, SharpnessText());
		else
			ReportStatus(Overlay::State::Error, "FSR 1 ERROR - SEE DBDTECHNOLOGIES.LOG");

		static bool logged = false;
		if (logged == false)
		{
			logged = true;
			Utilities::Log::Write("FSR 1: spatial upscale found (%dx%d -> %.0fx%.0f, target format %u) - %s",
			                      upscaleWait.renderWidth, upscaleWait.renderHeight, viewport.Width, viewport.Height,
			                      static_cast<unsigned>(rtvDesc.Format), done ? "replaced" : "left to the game");
		}
	}

	for (ID3D11ShaderResourceView* srv : srvs)
		if (srv)
			srv->Release();
	Utilities::SafeRelease(renderTarget);
	return done;
}

void __fastcall DLSS::HookDraw(void* context, UINT baseVertex, UINT numPrimitives, UINT numInstances)
{
	if (upscaleWait.armed && TryFsr1(context))
		return;
	originalDraw(context, baseVertex, numPrimitives, numInstances);
}

void __fastcall DLSS::HookDrawIndexed(void* context, void* indexBuffer, INT baseVertex, UINT firstInstance, UINT numVertices,
                                      UINT startIndex, UINT numPrimitives, UINT numInstances)
{
	if (upscaleWait.armed && TryFsr1(context))
		return;
	originalDrawIndexed(context, indexBuffer, baseVertex, firstInstance, numVertices, startIndex, numPrimitives, numInstances);
}




/* ================================================================== перехват 4: конец кадра */

void __fastcall DLSS::HookEndDrawing(void* context, void* viewport, bool present, bool lockToVsync)
{
	ID3D11DeviceContext* d3dContext = Utilities::Memory::Read<ID3D11DeviceContext*>(context, OFF_CONTEXT_D3D11);

	/* Reflex нужно устройство D3D11 игры — берём его здесь один раз */
	static bool deviceShared = false;
	if (deviceShared == false && d3dContext)
	{
		ID3D11Device* d3dDevice = nullptr;
		d3dContext->GetDevice(&d3dDevice);
		if (d3dDevice)
		{
			Reflex::SetDevice(d3dDevice);
			d3dDevice->Release();
			deviceShared = true;
		}
	}

	/* растягивание в этом кадре не встретилось — не ищем его в следующем по старым данным */
	if (upscaleWait.armed && settings.method == MethodFSR1 && upscaleWait.renderWidth < static_cast<int>(backBufferWidth))
	{
		static int notFound = 0;
		if (notFound++ == 120)
			Utilities::Log::Write("FSR 1: spatial upscale draw not found (input %dx%d in %dx%d buffer, screen %ux%u)",
			                      upscaleWait.renderWidth, upscaleWait.renderHeight, upscaleWait.colorWidth, upscaleWait.colorHeight,
			                      backBufferWidth, backBufferHeight);
		ReportStatus(Overlay::State::Error, "FSR 1: UPSCALE PASS NOT FOUND (GAME UPSCALE)");
	}
	upscaleWait.armed = false;

	if (present && d3dContext && Utilities::Memory::Read<uintptr_t>(viewport) == base + RVA_VIEWPORT_VTABLE)
	{
		void* backBufferTexture = Utilities::Memory::Read<void*>(viewport, OFF_VIEWPORT_BACKBUFFER);
		ID3D11Resource* backBuffer = Utilities::Memory::Read<ID3D11Resource*>(backBufferTexture, OFF_RHI_NATIVE);

		if (backBuffer)
		{
			D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
			backBuffer->GetType(&dimension);
			if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
			{
				D3D11_TEXTURE2D_DESC desc;
				static_cast<ID3D11Texture2D*>(backBuffer)->GetDesc(&desc);
				backBufferWidth = desc.Width;
				backBufferHeight = desc.Height;

				Overlay::HdrMode hdr = DetectHdrMode(desc.Format);
				static int lastHdr = -1;
				if (static_cast<int>(hdr) != lastHdr)
				{
					static const char* names[] = { "SDR", "HDR10 (PQ)", "HDR scRGB" };
					Utilities::Log::Write("output: %s, back buffer %ux%u format %u", names[static_cast<int>(hdr)], desc.Width, desc.Height,
					                      static_cast<unsigned>(desc.Format));
					lastHdr = static_cast<int>(hdr);
				}
				Overlay::SetHdrMode(hdr);
			}
			if (settings.overlay)
				Overlay::Draw(d3dContext, backBuffer);
		}
	}

	originalEndDrawing(context, viewport, present, lockToVsync);
}




/* ================================================================== установка */

bool DLSS::InstallHooks()
{
	using Utilities::Memory;

	uintptr_t* upscalerVtable = reinterpret_cast<uintptr_t*>(base + RVA_UPSCALER_VTABLE);
	auto slot = [](uintptr_t address) { return Memory::Read<uintptr_t>(reinterpret_cast<void*>(address)); };

	bool matches =
		Memory::CheckBytes(base + RVA_ADDPASSES, SIGNATURE_ADDPASSES, sizeof(SIGNATURE_ADDPASSES)) &&
		Memory::CheckBytes(base + RVA_DISPATCH, SIGNATURE_DISPATCH, sizeof(SIGNATURE_DISPATCH)) &&
		Memory::CheckBytes(base + RVA_END_DRAWING, SIGNATURE_END_DRAWING, sizeof(SIGNATURE_END_DRAWING)) &&
		slot(reinterpret_cast<uintptr_t>(&upscalerVtable[1])) == base + RVA_GETDEBUGNAME &&
		slot(reinterpret_cast<uintptr_t>(&upscalerVtable[2])) == base + RVA_ADDPASSES &&
		slot(base + RVA_DISPATCH_SLOT) == base + RVA_DISPATCH &&
		slot(base + RVA_END_DRAWING_SLOT) == base + RVA_END_DRAWING;

	if (matches == false)
	{
		Utilities::Log::Write("this exe does not match Dead by Daylight 6.7.2 (FBB8A25C...) - mod disabled");
		return false;
	}

	if (Memory::PatchPointer(reinterpret_cast<uintptr_t>(&upscalerVtable[2]), reinterpret_cast<void*>(&HookAddPasses),
	                         reinterpret_cast<void**>(&originalAddPasses)) == false ||
	    Memory::PatchPointer(base + RVA_DISPATCH_SLOT, reinterpret_cast<void*>(&HookDispatch),
	                         reinterpret_cast<void**>(&originalDispatch)) == false ||
	    Memory::PatchPointer(base + RVA_END_DRAWING_SLOT, reinterpret_cast<void*>(&HookEndDrawing),
	                         reinterpret_cast<void**>(&originalEndDrawing)) == false)
	{
		Utilities::Log::Write("failed to patch vtables");
		return false;
	}

	/* FSR 1: перехват отрисовки (не обязателен — без него просто не будет FSR 1) */
	bool drawMatches =
		Memory::CheckBytes(base + RVA_DRAW, SIGNATURE_DRAW, sizeof(SIGNATURE_DRAW)) &&
		Memory::CheckBytes(base + RVA_DRAW_INDEXED, SIGNATURE_DRAW_INDEXED, sizeof(SIGNATURE_DRAW_INDEXED)) &&
		slot(base + RVA_DRAW_SLOT) == base + RVA_DRAW &&
		slot(base + RVA_DRAW_INDEXED_SLOT) == base + RVA_DRAW_INDEXED;
	if (drawMatches &&
	    Memory::PatchPointer(base + RVA_DRAW_SLOT, reinterpret_cast<void*>(&HookDraw), reinterpret_cast<void**>(&originalDraw)) &&
	    Memory::PatchPointer(base + RVA_DRAW_INDEXED_SLOT, reinterpret_cast<void*>(&HookDrawIndexed), reinterpret_cast<void**>(&originalDrawIndexed)))
		Utilities::Log::Write("draw hooks installed (FSR 1)");
	else
		Utilities::Log::Write("draw hooks not installed - FSR 1 unavailable");

	return true;
}

void DLSS::Init()
{
	Utilities::Log::Open(Utilities::Module::GetDirectory() + L"DBDTechnologies.log");
	Utilities::Log::Write("Starting initialization sequence...");
	InitializeCriticalSection(&frameLock);

	base = Utilities::Module::GetBase();
	Utilities::Log::Write("exe base %p", reinterpret_cast<void*>(base));

	if (InstallHooks())
	{
		Overlay::SetExtraTextProvider(&Reflex::StatusText);
		Reflex::gameThreadCallback = &DLSS::GameThreadTick;
		if (Reflex::InstallHooks(base))
			Utilities::Log::Write("Reflex hook installed (game thread frame start)");
		Utilities::Log::Write("hooks installed. Console: r.Upscaler.*, r.DLSS.*, r.LowLatency.* (defaults: everything off)");
	}
}
