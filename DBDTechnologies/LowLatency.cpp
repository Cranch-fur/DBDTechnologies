#include "LowLatency.h"
#include <dxgi.h>
#include "nvapi.h"
#include "ffx_antilag2_dx11.h"

#pragma comment(lib, "nvapi64.lib")
#pragma comment(lib, "dxgi.lib")






static const uint8_t SIGNATURE_UPDATE_TIME[] = { 0x48,0x89,0x4C,0x24,0x08,0x55,0x57,0x48,0x8D,0x6C,0x24,0xE8,0x48,0x81,0xEC,0x18,0x01,0x00,0x00 };
static const uint8_t SIGNATURE_GAME_TICK[] = { 0x48,0x8B,0xC4,0x44,0x88,0x40,0x18,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55 };
/* Prologue of both ExecuteTask functions and "lea rcx, [rsi+10h]" that passes the lambda */
static const uint8_t SIGNATURE_TASK_PROLOGUE[] = { 0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF1 };
static const uint8_t SIGNATURE_TASK_LAMBDA[] = { 0x48,0x8D,0x4E,0x10 };




bool LowLatency::InstallHooks(uintptr_t base)
{
	using Utilities::Memory;
	auto slotValue = [base](uintptr_t slot) { return Memory::Read<uintptr_t>(reinterpret_cast<void*>(base + slot)); };

	if (Memory::CheckBytes(base + RVA_UPDATE_TIME, SIGNATURE_UPDATE_TIME, sizeof(SIGNATURE_UPDATE_TIME)) == false)
	{
		Utilities::Log::Write("Low latency: UpdateTimeAndHandleMaxTickRate does not match - Reflex/Anti-Lag 2 disabled");
		return false;
	}
	for (uintptr_t slot : RVA_UPDATE_TIME_SLOTS)
	{
		if (slotValue(slot) != base + RVA_UPDATE_TIME)
		{
			Utilities::Log::Write("Low latency: vtable slot 0x%llX does not match - Reflex/Anti-Lag 2 disabled", static_cast<unsigned long long>(slot));
			return false;
		}
	}
	for (uintptr_t slot : RVA_UPDATE_TIME_SLOTS)
	{
		void* previous = nullptr;
		if (Memory::PatchPointer(base + slot, reinterpret_cast<void*>(&HookUpdateTime), &previous) == false)
		{
			Utilities::Log::Write("Low latency: failed to patch vtable");
			return false;
		}
	}
	originalUpdateTime = reinterpret_cast<UpdateTime_t>(base + RVA_UPDATE_TIME);

	/* Marker hooks are optional: without them Reflex still works with Sleep only */
	bool ok = true;

	int32_t displacement = Memory::Read<int32_t>(reinterpret_cast<void*>(base + RVA_FRAME_COUNTER_USE + 3));
	const uint8_t movRbx[] = { 0x48, 0x8B, 0x1D };
	if (Memory::CheckBytes(base + RVA_FRAME_COUNTER_USE, movRbx, sizeof(movRbx)) == false ||
	    base + RVA_FRAME_COUNTER_USE + 7 + displacement != base + RVA_FRAME_COUNTER)
	{
		Utilities::Log::Write("Low latency: GFrameCounter not found - Reflex markers disabled");
		ok = false;
	}

	if (ok && (Memory::CheckBytes(base + RVA_GAME_TICK, SIGNATURE_GAME_TICK, sizeof(SIGNATURE_GAME_TICK)) == false ||
	           slotValue(RVA_GAME_TICK_SLOTS[0]) != base + RVA_GAME_TICK ||
	           (slotValue(RVA_GAME_TICK_SLOTS[1]) != base + RVA_GAME_TICK && slotValue(RVA_GAME_TICK_SLOTS[1]) != base + RVA_GAME_TICK_THUNK)))
	{
		Utilities::Log::Write("Low latency: UGameEngine::Tick does not match - Reflex markers disabled");
		ok = false;
	}

	const uintptr_t tasks[2][2] = { { RVA_BEGIN_FRAME_TASK, RVA_BEGIN_FRAME_SLOT }, { RVA_END_FRAME_TASK, RVA_END_FRAME_SLOT } };
	for (const auto& task : tasks)
	{
		if (ok && (Memory::CheckBytes(base + task[0], SIGNATURE_TASK_PROLOGUE, sizeof(SIGNATURE_TASK_PROLOGUE)) == false ||
		           Memory::CheckBytes(base + task[0] + 0x1D, SIGNATURE_TASK_LAMBDA, sizeof(SIGNATURE_TASK_LAMBDA)) == false ||
		           slotValue(task[1]) != base + task[0]))
		{
			Utilities::Log::Write("Low latency: render frame task 0x%llX does not match - Reflex markers disabled",
			                      static_cast<unsigned long long>(task[0]));
			ok = false;
		}
	}

	if (ok)
	{
		void* previous = nullptr;
		ok = Memory::PatchPointer(base + RVA_GAME_TICK_SLOTS[0], reinterpret_cast<void*>(&HookGameTick), &previous) &&
		     Memory::PatchPointer(base + RVA_GAME_TICK_SLOTS[1], reinterpret_cast<void*>(&HookGameTick), &previous) &&
		     Memory::PatchPointer(base + RVA_BEGIN_FRAME_SLOT, reinterpret_cast<void*>(&HookBeginFrame), &previous) &&
		     Memory::PatchPointer(base + RVA_END_FRAME_SLOT, reinterpret_cast<void*>(&HookEndFrame), &previous);
		originalGameTick = reinterpret_cast<GameTick_t>(base + RVA_GAME_TICK);
		originalBeginFrame = reinterpret_cast<ExecuteTask_t>(base + RVA_BEGIN_FRAME_TASK);
		originalEndFrame = reinterpret_cast<ExecuteTask_t>(base + RVA_END_FRAME_TASK);
		frameCounter = reinterpret_cast<volatile uint64_t*>(base + RVA_FRAME_COUNTER);
		if (ok == false)
			Utilities::Log::Write("Low latency: failed to patch marker hooks");
	}
	markerHooks = ok;
	Utilities::Log::Write("Low latency: hooks installed (Reflex markers %s)", markerHooks ? "available" : "unavailable");
	return true;
}




