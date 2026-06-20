#include "d3d9_proxy.h"
#include "MinHook.h"
#include "d3d9_proxy_d3d9.h"
#include "d3d9_proxy_device.h"
#include "d3d9_proxy_surface.h"
#include "d3d9_proxy_swapchain.h"

using namespace GamePlug;

// Global flag to bypass scaling for upscaler passes
bool g_InUpscalerPass = false;

// --- Helper Functions ---
void GetScaledResolution(int& outW, int& outH) {
    try {
        Config& cfg = Config::Get();
        // Poll current target from config (refreshed by overlay UI)
        int targetWidth = cfg.GetTargetWidth();
        int targetHeight = cfg.GetTargetHeight();

        if (targetWidth > 0 && targetHeight > 0) {
            bool nativeRendering = true;
            bool upscaling = false;
            int quality = 0;

            if (!nativeRendering && upscaling) {
                float scale = 1.0f;
                switch (quality) {
                case 0:
                    scale = 1.2f;
                    break; // Ultra Ultra Quality
                case 1:
                    scale = 1.3f;
                    break; // Ultra Quality
                case 2:
                    scale = 1.5f;
                    break; // Quality
                case 3:
                    scale = 1.7f;
                    break; // Balanced
                case 4:
                    scale = 2.0f;
                    break; // Performance
                case 5:
                    scale = 3.0f;
                    break; // Ultra Performance
                default:
                    scale = 1.3f;
                    break;
                }

                outW = (int)(targetWidth / scale + 0.5f);
                outH = (int)(targetHeight / scale + 0.5f);
            } else {
                outW = targetWidth;
                outH = targetHeight;
            }
        }
    } catch (...) {
    }
}

// --- Proxy D3D9 ---

// --- Hooking Logic ---
PFN_Direct3DCreate9 Trampoline_Direct3DCreate9 = NULL;
PFN_Direct3DCreate9Ex Trampoline_Direct3DCreate9Ex = NULL;

IDirect3D9* WINAPI Hook_Direct3DCreate9(UINT SDKVersion) {
    static bool insideHook = false;
    if (insideHook)
        return Trampoline_Direct3DCreate9(SDKVersion);
    insideHook = true;
    Logger::info("Hook_Direct3DCreate9: Intercepted call.");
    IDirect3D9* pReal = Trampoline_Direct3DCreate9(SDKVersion);
    if (pReal)
        pReal = (IDirect3D9*)new ProxyDirect3D9(pReal);
    insideHook = false;
    return pReal;
}

HRESULT WINAPI Hook_Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppDirect3D9Ex) {
    static bool insideHook = false;
    if (insideHook)
        return Trampoline_Direct3DCreate9Ex(SDKVersion, ppDirect3D9Ex);
    insideHook = true;
    Logger::info("Hook_Direct3DCreate9Ex: Intercepted call.");
    HRESULT hr = Trampoline_Direct3DCreate9Ex(SDKVersion, ppDirect3D9Ex);
    if (SUCCEEDED(hr) && ppDirect3D9Ex && *ppDirect3D9Ex) {
        *ppDirect3D9Ex = (IDirect3D9Ex*)new ProxyDirect3D9(*ppDirect3D9Ex);
    }
    insideHook = false;
    return hr;
}

void InitializeHooks() {
    static bool hooksInitialized = false;
    if (hooksInitialized)
        return;

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        Logger::error("InitializeHooks: MH_Initialize failed!");
        return;
    }

    HMODULE hD3D9 = GetModuleHandleA("d3d9.dll");
    if (hD3D9) {
        HMODULE hMe = NULL;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&InitializeHooks, &hMe);

        char myPath[MAX_PATH];
        GetModuleFileNameA(hMe, myPath, MAX_PATH);
        std::string pathStr = myPath;
        for (auto& c : pathStr)
            c = tolower(c);

        bool isProxy = (pathStr.find("dinput8.dll") != std::string::npos) || (pathStr.find("version.dll") != std::string::npos);

        if (isProxy && hD3D9 != hMe) {
            void* pDirect3DCreate9 = (void*)GetProcAddress(hD3D9, "Direct3DCreate9");
            void* pDirect3DCreate9Ex = (void*)GetProcAddress(hD3D9, "Direct3DCreate9Ex");

            if (pDirect3DCreate9) {
                if (MH_CreateHook(pDirect3DCreate9, (LPVOID)&Hook_Direct3DCreate9, (LPVOID*)&Trampoline_Direct3DCreate9) == MH_OK) {
                    MH_EnableHook(pDirect3DCreate9);
                    Logger::info("InitializeHooks: Hooked Direct3DCreate9 in d3d9.dll");
                }
            }
            if (pDirect3DCreate9Ex) {
                if (MH_CreateHook(pDirect3DCreate9Ex, (LPVOID)&Hook_Direct3DCreate9Ex, (LPVOID*)&Trampoline_Direct3DCreate9Ex) == MH_OK) {
                    MH_EnableHook(pDirect3DCreate9Ex);
                    Logger::info("InitializeHooks: Hooked Direct3DCreate9Ex in d3d9.dll");
                }
            }
        }
    }
    hooksInitialized = true;
}
