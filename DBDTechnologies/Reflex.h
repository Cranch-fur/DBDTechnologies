#pragma once
#include "Utilities.h"






/*
	NVIDIA Reflex Low Latency (NVAPI, D3D11).

	Sleep вызывается в начале игрового кадра — в UEngine::UpdateTimeAndHandleMaxTickRate
	(перехват через vtable). Это то же место, где его вызывает официальный плагин Reflex
	для UE 4.27 (через IMaxTickRateHandlerModule). Ограничение FPS игры при этом не трогаем.

	r.LowLatency.Mode: 0 = выкл, 1 = вкл, 2 = вкл + Boost. Клавиша F5 — по кругу.
	Команда общая: позже сюда же подключится AMD Anti-Lag 2 (выбор по производителю видеокарты).
*/
class Reflex
{
public:
	enum Mode : int32_t
	{
		Off = 0,
		On = 1,
		Boost = 2,
	};

	static inline int32_t mode = Off;   // сюда пишет консоль движка (r.LowLatency.Mode)

	/* Вызывается в игровом потоке в начале каждого кадра (для дел, которые нельзя делать из потока рендера) */
	using GameThreadCallback = void (*)();
	static inline GameThreadCallback gameThreadCallback = nullptr;

	static bool InstallHooks(uintptr_t base);

	/* Устройство D3D11 игры: передаётся из перехвата конца кадра (поток RHI) */
	static void SetDevice(ID3D11Device* device);

	/* Проверка значения после клавиш/консоли */
	static void ValidateMode();

	/* Короткий текст для надписи: "" / "REFLEX ON" / "REFLEX BOOST" / "REFLEX N/A" */
	static const char* StatusText();


private:
	static constexpr uintptr_t RVA_UPDATE_TIME = 0x75694D0;        // UEngine::UpdateTimeAndHandleMaxTickRate
	static constexpr uintptr_t RVA_UPDATE_TIME_SLOTS[3] = { 0x9740690, 0xA2864A0, 0xA28A0F0 };

	using UpdateTime_t = void(__fastcall*)(void* engine);
	static inline UpdateTime_t originalUpdateTime = nullptr;

	static inline ID3D11Device* volatile device = nullptr;
	static inline bool nvapiTried = false;
	static inline bool nvapiReady = false;
	static inline int32_t appliedMode = -1;
	static inline bool failed = false;

	static void __fastcall HookUpdateTime(void* engine);
	static bool EnsureNvapi();
	static void ApplyMode();
};
