#include "Reflex.h"
#include "nvapi.h"

#pragma comment(lib, "nvapi64.lib")






static const uint8_t SIGNATURE_UPDATE_TIME[] = { 0x48,0x89,0x4C,0x24,0x08,0x55,0x57,0x48,0x8D,0x6C,0x24,0xE8,0x48,0x81,0xEC,0x18,0x01,0x00,0x00 };


bool Reflex::InstallHooks(uintptr_t base)
{
	using Utilities::Memory;

	if (Memory::CheckBytes(base + RVA_UPDATE_TIME, SIGNATURE_UPDATE_TIME, sizeof(SIGNATURE_UPDATE_TIME)) == false)
	{
		Utilities::Log::Write("Reflex: UpdateTimeAndHandleMaxTickRate does not match - Reflex disabled");
		return false;
	}
	for (uintptr_t slot : RVA_UPDATE_TIME_SLOTS)
	{
		if (Memory::Read<uintptr_t>(reinterpret_cast<void*>(base + slot)) != base + RVA_UPDATE_TIME)
		{
			Utilities::Log::Write("Reflex: vtable slot 0x%llX does not match - Reflex disabled", static_cast<unsigned long long>(slot));
			return false;
		}
	}

	/* Функция одна и та же в трёх vtable (UEngine, UGameEngine и наследник игры) — перехватываем все */
	for (uintptr_t slot : RVA_UPDATE_TIME_SLOTS)
	{
		void* previous = nullptr;
		if (Memory::PatchPointer(base + slot, reinterpret_cast<void*>(&HookUpdateTime), &previous) == false)
		{
			Utilities::Log::Write("Reflex: failed to patch vtable");
			return false;
		}
	}
	originalUpdateTime = reinterpret_cast<UpdateTime_t>(base + RVA_UPDATE_TIME);
	return true;
}

void Reflex::SetDevice(ID3D11Device* newDevice)
{
	if (device == nullptr && newDevice)
	{
		newDevice->AddRef();   /* держим ссылку до конца работы игры */
		device = newDevice;
	}
}

void Reflex::ValidateMode()
{
	if (mode < Off || mode > Boost)
	{
		Utilities::Log::Write("r.LowLatency.Mode %d is not supported (0 = off, 1 = on, 2 = on + boost) -> 0", mode);
		mode = Off;
	}
}

const char* Reflex::StatusText()
{
	if (mode == Off)
		return "";
	if (failed)
		return "REFLEX N/A";
	if (appliedMode != mode)
		return "REFLEX ...";
	return mode == Boost ? "REFLEX BOOST" : "REFLEX ON";
}

bool Reflex::EnsureNvapi()
{
	if (nvapiTried)
		return nvapiReady;
	nvapiTried = true;

	NvAPI_Status status = NvAPI_Initialize();
	if (status != NVAPI_OK)
	{
		Utilities::Log::Write("Reflex: NvAPI_Initialize failed (%d) - not an NVIDIA GPU or driver problem", static_cast<int>(status));
		failed = true;
		return false;
	}

	nvapiReady = true;
	return true;
}

void Reflex::ApplyMode()
{
	ID3D11Device* d3dDevice = device;
	if (d3dDevice == nullptr || EnsureNvapi() == false)
		return;

	NV_SET_SLEEP_MODE_PARAMS params = {};
	params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
	params.bLowLatencyMode = mode != Off;
	params.bLowLatencyBoost = mode == Boost;
	params.minimumIntervalUs = 0;          /* ограничение FPS оставляем игре */
	params.bUseMarkersToOptimize = false;  /* маркеры кадра пока не отправляем */

	NvAPI_Status status = NvAPI_D3D_SetSleepMode(d3dDevice, &params);
	if (status != NVAPI_OK)
	{
		Utilities::Log::Write("Reflex: NvAPI_D3D_SetSleepMode failed (%d) - GPU/driver without Reflex support?", static_cast<int>(status));
		failed = true;
		appliedMode = mode;
		return;
	}

	failed = false;
	appliedMode = mode;
	Utilities::Log::Write("Reflex: %s", mode == Off ? "off" : mode == Boost ? "on + boost" : "on");
}

void __fastcall Reflex::HookUpdateTime(void* engine)
{
	/* Игровой поток, начало кадра: до обработки ввода и симуляции */
	if (gameThreadCallback)
		gameThreadCallback();

	if (device)
	{
		bool neverEnabled = appliedMode < 0 && mode == Off;   /* пока Reflex не включали, NVAPI не трогаем */
		if (mode != appliedMode && neverEnabled == false && (failed == false || mode == Off))
			ApplyMode();

		if (mode != Off && failed == false && appliedMode == mode)
			NvAPI_D3D_Sleep(device);
	}

	originalUpdateTime(engine);
}
