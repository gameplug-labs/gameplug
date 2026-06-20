#include <windows.h>
#include <shlwapi.h>
#include <string>

#pragma comment(lib, "shlwapi.lib")

// Forward exports to system dinput8.dll
#pragma comment(linker, "/export:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create")
#pragma comment(linker, "/export:DllCanUnloadNow=C:\\Windows\\System32\\dinput8.DllCanUnloadNow")
#pragma comment(linker, "/export:DllGetClassObject=C:\\Windows\\System32\\dinput8.DllGetClassObject")
#pragma comment(linker, "/export:DllRegisterServer=C:\\Windows\\System32\\dinput8.DllRegisterServer")
#pragma comment(linker, "/export:DllUnregisterServer=C:\\Windows\\System32\\dinput8.DllUnregisterServer")
#pragma comment(linker, "/export:GetdfDIJoystick=C:\\Windows\\System32\\dinput8.GetdfDIJoystick")

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace GamePlug {
    static void SetupVulkanLayers() {
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW((HINSTANCE)&__ImageBase, buffer, MAX_PATH);
        PathRemoveFileSpecW(buffer);
        std::wstring selfDir(buffer);

        // 1. Set VK_LAYER_PATH to the loader's directory
        SetEnvironmentVariableW(L"VK_LAYER_PATH", selfDir.c_str());

        // 2. Add our directory to PATH so vklayer.dll can find framework.dll/dependencies
        wchar_t oldPath[4096];
        DWORD pathLen = GetEnvironmentVariableW(L"PATH", oldPath, 4096);
        if (pathLen > 0 && pathLen < 4096) {
            std::wstring newPath = selfDir + L";" + oldPath;
            SetEnvironmentVariableW(L"PATH", newPath.c_str());
        } else {
            SetEnvironmentVariableW(L"PATH", selfDir.c_str());
        }

        // 3. Enable the layer
        SetEnvironmentVariableW(L"VK_INSTANCE_LAYERS", L"VK_LAYER_GAMEPLUG");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            GamePlug::SetupVulkanLayers();
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
