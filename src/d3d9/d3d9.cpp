#include "d3d9_proxy.h"
#include "d3d9_proxy_d3d9.h"

// --- DirectInput8 Forwarding ---
typedef HRESULT(WINAPI* PFN_DirectInput8Create)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter);

static PFN_DirectInput8Create Real_DirectInput8Create = nullptr;
static HMODULE g_hRealDInput8 = nullptr;

static void EnsureRealDInput8Loaded() {
    if (g_hRealDInput8)
        return;
    char sysPath[MAX_PATH];
    GetSystemDirectoryA(sysPath, MAX_PATH);
    strcat_s(sysPath, sizeof(sysPath), "\\dinput8.dll");
    g_hRealDInput8 = LoadLibraryA(sysPath);
    if (g_hRealDInput8) {
        Real_DirectInput8Create = (PFN_DirectInput8Create)GetProcAddress(g_hRealDInput8, "DirectInput8Create");
        Logger::info("Loaded system dinput8.dll for forwarding.");
    }
}

// --- Direct3D9 Forwarding ---
static HMODULE g_hRealDll = nullptr;

static PFN_Direct3DCreate9 Real_Direct3DCreate9 = nullptr;
static PFN_Direct3DCreate9Ex Real_Direct3DCreate9Ex = nullptr;

typedef int(WINAPI* PFN_D3DPERF_BeginEvent)(D3DCOLOR col, LPCWSTR wszName);
typedef int(WINAPI* PFN_D3DPERF_EndEvent)(void);
typedef DWORD(WINAPI* PFN_D3DPERF_GetStatus)(void);
typedef BOOL(WINAPI* PFN_D3DPERF_QueryRepeatFrame)(void);
typedef void(WINAPI* PFN_D3DPERF_SetMarker)(D3DCOLOR col, LPCWSTR wszName);
typedef void(WINAPI* PFN_D3DPERF_SetOptions)(DWORD dwOptions);
typedef void(WINAPI* PFN_D3DPERF_SetRegion)(D3DCOLOR col, LPCWSTR wszName);

static PFN_D3DPERF_BeginEvent Real_D3DPERF_BeginEvent = nullptr;
static PFN_D3DPERF_EndEvent Real_D3DPERF_EndEvent = nullptr;
static PFN_D3DPERF_GetStatus Real_D3DPERF_GetStatus = nullptr;
static PFN_D3DPERF_QueryRepeatFrame Real_D3DPERF_QueryRepeatFrame = nullptr;
static PFN_D3DPERF_SetMarker Real_D3DPERF_SetMarker = nullptr;
static PFN_D3DPERF_SetOptions Real_D3DPERF_SetOptions = nullptr;
static PFN_D3DPERF_SetRegion Real_D3DPERF_SetRegion = nullptr;

static void EnsureRealDllLoaded() {
    if (g_hRealDll)
        return;
    g_hRealDll = LoadLibraryA("real_d3d9.dll");
    if (g_hRealDll)
        Logger::info("Loaded chained DLL: real_d3d9.dll");
    if (!g_hRealDll) {
        char sysPath[MAX_PATH];
        GetSystemDirectoryA(sysPath, MAX_PATH);
        strcat_s(sysPath, sizeof(sysPath), "\\d3d9.dll");
        g_hRealDll = LoadLibraryA(sysPath);
        if (g_hRealDll)
            Logger::info("Loaded system DLL: {}", sysPath);
    }
    if (g_hRealDll) {
        Real_Direct3DCreate9 = (PFN_Direct3DCreate9)GetProcAddress(g_hRealDll, "Direct3DCreate9");
        Real_Direct3DCreate9Ex = (PFN_Direct3DCreate9Ex)GetProcAddress(g_hRealDll, "Direct3DCreate9Ex");
        Real_D3DPERF_BeginEvent = (PFN_D3DPERF_BeginEvent)GetProcAddress(g_hRealDll, "D3DPERF_BeginEvent");
        Real_D3DPERF_EndEvent = (PFN_D3DPERF_EndEvent)GetProcAddress(g_hRealDll, "D3DPERF_EndEvent");
        Real_D3DPERF_GetStatus = (PFN_D3DPERF_GetStatus)GetProcAddress(g_hRealDll, "D3DPERF_GetStatus");
        Real_D3DPERF_QueryRepeatFrame = (PFN_D3DPERF_QueryRepeatFrame)GetProcAddress(g_hRealDll, "D3DPERF_QueryRepeatFrame");
        Real_D3DPERF_SetMarker = (PFN_D3DPERF_SetMarker)GetProcAddress(g_hRealDll, "D3DPERF_SetMarker");
        Real_D3DPERF_SetOptions = (PFN_D3DPERF_SetOptions)GetProcAddress(g_hRealDll, "D3DPERF_SetOptions");
        Real_D3DPERF_SetRegion = (PFN_D3DPERF_SetRegion)GetProcAddress(g_hRealDll, "D3DPERF_SetRegion");
    }
}

