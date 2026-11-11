#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <cmath>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Windows.h"
#include <d3d11.h>






namespace Utilities
{
	class Log
	{
	private:
		static inline FILE* file = nullptr;
		static inline CRITICAL_SECTION lock = {};
		static inline bool lockReady = false;


	public:
		static void Open(const std::wstring& path);
		static void Write(const char* format, ...);
	};


	class Module
	{
	public:
		static uintptr_t GetBase();
		static std::wstring GetDirectory();      // Folder of the game executable
		static std::wstring GetOwnDirectory();   // Folder of this module (.dll or .asi)
	};


	class Memory
	{
	public:
		/* Returns false instead of crashing when the source address is not readable */
		static bool SafeCopy(void* destination, const void* source, size_t size);

		template <class T>
		static T Read(const void* base, uintptr_t offset = 0)
		{
			T value{};
			if (base)
				SafeCopy(&value, reinterpret_cast<const uint8_t*>(base) + offset, sizeof(T));
			return value;
		}

		static bool CheckBytes(uintptr_t address, const uint8_t* signature, size_t size);
		static bool PatchPointer(uintptr_t address, void* newValue, void** oldValue);
	};


	/* Row-vector convention like Unreal Engine: clip = position * M */
	struct Matrix4
	{
		double m[16] = {};

		static Matrix4 Multiply(const Matrix4& a, const Matrix4& b);
		static bool Inverse(const Matrix4& in, Matrix4& out);
		static bool ReadFloat(const void* address, Matrix4& out);
	};


	/* Saves and restores the compute stage so the engine's D3D11 state cache stays valid */
	class ComputeState
	{
	public:
		static constexpr UINT SrvSlots = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
		static constexpr UINT CbSlots = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
		static constexpr UINT SamplerSlots = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
		static constexpr UINT UavSlots = 8;

		void Save(ID3D11DeviceContext* context);
		void Restore(ID3D11DeviceContext* context);

		static void Unbind(ID3D11DeviceContext* context);


	private:
		ID3D11ComputeShader* shader = nullptr;
		ID3D11ShaderResourceView* srv[SrvSlots] = {};
		ID3D11UnorderedAccessView* uav[UavSlots] = {};
		ID3D11Buffer* cb[CbSlots] = {};
		ID3D11SamplerState* samplers[SamplerSlots] = {};
	};


	template <class T>
	static void SafeRelease(T*& pointer)
	{
		if (pointer)
		{
			pointer->Release();
			pointer = nullptr;
		}
	}
}
