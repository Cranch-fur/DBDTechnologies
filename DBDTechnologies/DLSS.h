#pragma once
#include "Utilities.h"
#include "Overlay.h"
#include "Reflex.h"
#include "FSR.h"
#include "XeSS.h"
#include "FSR2.h"

struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;






/*
	Апскейлеры вместо TAA/TAAU для Dead by Daylight 6.7.2 (UE 4.27.2, D3D11).

	1) FDefaultTemporalUpscaler::AddPasses (vtable): после того как движок построил проход TAA,
	   запоминаем выходную текстуру, входное разрешение, матрицы камеры и jitter.
	2) FD3D11DynamicRHI::RHIDispatchComputeShader (vtable): когда на GPU запускается этот TAA,
	   при r.Upscaler.Method 1 вместо него работает DLSS (NGX D3D11), при 3 — Intel XeSS (только видеокарты Intel),
	   при 4 — AMD FSR 2.2 (любая видеокарта, свой бэкенд D3D11).
	   При ошибке — обычный TAA.
	3) FD3D11DynamicRHI::RHIDrawPrimitive / RHIDrawIndexedPrimitive (vtable): при r.Upscaler.Method 2
	   пространственное растягивание кадра движком (FUpscalePS) заменяется на FSR 1 (EASU + RCAS).
	4) FD3D11DynamicRHI::RHIEndDrawingViewport (vtable): перед выводом кадра рисуем надпись.
	5) Консольные переменные r.Upscaler.*, r.DLSS.*, r.LowLatency.* регистрируются в консоли самого
	   движка (работают и из Engine.ini, раздел [SystemSettings]). Горячих клавиш нет.
	6) Снижение задержки (NVIDIA Reflex) — см. Reflex.h.
*/
class DLSS
{
public:
	static void Init();


private:
	/* ------------------------------------------------ адреса (DeadByDaylight-Win64-Shipping.exe 6.7.2) */
	static constexpr uintptr_t RVA_ADDPASSES = 0x5C32B40;           // FDefaultTemporalUpscaler::AddPasses
	static constexpr uintptr_t RVA_UPSCALER_VTABLE = 0x9DF3F30;     // его vtable (AddPasses = слот 2)
	static constexpr uintptr_t RVA_GETDEBUGNAME = 0x5C3D240;        // слот 1
	static constexpr uintptr_t RVA_DISPATCH = 0x584F770;            // FD3D11DynamicRHI::RHIDispatchComputeShader
	static constexpr uintptr_t RVA_DISPATCH_SLOT = 0x9D6C1A8;
	static constexpr uintptr_t RVA_DRAW = 0x584FE40;                // FD3D11DynamicRHI::RHIDrawPrimitive
	static constexpr uintptr_t RVA_DRAW_SLOT = 0x9D6C3C0;
	static constexpr uintptr_t RVA_DRAW_INDEXED = 0x584FB00;        // FD3D11DynamicRHI::RHIDrawIndexedPrimitive
	static constexpr uintptr_t RVA_DRAW_INDEXED_SLOT = 0x9D6C3D8;
	static constexpr uintptr_t RVA_END_DRAWING = 0x5850080;         // FD3D11DynamicRHI::RHIEndDrawingViewport
	static constexpr uintptr_t RVA_END_DRAWING_SLOT = 0x9D6C310;
	static constexpr uintptr_t RVA_VIEWPORT_VTABLE = 0x9D6B6B0;     // FD3D11Viewport (сохраняется конструктором)
	static constexpr uintptr_t RVA_CONSOLE_SINGLETON = 0xC010170;   // IConsoleManager::Singleton
	static constexpr uintptr_t RVA_CONSOLE_VTABLE = 0x9CB5800;      // FConsoleManager
	static constexpr int SLOT_REGISTER_FLOAT_REF = 6;               // RegisterConsoleVariableRef(float&)
	static constexpr int SLOT_REGISTER_INT_REF = 7;                 // RegisterConsoleVariableRef(int32&)
	static constexpr int SLOT_FIND_CONSOLE_VARIABLE = 22;           // FindConsoleVariable(name, bTrackFrequentCalls)
	static constexpr int SLOT_CVAR_SET = 16;                        // IConsoleVariable::Set(const TCHAR*, SetBy)
	static constexpr int SLOT_CVAR_GET_INT = 18;                    // IConsoleVariable::GetInt
	static constexpr uint32_t ECVF_SET_BY_CONSOLE = 0x09000000;
	static constexpr uintptr_t RVA_CVAR_FLOAT_REF_VTABLE = 0x9CB60E8; // FConsoleVariableRef<float>
	static constexpr uintptr_t RVA_CVAR_INT_REF_VTABLE = 0x9CB6030;   // FConsoleVariableRef<int32>