extern "C" {
HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
    EnsureRealDInput8Loaded();
    if (!Real_DirectInput8Create)
        return E_FAIL;
    return Real_DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    EnsureRealDllLoaded();
    if (!Real_Direct3DCreate9)
        return NULL;
    IDirect3D9* pReal = Real_Direct3DCreate9(SDKVersion);
    if (!pReal)
        return NULL;
    return (IDirect3D9*)new ProxyDirect3D9(pReal);
}

HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppDirect3D9Ex) {
    EnsureRealDllLoaded();
    if (!Real_Direct3DCreate9Ex)
        return D3DERR_INVALIDCALL;
    HRESULT hr = Real_Direct3DCreate9Ex(SDKVersion, ppDirect3D9Ex);
    if (SUCCEEDED(hr) && ppDirect3D9Ex && *ppDirect3D9Ex) {
        *ppDirect3D9Ex = (IDirect3D9Ex*)new ProxyDirect3D9(*ppDirect3D9Ex);
    }
    return hr;
}

int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) {
    EnsureRealDllLoaded();
    if (Real_D3DPERF_BeginEvent)
        return Real_D3DPERF_BeginEvent(col, wszName);
    return 0;
}
int WINAPI D3DPERF_EndEvent(void) {
    EnsureRealDllLoaded();
    if (Real_D3DPERF_EndEvent)
        return Real_D3DPERF_EndEvent();
    return 0;
}
DWORD WINAPI D3DPERF_GetStatus(void) {
    EnsureRealDllLoaded();
    if (Real_D3DPERF_GetStatus)
        return Real_D3DPERF_GetStatus();
    return 0;
}
BOOL WINAPI D3DPERF_QueryRepeatFrame(void) {
    EnsureRealDllLoaded();
    if (Real_D3DPERF_QueryRepeatFrame)
        return Real_D3DPERF_QueryRepeatFrame();
    return FALSE;
}
void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) {
    EnsureRealDllLoaded();
    if (Real_D3DPERF_SetMarker)
        Real_D3DPERF_SetMarker(col, wszName);
}
void WINAPI D3DPERF_SetOptions(DWORD dwOptions) {
    EnsureRealDllLoaded();
    if (Real_D3DPERF_SetOptions)
        Real_D3DPERF_SetOptions(dwOptions);
}
void WINAPI D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) {
    EnsureRealDllLoaded();
    if (Real_D3DPERF_SetRegion)
        Real_D3DPERF_SetRegion(col, wszName);
}
void WINAPI DebugSetLevel(void) {
}
void WINAPI DebugSetMute(void) {
}
int WINAPI Direct3D9EnableMaximizedWindowedModeShim(UINT a) {
    return 0;
}
void* WINAPI Direct3DShaderValidatorCreate9(void) {
    return NULL;
}
void WINAPI PSGPError(void* a, UINT b, UINT c) {
}
void WINAPI PSGPSampleTexture(void* a, UINT b, float (*const c)[4], UINT d, float (*const e)[4]) {
}
}

extern "C" void StartFramework();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        OutputDebugStringA("[GamePlug] d3d9/dinput8.dll: DLL_PROCESS_ATTACH");
        DisableThreadLibraryCalls(hinstDLL);
        StartFramework();
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        Logger::info("--- Proxy Unloaded ---");
    }
    return TRUE;
}
