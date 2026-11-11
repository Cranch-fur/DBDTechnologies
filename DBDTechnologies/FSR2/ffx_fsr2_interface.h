/*
	Replacement for ffx_fsr2_interface.h, which is not part of the src\ffx-fsr2-api copy in SDKs\FSR2.
	Declares what ffx_fsr2.cpp uses. An official copy placed next to ffx_fsr2.h takes priority.
*/

#pragma once

#include <assert.h>
#include <stddef.h>

#include "ffx_types.h"
#include "ffx_error.h"
#include "ffx_util.h"

#ifndef FFX_ASSERT
#define FFX_ASSERT(x) assert(x)
#endif
#ifndef FFX_ASSERT_MESSAGE
#define FFX_ASSERT_MESSAGE(x, message) assert((x) && (message))
#endif
/* Same values as shaders/ffx_fsr2_common.h, which ffx_fsr2.cpp does not include */
#ifndef LOCK_LIFETIME_REMAINING
#define LOCK_LIFETIME_REMAINING 0
#endif
#ifndef LOCK_TEMPORAL_LUMA
#define LOCK_TEMPORAL_LUMA 1
#endif

#ifndef FFX_STATIC_ASSERT
#define FFX_STATIC_ASSERT(x) static_assert(x, #x)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

FFX_FORWARD_DECLARE(FfxFsr2Interface);

typedef enum FfxFsr2Pass {
    FFX_FSR2_PASS_DEPTH_CLIP = 0,
    FFX_FSR2_PASS_RECONSTRUCT_PREVIOUS_DEPTH = 1,
    FFX_FSR2_PASS_LOCK = 2,
    FFX_FSR2_PASS_ACCUMULATE = 3,
    FFX_FSR2_PASS_ACCUMULATE_SHARPEN = 4,
    FFX_FSR2_PASS_RCAS = 5,
    FFX_FSR2_PASS_COMPUTE_LUMINANCE_PYRAMID = 6,
    FFX_FSR2_PASS_GENERATE_REACTIVE = 7,
    FFX_FSR2_PASS_TCR_AUTOGENERATE = 8,

    FFX_FSR2_PASS_COUNT
} FfxFsr2Pass;

typedef enum FfxFsr2MsgType {
    FFX_FSR2_MESSAGE_TYPE_ERROR = 0,
    FFX_FSR2_MESSAGE_TYPE_WARNING = 1,
    FFX_FSR2_MESSAGE_TYPE_COUNT
} FfxFsr2MsgType;

typedef void (*FfxFsr2Message)(FfxFsr2MsgType type, const wchar_t* message);

typedef FfxErrorCode (*FfxFsr2CreateBackendContextFunc)(FfxFsr2Interface* backendInterface, FfxDevice device);
typedef FfxErrorCode (*FfxFsr2GetDeviceCapabilitiesFunc)(FfxFsr2Interface* backendInterface, FfxDeviceCapabilities* outDeviceCapabilities, FfxDevice device);
typedef FfxErrorCode (*FfxFsr2DestroyBackendContextFunc)(FfxFsr2Interface* backendInterface);
typedef FfxErrorCode (*FfxFsr2CreateResourceFunc)(FfxFsr2Interface* backendInterface, const FfxCreateResourceDescription* createResourceDescription, FfxResourceInternal* outResource);
typedef FfxErrorCode (*FfxFsr2RegisterResourceFunc)(FfxFsr2Interface* backendInterface, const FfxResource* inResource, FfxResourceInternal* outResourceInternal);
typedef FfxErrorCode (*FfxFsr2UnregisterResourcesFunc)(FfxFsr2Interface* backendInterface);
typedef FfxResourceDescription (*FfxFsr2GetResourceDescriptionFunc)(FfxFsr2Interface* backendInterface, FfxResourceInternal resource);
typedef FfxErrorCode (*FfxFsr2DestroyResourceFunc)(FfxFsr2Interface* backendInterface, FfxResourceInternal resource);
typedef FfxErrorCode (*FfxFsr2CreatePipelineFunc)(FfxFsr2Interface* backendInterface, FfxFsr2Pass pass, const FfxPipelineDescription* pipelineDescription, FfxPipelineState* outPipeline);
typedef FfxErrorCode (*FfxFsr2DestroyPipelineFunc)(FfxFsr2Interface* backendInterface, FfxPipelineState* pipeline);
typedef FfxErrorCode (*FfxFsr2ScheduleGpuJobFunc)(FfxFsr2Interface* backendInterface, const FfxGpuJobDescription* job);
typedef FfxErrorCode (*FfxFsr2ExecuteGpuJobsFunc)(FfxFsr2Interface* backendInterface, FfxCommandList commandList);

/* Implemented for Direct3D 11 in FSR2.cpp */
typedef struct FfxFsr2Interface {
    FfxFsr2CreateBackendContextFunc     fpCreateBackendContext;
    FfxFsr2GetDeviceCapabilitiesFunc    fpGetDeviceCapabilities;
    FfxFsr2DestroyBackendContextFunc    fpDestroyBackendContext;
    FfxFsr2CreateResourceFunc           fpCreateResource;
    FfxFsr2RegisterResourceFunc         fpRegisterResource;
    FfxFsr2UnregisterResourcesFunc      fpUnregisterResources;
    FfxFsr2GetResourceDescriptionFunc   fpGetResourceDescription;
    FfxFsr2DestroyResourceFunc          fpDestroyResource;
    FfxFsr2CreatePipelineFunc           fpCreatePipeline;
    FfxFsr2DestroyPipelineFunc          fpDestroyPipeline;
    FfxFsr2ScheduleGpuJobFunc           fpScheduleGpuJob;
    FfxFsr2ExecuteGpuJobsFunc           fpExecuteGpuJobs;

    void*                               scratchBuffer;
    size_t                              scratchBufferSize;
} FfxFsr2Interface;

#if defined(__cplusplus)
}
#endif
