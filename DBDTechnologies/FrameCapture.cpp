#include "FrameCapture.h"
#include "Console.h"

#include <share.h>






const char* FrameCapture::FormatName(DXGI_FORMAT format)
{
	switch (format)
	{
		case DXGI_FORMAT_UNKNOWN: return "UNKNOWN";
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return "RGBA32F";
		case DXGI_FORMAT_R32G32B32A32_UINT: return "RGBA32U";
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: return "RGBA16_TYPELESS";
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return "RGBA16F";
		case DXGI_FORMAT_R16G16B16A16_UNORM: return "RGBA16";
		case DXGI_FORMAT_R16G16B16A16_UINT: return "RGBA16U";
		case DXGI_FORMAT_R32G32_FLOAT: return "RG32F";
		case DXGI_FORMAT_R32G8X24_TYPELESS: return "R32G8X24_TYPELESS";
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return "D32S8";
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return "R32_X8X24 (depth)";
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: return "X32_G8X24 (stencil)";
		case DXGI_FORMAT_R10G10B10A2_TYPELESS: return "RGB10A2_TYPELESS";
		case DXGI_FORMAT_R10G10B10A2_UNORM: return "RGB10A2";
		case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10F";
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: return "RGBA8_TYPELESS";
		case DXGI_FORMAT_R8G8B8A8_UNORM: return "RGBA8";
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "RGBA8_SRGB";
		case DXGI_FORMAT_R8G8B8A8_UINT: return "RGBA8U";
		case DXGI_FORMAT_R16G16_TYPELESS: return "RG16_TYPELESS";
		case DXGI_FORMAT_R16G16_FLOAT: return "RG16F";
		case DXGI_FORMAT_R16G16_UNORM: return "RG16";
		case DXGI_FORMAT_R32_TYPELESS: return "R32_TYPELESS";
		case DXGI_FORMAT_D32_FLOAT: return "D32F";
		case DXGI_FORMAT_R32_FLOAT: return "R32F";
		case DXGI_FORMAT_R32_UINT: return "R32U";
		case DXGI_FORMAT_R24G8_TYPELESS: return "R24G8_TYPELESS";
		case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24S8";
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return "R24_X8 (depth)";
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT: return "X24_G8 (stencil)";
		case DXGI_FORMAT_R8G8_UNORM: return "RG8";
		case DXGI_FORMAT_R16_TYPELESS: return "R16_TYPELESS";
		case DXGI_FORMAT_R16_FLOAT: return "R16F";
		case DXGI_FORMAT_R16_UNORM: return "R16";
		case DXGI_FORMAT_R16_UINT: return "R16U";
		case DXGI_FORMAT_R8_UNORM: return "R8";
		case DXGI_FORMAT_R8_UINT: return "R8U";
		case DXGI_FORMAT_BC1_UNORM: return "BC1";
		case DXGI_FORMAT_BC1_UNORM_SRGB: return "BC1_SRGB";
		case DXGI_FORMAT_BC3_UNORM: return "BC3";
		case DXGI_FORMAT_BC3_UNORM_SRGB: return "BC3_SRGB";
		case DXGI_FORMAT_BC4_UNORM: return "BC4";
		case DXGI_FORMAT_BC5_UNORM: return "BC5";
		case DXGI_FORMAT_BC6H_UF16: return "BC6H";
		case DXGI_FORMAT_BC7_UNORM: return "BC7";
		case DXGI_FORMAT_BC7_UNORM_SRGB: return "BC7_SRGB";
		case DXGI_FORMAT_B8G8R8A8_UNORM: return "BGRA8";
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: return "BGRA8_TYPELESS";
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "BGRA8_SRGB";
		default:
		{
			static char text[16];   // RHI thread only
			snprintf(text, sizeof(text), "fmt%u", static_cast<unsigned>(format));
			return text;
		}
	}
}

int FrameCapture::Append(char* buffer, size_t size, int used, const char* format, ...)
{
	if (used < 0 || static_cast<size_t>(used) >= size - 1)
		return used;
	va_list args;
	va_start(args, format);
	int written = vsnprintf(buffer + used, size - used, format, args);
	va_end(args);
	if (written < 0)
		return used;
	used += written;
	return static_cast<size_t>(used) >= size ? static_cast<int>(size - 1) : used;
}




