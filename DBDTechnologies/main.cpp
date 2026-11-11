#include "Technologies.h"






DWORD WINAPI InitThread(LPVOID lpParam)
{
    Technologies::Init();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            if (HANDLE hThread = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)InitThread, hModule, 0, nullptr))
                CloseHandle(hThread);
            break;

        case DLL_THREAD_ATTACH:
            break;

        case DLL_THREAD_DETACH:
            break;

        case DLL_PROCESS_DETACH:
            break;
    }


    return TRUE;
}
