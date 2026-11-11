#pragma once
#include "Utilities.h"






/* Status line in the top-left corner, drawn into the back buffer right before Present */
class Overlay
{
public:
	enum class State
	{
		Idle,       // No 3D scene this frame
		Active,     // An upscaler of the mod is running
		Disabled,   // Engine TAA/TAAU
		Error       // The mod upscaler failed, the engine one is used
	};

	enum class HdrMode
	{
		SDR,
		PQ,       // HDR10, ST.2084
		scRGB     // Linear FP16, 1.0 = 80 nits
	};

	static constexpr int MaxChars = 96;


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
	static inline const char* (*extraTextProvider)() = nullptr;
	static inline HdrMode hdrMode = HdrMode::SDR;

	static inline bool initTried = false;
	static inline bool ready = false;
	static inline ID3D11ComputeShader* shader = nullptr;
	static inline ID3D11Buffer* constants = nullptr;
	static inline ID3D11Texture2D* mask = nullptr;
	static inline ID3D11ShaderResourceView* maskSrv = nullptr;

	/* The back buffer has no UAV support: copy the area out, blend in a shader, copy it back */
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
	static void BuildText(char* buffer, size_t size, State& colorState);
	static void EncodeColor(const float srgb[4], float out[4]);


public:
	static void SetStatus(State state, const char* text);
	static void SetHdrMode(HdrMode mode) { hdrMode = mode; }

	/* Text appended to the status line, e.g. the low latency state */
	static void SetExtraTextProvider(const char* (*provider)()) { extraTextProvider = provider; }

	static void Draw(ID3D11DeviceContext* context, ID3D11Resource* backBuffer);
};