FrameCapture::TextureInfo FrameCapture::Inspect(ID3D11Resource* resource)
{
	TextureInfo info;
	if (resource == nullptr)
		return info;

	resource->GetType(&info.dimension);
	switch (info.dimension)
	{
		case D3D11_RESOURCE_DIMENSION_BUFFER:
		{
			D3D11_BUFFER_DESC desc;
			static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
			info.byteWidth = desc.ByteWidth;
			info.valid = true;
			break;
		}
		case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
		{
			D3D11_TEXTURE1D_DESC desc;
			static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
			info.width = desc.Width;
			info.height = 1;
			info.mips = desc.MipLevels;
			info.arraySize = desc.ArraySize;
			info.format = desc.Format;
			info.valid = true;
			break;
		}
		case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
		{
			D3D11_TEXTURE2D_DESC desc;
			static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
			info.width = desc.Width;
			info.height = desc.Height;
			info.mips = desc.MipLevels;
			info.arraySize = desc.ArraySize;
			info.format = desc.Format;
			info.cube = (desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;
			info.valid = true;
			break;
		}
		case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
		{
			D3D11_TEXTURE3D_DESC desc;
			static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
			info.width = desc.Width;
			info.height = desc.Height;
			info.depth = desc.Depth;
			info.mips = desc.MipLevels;
			info.arraySize = 1;
			info.format = desc.Format;
			info.valid = true;
			break;
		}
		default:
			break;
	}
	return info;
}

/* UE 4.27 builds the furthest HZB as PF_R16F (closest + furthest as PF_G16R16F) with a full mip chain at about half the view size */
bool FrameCapture::IsHzb(const TextureInfo& info)
{
	if (info.valid == false || info.dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D || info.cube || info.arraySize != 1 || info.mips < 4)
		return false;
	switch (info.format)
	{
		case DXGI_FORMAT_R16_FLOAT:
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R16G16_TYPELESS:
			return true;
		default:
			return false;
	}
}

/* Writes "@address WxH[xD] FORMAT mN" so the same texture can be followed from pass to pass */
int FrameCapture::AppendResource(char* buffer, size_t size, int used, ID3D11Resource* resource, DXGI_FORMAT viewFormat)
{
	TextureInfo info = Inspect(resource);
	if (info.valid == false)
		return Append(buffer, size, used, "@%p ?", static_cast<void*>(resource));

	if (info.dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
		return Append(buffer, size, used, "@%p buf %uB", static_cast<void*>(resource), info.byteWidth);

	used = Append(buffer, size, used, "@%p %ux%u", static_cast<void*>(resource), info.width, info.height);
	if (info.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
		used = Append(buffer, size, used, "x%u 3D", info.depth);
	else if (info.arraySize > 1)
		used = Append(buffer, size, used, " [%u]", info.arraySize);
	if (info.cube)
		used = Append(buffer, size, used, " cube");
	used = Append(buffer, size, used, " %s", FormatName(info.format));
	if (viewFormat != DXGI_FORMAT_UNKNOWN && viewFormat != info.format)
		used = Append(buffer, size, used, "/%s", FormatName(viewFormat));
	if (info.mips > 1)
		used = Append(buffer, size, used, " m%u", info.mips);
	return used;
}

int FrameCapture::AppendShaderResources(char* buffer, size_t size, int used, ID3D11ShaderResourceView* const* srvs, UINT count, bool& hasHzb)
{
	for (UINT slot = 0; slot < count; ++slot)
	{
		ID3D11ShaderResourceView* srv = srvs[slot];
		if (srv == nullptr)
			continue;
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		srv->GetDesc(&desc);
		ID3D11Resource* resource = nullptr;
		srv->GetResource(&resource);

		used = Append(buffer, size, used, "\n      t%-2u ", slot);
		used = AppendResource(buffer, size, used, resource, desc.Format);
		if (IsHzb(Inspect(resource)))
		{
			hasHzb = true;
			used = Append(buffer, size, used, "  <HZB?>");
		}
		Utilities::SafeRelease(resource);
	}
	return used;
}

int FrameCapture::AppendConstantBuffers(char* buffer, size_t size, int used, ID3D11Buffer* const* buffers, UINT count)
{
	bool any = false;
	for (UINT slot = 0; slot < count; ++slot)
	{
		if (buffers[slot] == nullptr)
			continue;
		D3D11_BUFFER_DESC desc;
		buffers[slot]->GetDesc(&desc);
		used = Append(buffer, size, used, "%s b%u:%uB", any ? "" : "\n      cb", slot, desc.ByteWidth);
		any = true;
	}
	return used;
}




void FrameCapture::Begin(UINT backBufferWidth, UINT backBufferHeight, LONG frameNumber)
{
	++captureNumber;
	wchar_t name[64];
	swprintf(name, _countof(name), L"DBDTechnologies_Capture_%d.log", captureNumber);
	std::wstring path = Utilities::Module::GetDirectory() + name;
	file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
	if (file == nullptr)
	{
		Utilities::Log::Write("capture: cannot create %ls", path.c_str());
		return;
	}
	setvbuf(file, nullptr, _IOFBF, 1 << 20);

	screenWidth = backBufferWidth;
	screenHeight = backBufferHeight;
	eventIndex = loggedEvents = skippedEvents = hzbEvents = ssrCandidates = 0;
	active = true;

	/* Engine values that decide which reflection passes run this frame */
	static const wchar_t* names[] = { L"r.SSR.Quality", L"r.SSR.Temporal", L"r.SSR.MaxRoughness", L"r.SSGI.Enable",
	                                  L"r.ScreenPercentage", L"r.TemporalAA.Upsampling", L"r.HZBOcclusion" };
	static void* variables[_countof(names)] = {};
	static bool searched = false;
	if (searched == false)
	{
		for (size_t i = 0; i < _countof(names); ++i)
			variables[i] = Console::FindEngineVariable(names[i]);
		searched = true;
	}

	fprintf(file, "DBDTechnologies frame capture %d, mode %d (%s), frame %ld, back buffer %ux%u\n", captureNumber, mode,
	        mode == 2 ? "every draw" : "full-screen draws only", frameNumber, backBufferWidth, backBufferHeight);
	for (size_t i = 0; i < _countof(names); ++i)
		fprintf(file, "  %ls = %d%s\n", names[i], variables[i] ? Console::GetEngineInt(variables[i], -1) : -1,
		        variables[i] ? "" : " (not found)");
	fprintf(file, "  (float variables are shown truncated to int)\n");
	if (drawHooks == false)
		fprintf(file, "  WARNING: draw hooks are not installed - only dispatches are captured\n");
	fprintf(file, "\nEvents are listed after the engine issued them. Texture: @address WxH FORMAT[/view format] mMips\n\n");

	Utilities::Log::Write("capture %d started (mode %d) -> %ls", captureNumber, mode, path.c_str());
}

void FrameCapture::End()
{
	active = false;
	if (file == nullptr)
		return;
	fprintf(file, "\nEnd of frame. %u events, %u logged, %u filtered out, %u with an HZB-like input, %u SSR candidates\n",
	        eventIndex, loggedEvents, skippedEvents, hzbEvents, ssrCandidates);
	fclose(file);
	file = nullptr;
	Utilities::Log::Write("capture %d finished: %u events, %u logged, %u SSR candidates", captureNumber, eventIndex, loggedEvents, ssrCandidates);
}

void FrameCapture::OnPresent(ID3D11DeviceContext* context, UINT backBufferWidth, UINT backBufferHeight, LONG frameNumber)
{
	(void)context;
	if (active)
		End();

	LONG requested = InterlockedExchange(reinterpret_cast<volatile LONG*>(&request), 0);
	if (requested == 1 || requested == 2)
	{
		mode = static_cast<int>(requested);
		Begin(backBufferWidth, backBufferHeight, frameNumber);
	}
	else if (requested != 0)
		Utilities::Log::Write("r.VoxelReflections.Capture %ld is not supported (1 = full-screen draws, 2 = every draw)", requested);
}




void FrameCapture::OnDraw(ID3D11DeviceContext* context, const char* kind, UINT numPrimitives, UINT numInstances, bool replaced)
{
	if (active == false || file == nullptr || context == nullptr)
		return;
	const UINT index = eventIndex++;

	UINT viewportCount = 1;
	D3D11_VIEWPORT viewport = {};
	context->RSGetViewports(&viewportCount, &viewport);
	if (viewportCount == 0)
		viewport = {};

	/* Mode 1: SSR, lighting and post process passes are one or two triangles covering most of the view */
	const bool fullScreen = numPrimitives <= 2 && numInstances <= 1 &&
	                        viewport.Width >= screenWidth * 0.25f && viewport.Height >= screenHeight * 0.25f;
	if (mode == 1 && fullScreen == false)
	{
		++skippedEvents;
		return;
	}

	ID3D11RenderTargetView* rtvs[RtvSlots] = {};
	ID3D11DepthStencilView* dsv = nullptr;
	context->OMGetRenderTargets(RtvSlots, rtvs, &dsv);
	ID3D11ShaderResourceView* srvs[SrvSlots] = {};
	context->PSGetShaderResources(0, SrvSlots, srvs);
	ID3D11Buffer* cbs[CbSlots] = {};
	context->PSGetConstantBuffers(0, CbSlots, cbs);
	ID3D11PixelShader* ps = nullptr;
	context->PSGetShader(&ps, nullptr, nullptr);
	ID3D11VertexShader* vs = nullptr;
	context->VSGetShader(&vs, nullptr, nullptr);

	static char line[16384];
	int used = 0;
	used = Append(line, sizeof(line), used, "#%04u %s prims=%u inst=%u%s | VP %.0f,%.0f %.0fx%.0f | VS %p PS %p", index, kind, numPrimitives,
	              numInstances, replaced ? " (replaced by the mod)" : "", viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height,
	              static_cast<void*>(vs), static_cast<void*>(ps));

	bool fp16Target = false;
	for (UINT slot = 0; slot < RtvSlots; ++slot)
	{
		if (rtvs[slot] == nullptr)
			continue;
		D3D11_RENDER_TARGET_VIEW_DESC desc = {};
		rtvs[slot]->GetDesc(&desc);
		ID3D11Resource* resource = nullptr;
		rtvs[slot]->GetResource(&resource);
		TextureInfo info = Inspect(resource);
		if (info.valid && (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || info.format == DXGI_FORMAT_R16G16B16A16_FLOAT) &&
		    info.width + 1.0f >= viewport.Width && info.height + 1.0f >= viewport.Height)
			fp16Target = true;
		used = Append(line, sizeof(line), used, "\n      RT%u ", slot);
		used = AppendResource(line, sizeof(line), used, resource, desc.Format);
		Utilities::SafeRelease(resource);
	}
	if (dsv)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
		dsv->GetDesc(&desc);
		ID3D11Resource* resource = nullptr;
		dsv->GetResource(&resource);
		used = Append(line, sizeof(line), used, "\n      DS  ");
		used = AppendResource(line, sizeof(line), used, resource, desc.Format);
		if (desc.Flags & (D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL))
			used = Append(line, sizeof(line), used, " (read-only)");
		Utilities::SafeRelease(resource);
	}

	bool hasHzb = false;
	used = AppendShaderResources(line, sizeof(line), used, srvs, SrvSlots, hasHzb);
	used = AppendConstantBuffers(line, sizeof(line), used, cbs, CbSlots);

	const bool candidate = hasHzb && fp16Target && fullScreen;
	if (hasHzb)
		++hzbEvents;
	if (candidate)
		++ssrCandidates;
	fprintf(file, "%s%s\n", candidate ? "[SSR?] " : (hasHzb ? "[HZB] " : ""), line);
	++loggedEvents;

	for (ID3D11RenderTargetView*& rtv : rtvs)
		Utilities::SafeRelease(rtv);
	Utilities::SafeRelease(dsv);
	for (ID3D11ShaderResourceView*& srv : srvs)
		Utilities::SafeRelease(srv);
	for (ID3D11Buffer*& cb : cbs)
		Utilities::SafeRelease(cb);
	Utilities::SafeRelease(ps);
	Utilities::SafeRelease(vs);
}

void FrameCapture::OnDispatch(ID3D11DeviceContext* context, UINT x, UINT y, UINT z)
{
	if (active == false || file == nullptr || context == nullptr)
		return;
	const UINT index = eventIndex++;

	ID3D11ShaderResourceView* srvs[SrvSlots] = {};
	context->CSGetShaderResources(0, SrvSlots, srvs);
	ID3D11UnorderedAccessView* uavs[UavSlots] = {};
	context->CSGetUnorderedAccessViews(0, UavSlots, uavs);
	ID3D11Buffer* cbs[CbSlots] = {};
	context->CSGetConstantBuffers(0, CbSlots, cbs);
	ID3D11ComputeShader* cs = nullptr;
	context->CSGetShader(&cs, nullptr, nullptr);

	static char line[16384];
	int used = 0;
	used = Append(line, sizeof(line), used, "#%04u Dispatch %ux%ux%u | CS %p", index, x, y, z, static_cast<void*>(cs));

	for (UINT slot = 0; slot < UavSlots; ++slot)
	{
		if (uavs[slot] == nullptr)
			continue;
		D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
		uavs[slot]->GetDesc(&desc);
		ID3D11Resource* resource = nullptr;
		uavs[slot]->GetResource(&resource);
		used = Append(line, sizeof(line), used, "\n      u%u  ", slot);
		used = AppendResource(line, sizeof(line), used, resource, desc.Format);
		if (desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D && desc.Texture2D.MipSlice > 0)
			used = Append(line, sizeof(line), used, " (mip %u)", desc.Texture2D.MipSlice);
		Utilities::SafeRelease(resource);
	}

	bool hasHzb = false;
	used = AppendShaderResources(line, sizeof(line), used, srvs, SrvSlots, hasHzb);
	used = AppendConstantBuffers(line, sizeof(line), used, cbs, CbSlots);

	if (hasHzb)
		++hzbEvents;
	fprintf(file, "%s%s\n", hasHzb ? "[HZB] " : "", line);
	++loggedEvents;

	for (ID3D11ShaderResourceView*& srv : srvs)
		Utilities::SafeRelease(srv);
	for (ID3D11UnorderedAccessView*& uav : uavs)
		Utilities::SafeRelease(uav);
	for (ID3D11Buffer*& cb : cbs)
		Utilities::SafeRelease(cb);
	Utilities::SafeRelease(cs);
}
