/*
	Forced include for ffx_fsr3upscaler.cpp. It pulls in ffx_backends.h -> ffx_provider.h, which uses ffxProviderExternal
	from a header the SDK does not ship. Nothing of the provider system is used here, so a never-valid stub is enough.
*/
#pragma once
#include "../../SDKs/FidelityFX-SDK/Kits/FidelityFX/api/internal/ffx_api_helper.h"

/* Template, because ffxProvider is declared later in ffx_provider.h */
template <class Provider>
class ProviderExternalStub : public Provider
{
public:
	ProviderExternalStub(void*, uint64_t) : Provider(0, 0, "") {}

	bool Valid() const { return false; }

	ffxReturnCode_t CreateContext(ffxContext*, ffxCreateContextDescHeader*, Allocator&) override { return FFX_API_RETURN_ERROR; }
	ffxReturnCode_t DestroyContext(ffxContext*, Allocator&) override { return FFX_API_RETURN_ERROR; }
	ffxReturnCode_t Configure(ffxContext*, const ffxConfigureDescHeader*) const override { return FFX_API_RETURN_ERROR; }
	ffxReturnCode_t Query(ffxContext*, ffxQueryDescHeader*) const override { return FFX_API_RETURN_ERROR; }
	ffxReturnCode_t Dispatch(ffxContext*, const ffxDispatchDescHeader*) const override { return FFX_API_RETURN_ERROR; }
};

#define ffxProviderExternal ProviderExternalStub<ffxProvider>