void LowLatency::SetDevice(ID3D11Device* newDevice)
{
	if (device == nullptr && newDevice)
	{
		newDevice->AddRef();
		device = newDevice;
		DetectBackend();
	}
}

void LowLatency::DetectBackend()
{
	unsigned vendor = 0;
	IDXGIDevice* dxgiDevice = nullptr;
	IDXGIAdapter* adapter = nullptr;
	if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
	    SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
	{
		DXGI_ADAPTER_DESC desc = {};
		if (SUCCEEDED(adapter->GetDesc(&desc)))
			vendor = desc.VendorId;
	}
	Utilities::SafeRelease(adapter);
	Utilities::SafeRelease(dxgiDevice);

	switch (vendor)
	{
	case 0x10DE: backend = Backend::Reflex; break;
	case 0x1002: backend = Backend::AntiLag2; break;
	default: backend = Backend::None; break;
	}
	Utilities::Log::Write("Low latency: GPU vendor 0x%04X -> %s", vendor,
	                      backend == Backend::Reflex ? "NVIDIA Reflex" : backend == Backend::AntiLag2 ? "AMD Anti-Lag 2" :
	                      "not supported (Intel XeLL exists only for DX12)");
}

void LowLatency::ValidateMode()
{
	if (mode < Off || mode > Boost)
	{
		Utilities::Log::Write("r.LowLatency.Mode %d is not supported (0 = off, 1 = on, 2 = on + boost) -> 0", mode);
		mode = Off;
	}
	if (markers != 0 && markers != 1)
	{
		Utilities::Log::Write("r.LowLatency.Markers %d is not supported (0 or 1) -> 1", markers);
		markers = 1;
	}
}

const char* LowLatency::StatusText()
{
	if (mode == Off)
		return "";
	switch (backend)
	{
	case Backend::Unknown:
		return "LOW LATENCY ...";
	case Backend::None:
		return "LOW LATENCY N/A";
	case Backend::AntiLag2:
		return failed ? "ANTI-LAG 2 N/A" : antiLagContext ? "ANTI-LAG 2 ON" : "ANTI-LAG 2 ...";
	case Backend::Reflex:
	default:
		if (failed)
			return "REFLEX N/A";
		if (appliedMode != mode)
			return "REFLEX ...";
		return mode == Boost ? "REFLEX BOOST" : "REFLEX ON";
	}
}




bool LowLatency::EnsureNvapi()
{
	if (nvapiTried)
		return nvapiReady;
	nvapiTried = true;

	NvAPI_Status status = NvAPI_Initialize();
	if (status != NVAPI_OK)
	{
		Utilities::Log::Write("Reflex: NvAPI_Initialize failed (%d) - driver problem?", static_cast<int>(status));
		failed = true;
		return false;
	}
	nvapiReady = true;
	return true;
}

void LowLatency::ApplyReflexMode()
{
	ID3D11Device* d3dDevice = device;
	if (d3dDevice == nullptr || EnsureNvapi() == false)
		return;

	const bool useMarkers = markers == 1 && markerHooks && mode != Off;

	NV_SET_SLEEP_MODE_PARAMS params = {};
	params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
	params.bLowLatencyMode = mode != Off;
	params.bLowLatencyBoost = mode == Boost;
	params.minimumIntervalUs = 0;                 // Frame rate limiting stays with the game
	params.bUseMarkersToOptimize = useMarkers;

	NvAPI_Status status = NvAPI_D3D_SetSleepMode(d3dDevice, &params);
	appliedMode = mode;
	appliedMarkers = markers;
	if (status != NVAPI_OK)
	{
		Utilities::Log::Write("Reflex: NvAPI_D3D_SetSleepMode failed (%d) - GPU/driver without Reflex support?", static_cast<int>(status));
		failed = true;
		markersActive = false;
		return;
	}

	failed = false;
	markersActive = useMarkers;
	Utilities::Log::Write("Reflex: %s, markers %s", mode == Off ? "off" : mode == Boost ? "on + boost" : "on",
	                      useMarkers ? "on" : markers == 1 && markerHooks == false ? "unavailable" : "off");
}

