#pragma once
#include "Utilities.h"






/*
	Builds the motion vector and depth textures shared by DLSS, XeSS and FSR 2.
	Moving objects come from the engine velocity buffer, everything else from depth and camera matrices.
*/
class MotionVectors
{
private:
	struct Constants
	{
		float clipToPrevClip[16];
		float renderSize[2];
		float invRenderSize[2];
		uint32_t hasVelocity;
		uint32_t decodeMode;
		float pad[2];
	};

	static inline ID3D11Device* device = nullptr;
	static inline bool initTried = false;
	static inline ID3D11ComputeShader* shader = nullptr;
	static inline ID3D11Buffer* constants = nullptr;

	static inline ID3D11Texture2D* motionTexture = nullptr;
	static inline ID3D11UnorderedAccessView* motionUav = nullptr;
	static inline ID3D11Texture2D* depthTexture = nullptr;
	static inline ID3D11UnorderedAccessView* depthUav = nullptr;
	static inline int textureWidth = 0, textureHeight = 0;

	static bool Initialize(ID3D11Device* d3dDevice);
	static bool EnsureTextures(int width, int height);


public:
	struct Inputs
	{
		ID3D11ShaderResourceView* depth = nullptr;      // Engine depth (R32 view)
		ID3D11ShaderResourceView* velocity = nullptr;   // Engine velocity buffer, nullptr when nothing moves
		const float* clipToPrevClip = nullptr;          // 4x4, row-major
		int renderWidth = 0, renderHeight = 0;
		int bufferWidth = 0, bufferHeight = 0;
		int decodeMode = 0;                             // r.Upscaler.VelocityDecode
	};

	/* Runs one compute pass; the caller saves and restores the compute state */
	static bool Generate(ID3D11DeviceContext* context, const Inputs& inputs);

	static ID3D11Texture2D* Motion() { return motionTexture; }
	static ID3D11Texture2D* Depth() { return depthTexture; }
};
