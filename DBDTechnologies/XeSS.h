#pragma once
#include "Utilities.h"

struct _xess_context_handle_t;






/*
	Intel XeSS (D3D11, SDK 2.x, MIT). По документации Intel версия D3D11 работает ТОЛЬКО на видеокартах Intel
	(Arc / Xe). На NVIDIA и AMD мод покажет "XESS DX11: INTEL GPU ONLY" и оставит обычный TAA.

	libxess_dx11.dll загружается динамически (LoadLibrary): если файла нет, мод и игра работают как раньше.
	Где искать: рядом с DBDTechnologies.asi, затем рядом с exe.

	Входы те же, что у DLSS: цвет сцены (HDR), наши векторы движения (в пикселях входного разрешения,
	к прошлому кадру), глубина (обратная, как в UE), jitter в пикселях. Качество (Quality / Balanced / ...)
	выбирается автоматически по r.ScreenPercentage, как у DLSS.
*/
class XeSS
{
public:
	struct Inputs
	{
		ID3D11Resource* color = nullptr;
		ID3D11Resource* motionVectors = nullptr;
		ID3D11Resource* depth = nullptr;
		ID3D11Resource* output = nullptr;
		int renderWidth = 0, renderHeight = 0;
		int outputWidth = 0, outputHeight = 0;
		float jitterX = 0.0f, jitterY = 0.0f;
		bool reset = false;
	};

	/* Одноразовая проверка: видеокарта Intel, библиотека найдена, контекст создан. false = XeSS недоступен до перезапуска */
	static bool Initialize(ID3D11Device* device);

	/* Выполнить XeSS (на immediate context устройства — того же, что у игры) */
	static bool Execute(const Inputs& inputs);

	static const char* ErrorReason() { return errorReason; }
	static const char* VersionText() { return versionText; }
	static const char* QualityText() { return qualityText; }


private:
	using Context = _xess_context_handle_t*;

	static inline bool initTried = false;
	static inline bool ready = false;
	static inline HMODULE library = nullptr;
	static inline Context context = nullptr;
	static inline ID3D11Device* device = nullptr;

	static inline int initOutputWidth = 0, initOutputHeight = 0, initQuality = -1;
	static inline int initMinWidth = 0, initMinHeight = 0, initMaxWidth = 0, initMaxHeight = 0;   // диапазон входа текущего качества
	static inline int failedKey[3] = { -1, -1, -1 };

	static inline const char* errorReason = "";
	static inline char versionText[24] = "";
	static inline const char* qualityText = "";

	/* функции libxess_dx11.dll */
	static inline void* pGetVersion = nullptr;
	static inline void* pCreateContext = nullptr;
	static inline void* pInit = nullptr;
	static inline void* pExecute = nullptr;
	static inline void* pDestroyContext = nullptr;
	static inline void* pGetOptimalInputResolution = nullptr;
	static inline void* pSetVelocityScale = nullptr;
	static inline void* pSetJitterScale = nullptr;
	static inline void* pSetLoggingCallback = nullptr;

	static bool IsIntelGpu(ID3D11Device* device, unsigned& vendorId);
	static bool LoadLibraryAndFunctions();
	static int ChooseQuality(int renderWidth, int renderHeight, int outputWidth, int outputHeight, int range[4]);
	static bool EnsureInitialized(int renderWidth, int renderHeight, int outputWidth, int outputHeight);
	static const char* ResultName(int result);
};
