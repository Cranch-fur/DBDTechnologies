#pragma once
#include "Utilities.h"






/*
	Voxel reflections: replaces the engine SSR pass with reflections traced through a voxel cache built from the screen.

	The SSR pass of Dead by Daylight 6.7.2 (UE 4.27 FScreenSpaceReflectionsPS) is one full-screen DrawIndexed:
	  RT0  SSR output, RGBA16F, view size (rgb = reflection, a = coverage; read by the reflection composite)
	  t0   scene depth (R32_X8X24 view of the D32S8 buffer, reversed Z)
	  t1   GBufferA, world normal * 0.5 + 0.5 (RGB10A2)
	  t2   GBufferB, metallic / specular / roughness / shading model (BGRA8)
	  t6   scene color, lit without reflections (RGBA16F)
	  t7   furthest HZB (R16F, full mip chain)
	  b1   View uniform buffer (camera matrices of this exact frame; slot set by r.VoxelReflections.ViewBufferSlot)
	The draw is recognised by these bindings, then its pixel shader address is remembered for the rest of the session.

	Each frame, instead of the SSR draw (all compute, Shaders\Voxel*.hlsl):
	  1. Update  move the clipmap windows with the camera
	  2. Carve   copy last frame's radiance, clear voxels that scrolled in, fade voxels the depth buffer sees through
	  3. Inject  blend the lit scene color of visible surfaces into their voxels (averaged over frames: no flicker)
	  4. Trace   cone-march each pixel's reflection ray through the clipmaps
	The result is copied into the SSR target; where coverage < 1 the engine adds reflection captures and sky light.
*/
class VoxelReflections
{
public:
	static inline int32_t enable = 0;            // r.VoxelReflections.Enable
	static inline int32_t debug = 0;             // r.VoxelReflections.Debug
	static inline float debugScale = 16.0f;      // r.VoxelReflections.DebugScale
	static inline float voxelSize = 8.0f;        // r.VoxelReflections.VoxelSize, cm
	static inline float maxRoughness = 0.6f;     // r.VoxelReflections.MaxRoughness
	static inline float maxDistance = 3000.0f;   // r.VoxelReflections.MaxDistance, cm
	static inline int32_t viewBufferSlot = 1;    // r.VoxelReflections.ViewBufferSlot

	static bool IsEnabled() { return enable != 0; }

	/* RHI thread, before the engine DrawIndexed. Returns true when the SSR draw was replaced and must not be issued */
	static bool OnDrawIndexed(ID3D11DeviceContext* context, UINT numPrimitives, UINT numInstances);

	/* RHI thread, once per presented frame */
	static void OnPresent();


private:
	static constexpr UINT SLOT_DEPTH = 0;
	static constexpr UINT SLOT_GBUFFER_A = 1;
	static constexpr UINT SLOT_GBUFFER_B = 2;
	static constexpr UINT SLOT_SCENE_COLOR = 6;
	static constexpr UINT SLOT_HZB = 7;
	static constexpr UINT SLOT_COUNT = 8;

	static constexpr UINT LEVEL_COUNT = 4;          // Must match VoxelCommon.hlsli
	static constexpr UINT RESOLUTION = 128;         // Voxels per clipmap axis
	static constexpr UINT STATE_ELEMENTS = 16;      // int4: origins + previous origins
	static constexpr float CARVE_RATE = 0.75f;
	static constexpr int MAX_MODE = 6;

	struct SsrDraw
	{
		ID3D11ShaderResourceView* srvs[SLOT_COUNT] = {};   // As bound by the engine, AddRef'd
		ID3D11Buffer* view = nullptr;                       // Engine View uniform buffer, AddRef'd
		ID3D11Resource* target = nullptr;                   // RT0, AddRef'd
		DXGI_FORMAT targetFormat = DXGI_FORMAT_UNKNOWN;
		UINT targetWidth = 0, targetHeight = 0;
		UINT hzbWidth = 0, hzbHeight = 0, hzbMips = 0;
		D3D11_VIEWPORT viewport = {};

		void Release();
	};

	/* Must match cbuffer Constants in VoxelCommon.hlsli */
	struct Constants
	{
		uint32_t offset[2];
		uint32_t size[2];
		uint32_t mode;
		float scale;
		uint32_t resolution;
		uint32_t reset;
		float voxelSize0;
		float maxRoughness;
		float maxDistance;
		float carveRate;
		uint32_t frame;
		float padding[3];
	};
	static_assert(sizeof(Constants) == 64, "Constants must match VoxelCommon.hlsli");

	/* Only used for static members, which start zeroed (default member initializers are not allowed here) */
	struct Volume
	{
		ID3D11Texture3D* texture;
		ID3D11ShaderResourceView* srv;
		ID3D11UnorderedAccessView* uav;

		bool Create(ID3D11Device* device, DXGI_FORMAT format);
		void Release();
	};

	static inline void* ssrPixelShader = nullptr;   // Only compared, never dereferenced
	static inline bool foundThisFrame = false;
	static inline UINT framesWithoutSsr = 0;
	static inline bool warnedMissing = false;
	static inline uint32_t frameIndex = 0;
	static inline bool resetPending = true;
	static inline float appliedVoxelSize = 0.0f;

	static inline bool initTried = false;
	static inline bool ready = false;
	static inline ID3D11ComputeShader* debugShader = nullptr;
	static inline ID3D11ComputeShader* updateShader = nullptr;
	static inline ID3D11ComputeShader* carveShader = nullptr;
	static inline ID3D11ComputeShader* injectShader = nullptr;
	static inline ID3D11ComputeShader* traceShader = nullptr;
	static inline ID3D11Buffer* constants = nullptr;
	static inline ID3D11SamplerState* linearWrap = nullptr;

	static inline bool volumesTried = false;
	static inline bool volumesReady = false;
	static inline Volume occupancy[LEVEL_COUNT];
	static inline Volume radiance[2][LEVEL_COUNT];          // Alternate every frame: [historyIndex] = last frame's
	static inline UINT historyIndex = 0;
	static inline DXGI_FORMAT radianceFormat = DXGI_FORMAT_R11G11B10_FLOAT;
	static inline ID3D11Buffer* state = nullptr;
	static inline ID3D11ShaderResourceView* stateSrv = nullptr;
	static inline ID3D11UnorderedAccessView* stateUav = nullptr;

	static inline ID3D11Texture2D* output = nullptr;
	static inline ID3D11UnorderedAccessView* outputUav = nullptr;
	static inline UINT outputWidth = 0, outputHeight = 0;
	static inline Utilities::ComputeState savedState;

	static bool Match(ID3D11DeviceContext* context, SsrDraw& draw);
	static bool Initialize(ID3D11Device* device);
	static bool CreateVolumes(ID3D11Device* device);
	static bool EnsureOutput(ID3D11Device* device, UINT width, UINT height);
	static bool Prepare(ID3D11DeviceContext* context, const SsrDraw& draw, int mode, bool needVolumes);
	static void Finish(ID3D11DeviceContext* context, const SsrDraw& draw);
	static bool RunDebug(ID3D11DeviceContext* context, const SsrDraw& draw, int mode);
	static bool RunVoxels(ID3D11DeviceContext* context, const SsrDraw& draw, int mode);
	static void UnbindAll(ID3D11DeviceContext* context);
};