	/* ------------------------------------------------ смещения в структурах движка */
	static constexpr uintptr_t OFF_CONTEXT_D3D11 = 0x178;    // IRHICommandContext -> ID3D11DeviceContext*
	static constexpr uintptr_t OFF_INPUT_COLOR = 0x08;       // FPassInputs
	static constexpr uintptr_t OFF_INPUT_VELOCITY = 0x18;
	static constexpr uintptr_t OFF_RDG_RHI = 0x10;           // FRDGTexture -> FRHITexture*
	static constexpr uintptr_t OFF_RDG_EXTENT = 0x48;        // FRDGTexture -> int W, H
	static constexpr uintptr_t OFF_RHI_NATIVE = 0x98;        // FD3D11Texture2D -> ID3D11Resource*
	static constexpr uintptr_t OFF_VIEW_RECT = 0x1690;       // FViewInfo -> FIntRect ViewRect
	static constexpr uintptr_t OFF_VIEW_MATRICES = 0x280;    // FViewInfo -> FViewMatrices
	static constexpr uintptr_t OFF_MATRIX_PROJ = 0x000;      // ProjectionMatrix (с jitter)
	static constexpr uintptr_t OFF_MATRIX_PROJ_NOAA = 0x040; // ProjectionNoAAMatrix
	static constexpr uintptr_t OFF_MATRIX_VIEW = 0x0C0;      // ViewMatrix
	static constexpr uintptr_t OFF_VIEWPORT_BACKBUFFER = 0x78; // FD3D11Viewport -> FD3D11Texture2D* BackBuffer

	/* ------------------------------------------------ настройки (сюда пишет консоль движка) */
	enum Method : int32_t
	{
		MethodTAA = 0,
		MethodDLSS = 1,
		MethodFSR1 = 2,
		MethodXeSS = 3,
		MethodFSR2 = 4,
	};

	struct Settings
	{
		int32_t method = MethodTAA;    // r.Upscaler.Method
		int32_t overlay = 0;           // r.Upscaler.Overlay
		int32_t jitterMode = 0;        // r.Upscaler.JitterMode
		int32_t velocityDecode = 0;    // r.Upscaler.VelocityDecode
		int32_t resetRequest = 0;      // r.Upscaler.Reset
		float sharpness = 0.0f;        // r.Upscaler.Sharpness
		int32_t preset = 0;            // r.DLSS.Preset
		int32_t targetFps = 0;         // r.Upscaler.TargetFPS (0 = динамическое разрешение мода выключено)
		int32_t minResolution = 50;    // r.Upscaler.MinResolution, % (нижняя граница динамического разрешения)
	};
	static inline Settings settings = {};

	struct PresetInfo
	{
		int32_t value;
		const char* name;
	};
	/* nvsdk_ngx_defs.h (SDK 310.9): A–D удалены, G/H/I/N/O "не использовать" */
	static inline const PresetInfo presets[7] =
	{
		{ 0,  "DEFAULT" },
		{ 10, "J" },
		{ 11, "K" },
		{ 12, "L" },
		{ 13, "M" },
		{ 5,  "E" },
		{ 6,  "F" },
	};

	/* ------------------------------------------------ данные кадра */
	struct IntRect
	{
		int32_t minX, minY, maxX, maxY;
	};

	struct FrameData
	{
		bool valid = false;
		void* outputTexture = nullptr;     // FRDGTexture*
		void* colorTexture = nullptr;
		void* velocityTexture = nullptr;
		int renderWidth = 0, renderHeight = 0;
		int outputWidth = 0, outputHeight = 0;
		int colorWidth = 0, colorHeight = 0;   // размер буфера цвета (ViewRect лежит внутри)
		float jitterX = 0.0f, jitterY = 0.0f;
		float clipToPrevClip[16] = {};
		float cameraNear = 10.0f;     // ближняя плоскость (см), для FSR 2
		float fovY = 1.0f;            // вертикальный угол обзора (рад), для FSR 2
		bool reset = false;
	};

	static inline FrameData frame = {};
	static inline CRITICAL_SECTION frameLock = {};
	static inline volatile LONG frameNumber = 0;
	static inline volatile LONG forceReset = 1;

	static inline Utilities::Matrix4 previousViewProjection = {};
	static inline bool havePrevious = false;
	static inline int lastRenderWidth = 0, lastRenderHeight = 0, lastOutputWidth = 0, lastOutputHeight = 0;
	static inline int lastColorWidth = 0, lastColorHeight = 0;

	/* Динамическое разрешение движка (r.DynamicRes.OperationMode != 0): вход меняется каждый кадр внутри буфера */
	static inline volatile LONG dynamicRes = 0;
	struct EngineVariables
	{
		void* operationMode = nullptr;      // r.DynamicRes.OperationMode
		void* frameTimeBudget = nullptr;    // r.DynamicRes.FrameTimeBudget
		void* minScreenPercentage = nullptr;
		void* maxScreenPercentage = nullptr;
	};
	static inline EngineVariables engine = {};
	static inline volatile LONG consoleReady = 0;