uint64_t LowLatency::GameFrame()
{
	return frameCounter ? *frameCounter : 0;
}

void LowLatency::Marker(int type, uint64_t frame)
{
	ID3D11Device* d3dDevice = device;
	if (markersActive == false || d3dDevice == nullptr)
		return;

	NV_LATENCY_MARKER_PARAMS params = {};
	params.version = NV_LATENCY_MARKER_PARAMS_VER;
	params.frameID = frame;
	params.markerType = static_cast<NV_LATENCY_MARKER_TYPE>(type);
	NvAPI_D3D_SetLatencyMarker(d3dDevice, &params);
}

/* The RHI thread lags at most one frame behind the render thread, so Present belongs to the latest or the previous render frame */
void LowLatency::PresentStart()
{
	if (markersActive == false)
		return;
	LONG64 latest = latestRenderFrame;
	LONG64 frame = lastPresentFrame + 1;
	if (frame < latest - 1)
		frame = latest - 1;
	if (frame > latest)
		frame = latest;
	lastPresentFrame = frame;
	presentFrame = static_cast<uint64_t>(frame);
	Marker(PRESENT_START, presentFrame);
}

void LowLatency::PresentEnd()
{
	if (markersActive)
		Marker(PRESENT_END, presentFrame);
}




void LowLatency::UpdateAntiLag()
{
	using namespace AMD::AntiLag2DX11;

	if (antiLagTried == false)
	{
		if (mode == Off)
			return;   // Do not touch the driver until enabled once
		antiLagTried = true;

		Context* context = new Context();
		HRESULT result = Initialize(context);
		if (result != S_OK)
		{
			Utilities::Log::Write("Anti-Lag 2: not available (0x%08X) - needs Radeon RX 5000+ and AMD driver 24.6.1 or newer",
			                      static_cast<unsigned>(result));
			delete context;
			failed = true;
			return;
		}
		antiLagContext = context;
		Utilities::Log::Write("Anti-Lag 2: initialized");
		if (mode == Boost)
			Utilities::Log::Write("Anti-Lag 2: Boost is NVIDIA only - using normal mode");
	}

	if (antiLagContext)
	{
		/* Must run every frame before input is polled */
		static int32_t lastMode = -1;
		if (lastMode != mode)
		{
			lastMode = mode;
			Utilities::Log::Write("Anti-Lag 2: %s", mode == Off ? "off" : "on");
		}
		Update(static_cast<Context*>(antiLagContext), mode != Off, 0);
	}
}




void __fastcall LowLatency::HookUpdateTime(void* engine)
{
	if (gameThreadCallback)
		gameThreadCallback();

	if (device)
	{
		if (backend == Backend::Reflex)
		{
			bool neverEnabled = appliedMode < 0 && mode == Off;   // Do not load NVAPI until enabled once
			bool changed = mode != appliedMode || (mode != Off && markers != appliedMarkers);
			if (changed && neverEnabled == false && (failed == false || mode == Off))
				ApplyReflexMode();

			if (mode != Off && failed == false && appliedMode == mode)
			{
				NvAPI_D3D_Sleep(device);
				Marker(SIMULATION_START, GameFrame());
			}
		}
		else if (backend == Backend::AntiLag2)
		{
			UpdateAntiLag();
		}
	}

	originalUpdateTime(engine);
}

void __fastcall LowLatency::HookGameTick(void* engine, float deltaSeconds, bool idleMode)
{
	originalGameTick(engine, deltaSeconds, idleMode);
	Marker(SIMULATION_END, GameFrame());
}

void __fastcall LowLatency::HookBeginFrame(void* task, void* newTasks, int currentThread)
{
	/* The task is deleted by ExecuteTask, read the frame number first */
	uint64_t frame = Utilities::Memory::Read<uint64_t>(task, OFF_TASK_FRAME_COUNTER);
	InterlockedExchange64(&latestRenderFrame, static_cast<LONG64>(frame));
	Marker(RENDERSUBMIT_START, frame);
	originalBeginFrame(task, newTasks, currentThread);
}

void __fastcall LowLatency::HookEndFrame(void* task, void* newTasks, int currentThread)
{
	uint64_t frame = Utilities::Memory::Read<uint64_t>(task, OFF_TASK_FRAME_COUNTER);
	originalEndFrame(task, newTasks, currentThread);
	Marker(RENDERSUBMIT_END, frame);
}
