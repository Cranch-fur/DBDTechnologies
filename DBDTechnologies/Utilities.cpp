#include "Utilities.h"
#include <share.h>






/* ------------------------------------------------------------------ Log */

void Utilities::Log::Open(const std::wstring& path)
{
	if (lockReady == false)
	{
		InitializeCriticalSection(&lock);
		lockReady = true;
	}

	/* _SH_DENYWR: лог можно открыть и прочитать, пока игра запущена */
	file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
}

void Utilities::Log::Write(const char* format, ...)
{
	if (lockReady == false)
		return;

	char message[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);

	EnterCriticalSection(&lock);

	printf("[DBDTechnologies] %s\n", message);

	if (file)
	{
		SYSTEMTIME time;
		GetLocalTime(&time);
		fprintf(file, "%02d:%02d:%02d.%03d  %s\n", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, message);
		fflush(file);
	}

	LeaveCriticalSection(&lock);
}




/* ------------------------------------------------------------------ Module */

uintptr_t Utilities::Module::GetBase()
{
	return reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
}

std::wstring Utilities::Module::GetDirectory()
{
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, path, MAX_PATH);

	std::wstring directory = path;
	size_t slash = directory.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		directory.resize(slash + 1);

	return directory;
}




std::wstring Utilities::Module::GetOwnDirectory()
{
	/* модуль, которому принадлежит этот код, — сам .asi */
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCWSTR>(&Utilities::Module::GetOwnDirectory), &self);

	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(self, path, MAX_PATH);

	std::wstring directory = path;
	size_t slash = directory.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		directory.resize(slash + 1);

	return directory;
}




/* ------------------------------------------------------------------ Memory */

bool Utilities::Memory::SafeCopy(void* destination, const void* source, size_t size)
{
	__try
	{
		memcpy(destination, source, size);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

bool Utilities::Memory::CheckBytes(uintptr_t address, const uint8_t* signature, size_t size)
{
	std::vector<uint8_t> bytes(size);
	return SafeCopy(bytes.data(), reinterpret_cast<const void*>(address), size) && memcmp(bytes.data(), signature, size) == 0;
}

bool Utilities::Memory::PatchPointer(uintptr_t address, void* newValue, void** oldValue)
{
	DWORD protection = 0;
	if (VirtualProtect(reinterpret_cast<void*>(address), sizeof(void*), PAGE_READWRITE, &protection) == FALSE)
		return false;

	if (oldValue)
		*oldValue = *reinterpret_cast<void**>(address);
	*reinterpret_cast<void**>(address) = newValue;

	VirtualProtect(reinterpret_cast<void*>(address), sizeof(void*), protection, &protection);
	return true;
}




/* ------------------------------------------------------------------ Matrix4 */

Utilities::Matrix4 Utilities::Matrix4::Multiply(const Matrix4& a, const Matrix4& b)
{
	Matrix4 result;
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			double sum = 0.0;
			for (int k = 0; k < 4; ++k)
				sum += a.m[i * 4 + k] * b.m[k * 4 + j];
			result.m[i * 4 + j] = sum;
		}
	}
	return result;
}

bool Utilities::Matrix4::Inverse(const Matrix4& in, Matrix4& out)
{
	const double* m = in.m;
	double inv[16];

	inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
	inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
	inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
	inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
	inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
	inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
	inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
	inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
	inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
	inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
	inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
	inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
	inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
	inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
	inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
	inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

	double determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (std::fabs(determinant) < 1e-30)
		return false;

	for (int i = 0; i < 16; ++i)
		out.m[i] = inv[i] / determinant;
	return true;
}

bool Utilities::Matrix4::ReadFloat(const void* address, Matrix4& out)
{
	float values[16];
	if (Memory::SafeCopy(values, address, sizeof(values)) == false)
		return false;

	for (int i = 0; i < 16; ++i)
	{
		if (std::isfinite(values[i]) == false)
			return false;
		out.m[i] = values[i];
	}
	return true;
}




/* ------------------------------------------------------------------ ComputeState */

void Utilities::ComputeState::Save(ID3D11DeviceContext* context)
{
	context->CSGetShader(&shader, nullptr, nullptr);
	context->CSGetShaderResources(0, SrvSlots, srv);
	context->CSGetUnorderedAccessViews(0, UavSlots, uav);
	context->CSGetConstantBuffers(0, CbSlots, cb);
	context->CSGetSamplers(0, SamplerSlots, samplers);
}

void Utilities::ComputeState::Restore(ID3D11DeviceContext* context)
{
	UINT keepCounters[UavSlots];
	for (UINT& counter : keepCounters)
		counter = static_cast<UINT>(-1);

	context->CSSetShader(shader, nullptr, 0);
	context->CSSetShaderResources(0, SrvSlots, srv);
	context->CSSetUnorderedAccessViews(0, UavSlots, uav, keepCounters);
	context->CSSetConstantBuffers(0, CbSlots, cb);
	context->CSSetSamplers(0, SamplerSlots, samplers);

	SafeRelease(shader);
	for (auto*& p : srv) SafeRelease(p);
	for (auto*& p : uav) SafeRelease(p);
	for (auto*& p : cb) SafeRelease(p);
	for (auto*& p : samplers) SafeRelease(p);
}

void Utilities::ComputeState::Unbind(ID3D11DeviceContext* context)
{
	ID3D11ShaderResourceView* nullSrv[SrvSlots] = {};
	ID3D11UnorderedAccessView* nullUav[UavSlots] = {};
	context->CSSetShaderResources(0, SrvSlots, nullSrv);
	context->CSSetUnorderedAccessViews(0, UavSlots, nullUav, nullptr);
}
