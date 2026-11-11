#pragma once
#include "Utilities.h"






/*
	Надпись-индикатор в левом верхнем углу.
	Рисуется в самом конце кадра прямо в back buffer (поверх 3D, постобработки и интерфейса игры),
	поэтому её размер считается от разрешения экрана и не зависит от r.ScreenPercentage.
*/
class Overlay
{
public:
	enum class State
	{
		Idle,      // в этом кадре апскейлер не встречался (меню без 3D-сцены)
		Active,    // апскейлер мода отработал
		Disabled,  // r.Upscaler.Method 0 — обычный TAA
		Error      // апскейлер мода не запустился — работает то, что было в игре
	};

	static constexpr int MaxChars = 96;

	/* Как кодирован back buffer: обычный SDR, HDR10 (PQ, ST.2084) или scRGB (линейный FP16, 1.0 = 80 нит) */
	enum class HdrMode
	{
		SDR,
		PQ,
		scRGB
	};
	static void SetHdrMode(HdrMode mode) { hdrMode = mode; }

	/* Состояние и готовый текст строки (поток RHI) */
	static void SetStatus(State state, const char* text);

	/* Вызывается перед выводом кадра на экран (поток RHI) */
	static void Draw(ID3D11DeviceContext* context, ID3D11Resource* backBuffer);

	/* Дополнительный текст в конце строки (например, состояние Reflex) */
	using ExtraTextProvider = const char* (*)();
	static void SetExtraTextProvider(ExtraTextProvider provider);


private:
	static constexpr int MaskWidth = MaxChars * 6 + 2;
	static constexpr int MaskHeight = 9;

	struct Constants
	{
		uint32_t maskSize[2];
		uint32_t scale;
		uint32_t pad;
		float textColor[4];
		float backgroundColor[4];
	};

	static inline State statusState = State::Idle;
	static inline char statusText[MaxChars + 1] = {};
	static inline ULONGLONG statusTick = 0;
	static inline ExtraTextProvider extraTextProvider = nullptr;
	static inline HdrMode hdrMode = HdrMode::SDR;

	static inline bool initTried = false;
	static inline bool ready = false;
	static inline ID3D11ComputeShader* shader = nullptr;
	static inline ID3D11Buffer* constants = nullptr;
	static inline ID3D11Texture2D* mask = nullptr;
	static inline ID3D11ShaderResourceView* maskSrv = nullptr;

	/* Копия области back buffer под надписью: src (чтение) -> шейдер -> dst (запись) -> back buffer */
	static inline ID3D11Texture2D* source = nullptr;
	static inline ID3D11ShaderResourceView* sourceSrv = nullptr;
	static inline ID3D11Texture2D* destination = nullptr;
	static inline ID3D11UnorderedAccessView* destinationUav = nullptr;
	static inline UINT areaWidth = 0, areaHeight = 0;
	static inline DXGI_FORMAT areaFormat = DXGI_FORMAT_UNKNOWN;

	static inline char text[MaxChars + 1] = {};
	static inline int textLength = 0;

	static inline Utilities::ComputeState savedState;

	static bool Initialize(ID3D11Device* device);
	static bool EnsureArea(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format);
	static void Rasterize(ID3D11DeviceContext* context, const char* newText);
	static void BuildText(char* buffer, size_t size, UINT screenWidth, State& colorState);
	static void EncodeColor(const float srgb[4], float out[4]);
};
