#include <windows.h>

BOOL WINAPI SdkCoreDllMain(HINSTANCE, DWORD, LPVOID);
BOOL APIENTRY SdkPhysicsDllMain(HMODULE, DWORD, LPVOID);

// The merged runtime has one loader entry point. Keep the allocator and thread
// hooks from the original modules; CDB/XML/CPU had no loader initialization.
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH || reason == DLL_THREAD_DETACH)
    {
        SdkPhysicsDllMain(module, reason, reserved);
        return SdkCoreDllMain(module, reason, reserved);
    }
    return SdkCoreDllMain(module, reason, reserved) &&
        SdkPhysicsDllMain(module, reason, reserved);
}
