#include "Technologies.h"
#include "Console.h"
#include "DynamicResolution.h"
#include "MotionVectors.h"
#include "LowLatency.h"
#include "DLSS.h"
#include "XeSS.h"
#include "FSR1.h"
#include "FSR2.h"
#include "FSR3.h"

#include <algorithm>

#pragma comment(lib, "d3d11.lib")






static const uint8_t SIGNATURE_ADDPASSES[] = { 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40 };
static const uint8_t SIGNATURE_DISPATCH[] = { 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
static const uint8_t SIGNATURE_END_DRAWING[] = { 0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 };
static const uint8_t SIGNATURE_DRAW[] = { 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
static const uint8_t SIGNATURE_DRAW_INDEXED[] = { 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x4C };




const char* Technologies::QualityName(int renderWidth, int outputWidth)
{
	float ratio = outputWidth > 0 ? static_cast<float>(renderWidth) / static_cast<float>(outputWidth) : 1.0f;
	if (ratio >= 0.99f) return "NATIVE AA";
	if (ratio >= 0.62f) return "QUALITY";
	if (ratio >= 0.55f) return "BALANCED";
	if (ratio >= 0.45f) return "PERFORMANCE";
	return "ULTRA PERF";
}

const char* Technologies::SharpnessText()
{
	static char text[24];
	if (FSR1::IsSharpening(Console::settings.sharpness))
		snprintf(text, sizeof(text), "  SHARP %.2f", Console::settings.sharpness);
	else
		text[0] = 0;
	return text;
}

void Technologies::ReportStatus(Overlay::State state, const char* format, ...)
{
	char text[Overlay::MaxChars + 1];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	Overlay::SetStatus(state, text);
}

/* An FP16 back buffer is scRGB. A 10-bit back buffer is also used for SDR, so HDR10 is detected through r.HDR.EnableHDROutput */
Overlay::HdrMode Technologies::DetectHdrMode(DXGI_FORMAT format)
{
	if (format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R16G16B16A16_TYPELESS)
		return Overlay::HdrMode::scRGB;
	if (hdrOutputVariable && Console::GetEngineInt(hdrOutputVariable, 0) != 0 &&
	    (format == DXGI_FORMAT_R10G10B10A2_UNORM || format == DXGI_FORMAT_R10G10B10A2_TYPELESS))
		return Overlay::HdrMode::PQ;
	return Overlay::HdrMode::SDR;
}




ID3D11Resource* Technologies::NativeOf(void* rdgTexture)
{
	void* rhiTexture = Utilities::Memory::Read<void*>(rdgTexture, OFF_RDG_RHI);
	return Utilities::Memory::Read<ID3D11Resource*>(rhiTexture, OFF_RHI_NATIVE);
}

/*
	candidate comes from an FRDGTexture that may already be freed, so it is only compared with the bound textures.
	Without a match the bound texture with the scene color size and a color format is used.
*/
ID3D11Resource* Technologies::FindSceneColor(ID3D11ShaderResourceView* const* srvs, UINT count, ID3D11Resource* candidate, int width, int height)
{
	ID3D11Resource* bySize = nullptr;
	for (UINT i = 0; i < count; ++i)
	{
		if (srvs[i] == nullptr || i == TAA_SLOT_DEPTH || i == TAA_SLOT_VELOCITY)
			continue;
		ID3D11Resource* resource = nullptr;
		srvs[i]->GetResource(&resource);
		if (resource == nullptr)
			continue;
		resource->Release();   // Kept alive by the binding

		if (candidate && resource == candidate)
			return resource;

		if (bySize == nullptr)
		{
			D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
			resource->GetType(&dimension);
			if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
				continue;
			D3D11_TEXTURE2D_DESC desc;
			static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
			const bool colorFormat = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS ||
			                         desc.Format == DXGI_FORMAT_R11G11B10_FLOAT || desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT;
			if (colorFormat && static_cast<int>(desc.Width) == width && static_cast<int>(desc.Height) == height)
				bySize = resource;
		}
	}

	static bool logged = false;
	if (bySize && logged == false)
	{
		logged = true;
		Utilities::Log::Write("scene color: RDG pointer was stale, using the bound texture of the same size");
	}
	return bySize;
}

/* The engine binds a 1x1 dummy when nothing moves */
bool Technologies::HasVelocity(ID3D11ShaderResourceView* velocity)
{
	if (velocity == nullptr)
		return false;

	ID3D11Resource* resource = nullptr;
	velocity->GetResource(&resource);
	if (resource == nullptr)
		return false;

	bool result = false;
	D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
	resource->GetType(&dimension);
	if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
	{
		D3D11_TEXTURE2D_DESC desc;
		static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
		result = desc.Width > 1;
	}
	resource->Release();
	return result;
}




void Technologies::OnFirstFrame()
{
	Console::Register(base);
	DynamicResolution::FindEngineVariables();
	hdrOutputVariable = Console::FindEngineVariable(L"r.HDR.EnableHDROutput");
	Utilities::Log::Write("r.HDR.EnableHDROutput %s", hdrOutputVariable ? "found" : "not found");
	temporalAlgorithmVariable = Console::FindEngineVariable(L"r.TemporalAA.Algorithm");
	Utilities::Log::Write("r.TemporalAA.Algorithm %s", temporalAlgorithmVariable ? "found" : "not found - TSR unavailable");
}

/* Engine variables can only be changed on the game thread. r.TemporalAA.Algorithm follows r.Upscaler.Method when the method changes */
void Technologies::GameThreadUpdate()
{
	DynamicResolution::Update();

	if (Console::IsReady() == false || temporalAlgorithmVariable == nullptr)
		return;

	static int32_t applied = -1;
	const int32_t wanted = Console::settings.Method() == UpscalerMethod::TSR ? 1 : 0;
	if (wanted == applied)
		return;
	Console::SetEngineValue(temporalAlgorithmVariable, wanted ? L"1" : L"0");
	Utilities::Log::Write("r.TemporalAA.Algorithm %d (%s)", wanted, wanted ? "Gen 5 TAAU / TSR" : "Gen 4 TAA / TAAU");
	applied = wanted;
}

Technologies::FrameData Technologies::CaptureFrame(const void* view, const void* passInputs, void** outTexture, IntRect* outRect, LONG number)
{
	using Utilities::Memory;
	using Utilities::Matrix4;

	FrameData f;
	f.outputTexture = Memory::Read<void*>(outTexture);
	f.colorTexture = Memory::Read<void*>(passInputs, OFF_INPUT_COLOR);
	IntRect output = Memory::Read<IntRect>(outRect);
	IntRect viewRect = Memory::Read<IntRect>(view, OFF_VIEW_RECT);
	f.outputWidth = output.maxX - output.minX;
	f.outputHeight = output.maxY - output.minY;
	f.renderWidth = viewRect.maxX - viewRect.minX;
	f.renderHeight = viewRect.maxY - viewRect.minY;
	f.colorWidth = Memory::Read<int>(f.colorTexture, OFF_RDG_EXTENT);
	f.colorHeight = Memory::Read<int>(f.colorTexture, OFF_RDG_EXTENT + 4);

	bool sane = f.outputTexture && f.colorTexture && f.outputWidth > 0 && f.outputHeight > 0 &&
	            f.renderWidth > 0 && f.renderHeight > 0 && f.renderWidth <= f.colorWidth && f.renderHeight <= f.colorHeight &&
	            viewRect.minX == 0 && viewRect.minY == 0;

	const uint8_t* matrices = reinterpret_cast<const uint8_t*>(view) + OFF_VIEW_MATRICES;
	Matrix4 projection, projectionNoAA, viewMatrix;
	sane = sane && Matrix4::ReadFloat(matrices + OFF_MATRIX_PROJ, projection) &&
	       Matrix4::ReadFloat(matrices + OFF_MATRIX_PROJ_NOAA, projectionNoAA) &&
	       Matrix4::ReadFloat(matrices + OFF_MATRIX_VIEW, viewMatrix) &&
	       projection.m[11] == 1.0 && projection.m[15] == 0.0;

	if (sane == false)
	{
		havePrevious = false;
		return f;
	}

	/* Unreal stores the jitter in ProjectionMatrix: M[2][0] = 2 * x / width, M[2][1] = -2 * y / height */
	const int jitterMode = Console::settings.jitterMode;
	float jitterX = static_cast<float>(projection.m[8] * f.renderWidth * 0.5);
	float jitterY = static_cast<float>(-projection.m[9] * f.renderHeight * 0.5);
	f.jitterX = (jitterMode & 1) ? -jitterX : jitterX;
	f.jitterY = (jitterMode & 2) ? -jitterY : jitterY;

	/* Reversed infinite projection: M[3][2] = near plane, M[1][1] = 1 / tan(fovY / 2) */
	if (projectionNoAA.m[14] > 0.0)
		f.cameraNear = static_cast<float>(projectionNoAA.m[14]);
	if (projectionNoAA.m[5] > 0.0)
		f.fovY = static_cast<float>(2.0 * atan(1.0 / projectionNoAA.m[5]));

	/* With dynamic resolution the input size changes all the time, so only buffer and output changes reset the history */
	const bool renderChanged = f.renderWidth != lastRenderWidth || f.renderHeight != lastRenderHeight;
	const bool sizeChanged = f.outputWidth != lastOutputWidth || f.outputHeight != lastOutputHeight ||
	                         f.colorWidth != lastColorWidth || f.colorHeight != lastColorHeight ||
	                         (renderChanged && DynamicResolution::IsActive() == false);

	Matrix4 viewProjection = Matrix4::Multiply(viewMatrix, projectionNoAA);
	Matrix4 inverseViewProjection;
	if (havePrevious && sizeChanged == false && Matrix4::Inverse(viewProjection, inverseViewProjection))
	{
		Matrix4 clipToPrevClip = Matrix4::Multiply(inverseViewProjection, previousViewProjection);
		for (int i = 0; i < 16; ++i)
			f.clipToPrevClip[i] = static_cast<float>(clipToPrevClip.m[i]);
	}
	else
	{
		for (int i = 0; i < 16; ++i)
			f.clipToPrevClip[i] = (i % 5 == 0) ? 1.0f : 0.0f;
		f.reset = true;
	}

	if (sizeChanged)
		Utilities::Log::Write("frame %ld: input %dx%d (buffer %dx%d) -> output %dx%d (%.1f%%)%s", number, f.renderWidth, f.renderHeight,
		                      f.colorWidth, f.colorHeight, f.outputWidth, f.outputHeight, 100.0 * f.renderWidth / f.outputWidth,
		                      DynamicResolution::IsActive() ? ", dynamic resolution" : "");

	previousViewProjection = viewProjection;
	havePrevious = true;
	lastRenderWidth = f.renderWidth;
	lastRenderHeight = f.renderHeight;
	lastOutputWidth = f.outputWidth;
	lastOutputHeight = f.outputHeight;
	lastColorWidth = f.colorWidth;
	lastColorHeight = f.colorHeight;
	f.valid = true;
	return f;
}

void __fastcall Technologies::HookAddPasses(const void* self, void* graphBuilder, const void* view, const void* passInputs,
                                            void** outTexture, IntRect* outRect, void** outHalfTexture, IntRect* outHalfRect)
{
	originalAddPasses(self, graphBuilder, view, passInputs, outTexture, outRect, outHalfTexture, outHalfRect);

	LONG number = InterlockedIncrement(&frameNumber);
	if (number == 1)
		OnFirstFrame();
	Console::Validate();

	FrameData f = CaptureFrame(view, passInputs, outTexture, outRect, number);

	/* Gen 5 TAAU runs its own chain of dispatches, nothing is replaced */
	if (f.valid && Console::settings.Method() == UpscalerMethod::TSR)
	{
		ReportTsr(f);
		f.valid = false;
	}

	EnterCriticalSection(&frameLock);
	frame = f;
	LeaveCriticalSection(&frameLock);
}




bool Technologies::RunTemporalUpscaler(ID3D11DeviceContext* context, ID3D11Resource* output, const FrameData& f, int32_t method)
{
	const Settings& settings = Console::settings;
	const UpscalerMethod upscaler = static_cast<UpscalerMethod>(method);

	ID3D11ShaderResourceView* bound[16] = {};
	context->CSGetShaderResources(0, _countof(bound), bound);
	ID3D11ShaderResourceView* depth = bound[TAA_SLOT_DEPTH];
	ID3D11ShaderResourceView* velocity = bound[TAA_SLOT_VELOCITY];
	ID3D11Resource* color = FindSceneColor(bound, _countof(bound), NativeOf(f.colorTexture), f.colorWidth, f.colorHeight);

	bool ok = false;
	if (color && depth)
	{
		savedState.Save(context);

		MotionVectors::Inputs vectors;
		vectors.depth = depth;
		vectors.velocity = HasVelocity(velocity) ? velocity : nullptr;
		vectors.clipToPrevClip = f.clipToPrevClip;
		vectors.renderWidth = f.renderWidth;
		vectors.renderHeight = f.renderHeight;
		vectors.bufferWidth = f.colorWidth;
		vectors.bufferHeight = f.colorHeight;
		vectors.decodeMode = settings.velocityDecode;

		if (MotionVectors::Generate(context, vectors))
		{
			static LARGE_INTEGER frequency = {}, previous = {};
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			if (frequency.QuadPart == 0)
				QueryPerformanceFrequency(&frequency);
			float frameMs = previous.QuadPart ? static_cast<float>((now.QuadPart - previous.QuadPart) * 1000.0 / frequency.QuadPart) : 16.6f;
			previous = now;

			UpscalerInputs in;
			in.color = color;
			in.motionVectors = MotionVectors::Motion();
			in.depth = MotionVectors::Depth();
			in.output = output;
			in.renderWidth = f.renderWidth;
			in.renderHeight = f.renderHeight;
			in.maxRenderWidth = f.renderWidth;
			in.maxRenderHeight = f.renderHeight;
			in.dynamicResolution = DynamicResolution::IsActive();
			if (in.dynamicResolution)
			{
				/* Created once for the largest possible input: the scene buffer, but not more than the output */
				in.maxRenderWidth = (std::max)(f.renderWidth, (std::min)(f.colorWidth, f.outputWidth));
				in.maxRenderHeight = (std::max)(f.renderHeight, (std::min)(f.colorHeight, f.outputHeight));
			}
			in.outputWidth = f.outputWidth;
			in.outputHeight = f.outputHeight;
			in.jitterX = f.jitterX;
			in.jitterY = f.jitterY;
			in.cameraNear = f.cameraNear;
			in.fovY = f.fovY;
			in.sharpness = FSR1::IsSharpening(settings.sharpness) ? settings.sharpness : 0.0f;
			in.frameTimeMs = frameMs < 1.0f ? 1.0f : frameMs;
			in.reset = Console::ConsumeReset() || f.reset;

			switch (upscaler)
			{
			case UpscalerMethod::DLSS:
				ok = DLSS::Execute(context, in, settings.dlssPreset);
				errorReason = DLSS::ErrorReason();
				break;
			case UpscalerMethod::XeSS:
				ok = XeSS::Execute(context, in);
				errorReason = XeSS::ErrorReason();
				break;
			case UpscalerMethod::FSR2:
				ok = FSR2::Execute(context, in);
				errorReason = FSR2::ErrorReason();
				break;
			case UpscalerMethod::FSR3:
				ok = FSR3::Execute(context, in);
				errorReason = FSR3::ErrorReason();
				break;
			default:
				break;
			}
		}
		else
		{
			errorReason = "MOTION VECTORS FAILED";
		}

		savedState.Restore(context);

		/* FSR 2 and FSR 3 sharpen internally */
		if (ok && upscaler != UpscalerMethod::FSR2 && upscaler != UpscalerMethod::FSR3)
			FSR1::Sharpen(context, output, static_cast<UINT>(f.outputWidth), static_cast<UINT>(f.outputHeight), settings.sharpness);
	}
	else
	{
		errorReason = "NO TAA INPUTS";
		static int warned = 0;
		if (warned++ < 5)
			Utilities::Log::Write("frame %ld: TAA inputs not found (color %p, depth %p)", frameNumber, color, depth);
	}

	static bool firstFrame[7] = { true, true, true, true, true, true, true };
	if (ok && firstFrame[method])
	{
		firstFrame[method] = false;
		Utilities::Log::Write("frame %ld: first frame with upscaler %d done", frameNumber, method);
	}

	for (ID3D11ShaderResourceView* srv : bound)
		if (srv)
			srv->Release();
	return ok;
}

void Technologies::ReportTemporal(const FrameData& f, int32_t method, bool success)
{
	const Settings& settings = Console::settings;
	const char* upsamplingOff = (f.renderWidth >= f.outputWidth && backBufferWidth > static_cast<UINT>(f.outputWidth)) ? "  (UPSAMPLING OFF)" : "";
	const char* quality = QualityName(f.renderWidth, f.outputWidth);

	switch (static_cast<UpscalerMethod>(method))
	{
	case UpscalerMethod::DLSS:
		if (success)
			ReportStatus(Overlay::State::Active, "DLSS %s  %s  PRESET %s  %dX%d > %dX%d  J%d%s%s%s",
			             DLSS::VersionText()[0] ? DLSS::VersionText() : "ON", strcmp(quality, "NATIVE AA") == 0 ? "DLAA" : quality,
			             DLSS::PresetName(DLSS::ActivePreset() >= 0 ? DLSS::ActivePreset() : settings.dlssPreset), f.renderWidth, f.renderHeight,
			             f.outputWidth, f.outputHeight, settings.jitterMode, SharpnessText(), DynamicResolution::StatusText(), upsamplingOff);
		else if (DLSS::IsLoading())
			ReportStatus(Overlay::State::Disabled, "DLSS: LOADING NGX... (TAA)");
		else
			ReportStatus(Overlay::State::Error, "DLSS ERROR: %s (TAA)", errorReason[0] ? errorReason : "SEE DBDTECHNOLOGIES.LOG");
		break;

	case UpscalerMethod::XeSS:
		if (success)
			ReportStatus(Overlay::State::Active, "XESS %s  %s  %dX%d > %dX%d  J%d%s%s%s",
			             XeSS::VersionText()[0] ? XeSS::VersionText() : "ON", XeSS::QualityText(), f.renderWidth, f.renderHeight,
			             f.outputWidth, f.outputHeight, settings.jitterMode, SharpnessText(), DynamicResolution::StatusText(), upsamplingOff);
		else
			ReportStatus(Overlay::State::Error, "%s (TAA)", errorReason[0] ? errorReason : "XESS ERROR - SEE DBDTECHNOLOGIES.LOG");
		break;

	case UpscalerMethod::FSR2:
		if (success)
			ReportStatus(Overlay::State::Active, "FSR 2.2  %s  %dX%d > %dX%d  J%d%s%s%s", quality, f.renderWidth, f.renderHeight,
			             f.outputWidth, f.outputHeight, settings.jitterMode, SharpnessText(), DynamicResolution::StatusText(), upsamplingOff);
		else
			ReportStatus(Overlay::State::Error, "%s (TAA)", errorReason[0] ? errorReason : "FSR 2 ERROR - SEE DBDTECHNOLOGIES.LOG");
		break;

	case UpscalerMethod::FSR3:
		if (success)
			ReportStatus(Overlay::State::Active, "FSR 3.1  %s  %dX%d > %dX%d  J%d%s%s%s", quality, f.renderWidth, f.renderHeight,
			             f.outputWidth, f.outputHeight, settings.jitterMode, SharpnessText(), DynamicResolution::StatusText(), upsamplingOff);
		else
			ReportStatus(Overlay::State::Error, "%s (TAA)", errorReason[0] ? errorReason : "FSR 3 ERROR - SEE DBDTECHNOLOGIES.LOG");
		break;

	default:
		break;
	}
}

void Technologies::ReportEngine(const FrameData& f)
{
	if (f.renderWidth < f.outputWidth)
		ReportStatus(Overlay::State::Disabled, "TAAU (ENGINE)  %dX%d > %dX%d%s", f.renderWidth, f.renderHeight,
		             f.outputWidth, f.outputHeight, DynamicResolution::StatusText());
	else if (backBufferWidth > static_cast<UINT>(f.outputWidth))
		ReportStatus(Overlay::State::Disabled, "TAA (ENGINE)  %dX%d > %uX%u SPATIAL", f.renderWidth, f.renderHeight,
		             backBufferWidth, backBufferHeight);
	else
		ReportStatus(Overlay::State::Disabled, "TAA (ENGINE)  %dX%d NATIVE", f.renderWidth, f.renderHeight);
}

void Technologies::ReportTsr(const FrameData& f)
{
	if (temporalAlgorithmVariable == nullptr || Console::GetEngineInt(temporalAlgorithmVariable, 0) != 1)
	{
		ReportStatus(Overlay::State::Error, "TSR: R.TEMPORALAA.ALGORITHM IS NOT 1 (TAA)");
		return;
	}
	ReportStatus(Overlay::State::Active, "TSR (ENGINE GEN 5 TAAU)  %dX%d > %dX%d%s", f.renderWidth, f.renderHeight,
	             f.outputWidth, f.outputHeight, DynamicResolution::StatusText());
}

void __fastcall Technologies::HookDispatch(void* context, UINT x, UINT y, UINT z)
{
	FrameData f;
	EnterCriticalSection(&frameLock);
	f = frame;
	LeaveCriticalSection(&frameLock);

	if (f.valid == false)
	{
		originalDispatch(context, x, y, z);
		return;
	}

	ID3D11DeviceContext* d3dContext = Utilities::Memory::Read<ID3D11DeviceContext*>(context, OFF_CONTEXT_D3D11);
	ID3D11Resource* output = NativeOf(f.outputTexture);
	if (d3dContext == nullptr || output == nullptr)
	{
		originalDispatch(context, x, y, z);
		return;
	}

	/* The TAA dispatch of this frame writes to the output texture captured in AddPasses */
	ID3D11UnorderedAccessView* uav = nullptr;
	d3dContext->CSGetUnorderedAccessViews(0, 1, &uav);
	ID3D11Resource* boundOutput = nullptr;
	if (uav)
	{
		uav->GetResource(&boundOutput);
		uav->Release();
	}
	if (boundOutput)
		boundOutput->Release();   // Only the address is compared

	if (boundOutput != output)
	{
		originalDispatch(context, x, y, z);
		return;
	}

	EnterCriticalSection(&frameLock);
	frame.valid = false;
	LeaveCriticalSection(&frameLock);

	const int32_t method = Console::settings.method;
	const UpscalerMethod upscaler = static_cast<UpscalerMethod>(method);

	if (upscaler == UpscalerMethod::DLSS || upscaler == UpscalerMethod::XeSS || upscaler == UpscalerMethod::FSR2 || upscaler == UpscalerMethod::FSR3)
	{
		bool success = RunTemporalUpscaler(d3dContext, output, f, method);
		if (success == false)
			originalDispatch(context, x, y, z);
		ReportTemporal(f, method, success);
		return;
	}

	originalDispatch(context, x, y, z);

	if (upscaler != UpscalerMethod::FSR1)
	{
		ReportEngine(f);
		return;
	}

	if (f.renderWidth < f.outputWidth)
	{
		ReportStatus(Overlay::State::Error, "FSR 1 NEEDS R.TEMPORALAA.UPSAMPLING 0 (TAAU)");
		return;
	}

	spatialUpscale.armed = true;
	spatialUpscale.renderWidth = f.renderWidth;
	spatialUpscale.renderHeight = f.renderHeight;
	spatialUpscale.colorWidth = f.colorWidth;
	spatialUpscale.colorHeight = f.colorHeight;
	ReportStatus(Overlay::State::Disabled, "FSR 1: 100%% RESOLUTION - NOTHING TO UPSCALE (TAA)");
}




/* The engine upscale pass draws into a target larger than the input and reads a texture of the scene buffer size */
bool Technologies::TryFsr1(void* context)
{
	if (spatialUpscale.armed == false || Console::settings.Method() != UpscalerMethod::FSR1)
		return false;

	ID3D11DeviceContext* d3dContext = Utilities::Memory::Read<ID3D11DeviceContext*>(context, OFF_CONTEXT_D3D11);
	if (d3dContext == nullptr)
		return false;

	UINT viewportCount = 1;
	D3D11_VIEWPORT viewport = {};
	d3dContext->RSGetViewports(&viewportCount, &viewport);
	if (viewportCount == 0 || viewport.Width <= spatialUpscale.renderWidth * 1.01f || viewport.Height <= spatialUpscale.renderHeight * 1.01f)
		return false;

	ID3D11RenderTargetView* rtv = nullptr;
	d3dContext->OMGetRenderTargets(1, &rtv, nullptr);
	if (rtv == nullptr)
		return false;
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtv->GetDesc(&rtvDesc);
	ID3D11Resource* renderTarget = nullptr;
	rtv->GetResource(&renderTarget);
	rtv->Release();

	ID3D11ShaderResourceView* srvs[8] = {};
	d3dContext->PSGetShaderResources(0, 8, srvs);
	ID3D11ShaderResourceView* input = nullptr;
	for (ID3D11ShaderResourceView* srv : srvs)
	{
		if (srv == nullptr || input)
			continue;
		ID3D11Resource* resource = nullptr;
		srv->GetResource(&resource);
		D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
		if (resource)
			resource->GetType(&dimension);
		if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
		{
			D3D11_TEXTURE2D_DESC desc;
			static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
			if (static_cast<int>(desc.Width) == spatialUpscale.colorWidth && static_cast<int>(desc.Height) == spatialUpscale.colorHeight)
				input = srv;
		}
		Utilities::SafeRelease(resource);
	}

	bool done = false;
	if (input && renderTarget)
	{
		spatialUpscale.armed = false;
		done = FSR1::Upscale(d3dContext, input, static_cast<UINT>(spatialUpscale.renderWidth), static_cast<UINT>(spatialUpscale.renderHeight),
		                     static_cast<UINT>(spatialUpscale.colorWidth), static_cast<UINT>(spatialUpscale.colorHeight), renderTarget,
		                     rtvDesc.Format, viewport, Console::settings.sharpness);

		if (done)
			ReportStatus(Overlay::State::Active, "FSR 1  %s  %dX%d > %.0fX%.0f%s", QualityName(spatialUpscale.renderWidth, static_cast<int>(viewport.Width)),
			             spatialUpscale.renderWidth, spatialUpscale.renderHeight, viewport.Width, viewport.Height, SharpnessText());
		else
			ReportStatus(Overlay::State::Error, "FSR 1 ERROR - SEE DBDTECHNOLOGIES.LOG");

		static bool logged = false;
		if (logged == false)
		{
			logged = true;
			Utilities::Log::Write("FSR 1: spatial upscale found (%dx%d -> %.0fx%.0f, target format %u) - %s",
			                      spatialUpscale.renderWidth, spatialUpscale.renderHeight, viewport.Width, viewport.Height,
			                      static_cast<unsigned>(rtvDesc.Format), done ? "replaced" : "left to the game");
		}
	}

	for (ID3D11ShaderResourceView* srv : srvs)
		if (srv)
			srv->Release();
	Utilities::SafeRelease(renderTarget);
	return done;
}

void __fastcall Technologies::HookDraw(void* context, UINT baseVertex, UINT numPrimitives, UINT numInstances)
{
	if (spatialUpscale.armed && TryFsr1(context))
		return;
	originalDraw(context, baseVertex, numPrimitives, numInstances);
}

void __fastcall Technologies::HookDrawIndexed(void* context, void* indexBuffer, INT baseVertex, UINT firstInstance, UINT numVertices,
                                              UINT startIndex, UINT numPrimitives, UINT numInstances)
{
	if (spatialUpscale.armed && TryFsr1(context))
		return;
	originalDrawIndexed(context, indexBuffer, baseVertex, firstInstance, numVertices, startIndex, numPrimitives, numInstances);
}




void __fastcall Technologies::HookEndDrawing(void* context, void* viewport, bool present, bool lockToVsync)
{
	ID3D11DeviceContext* d3dContext = Utilities::Memory::Read<ID3D11DeviceContext*>(context, OFF_CONTEXT_D3D11);

	static bool deviceShared = false;
	if (deviceShared == false && d3dContext)
	{
		ID3D11Device* d3dDevice = nullptr;
		d3dContext->GetDevice(&d3dDevice);
		if (d3dDevice)
		{
			LowLatency::SetDevice(d3dDevice);
			d3dDevice->Release();
			deviceShared = true;
		}
	}

	if (spatialUpscale.armed && Console::settings.Method() == UpscalerMethod::FSR1 && spatialUpscale.renderWidth < static_cast<int>(backBufferWidth))
	{
		static int notFound = 0;
		if (notFound++ == 120)
			Utilities::Log::Write("FSR 1: spatial upscale draw not found (input %dx%d in %dx%d buffer, screen %ux%u)",
			                      spatialUpscale.renderWidth, spatialUpscale.renderHeight, spatialUpscale.colorWidth, spatialUpscale.colorHeight,
			                      backBufferWidth, backBufferHeight);
		ReportStatus(Overlay::State::Error, "FSR 1: UPSCALE PASS NOT FOUND (GAME UPSCALE)");
	}
	spatialUpscale.armed = false;

	if (present && d3dContext && Utilities::Memory::Read<uintptr_t>(viewport) == base + RVA_VIEWPORT_VTABLE)
	{
		void* backBufferTexture = Utilities::Memory::Read<void*>(viewport, OFF_VIEWPORT_BACKBUFFER);
		ID3D11Resource* backBuffer = Utilities::Memory::Read<ID3D11Resource*>(backBufferTexture, OFF_RHI_NATIVE);

		if (backBuffer)
		{
			D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
			backBuffer->GetType(&dimension);
			if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
			{
				D3D11_TEXTURE2D_DESC desc;
				static_cast<ID3D11Texture2D*>(backBuffer)->GetDesc(&desc);
				backBufferWidth = desc.Width;
				backBufferHeight = desc.Height;

				Overlay::HdrMode hdr = DetectHdrMode(desc.Format);
				static int lastHdr = -1;
				if (static_cast<int>(hdr) != lastHdr)
				{
					static const char* names[] = { "SDR", "HDR10 (PQ)", "HDR scRGB" };
					Utilities::Log::Write("output: %s, back buffer %ux%u format %u", names[static_cast<int>(hdr)], desc.Width, desc.Height,
					                      static_cast<unsigned>(desc.Format));
					lastHdr = static_cast<int>(hdr);
				}
				Overlay::SetHdrMode(hdr);
			}
			if (Console::settings.overlay)
				Overlay::Draw(d3dContext, backBuffer);
		}
	}

	if (present)
		LowLatency::PresentStart();
	originalEndDrawing(context, viewport, present, lockToVsync);
	if (present)
		LowLatency::PresentEnd();
}




bool Technologies::InstallHooks()
{
	using Utilities::Memory;

	uintptr_t* upscalerVtable = reinterpret_cast<uintptr_t*>(base + RVA_UPSCALER_VTABLE);
	auto slot = [](uintptr_t address) { return Memory::Read<uintptr_t>(reinterpret_cast<void*>(address)); };

	bool matches =
		Memory::CheckBytes(base + RVA_ADDPASSES, SIGNATURE_ADDPASSES, sizeof(SIGNATURE_ADDPASSES)) &&
		Memory::CheckBytes(base + RVA_DISPATCH, SIGNATURE_DISPATCH, sizeof(SIGNATURE_DISPATCH)) &&
		Memory::CheckBytes(base + RVA_END_DRAWING, SIGNATURE_END_DRAWING, sizeof(SIGNATURE_END_DRAWING)) &&
		slot(reinterpret_cast<uintptr_t>(&upscalerVtable[1])) == base + RVA_GETDEBUGNAME &&
		slot(reinterpret_cast<uintptr_t>(&upscalerVtable[2])) == base + RVA_ADDPASSES &&
		slot(base + RVA_DISPATCH_SLOT) == base + RVA_DISPATCH &&
		slot(base + RVA_END_DRAWING_SLOT) == base + RVA_END_DRAWING;

	if (matches == false)
	{
		Utilities::Log::Write("this exe does not match Dead by Daylight 6.7.2 - mod disabled");
		return false;
	}

	if (Memory::PatchPointer(reinterpret_cast<uintptr_t>(&upscalerVtable[2]), reinterpret_cast<void*>(&HookAddPasses),
	                         reinterpret_cast<void**>(&originalAddPasses)) == false ||
	    Memory::PatchPointer(base + RVA_DISPATCH_SLOT, reinterpret_cast<void*>(&HookDispatch),
	                         reinterpret_cast<void**>(&originalDispatch)) == false ||
	    Memory::PatchPointer(base + RVA_END_DRAWING_SLOT, reinterpret_cast<void*>(&HookEndDrawing),
	                         reinterpret_cast<void**>(&originalEndDrawing)) == false)
	{
		Utilities::Log::Write("failed to patch vtables");
		return false;
	}

	/* Optional: without the draw hooks only FSR 1 is unavailable */
	bool drawMatches =
		Memory::CheckBytes(base + RVA_DRAW, SIGNATURE_DRAW, sizeof(SIGNATURE_DRAW)) &&
		Memory::CheckBytes(base + RVA_DRAW_INDEXED, SIGNATURE_DRAW_INDEXED, sizeof(SIGNATURE_DRAW_INDEXED)) &&
		slot(base + RVA_DRAW_SLOT) == base + RVA_DRAW &&
		slot(base + RVA_DRAW_INDEXED_SLOT) == base + RVA_DRAW_INDEXED;
	if (drawMatches &&
	    Memory::PatchPointer(base + RVA_DRAW_SLOT, reinterpret_cast<void*>(&HookDraw), reinterpret_cast<void**>(&originalDraw)) &&
	    Memory::PatchPointer(base + RVA_DRAW_INDEXED_SLOT, reinterpret_cast<void*>(&HookDrawIndexed), reinterpret_cast<void**>(&originalDrawIndexed)))
		Utilities::Log::Write("draw hooks installed (FSR 1)");
	else
		Utilities::Log::Write("draw hooks not installed - FSR 1 unavailable");

	return true;
}

void Technologies::Init()
{
	Utilities::Log::Open(Utilities::Module::GetDirectory() + L"DBDTechnologies.log");
	Utilities::Log::Write("Starting initialization sequence...");
	InitializeCriticalSection(&frameLock);

	base = Utilities::Module::GetBase();
	Utilities::Log::Write("exe base %p", reinterpret_cast<void*>(base));

	if (InstallHooks() == false)
		return;

	Overlay::SetExtraTextProvider(&LowLatency::StatusText);
	LowLatency::gameThreadCallback = &GameThreadUpdate;
	LowLatency::InstallHooks(base);
	Utilities::Log::Write("hooks installed. Console: r.Upscaler.*, r.DLSS.*, r.LowLatency.* (defaults: everything off)");
}
