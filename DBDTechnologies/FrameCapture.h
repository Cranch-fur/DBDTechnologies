#pragma once
#include "Utilities.h"






/*
	One-frame GPU state capture, used to find engine passes the mod has to hook (first target: the SSR draw
	that voxel reflections will replace).

	r.VoxelReflections.Capture 1   full-screen draws and every dispatch of the next presented frame
	r.VoxelReflections.Capture 2   every draw and dispatch of the next presented frame (large file)

	Every call is logged after the engine has issued it, so resources the D3D11 RHI binds inside the call
	(uniform buffer resource tables) are included. Output: DBDTechnologies_Capture_<n>.log next to the game exe.

	Tags in the log:
	  [HZB]   a mipmapped single/two-channel float texture is bound (hierarchical Z, used by SSR and SSGI)
	  [SSR?]  [HZB] + an FP16 RGBA render target the size of the viewport: the likely SSR draw
*/
class FrameCapture
{
public:
	static inline int32_t request = 0;       // r.VoxelReflections.Capture, written by the engine console
	static inline bool drawHooks = false;    // Set by Technologies when the draw hooks are installed

	/* RHI thread. Starts a requested capture and ends a running one at each presented frame */
	static void OnPresent(ID3D11DeviceContext* context, UINT backBufferWidth, UINT backBufferHeight, LONG frameNumber);

	/* RHI thread, after the engine call */
	static void OnDraw(ID3D11DeviceContext* context, const char* kind, UINT numPrimitives, UINT numInstances, bool replaced);
	static void OnDispatch(ID3D11DeviceContext* context, UINT x, UINT y, UINT z);

	static bool IsActive() { return active; }


private:
	static constexpr UINT SrvSlots = 32;
	static constexpr UINT CbSlots = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
	static constexpr UINT UavSlots = 8;
	static constexpr UINT RtvSlots = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

	static inline bool active = false;
	static inline int mode = 0;
	static inline FILE* file = nullptr;
	static inline int captureNumber = 0;
	static inline UINT eventIndex = 0;
	static inline UINT loggedEvents = 0;
	static inline UINT skippedEvents = 0;
	static inline UINT hzbEvents = 0;
	static inline UINT ssrCandidates = 0;
	static inline UINT screenWidth = 0, screenHeight = 0;

	struct TextureInfo
	{
		bool valid = false;
		D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
		UINT width = 0, height = 0, depth = 0;
		UINT mips = 0, arraySize = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		UINT byteWidth = 0;        // Buffers
		bool cube = false;
	};

	static void Begin(UINT backBufferWidth, UINT backBufferHeight, LONG frameNumber);
	static void End();

	static TextureInfo Inspect(ID3D11Resource* resource);
	static bool IsHzb(const TextureInfo& info);
	static const char* FormatName(DXGI_FORMAT format);
	static int Append(char* buffer, size_t size, int used, const char* format, ...);
	static int AppendResource(char* buffer, size_t size, int used, ID3D11Resource* resource, DXGI_FORMAT viewFormat);
	static int AppendShaderResources(char* buffer, size_t size, int used, ID3D11ShaderResourceView* const* srvs, UINT count, bool& hasHzb);
	static int AppendConstantBuffers(char* buffer, size_t size, int used, ID3D11Buffer* const* buffers, UINT count);
};
