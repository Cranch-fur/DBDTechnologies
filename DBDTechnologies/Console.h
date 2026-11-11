#pragma once
#include "Utilities.h"






enum class UpscalerMethod : int32_t
{
	TAA = 0,    // Engine TAA / TAAU (r.TemporalAA.Algorithm 0)
	TSR = 1,    // Engine Gen 5 TAAU, the experimental TSR of 4.27 (r.TemporalAA.Algorithm 1)
	DLSS = 2,
	XeSS = 3,
	FSR1 = 4,
	FSR2 = 5,
	FSR3 = 6,
};


/* Values are written directly by the engine console through the registered references */
struct Settings
{
	int32_t method = 0;            // r.Upscaler.Method
	int32_t overlay = 0;           // r.Upscaler.Overlay
	int32_t jitterMode = 0;        // r.Upscaler.JitterMode
	int32_t velocityDecode = 0;    // r.Upscaler.VelocityDecode
	int32_t resetRequest = 0;      // r.Upscaler.Reset
	float sharpness = 0.0f;        // r.Upscaler.Sharpness
	int32_t targetFps = 0;         // r.Upscaler.TargetFPS
	int32_t minResolution = 50;    // r.Upscaler.MinResolution
	int32_t dlssPreset = 0;        // r.DLSS.Preset

	UpscalerMethod Method() const { return static_cast<UpscalerMethod>(method); }
};


class Console
{
private:
	static constexpr uintptr_t RVA_CONSOLE_SINGLETON = 0xC010170;     // IConsoleManager::Singleton
	static constexpr uintptr_t RVA_CONSOLE_VTABLE = 0x9CB5800;        // FConsoleManager
	static constexpr uintptr_t RVA_CVAR_FLOAT_REF_VTABLE = 0x9CB60E8; // FConsoleVariableRef<float>
	static constexpr uintptr_t RVA_CVAR_INT_REF_VTABLE = 0x9CB6030;   // FConsoleVariableRef<int32>

	/* MSVC places overloaded virtual functions in reverse declaration order */
	static constexpr int SLOT_REGISTER_FLOAT_REF = 6;
	static constexpr int SLOT_REGISTER_INT_REF = 7;
	static constexpr int SLOT_FIND_CONSOLE_VARIABLE = 22;
	static constexpr int SLOT_CVAR_SET = 16;
	static constexpr int SLOT_CVAR_GET_INT = 18;
	static constexpr uint32_t ECVF_SET_BY_CONSOLE = 0x09000000;

	static inline uintptr_t base = 0;
	static inline volatile LONG ready = 0;
	static inline volatile LONG resetPending = 1;

	static void* Manager();
	static bool IsInsideExecutable(uintptr_t address);


public:
	static inline Settings settings = {};

	/* Needs the engine console, which exists once the first frame is rendered */
	static void Register(uintptr_t executableBase);
	static bool IsReady() { return ready != 0; }

	/* Replaces invalid values with defaults and logs changes. Called once per frame on the render thread */
	static void Validate();

	/* True once after r.Upscaler.Reset, a method change or a jitter mode change */
	static bool ConsumeReset() { return InterlockedExchange(&resetPending, 0) != 0; }

	static void* FindEngineVariable(const wchar_t* name);
	static int GetEngineInt(void* variable, int fallback);

	/* Game thread only */
	static void SetEngineValue(void* variable, const wchar_t* value);
};
