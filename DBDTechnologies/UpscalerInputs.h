#pragma once
#include "Utilities.h"






/* Everything a temporal upscaler (DLSS, XeSS, FSR 2, FSR 3) needs for one frame */
struct UpscalerInputs
{
	ID3D11Resource* color = nullptr;           // Scene color, HDR, render resolution in the top-left corner
	ID3D11Resource* motionVectors = nullptr;   // R16G16_FLOAT, render-resolution pixels, towards the previous frame
	ID3D11Resource* depth = nullptr;           // R32_FLOAT, reversed infinite depth
	ID3D11Resource* output = nullptr;          // Output resolution, needs UAV support

	int renderWidth = 0, renderHeight = 0;
	int maxRenderWidth = 0, maxRenderHeight = 0;   // Largest input during dynamic resolution, otherwise equal to the input
	int outputWidth = 0, outputHeight = 0;

	float jitterX = 0.0f, jitterY = 0.0f;      // Pixels
	float cameraNear = 10.0f;                  // Unreal units (cm)
	float fovY = 1.0f;                         // Radians
	float sharpness = 0.0f;                    // 0..1
	float frameTimeMs = 16.6f;

	bool reset = false;
	bool dynamicResolution = false;
};
