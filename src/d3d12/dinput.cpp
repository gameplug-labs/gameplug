#include <windows.h>
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "shlwapi.lib")

// Typedef for the original DirectInput8Create
typedef HRESULT (WINAPI *PFN_DirectInput8Create)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut, LPUNKNOWN punkOuter);

static PFN_DirectInput8Create g_realDirectInput8Create = nullptr;
static HMODULE g_frameworkModule = nullptr;

// Function in the linked framework that we'll call to start everything
extern "C" void StartFramework();

void InitializeProxy() {
    if (g_realDirectInput8Create) return;

    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    strcat_s(path, "\\dinput8.dll");

    HMODULE hMod = LoadLibraryA(path);
    if (hMod) {
        g_realDirectInput8Create = (PFN_DirectInput8Create)GetProcAddress(hMod, "DirectInput8Create");
    }
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut, LPUNKNOWN punkOuter) {
    InitializeProxy();
    if (g_realDirectInput8Create) {
        return g_realDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    }
    return E_FAIL;
}

DWORD WINAPI InitThread(LPVOID lpParam) {
    // Wait for the game to stabilize (increased to 3s for better compatibility)
    Sleep(3000);

    // Call StartFramework directly since it's now statically linked
    OutputDebugStringA("[GamePlug] dinput8: Calling StartFramework...");
    StartFramework();

    // Force load DXVK (d3d9.dll)
    LoadLibraryA("d3d9.dll");

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        OutputDebugStringA("[GamePlug] dinput8.dll: DLL_PROCESS_ATTACH");
        DisableThreadLibraryCalls(hinstDLL);
        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    }
    return TRUE;
}
