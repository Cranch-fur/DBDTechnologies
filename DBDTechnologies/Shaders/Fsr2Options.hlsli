// FSR 2.2.1 (AMD FidelityFX, MIT) compiled for Direct3D 11 / Shader Model 5.0 by Visual Studio (FxCompile).
// One fixed permutation that matches what DBDTechnologies feeds FSR 2 in Unreal Engine 4.27:
//   HDR scene colour, motion vectors at render resolution (in pixels, without jitter),
//   reversed (inverted) depth, no FP16 and no wave intrinsics (not available in SM 5.0).
// FSR2.cpp creates the context with the matching flags.

#define FFX_GPU 1
#define FFX_HLSL 1
#define FFX_HALF 0
#define SPD_NO_WAVE_OPERATIONS 1

#define FFX_FSR2_OPTION_HDR_COLOR_INPUT 1
#define FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS 1
#define FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS 0
#define FFX_FSR2_OPTION_INVERTED_DEPTH 1
#define FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE 1
