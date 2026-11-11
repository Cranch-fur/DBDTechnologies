#pragma once
#include "Utilities.h"
#include "Overlay.h"






/*
	Hooks (vtable slots of DeadByDaylight-Win64-Shipping.exe 6.7.2):
	  FDefaultTemporalUpscaler::AddPasses            render thread: frame size, camera matrices, jitter
	  FD3D11DynamicRHI::RHIDispatchComputeShader     RHI thread: the TAA dispatch is replaced by DLSS, XeSS, FSR 2 or FSR 3
	  FD3D11DynamicRHI::RHIDrawPrimitive (+Indexed)  RHI thread: the spatial upscale draw is replaced by FSR 1
	  FD3D11DynamicRHI::RHIEndDrawingViewport        RHI thread: overlay, Reflex present markers
*/
class Technologies
{
private:
	static constexpr uintptr_t RVA_ADDPASSES = 0x5C32B40;
	static constexpr uintptr_t RVA_UPSCALER_VTABLE = 0x9DF3F30;   // AddPasses is slot 2
	static constexpr uintptr_t RVA_GETDEBUGNAME = 0x5C3D240;      // Slot 1
	static constexpr uintptr_t RVA_DISPATCH = 0x584F770;
	static constexpr uintptr_t RVA_DISPATCH_SLOT = 0x9D6C1A8;
	static constexpr uintptr_t RVA_DRAW = 0x584FE40;
	static constexpr uintptr_t RVA_DRAW_SLOT = 0x9D6C3C0;
	static constexpr uintptr_t RVA_DRAW_INDEXED = 0x584FB00;
	static constexpr uintptr_t RVA_DRAW_INDEXED_SLOT = 0x9D6C3D8;
	static constexpr uintptr_t RVA_END_DRAWING = 0x5850080;
	static constexpr uintptr_t RVA_END_DRAWING_SLOT = 0x9D6C310;
	static constexpr uintptr_t RVA_VIEWPORT_VTABLE = 0x9D6B6B0;   // FD3D11Viewport

	static constexpr uintptr_t OFF_CONTEXT_D3D11 = 0x178;       // IRHICommandContext -> ID3D11DeviceContext*
	static constexpr uintptr_t OFF_INPUT_COLOR = 0x08;          // FPassInputs -> FRDGTexture* SceneColor
	static constexpr uintptr_t OFF_RDG_RHI = 0x10;              // FRDGTexture -> FRHITexture*
	static constexpr uintptr_t OFF_RDG_EXTENT = 0x48;           // FRDGTexture -> FIntPoint Extent
	static constexpr uintptr_t OFF_RHI_NATIVE = 0x98;           // FD3D11Texture2D -> ID3D11Resource*
	static constexpr uintptr_t OFF_VIEW_RECT = 0x1690;          // FViewInfo -> FIntRect ViewRect
	static constexpr uintptr_t OFF_VIEW_MATRICES = 0x280;       // FViewInfo -> FViewMatrices
	static constexpr uintptr_t OFF_MATRIX_PROJ = 0x000;         // ProjectionMatrix, with jitter
	static constexpr uintptr_t OFF_MATRIX_PROJ_NOAA = 0x040;    // ProjectionNoAAMatrix
	static constexpr uintptr_t OFF_MATRIX_VIEW = 0x0C0;         // ViewMatrix
	static constexpr uintptr_t OFF_VIEWPORT_BACKBUFFER = 0x78;  // FD3D11Viewport -> FD3D11Texture2D*

	/* TAA compute shader bindings */
	static constexpr UINT TAA_SLOT_DEPTH = 2;
	static constexpr UINT TAA_SLOT_VELOCITY = 3;

	struct IntRect
	{
		int32_t minX, minY, maxX, maxY;
	};

	/* Written by AddPasses on the render thread, consumed by the TAA dispatch on the RHI thread */
	struct FrameData
	{
		bool valid = false;
		void* outputTexture = nullptr;   // FRDGTexture*, only compared with the bound UAV
		void* colorTexture = nullptr;    // FRDGTexture*, may already be freed when the RHI thread runs
		int renderWidth = 0, renderHeight = 0;
		int outputWidth = 0, outputHeight = 0;
		int colorWidth = 0, colorHeight = 0;
		float jitterX = 0.0f, jitterY = 0.0f;
		float clipToPrevClip[16] = {};
		float cameraNear = 10.0f;
		float fovY = 1.0f;
		bool reset = false;
	};

