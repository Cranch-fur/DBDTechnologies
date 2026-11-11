#pragma once
#include "Utilities.h"






/*
	AMD FidelityFX FSR 2.2.1 (MIT) на Direct3D 11.

	У AMD есть только бэкенды DX12 и Vulkan. Здесь — свой бэкенд D3D11: сама логика FSR 2 (ffx_fsr2.cpp из
	SDKs\FSR2) не меняется, а её "задания" (очистка, копирование, compute-проходы) выполняются на контексте
	D3D11 игры. Шейдеры — официальные, собираются Visual Studio под SM 5.0 (Shaders\Fsr2*.hlsl).

	Работает на любой видеокарте с DX11 и "typed UAV loads" для R16F/RG32F/R8 (GTX 900+/Radeon GCN+/Intel Gen9+).
	Входы те же, что у DLSS: цвет сцены (HDR), наши векторы движения (пиксели входа, к прошлому кадру),
	глубина (обратная, бесконечная — как в UE 4.27), jitter в пикселях.
*/
class FSR2
{
public:
	struct Inputs
	{
		ID3D11Resource* color = nullptr;           // буфер сцены (может быть больше входа — ViewRect в углу)
		ID3D11Resource* motionVectors = nullptr;   // R16G16_FLOAT, пиксели входа
		ID3D11Resource* depth = nullptr;           // R32_FLOAT
		ID3D11Resource* output = nullptr;          // выход TAA (полное разрешение, UAV)
		int renderWidth = 0, renderHeight = 0;     // вход этого кадра
		int maxRenderWidth = 0, maxRenderHeight = 0; // максимум входа (= вход, или буфер при динамическом разрешении)
		int outputWidth = 0, outputHeight = 0;
		float jitterX = 0.0f, jitterY = 0.0f;      // пиксели
		float cameraNear = 10.0f;                  // единицы UE (см)
		float fovY = 1.0f;                         // радианы
		float sharpness = 0.0f;                    // 0 = без резкости, 0..1
		float frameTimeMs = 16.6f;
		bool reset = false;
		bool dynamicResolution = false;
	};

	/* Выполнить FSR 2 на контексте игры. false = ошибка (причина — ErrorReason) */
	static bool Execute(ID3D11DeviceContext* context, const Inputs& inputs);

	static const char* ErrorReason() { return errorReason; }


private:
	static inline const char* errorReason = "";
	static inline bool unsupported = false;          // видеокарта не подходит — не пытаться снова
	static inline void* fsrContext = nullptr;        // FfxFsr2Context (64 КБ, в куче)
	static inline int contextMaxWidth = 0, contextMaxHeight = 0, contextOutputWidth = 0, contextOutputHeight = 0;
	static inline bool contextDynamic = false;
	static inline int failedKey[5] = { -1, -1, -1, -1, -1 };

	static bool CheckDevice(ID3D11Device* device);
	static bool EnsureContext(ID3D11Device* device, const Inputs& in);
	static void DestroyContext();
};
