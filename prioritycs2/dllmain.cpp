#include "pch.h"

#include "hooks.hpp"

namespace
{
    DWORD WINAPI bootstrap(LPVOID parameter)
    {
        const auto module = static_cast<HMODULE>(parameter);

        if (hooks::initialize(module))
        {
            while ((GetAsyncKeyState(VK_END) & 1) == 0)
                Sleep(50);

            hooks::shutdown();
        }

        FreeLibraryAndExitThread(module, 0);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;

    DisableThreadLibraryCalls(module);

    const HANDLE thread = CreateThread(nullptr, 0, bootstrap, module, 0, nullptr);
    if (!thread)
        return FALSE;

    CloseHandle(thread);
    return TRUE;
}
