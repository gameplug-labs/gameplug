#include "win32_hooks.h"
#include "config.h"
#include "logger.h"
#include "minhook.h"
#include <string>
#include <vector>
#include <windows.h>

namespace GamePlug {

typedef BOOL(WINAPI* PFN_EnumDisplaySettingsA)(LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA* lpDevMode);
typedef BOOL(WINAPI* PFN_EnumDisplaySettingsW)(LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode);

static PFN_EnumDisplaySettingsA g_OriginalEnumDisplaySettingsA = nullptr;
static PFN_EnumDisplaySettingsW g_OriginalEnumDisplaySettingsW = nullptr;

BOOL WINAPI HookedEnumDisplaySettingsA(LPCSTR lpszDeviceName, DWORD iModeNum, DEVMODEA* lpDevMode) {
    if (iModeNum == ENUM_CURRENT_SETTINGS || iModeNum == ENUM_REGISTRY_SETTINGS) {
        return g_OriginalEnumDisplaySettingsA(lpszDeviceName, iModeNum, lpDevMode);
    }

    static DWORD s_realCount = 0xFFFFFFFF;
    if (iModeNum == 0)
        s_realCount = 0xFFFFFFFF;

    if (s_realCount == 0xFFFFFFFF) {
        DWORD count = 0;
        DEVMODEA dummy;
        memset(&dummy, 0, sizeof(dummy));
        dummy.dmSize = sizeof(dummy);
        while (g_OriginalEnumDisplaySettingsA(lpszDeviceName, count, &dummy)) {
            count++;
        }
        s_realCount = count;
    }

    if (iModeNum < s_realCount) {
        return g_OriginalEnumDisplaySettingsA(lpszDeviceName, iModeNum, lpDevMode);
    }

    DWORD extraIdx = iModeNum - s_realCount;
    const auto& extra = Config::Get().GetExtraResolutions();
    if (extraIdx < extra.size()) {
        if (lpDevMode) {
            // Get mode 0 as a template to preserve device name and other fields
            if (!g_OriginalEnumDisplaySettingsA(lpszDeviceName, 0, lpDevMode)) {
                memset(lpDevMode, 0, sizeof(DEVMODEA));
                lpDevMode->dmSize = sizeof(DEVMODEA);
            }
            lpDevMode->dmPelsWidth = extra[extraIdx].width;
            lpDevMode->dmPelsHeight = extra[extraIdx].height;
            lpDevMode->dmBitsPerPel = 32;
            lpDevMode->dmDisplayFrequency = 60;
            lpDevMode->dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
        }
        return TRUE;
    }

    return FALSE;
}

BOOL WINAPI HookedEnumDisplaySettingsW(LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode) {
    if (iModeNum == ENUM_CURRENT_SETTINGS || iModeNum == ENUM_REGISTRY_SETTINGS) {
        return g_OriginalEnumDisplaySettingsW(lpszDeviceName, iModeNum, lpDevMode);
    }

    static DWORD s_realCount = 0xFFFFFFFF;
    if (iModeNum == 0)
        s_realCount = 0xFFFFFFFF;

    if (s_realCount == 0xFFFFFFFF) {
        DWORD count = 0;
        DEVMODEW dummy;
        memset(&dummy, 0, sizeof(dummy));
        dummy.dmSize = sizeof(dummy);
        while (g_OriginalEnumDisplaySettingsW(lpszDeviceName, count, &dummy)) {
            count++;
        }
        s_realCount = count;
    }

    if (iModeNum < s_realCount) {
        return g_OriginalEnumDisplaySettingsW(lpszDeviceName, iModeNum, lpDevMode);
    }

    DWORD extraIdx = iModeNum - s_realCount;
    const auto& extra = Config::Get().GetExtraResolutions();
    if (extraIdx < extra.size()) {
        if (lpDevMode) {
            // Get mode 0 as a template to preserve device name and other fields
            if (!g_OriginalEnumDisplaySettingsW(lpszDeviceName, 0, lpDevMode)) {
                memset(lpDevMode, 0, sizeof(DEVMODEW));
                lpDevMode->dmSize = sizeof(DEVMODEW);
            }
            lpDevMode->dmPelsWidth = extra[extraIdx].width;
            lpDevMode->dmPelsHeight = extra[extraIdx].height;
            lpDevMode->dmBitsPerPel = 32;
            lpDevMode->dmDisplayFrequency = 60;
            lpDevMode->dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
        }
        return TRUE;
    }

    return FALSE;
}

void InstallWin32Hooks() {
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    Logger::info("Installing Win32 Display Hooks...");

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        Logger::error("Win32 Hooks: MinHook failed to initialize");
        return;
    }

    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
        void* pEnumA = GetProcAddress(user32, "EnumDisplaySettingsA");
        void* pEnumW = GetProcAddress(user32, "EnumDisplaySettingsW");

        if (pEnumA)
            MH_CreateHook(pEnumA, (LPVOID)HookedEnumDisplaySettingsA, (LPVOID*)&g_OriginalEnumDisplaySettingsA);
        if (pEnumW)
            MH_CreateHook(pEnumW, (LPVOID)HookedEnumDisplaySettingsW, (LPVOID*)&g_OriginalEnumDisplaySettingsW);

        MH_EnableHook(MH_ALL_HOOKS);
        Logger::info("Win32 Hooks: EnumDisplaySettingsA/W hooked");
    }
}

} // namespace GamePlug
