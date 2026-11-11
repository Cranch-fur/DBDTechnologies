#include "Console.h"
#include "DLSS.h"
#include "LowLatency.h"






void* Console::Manager()
{
	void* manager = Utilities::Memory::Read<void*>(reinterpret_cast<void*>(base + RVA_CONSOLE_SINGLETON));
	uintptr_t vtable = Utilities::Memory::Read<uintptr_t>(manager);
	return (manager && vtable == base + RVA_CONSOLE_VTABLE) ? manager : nullptr;
}

bool Console::IsInsideExecutable(uintptr_t address)
{
	return address >= base && address < base + 0x10000000;
}

void Console::Register(uintptr_t executableBase)
{
	using RegisterIntRef_t = void* (__fastcall*)(void* self, const wchar_t* name, int32_t* reference, const wchar_t* help, uint32_t flags);
	using RegisterFloatRef_t = void* (__fastcall*)(void* self, const wchar_t* name, float* reference, const wchar_t* help, uint32_t flags);

	base = executableBase;
	void* manager = Manager();
	if (manager == nullptr)
	{
		Utilities::Log::Write("console manager not found - console variables unavailable");
		return;
	}

	uintptr_t vtable = Utilities::Memory::Read<uintptr_t>(manager);
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
		{ L"r.Upscaler.Method", &settings.method, nullptr,
		  L"Upscaler: 0 = TAA / TAAU (engine, default), 1 = TSR (engine Gen 5 TAAU, experimental in 4.27), "
		  L"2 = DLSS (any RTX GPU), 3 = XeSS (D3D11 version works on Intel GPUs only), "
		  L"4 = FSR 1 (replaces the spatial upscale, needs r.TemporalAA.Upsampling 0), 5 = FSR 2.2 (any GPU), 6 = FSR 3.1 (any GPU). "
		  L"DLSS, XeSS, FSR 2 and FSR 3 replace TAA/TAAU and need r.TemporalAA.Upsampling 1. Sets r.TemporalAA.Algorithm. Unsupported value -> 0." },
		{ L"r.Upscaler.Sharpness", nullptr, &settings.sharpness,
		  L"Sharpening after the upscaler (AMD RCAS), 0.0 = off (default) .. 1.0 = maximum. Out of range -> clamped." },
		{ L"r.Upscaler.Overlay", &settings.overlay, nullptr,
		  L"Status text in the top-left corner: 1 = show, 0 = hide (default)." },
		{ L"r.Upscaler.JitterMode", &settings.jitterMode, nullptr,
		  L"DLSS/XeSS/FSR 2/FSR 3 jitter sign mode 0..3. Pick the one with the most stable image at a low r.ScreenPercentage." },
		{ L"r.Upscaler.VelocityDecode", &settings.velocityDecode, nullptr,
		  L"UE velocity buffer decoding for DLSS/XeSS/FSR 2/FSR 3 motion vectors: 0 = linear (4.27), 1 = quadratic." },
		{ L"r.Upscaler.Reset", &settings.resetRequest, nullptr,
		  L"1 = reset upscaler history. Goes back to 0 by itself." },
		{ L"r.Upscaler.TargetFPS", &settings.targetFps, nullptr,
		  L"Dynamic resolution: 0 = off (default), 20..500 = lower the resolution when the GPU cannot hold this FPS. "
		  L"Sets the engine r.DynamicRes.* variables (between r.Upscaler.MinResolution and 100%). Needs r.TemporalAA.Upsampling 1." },
		{ L"r.Upscaler.MinResolution", &settings.minResolution, nullptr,
		  L"Lowest resolution for r.Upscaler.TargetFPS, percent of the screen: 25..100, default 50." },
		{ L"r.DLSS.Preset", &settings.dlssPreset, nullptr,
		  L"DLSS preset: 0 = Default, 10 = J, 11 = K (DLSS 4), 12 = L, 13 = M (DLSS 4.5), 5 = E, 6 = F. Unknown value -> Default." },
		{ L"r.LowLatency.Mode", &LowLatency::mode, nullptr,
		  L"Low latency mode: 0 = off, 1 = on, 2 = on + boost (boost: NVIDIA only). NVIDIA Reflex on GeForce, "
		  L"AMD Anti-Lag 2 on Radeon RX 5000+, not available on Intel (DX11). Unknown value -> 0." },
		{ L"r.LowLatency.Markers", &LowLatency::markers, nullptr,
		  L"NVIDIA Reflex latency markers: 1 = on (default, better pacing + latency stats in NVIDIA overlays), 0 = Sleep only." },
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
	InterlockedExchange(&ready, 1);
}

