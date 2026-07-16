#include <windows.h>

#if defined(GAMEPLUG_VULKAN)
// Forward exports to system dinput8.dll.
#pragma comment(linker, "/export:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create")
#pragma comment(linker, "/export:DllCanUnloadNow=C:\\Windows\\System32\\dinput8.DllCanUnloadNow")
#pragma comment(linker, "/export:DllGetClassObject=C:\\Windows\\System32\\dinput8.DllGetClassObject")
#pragma comment(linker, "/export:DllRegisterServer=C:\\Windows\\System32\\dinput8.DllRegisterServer")
#pragma comment(linker, "/export:DllUnregisterServer=C:\\Windows\\System32\\dinput8.DllUnregisterServer")
#pragma comment(linker, "/export:GetdfDIJoystick=C:\\Windows\\System32\\dinput8.GetdfDIJoystick")

extern "C" void StartVulkanHookSetup();

#else

typedef HRESULT(WINAPI* PFN_DirectInput8Create)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter);

static PFN_DirectInput8Create g_realDirectInput8Create = nullptr;

extern "C" void StartFramework();

static void InitializeProxy() {
    if (g_realDirectInput8Create)
        return;

    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    strcat_s(path, "\\dinput8.dll");

    HMODULE hMod = LoadLibraryA(path);
    if (hMod) {
        g_realDirectInput8Create =
            reinterpret_cast<PFN_DirectInput8Create>(GetProcAddress(hMod, "DirectInput8Create"));
    }
}

extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
    InitializeProxy();
    if (g_realDirectInput8Create) {
        return g_realDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    }
    return E_FAIL;
}

static DWORD WINAPI InitThread(LPVOID) {
    Sleep(3000);
    StartFramework();
    return 0;
}

#endif

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
#if defined(GAMEPLUG_VULKAN)
            StartVulkanHookSetup();
#else
            CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
#endif
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
