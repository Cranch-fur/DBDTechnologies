#include "Overlay.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")






/* Шрифт 5x7: 7 строк по 5 бит, старший бит — левый пиксель */
struct Glyph
{
	char c;
	uint8_t rows[7];
};

static const Glyph font[] =
{
	{' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
	{'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
	{'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
	{'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
	{'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
	{'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
	{'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
	{'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
	{'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
	{'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
	{'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
	{'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
	{'W', {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}}, {'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
	{'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
	{'0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
	{'2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
	{'4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
	{'6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
	{'8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
	{'>', {0x08,0x04,0x02,0x01,0x02,0x04,0x08}}, {'%', {0x18,0x19,0x02,0x04,0x08,0x13,0x03}},
	{'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}}, {':', {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}},
	{'-', {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}}, {'(', {0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
	{')', {0x08,0x04,0x02,0x02,0x02,0x04,0x08}}, {'/', {0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
	{'_', {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}}, {'?', {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}},
};

static const char* shaderSource = R"(
cbuffer Constants : register(b0)
{
    uint2  MaskSize;
    uint   Scale;
    uint   Pad;
    float4 TextColor;
    float4 BackgroundColor;
};
Texture2D<float>    Mask   : register(t0);
Texture2D<float4>   Source : register(t1);
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID)
{
    uint2 m = p / Scale;
    if (m.x >= MaskSize.x || m.y >= MaskSize.y) return;
    float4 under = Source[p];
    Output[p] = Mask[m] > 0.5 ? TextColor : float4(lerp(under.rgb, BackgroundColor.rgb, BackgroundColor.a), under.a);
}
)";




void Overlay::SetStatus(State state, const char* newText)
{
	statusState = state;
	strncpy_s(statusText, newText ? newText : "", _TRUNCATE);
	statusTick = GetTickCount64();
}

void Overlay::SetExtraTextProvider(ExtraTextProvider provider)
{
	extraTextProvider = provider;
}

bool Overlay::Initialize(ID3D11Device* device)
{
	initTried = true;

	ID3DBlob* code = nullptr;
	ID3DBlob* errors = nullptr;
	HRESULT result = D3DCompile(shaderSource, strlen(shaderSource), "DBDTechnologies_Overlay", nullptr, nullptr, "main", "cs_5_0",
	                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
	if (FAILED(result))
	{
		Utilities::Log::Write("Overlay: shader error: %s", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
		Utilities::SafeRelease(errors);
		return false;
	}

	result = device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
	Utilities::SafeRelease(code);
	Utilities::SafeRelease(errors);
	if (FAILED(result))
		return false;

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(Constants);
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(device->CreateBuffer(&bufferDesc, nullptr, &constants)))
		return false;

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width = MaskWidth;
	textureDesc.Height = MaskHeight;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Format = DXGI_FORMAT_R8_UNORM;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&textureDesc, nullptr, &mask)) ||
	    FAILED(device->CreateShaderResourceView(mask, nullptr, &maskSrv)))
		return false;

	return true;
}

bool Overlay::EnsureArea(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format)
{
	if (source && width <= areaWidth && height <= areaHeight && format == areaFormat)
		return true;

	Utilities::SafeRelease(sourceSrv);
	Utilities::SafeRelease(source);
	Utilities::SafeRelease(destinationUav);
	Utilities::SafeRelease(destination);

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.Format = format;
	desc.Usage = D3D11_USAGE_DEFAULT;

	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &source)) ||
	    FAILED(device->CreateShaderResourceView(source, nullptr, &sourceSrv)))
		return false;

	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &destination)) ||
	    FAILED(device->CreateUnorderedAccessView(destination, nullptr, &destinationUav)))
	{
		Utilities::Log::Write("Overlay: back buffer format %u does not support UAV, overlay disabled", static_cast<unsigned>(format));
		return false;
	}

	areaWidth = width;
	areaHeight = height;
	areaFormat = format;
	return true;
}

void Overlay::Rasterize(ID3D11DeviceContext* context, const char* newText)
{
	static uint8_t pixels[MaskHeight][MaskWidth];
	memset(pixels, 0, sizeof(pixels));

	int count = 0;
	for (; newText[count] && count < MaxChars; ++count)
	{
		char c = newText[count];
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');

		const Glyph* glyph = &font[0];
		for (const Glyph& candidate : font)
		{
			if (candidate.c == c)
			{
				glyph = &candidate;
				break;
			}
		}

		for (int y = 0; y < 7; ++y)
			for (int x = 0; x < 5; ++x)
				if (glyph->rows[y] & (0x10 >> x))
					pixels[1 + y][1 + count * 6 + x] = 255;
	}

	context->UpdateSubresource(mask, 0, nullptr, pixels, MaskWidth, sizeof(pixels));
	textLength = count;
}

void Overlay::BuildText(char* buffer, size_t size, UINT screenWidth, State& colorState)
{
	(void)screenWidth;
	State state = statusState;

	/* Если апскейлер давно не встречался — 3D-сцены нет (загрузка, меню без мира) */
	if (GetTickCount64() - statusTick > 1000)
		state = State::Idle;
	colorState = state;

	if (state == State::Idle)
		snprintf(buffer, size, "UPSCALER READY - NO 3D SCENE");
	else
		snprintf(buffer, size, "%s", statusText);

	const char* extra = extraTextProvider ? extraTextProvider() : "";
	if (extra && extra[0])
	{
		size_t length = strnlen(buffer, size);
		if (length + 2 < size)
			snprintf(buffer + length, size - length, "  %s", extra);
	}

	const char* hdr = hdrMode == HdrMode::PQ ? "HDR10" : hdrMode == HdrMode::scRGB ? "HDR SCRGB" : "";
	if (hdr[0])
	{
		size_t length = strnlen(buffer, size);
		if (length + 2 < size)
			snprintf(buffer + length, size - length, "  %s", hdr);
	}
}

/*
	Цвета надписи заданы как обычные sRGB-значения. В HDR их нужно перевести в пространство back buffer,
	иначе "1.0" в PQ — это 10 000 нит (слепит), а в scRGB — 80 нит (тускло). Белый надписи ~200 нит.
*/
void Overlay::EncodeColor(const float srgb[4], float out[4])
{
	const float paperWhiteNits = 200.0f;

	float linear[3];
	for (int i = 0; i < 3; ++i)
	{
		float c = srgb[i];
		linear[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
	}
	out[3] = srgb[3];

	if (hdrMode == HdrMode::scRGB)
	{
		for (int i = 0; i < 3; ++i)
			out[i] = linear[i] * paperWhiteNits / 80.0f;
	}
	else if (hdrMode == HdrMode::PQ)
	{
		/* Rec.709 -> Rec.2020, затем ST.2084 */
		const float m[3][3] =
		{
			{ 0.6274f, 0.3293f, 0.0433f },
			{ 0.0691f, 0.9195f, 0.0114f },
			{ 0.0164f, 0.0880f, 0.8956f },
		};
		for (int i = 0; i < 3; ++i)
		{
			float c = m[i][0] * linear[0] + m[i][1] * linear[1] + m[i][2] * linear[2];
			float y = c * paperWhiteNits / 10000.0f;
			float yp = powf(y > 0.0f ? y : 0.0f, 0.1593017578125f);
			out[i] = powf((0.8359375f + 18.8515625f * yp) / (1.0f + 18.6875f * yp), 78.84375f);
		}
	}
	else
	{
		for (int i = 0; i < 3; ++i)
			out[i] = srgb[i];
	}
}

void Overlay::Draw(ID3D11DeviceContext* context, ID3D11Resource* backBuffer)
{
	if (context == nullptr || backBuffer == nullptr)
		return;

	D3D11_RESOURCE_DIMENSION dimension;
	backBuffer->GetType(&dimension);
	if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
		return;

	D3D11_TEXTURE2D_DESC backBufferDesc;
	static_cast<ID3D11Texture2D*>(backBuffer)->GetDesc(&backBufferDesc);

	ID3D11Device* device = nullptr;
	context->GetDevice(&device);
	if (device == nullptr)
		return;

	if (initTried == false)
		ready = Initialize(device);

	State colorState = State::Idle;
	char newText[MaxChars + 1];
	BuildText(newText, sizeof(newText), backBufferDesc.Width, colorState);

	/* Размер точки шрифта в пикселях экрана, целый (без размытия): 1080p -> 2, 1440p -> 3, 2160p -> 4 */
	UINT scale = (backBufferDesc.Height + 250) / 500;
	if (scale < 1) scale = 1;
	int length = static_cast<int>(strnlen(newText, MaxChars));
	while (scale > 1 && static_cast<UINT>(length * 6 + 1) * scale + 24 > backBufferDesc.Width)
		--scale;

	const UINT margin = 2 * scale;
	UINT width = static_cast<UINT>(length * 6 + 1) * scale;
	UINT height = MaskHeight * scale;
	if (width + margin > backBufferDesc.Width) width = backBufferDesc.Width - margin;
	if (height + margin > backBufferDesc.Height) height = backBufferDesc.Height - margin;

	bool ok = ready && EnsureArea(device, width, height, backBufferDesc.Format);
	device->Release();
	if (ok == false)
		return;

	if (strcmp(newText, text) != 0)
	{
		strcpy_s(text, newText);
		Rasterize(context, newText);
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return;

	Constants c = {};
	c.maskSize[0] = static_cast<uint32_t>(textLength * 6 + 1);
	c.maskSize[1] = MaskHeight;
	c.scale = scale;
	const float green[4] = { 0.35f, 1.0f, 0.35f, 1.0f };
	const float white[4] = { 0.95f, 0.95f, 0.95f, 1.0f };
	const float red[4]   = { 1.0f, 0.3f, 0.25f, 1.0f };
	const float gray[4]  = { 0.65f, 0.65f, 0.65f, 1.0f };
	const float* color = colorState == State::Active ? green : colorState == State::Disabled ? white : colorState == State::Error ? red : gray;
	EncodeColor(color, c.textColor);
	c.backgroundColor[3] = 0.6f;   // полупрозрачная чёрная подложка
	memcpy(mapped.pData, &c, sizeof(c));
	context->Unmap(constants, 0);

	D3D11_BOX box = { margin, margin, 0, margin + width, margin + height, 1 };
	context->CopySubresourceRegion(source, 0, 0, 0, 0, backBuffer, 0, &box);

	savedState.Save(context);
	Utilities::ComputeState::Unbind(context);

	ID3D11ShaderResourceView* srv[2] = { maskSrv, sourceSrv };
	context->CSSetShader(shader, nullptr, 0);
	context->CSSetConstantBuffers(0, 1, &constants);
	context->CSSetShaderResources(0, 2, srv);
	context->CSSetUnorderedAccessViews(0, 1, &destinationUav, nullptr);
	context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

	Utilities::ComputeState::Unbind(context);
	savedState.Restore(context);

	D3D11_BOX back = { 0, 0, 0, width, height, 1 };
	context->CopySubresourceRegion(backBuffer, 0, margin, margin, 0, destination, 0, &back);
}