void Console::Validate()
{
	static Settings last = { -1 };
	static int32_t lastLowLatency = LowLatency::mode;

	if (settings.method < static_cast<int32_t>(UpscalerMethod::TAA) || settings.method > static_cast<int32_t>(UpscalerMethod::FSR3))
	{
		Utilities::Log::Write("r.Upscaler.Method %d is not supported (0 = TAA, 1 = TSR, 2 = DLSS, 3 = XeSS, 4 = FSR 1, 5 = FSR 2, 6 = FSR 3) -> 0",
		                      settings.method);
		settings.method = 0;
	}
	if (DLSS::IsValidPreset(settings.dlssPreset) == false)
	{
		Utilities::Log::Write("r.DLSS.Preset %d does not exist -> Default", settings.dlssPreset);
		settings.dlssPreset = 0;
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

	LowLatency::ValidateMode();
	if (LowLatency::mode != lastLowLatency)
	{
		Utilities::Log::Write("r.LowLatency.Mode %d", LowLatency::mode);
		lastLowLatency = LowLatency::mode;
	}

	if (settings.resetRequest)
	{
		settings.resetRequest = 0;
		InterlockedExchange(&resetPending, 1);
		Utilities::Log::Write("history reset");
	}

	if (last.method != -1)
	{
		static const char* methodNames[] = { "TAA (engine)", "TSR (engine)", "DLSS", "XeSS", "FSR 1", "FSR 2", "FSR 3" };
		if (settings.method != last.method)
			Utilities::Log::Write("upscaler: %s", methodNames[settings.method]);
		if (settings.dlssPreset != last.dlssPreset)
			Utilities::Log::Write("DLSS preset %s", DLSS::PresetName(settings.dlssPreset));
		if (settings.jitterMode != last.jitterMode)
			Utilities::Log::Write("jitter mode %d", settings.jitterMode);
		if (settings.overlay != last.overlay)
			Utilities::Log::Write("overlay %s", settings.overlay ? "shown" : "hidden");
		if (settings.sharpness != last.sharpness)
			Utilities::Log::Write("sharpness %.2f", settings.sharpness);
		if (settings.targetFps != last.targetFps || settings.minResolution != last.minResolution)
			Utilities::Log::Write("target FPS %d, min resolution %d%%", settings.targetFps, settings.minResolution);
		if (settings.method != last.method || settings.jitterMode != last.jitterMode)
			InterlockedExchange(&resetPending, 1);
	}
	last = settings;
}

void* Console::FindEngineVariable(const wchar_t* name)
{
	using Find_t = void* (__fastcall*)(void* self, const wchar_t* name, bool trackFrequentCalls);

	void* manager = Manager();
	if (manager == nullptr)
		return nullptr;

	uintptr_t vtable = Utilities::Memory::Read<uintptr_t>(manager);
	auto find = reinterpret_cast<Find_t>(Utilities::Memory::Read<uintptr_t>(reinterpret_cast<void*>(vtable), SLOT_FIND_CONSOLE_VARIABLE * 8));
	return find ? find(manager, name, false) : nullptr;
}

int Console::GetEngineInt(void* variable, int fallback)
{
	using GetInt_t = int32_t(__fastcall*)(void* self);

	uintptr_t vtable = Utilities::Memory::Read<uintptr_t>(variable);
	uintptr_t function = vtable ? Utilities::Memory::Read<uintptr_t>(reinterpret_cast<void*>(vtable), SLOT_CVAR_GET_INT * 8) : 0;
	if (IsInsideExecutable(function) == false)
		return fallback;
	return reinterpret_cast<GetInt_t>(function)(variable);
}

void Console::SetEngineValue(void* variable, const wchar_t* value)
{
	using Set_t = void(__fastcall*)(void* self, const wchar_t* value, uint32_t setBy);

	uintptr_t vtable = Utilities::Memory::Read<uintptr_t>(variable);
	uintptr_t function = vtable ? Utilities::Memory::Read<uintptr_t>(reinterpret_cast<void*>(vtable), SLOT_CVAR_SET * 8) : 0;
	if (IsInsideExecutable(function) == false)
		return;
	reinterpret_cast<Set_t>(function)(variable, value, ECVF_SET_BY_CONSOLE);
}