	/* FSR 1 waits for the engine spatial upscale draw after a TAA pass without TAAU */
	struct SpatialUpscale
	{
		bool armed = false;
		int renderWidth = 0, renderHeight = 0;
		int colorWidth = 0, colorHeight = 0;
	};

	static inline uintptr_t base = 0;

	static inline FrameData frame = {};
	static inline CRITICAL_SECTION frameLock = {};
	static inline volatile LONG frameNumber = 0;
	static inline Utilities::Matrix4 previousViewProjection = {};
	static inline bool havePrevious = false;
	static inline int lastRenderWidth = 0, lastRenderHeight = 0, lastOutputWidth = 0, lastOutputHeight = 0;
	static inline int lastColorWidth = 0, lastColorHeight = 0;

	static inline SpatialUpscale spatialUpscale = {};
	static inline UINT backBufferWidth = 0, backBufferHeight = 0;
	static inline void* hdrOutputVariable = nullptr;   // r.HDR.EnableHDROutput
	static inline void* temporalAlgorithmVariable = nullptr;   // r.TemporalAA.Algorithm, 1 = Gen 5 TAAU (TSR)
	static inline const char* errorReason = "";

	static inline Utilities::ComputeState savedState;

	using AddPasses_t = void(__fastcall*)(const void* self, void* graphBuilder, const void* view, const void* passInputs,
	                                      void** outTexture, IntRect* outRect, void** outHalfTexture, IntRect* outHalfRect);
	using Dispatch_t = void(__fastcall*)(void* context, UINT x, UINT y, UINT z);
	using Draw_t = void(__fastcall*)(void* context, UINT baseVertex, UINT numPrimitives, UINT numInstances);
	using DrawIndexed_t = void(__fastcall*)(void* context, void* indexBuffer, INT baseVertex, UINT firstInstance, UINT numVertices,
	                                        UINT startIndex, UINT numPrimitives, UINT numInstances);
	using EndDrawing_t = void(__fastcall*)(void* context, void* viewport, bool present, bool lockToVsync);

	static inline AddPasses_t originalAddPasses = nullptr;
	static inline Dispatch_t originalDispatch = nullptr;
	static inline Draw_t originalDraw = nullptr;
	static inline DrawIndexed_t originalDrawIndexed = nullptr;
	static inline EndDrawing_t originalEndDrawing = nullptr;

	static void __fastcall HookAddPasses(const void* self, void* graphBuilder, const void* view, const void* passInputs,
	                                     void** outTexture, IntRect* outRect, void** outHalfTexture, IntRect* outHalfRect);
	static void __fastcall HookDispatch(void* context, UINT x, UINT y, UINT z);
	static void __fastcall HookDraw(void* context, UINT baseVertex, UINT numPrimitives, UINT numInstances);
	static void __fastcall HookDrawIndexed(void* context, void* indexBuffer, INT baseVertex, UINT firstInstance, UINT numVertices,
	                                       UINT startIndex, UINT numPrimitives, UINT numInstances);
	static void __fastcall HookEndDrawing(void* context, void* viewport, bool present, bool lockToVsync);

	static bool InstallHooks();
	static void OnFirstFrame();
	static void GameThreadUpdate();
	static FrameData CaptureFrame(const void* view, const void* passInputs, void** outTexture, IntRect* outRect, LONG number);

	static ID3D11Resource* NativeOf(void* rdgTexture);
	static ID3D11Resource* FindSceneColor(ID3D11ShaderResourceView* const* srvs, UINT count, ID3D11Resource* candidate, int width, int height);
	static bool HasVelocity(ID3D11ShaderResourceView* velocity);

	static bool RunTemporalUpscaler(ID3D11DeviceContext* context, ID3D11Resource* output, const FrameData& f, int32_t method);
	static void ReportTemporal(const FrameData& f, int32_t method, bool success);
	static void ReportEngine(const FrameData& f);
	static void ReportTsr(const FrameData& f);
	static bool TryFsr1(void* context);

	static Overlay::HdrMode DetectHdrMode(DXGI_FORMAT format);
	static const char* QualityName(int renderWidth, int outputWidth);
	static const char* SharpnessText();
	static void ReportStatus(Overlay::State state, const char* format, ...);


public:
	static void Init();
};