	/* FSR 1: ожидаем пространственное растягивание в этом кадре (поток RHI) */
	struct UpscaleWait
	{
		bool armed = false;
		int renderWidth = 0, renderHeight = 0;
		int colorWidth = 0, colorHeight = 0;
	};
	static inline UpscaleWait upscaleWait = {};
	static inline UINT backBufferWidth = 0, backBufferHeight = 0;

	/* r.HDR.EnableHDROutput (переменная движка): нужна, чтобы отличить HDR10 от обычного 10-битного SDR */
	static inline void* volatile hdrOutputCvar = nullptr;

	/* ------------------------------------------------ D3D11 / NGX */
	static inline ID3D11Device* device = nullptr;
	static inline ID3D11ComputeShader* motionShader = nullptr;
	static inline ID3D11Buffer* motionConstants = nullptr;
	static inline ID3D11Texture2D* motionTexture = nullptr;
	static inline ID3D11UnorderedAccessView* motionUav = nullptr;
	static inline ID3D11Texture2D* depthTexture = nullptr;
	static inline ID3D11UnorderedAccessView* depthUav = nullptr;
	static inline int textureWidth = 0, textureHeight = 0;

	static inline bool ngxReady = false;
	static inline bool dlssFailed = false;
	static inline NVSDK_NGX_Parameter* ngxParameters = nullptr;
	static inline NVSDK_NGX_Handle* dlssHandle = nullptr;
	static inline int dlssRenderWidth = 0, dlssRenderHeight = 0, dlssOutputWidth = 0, dlssOutputHeight = 0, dlssPreset = -1;
	static inline int failedKey[5] = { -1, -1, -1, -1, -1 };
	static inline const char* errorReason = "";       // коротко, для надписи
	static inline char dllVersion[32] = "";           // версия загруженного DLSS

	static inline Utilities::ComputeState savedState;

	/* ------------------------------------------------ оригиналы перехваченных функций */
	using AddPasses_t = void(__fastcall*)(const void* self, void* graphBuilder, const void* view, const void* passInputs,
	                                      void** outTexture, IntRect* outRect, void** outHalfTexture, IntRect* outHalfRect);
	using Dispatch_t = void(__fastcall*)(void* context, UINT x, UINT y, UINT z);
	using Draw_t = void(__fastcall*)(void* context, UINT baseVertex, UINT numPrimitives, UINT numInstances);
	using DrawIndexed_t = void(__fastcall*)(void* context, void* indexBuffer, INT baseVertex, UINT firstInstance, UINT numVertices,
	                                        UINT startIndex, UINT numPrimitives, UINT numInstances);
	using EndDrawing_t = void(__fastcall*)(void* context, void* viewport, bool present, bool lockToVsync);

	static inline uintptr_t base = 0;
	static inline AddPasses_t originalAddPasses = nullptr;
	static inline Dispatch_t originalDispatch = nullptr;
	static inline Draw_t originalDraw = nullptr;
	static inline DrawIndexed_t originalDrawIndexed = nullptr;
	static inline EndDrawing_t originalEndDrawing = nullptr;

	/* ------------------------------------------------ функции */
	static void __fastcall HookAddPasses(const void* self, void* graphBuilder, const void* view, const void* passInputs,
	                                     void** outTexture, IntRect* outRect, void** outHalfTexture, IntRect* outHalfRect);
	static void __fastcall HookDispatch(void* context, UINT x, UINT y, UINT z);
	static void __fastcall HookDraw(void* context, UINT baseVertex, UINT numPrimitives, UINT numInstances);
	static void __fastcall HookDrawIndexed(void* context, void* indexBuffer, INT baseVertex, UINT firstInstance, UINT numVertices,
	                                       UINT startIndex, UINT numPrimitives, UINT numInstances);
	static void __fastcall HookEndDrawing(void* context, void* viewport, bool present, bool lockToVsync);

	static bool InstallHooks();
	static void RegisterConsoleVariables();
	static void ApplySettingChanges();
	static void* FindEngineVariable(const wchar_t* name);
	static int GetEngineInt(void* cvar, int fallback);
	static void SetEngineValue(void* cvar, const wchar_t* value);
	static void GameThreadTick();
	static const char* DynamicText();
	static Overlay::HdrMode DetectHdrMode(DXGI_FORMAT backBufferFormat);

	static bool IsValidPreset(int32_t value);
	static const char* PresetName(int32_t value);
	static const char* QualityName(int renderWidth, int outputWidth);
	static ID3D11Resource* NativeOf(void* rdgTexture);
	static void DescribeLoadedDll();

	static bool InitializeShader();
	static bool InitializeNgx();
	static bool EnsureTextures(int width, int height);
	static bool EnsureFeature(ID3D11DeviceContext* context, const FrameData& f);
	/* DLSS или XeSS вместо TAA: общие входы (векторы движения, глубина), затем сам апскейлер и резкость */
	static bool RunTemporal(ID3D11DeviceContext* context, ID3D11Resource* output, const FrameData& f, LONG number, int32_t method);
	static bool TryFsr1(void* context);

	static void ReportStatus(Overlay::State state, const char* format, ...);
	static const char* SharpnessText();
};
