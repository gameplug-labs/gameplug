#include <shlwapi.h>
#include <windows.h>
#include "hooks_common.h"

#pragma comment(lib, "shlwapi.lib")

extern "C" void StartFramework();

static HMODULE GetVersionModule() {
    static HMODULE hMod = []() {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat_s(path, "\\version.dll");
        return LoadLibraryA(path);
    }();
    return hMod;
}

extern "C" BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    static auto pfn = (decltype(&GetFileVersionInfoA))GetProcAddress(GetVersionModule(), "GetFileVersionInfoA");
    if (pfn) return pfn(lptstrFilename, dwHandle, dwLen, lpData);
    return FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    static auto pfn = (decltype(&GetFileVersionInfoW))GetProcAddress(GetVersionModule(), "GetFileVersionInfoW");
    if (pfn) return pfn(lptstrFilename, dwHandle, dwLen, lpData);
    return FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoByHandle(DWORD dwFlags, HANDLE hFile, LPVOID* lplpData, PDWORD pdwLen) {
    static auto pfn = (decltype(&GetFileVersionInfoByHandle))GetProcAddress(GetVersionModule(), "GetFileVersionInfoByHandle");
    if (pfn) return pfn(dwFlags, hFile, lplpData, pdwLen);
    return FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    static auto pfn = (decltype(&GetFileVersionInfoExA))GetProcAddress(GetVersionModule(), "GetFileVersionInfoExA");
    if (pfn) return pfn(dwFlags, lptstrFilename, dwHandle, dwLen, lpData);
    return FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    static auto pfn = (decltype(&GetFileVersionInfoExW))GetProcAddress(GetVersionModule(), "GetFileVersionInfoExW");
    if (pfn) return pfn(dwFlags, lptstrFilename, dwHandle, dwLen, lpData);
    return FALSE;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    static auto pfn = (decltype(&GetFileVersionInfoSizeA))GetProcAddress(GetVersionModule(), "GetFileVersionInfoSizeA");
    if (pfn) return pfn(lptstrFilename, lpdwHandle);
    return 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    static auto pfn = (decltype(&GetFileVersionInfoSizeW))GetProcAddress(GetVersionModule(), "GetFileVersionInfoSizeW");
    if (pfn) return pfn(lptstrFilename, lpdwHandle);
    return 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    static auto pfn = (decltype(&GetFileVersionInfoSizeExA))GetProcAddress(GetVersionModule(), "GetFileVersionInfoSizeExA");
    if (pfn) return pfn(dwFlags, lptstrFilename, lpdwHandle);
    return 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    static auto pfn = (decltype(&GetFileVersionInfoSizeExW))GetProcAddress(GetVersionModule(), "GetFileVersionInfoSizeExW");
    if (pfn) return pfn(dwFlags, lptstrFilename, lpdwHandle);
    return 0;
}

extern "C" DWORD WINAPI VerFindFileA(DWORD dwFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT lpcchCurDir, LPSTR szDestDir, PUINT lpcchDestDir) {
    static auto pfn = (decltype(&VerFindFileA))GetProcAddress(GetVersionModule(), "VerFindFileA");
    if (pfn) return pfn(dwFlags, szFileName, szWinDir, szAppDir, szCurDir, lpcchCurDir, szDestDir, lpcchDestDir);
    return 0;
}

extern "C" DWORD WINAPI VerFindFileW(DWORD dwFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT lpcchCurDir, LPWSTR szDestDir, PUINT lpcchDestDir) {
    static auto pfn = (decltype(&VerFindFileW))GetProcAddress(GetVersionModule(), "VerFindFileW");
    if (pfn) return pfn(dwFlags, szFileName, szWinDir, szAppDir, szCurDir, lpcchCurDir, szDestDir, lpcchDestDir);
    return 0;
}

extern "C" DWORD WINAPI VerInstallFileA(DWORD dwFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFileName, PUINT lpcchTmpFileName) {
    static auto pfn = (decltype(&VerInstallFileA))GetProcAddress(GetVersionModule(), "VerInstallFileA");
    if (pfn) return pfn(dwFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFileName, lpcchTmpFileName);
    return 0;
}

extern "C" DWORD WINAPI VerInstallFileW(DWORD dwFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFileName, PUINT lpcchTmpFileName) {
    static auto pfn = (decltype(&VerInstallFileW))GetProcAddress(GetVersionModule(), "VerInstallFileW");
    if (pfn) return pfn(dwFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFileName, lpcchTmpFileName);
    return 0;
}

extern "C" DWORD WINAPI VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang) {
    static auto pfn = (decltype(&VerLanguageNameA))GetProcAddress(GetVersionModule(), "VerLanguageNameA");
    if (pfn) return pfn(wLang, szLang, cchLang);
    return 0;
}

extern "C" DWORD WINAPI VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang) {
    static auto pfn = (decltype(&VerLanguageNameW))GetProcAddress(GetVersionModule(), "VerLanguageNameW");
    if (pfn) return pfn(wLang, szLang, cchLang);
    return 0;
}

extern "C" BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    static auto pfn = (decltype(&VerQueryValueA))GetProcAddress(GetVersionModule(), "VerQueryValueA");
    if (pfn) return pfn(pBlock, lpSubBlock, lplpBuffer, puLen);
    return FALSE;
}

extern "C" BOOL WINAPI VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    static auto pfn = (decltype(&VerQueryValueW))GetProcAddress(GetVersionModule(), "VerQueryValueW");
    if (pfn) return pfn(pBlock, lpSubBlock, lplpBuffer, puLen);
    return FALSE;
}

DWORD WINAPI InitThread(LPVOID lpParam) {
    OutputDebugStringA("[GamePlug] version: Calling StartFramework...");
    StartFramework();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        OutputDebugStringA("[GamePlug] version.dll: DLL_PROCESS_ATTACH");
        DisableThreadLibraryCalls(hinstDLL);
        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    }
    return TRUE;
}

#ifdef FALLOUT4
#include "fallout4_bridge_api.h"
// Volatile pointer reference to force linker to preserve the unreferenced exports from framework_d3d11_fallout4.lib
extern "C" __declspec(dllexport) void* g_forceKeepFallout4Exports[] = {
    (void*)&GamePlug_SetFallout4Data,
    (void*)&GamePlug_GetFallout4ResolutionOverride,
    (void*)&GamePlug_IsFallout4OverlayVisible
};
#endif
