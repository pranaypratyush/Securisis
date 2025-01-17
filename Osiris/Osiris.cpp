#include <memory>

#ifdef _WIN32
#include <clocale>
#include <Windows.h>
#endif

#include "Hooks.h"
#include "Security/VMProtectSDK.h"

#ifdef _WIN32

extern "C" BOOL WINAPI _CRT_INIT(HMODULE moduleHandle, DWORD reason, LPVOID reserved);

BOOL APIENTRY DllEntryPoint(HMODULE moduleHandle, DWORD reason, LPVOID reserved)
{
    VMProtectBeginMutation("DllEntryPoint");
    if (!_CRT_INIT(moduleHandle, reason, reserved))
    {
        VMProtectEnd();
        return FALSE;
    }

    if (reason == DLL_PROCESS_ATTACH) {
        std::setlocale(LC_CTYPE, ".utf8");
        hooks = std::make_unique<Hooks>(moduleHandle);
    }

    VMProtectEnd();
    return TRUE;
}

#else

void __attribute__((constructor)) DllEntryPoint()
{
    hooks = std::make_unique<Hooks>();
}

#endif
