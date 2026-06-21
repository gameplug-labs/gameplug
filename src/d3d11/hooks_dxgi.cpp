#include "hooks_common.h"
#include "upscaler_manager.h"
#include <iomanip>
#include <psapi.h>
#include <sstream>
#include <string>

namespace GamePlug {

ULONG STDMETHODCALLTYPE HookedRelease(IUnknown* pUnk) {
    if (g_InHook)
        return g_OriginalRelease(pUnk);
    ScopedRecursionGuard guard;

    ULONG refCount = g_OriginalRelease(pUnk);
    if (refCount == 0) {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (g_HookedVTables.count(pUnk)) {
            g_HookedVTables.erase(pUnk);
            Logger::info("DX Hooks D3D11: Object " + std::to_string((uintptr_t)pUnk) + " destroyed. Removed from tracking.");
        }
    }
    return refCount;
}

HRESULT STDMETHODCALLTYPE HookedQueryInterface(IUnknown* pUnk, REFIID riid, void** ppvObject) {
    if (g_InHook)
        return g_OriginalQueryInterface(pUnk, riid, ppvObject);
    ScopedRecursionGuard guard;

    HRESULT hr = g_OriginalQueryInterface(pUnk, riid, ppvObject);
    if (SUCCEEDED(hr) && ppvObject && *ppvObject) {
        bool isSwapChain = false;
        std::string iidName = "Unknown";

        if (riid == __uuidof(IDXGISwapChain)) {
            isSwapChain = true;
            iidName = "IDXGISwapChain";
        } else if (riid == __uuidof(IDXGISwapChain1)) {
            isSwapChain = true;
            iidName = "IDXGISwapChain1";
        } else if (riid == __uuidof(IDXGISwapChain2)) {
            isSwapChain = true;
            iidName = "IDXGISwapChain2";
        } else if (riid == __uuidof(IDXGISwapChain3)) {
            isSwapChain = true;
            iidName = "IDXGISwapChain3";
        } else if (riid == __uuidof(IDXGISwapChain4)) {
            isSwapChain = true;
            iidName = "IDXGISwapChain4";
        }

        if (isSwapChain) {
            Logger::info("DX Hooks D3D11: QueryInterface returned " + iidName + " interface at " + std::to_string((uintptr_t)*ppvObject));
            ApplySwapChainHooks(*ppvObject);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (g_InHook)
        return g_OriginalPresent(pSwapChain, SyncInterval, Flags);
    ScopedRecursionGuard guard;

    g_frameCount++;
    // Logger::info("HookedPresent D3D11 Entry [SC=" + std::to_string((uintptr_t)pSwapChain) +
    //              " VT=" + std::to_string((uintptr_t)(*(void***)pSwapChain)) + "]");
    OnDXPresent(pSwapChain);

    if (DXUpscalerManager::Get().PresentFrameDX11(pSwapChain, SyncInterval, Flags)) {
        return S_OK; // Plugin handled presentation (e.g. Frame Generation)
    }

    return g_OriginalPresent(pSwapChain, SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE HookedPresent1(
    IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    if (g_InHook)
        return g_OriginalPresent1(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
    ScopedRecursionGuard guard;

    static uint64_t s_present1Calls = 0;
    if (s_present1Calls++ % 60 == 0) {
        Logger::info("HookedPresent1 D3D11 Entry [" + std::to_string(s_present1Calls) + "] SC=" + std::to_string((uintptr_t)pSwapChain));
    }
    g_frameCount++;
    OnDXPresent(pSwapChain);

    if (DXUpscalerManager::Get().PresentFrameDX11(pSwapChain, SyncInterval, PresentFlags)) {
        return S_OK;
    }

    return g_OriginalPresent1(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
    IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    if (g_InHook)
        return g_OriginalResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    ScopedRecursionGuard guard;

    Logger::info("HookedResizeBuffers D3D11 Entry [SC=" + std::to_string((uintptr_t)pSwapChain) + "]");
    g_IsResizing = true;
    OnDXResize(pSwapChain);

    MH_DisableHook(MH_ALL_HOOKS);

    void** vtable = *(void***)pSwapChain;
    DWORD old;
    if (VirtualProtect(vtable, 128 * sizeof(void*), PAGE_READWRITE, &old)) {
        vtable[0] = (void*)g_OriginalQueryInterface;
        vtable[2] = (void*)g_OriginalRelease;
        vtable[8] = (void*)g_OriginalPresent;
        vtable[9] = (void*)g_OriginalGetBuffer;
        vtable[13] = (void*)g_OriginalResizeBuffers;
        vtable[21] = (void*)g_OriginalPresent1;
        vtable[22] = (void*)g_OriginalPresent1;
        vtable[31] = (void*)g_OriginalResizeBuffers1;
        VirtualProtect(vtable, 128 * sizeof(void*), old, &old);
    }

    HRESULT hr = g_OriginalResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    if (VirtualProtect(vtable, 128 * sizeof(void*), PAGE_READWRITE, &old)) {
        vtable[0] = (void*)HookedQueryInterface;
        vtable[2] = (void*)HookedRelease;
        vtable[8] = (void*)HookedPresent;
        vtable[9] = (void*)HookedGetBuffer;
        vtable[13] = (void*)HookedResizeBuffers;
        vtable[21] = (void*)HookedPresent1;
        vtable[22] = (void*)HookedPresent1;
        vtable[31] = (void*)HookedResizeBuffers1;
        VirtualProtect(vtable, 128 * sizeof(void*), old, &old);
    }

    MH_EnableHook(MH_ALL_HOOKS);

    g_IsResizing = false;
    if (FAILED(hr)) {
        char hex[16];
        sprintf(hex, "0x%08X", (unsigned int)hr);
        Logger::error("HookedResizeBuffers D3D11: FAILED with HR=" + std::string(hex));
    } else {
        Logger::info("HookedResizeBuffers D3D11: Success");
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers1(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
    DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pNodeMask, IUnknown* const* ppPresentQueue) {
    if (g_InHook)
        return g_OriginalResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags, pNodeMask, ppPresentQueue);
    ScopedRecursionGuard guard;

    Logger::info("HookedResizeBuffers1 D3D11 Entry [SC=" + std::to_string((uintptr_t)pSwapChain) + "]");
    g_IsResizing = true;
    OnDXResize(pSwapChain);

    MH_DisableHook(MH_ALL_HOOKS);

    void** vtable = *(void***)pSwapChain;
    DWORD old;
    if (VirtualProtect(vtable, 128 * sizeof(void*), PAGE_READWRITE, &old)) {
        vtable[0] = (void*)g_OriginalQueryInterface;
        vtable[2] = (void*)g_OriginalRelease;
        vtable[8] = (void*)g_OriginalPresent;
        vtable[9] = (void*)g_OriginalGetBuffer;
        vtable[13] = (void*)g_OriginalResizeBuffers;
        vtable[21] = (void*)g_OriginalPresent1;
        vtable[22] = (void*)g_OriginalPresent1;
        vtable[31] = (void*)g_OriginalResizeBuffers1;
        VirtualProtect(vtable, 128 * sizeof(void*), old, &old);
    }

    HRESULT hr = g_OriginalResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags, pNodeMask, ppPresentQueue);

    if (VirtualProtect(vtable, 128 * sizeof(void*), PAGE_READWRITE, &old)) {
        vtable[0] = (void*)HookedQueryInterface;
        vtable[2] = (void*)HookedRelease;
        vtable[8] = (void*)HookedPresent;
        vtable[9] = (void*)HookedGetBuffer;
        vtable[13] = (void*)HookedResizeBuffers;
        vtable[21] = (void*)HookedPresent1;
        vtable[22] = (void*)HookedPresent1;
        vtable[31] = (void*)HookedResizeBuffers1;
        VirtualProtect(vtable, 128 * sizeof(void*), old, &old);
    }

    MH_EnableHook(MH_ALL_HOOKS);

    g_IsResizing = false;
    if (FAILED(hr)) {
        char hex[16];
        sprintf(hex, "0x%08X", (unsigned int)hr);
        Logger::error("HookedResizeBuffers1 D3D11: FAILED with HR=" + std::string(hex));
    } else {
        Logger::info("HookedResizeBuffers1 D3D11: Success");
    }
    return hr;
}

void LogVTable(void* pObject, int numEntries) {
    void** pVTable = *(void***)pObject;
    Logger::info("--- SwapChain VTable Log ---");
    for (int i = 0; i < numEntries; ++i) {
        void* pFunc = pVTable[i];
        char moduleName[MAX_PATH] = "Unknown";
        DWORD_PTR offset = 0;

        HMODULE hMod = NULL;
        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pFunc, &hMod)) {
            GetModuleFileNameA(hMod, moduleName, MAX_PATH);
            char* pLastSlash = strrchr(moduleName, '\\');
            std::string modStr = pLastSlash ? (pLastSlash + 1) : moduleName;
            offset = (DWORD_PTR)pFunc - (DWORD_PTR)hMod;
            std::stringstream ss;
            ss << "  Index " << i << ": " << pFunc << " (" << modStr << "+0x" << std::hex << offset << ")";
            Logger::info(ss.str());
        } else {
            std::stringstream ss;
            ss << "  Index " << i << ": " << pFunc << " (Unknown Module)";
            Logger::info(ss.str());
        }
    }
    Logger::info("----------------------------");
}

HRESULT STDMETHODCALLTYPE HookedGetBuffer(IDXGISwapChain* pSwapChain, UINT Buffer, REFIID riid, void** ppSurface) {
    if (g_InHook)
        return g_OriginalGetBuffer(pSwapChain, Buffer, riid, ppSurface);
    ScopedRecursionGuard guard;

    if (Buffer == 0) {
        Logger::info("HookedGetBuffer: Buffer=0 requested.");
        if (!DXUpscalerManager::Get().GetDevice()) {
            Logger::info("HookedGetBuffer: Device is NULL, initializing...");
            ID3D11Device* device = nullptr;
            HRESULT hrDev = pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device);
            if (SUCCEEDED(hrDev) && device) {
                Logger::info("HookedGetBuffer: GetDevice succeeded. Getting context...");
                ID3D11DeviceContext* context = nullptr;
                device->GetImmediateContext(&context);
                if (context) {
                    Logger::info("HookedGetBuffer: GetImmediateContext succeeded. Calling InitDX11...");
                    DXUpscalerManager::Get().InitDX11(device, context);
                    context->Release();
                } else {
                    Logger::warn("HookedGetBuffer: GetImmediateContext FAILED!");
                }
                device->Release();
            } else {
                Logger::warn("HookedGetBuffer: GetDevice FAILED with hr=" + std::to_string(hrDev));
            }
        }

        Logger::info("HookedGetBuffer: IsUpscalingEnabled=" + std::to_string(DXUpscalerManager::Get().IsUpscalingEnabled()));

        if (DXUpscalerManager::Get().IsUpscalingEnabled()) {
            if (!DXUpscalerManager::Get().GetFakeBackBuffer()) {
                Logger::info("HookedGetBuffer: Fake BackBuffer is NULL, creating...");
                DXUpscalerManager::Get().CreateFakeBackBuffer(pSwapChain);
            }
            ID3D11Texture2D* fakeBuffer = DXUpscalerManager::Get().GetFakeBackBuffer();
            if (fakeBuffer) {
                HRESULT hr = fakeBuffer->QueryInterface(riid, ppSurface);
                if (SUCCEEDED(hr)) {
                    static uint64_t s_logCount = 0;
                    if (s_logCount++ % 100 == 0) {
                        Logger::info(
                            "DX Hooks D3D11: Redirected GetBuffer(0) to Fake BackBuffer (" + std::to_string((uintptr_t)*ppSurface) + ")");
                    }
                    return S_OK;
                } else {
                    Logger::warn("HookedGetBuffer: QueryInterface on Fake BackBuffer FAILED with hr=" + std::to_string(hr));
                }
            } else {
                Logger::warn("HookedGetBuffer: GetFakeBackBuffer returned NULL!");
            }
        }
    }

    return g_OriginalGetBuffer(pSwapChain, Buffer, riid, ppSurface);
}

void ApplySwapChainHooks(void* pSwapChain) {
    std::lock_guard<std::mutex> lock(g_HookMtx);
    if (g_HookedVTables.count(pSwapChain))
        return;

    LogVTable(pSwapChain, 40);

    void** pVTable = *(void***)pSwapChain;
    Logger::info("DX Hooks D3D11: Patching Instance VTable for SC=" + std::to_string((uintptr_t)pSwapChain) +
                 " [VT=" + std::to_string((uintptr_t)pVTable) + "]");

    static std::set<void**> s_PatchedVTables;
    if (s_PatchedVTables.count(pVTable)) {
        g_HookedVTables.insert(pSwapChain); // Already patched globally
        return;
    }

    // Save originals globally
    if (!g_OriginalQueryInterface)
        g_OriginalQueryInterface = (PFN_QueryInterface)pVTable[0];
    if (!g_OriginalRelease)
        g_OriginalRelease = (PFN_Release)pVTable[2];
    if (!g_OriginalPresent)
        g_OriginalPresent = (PFN_Present)pVTable[8];
    if (!g_OriginalGetBuffer)
        g_OriginalGetBuffer = (PFN_GetBuffer)pVTable[9];
    if (!g_OriginalResizeBuffers)
        g_OriginalResizeBuffers = (PFN_ResizeBuffers)pVTable[13];
    if (!g_OriginalPresent1)
        g_OriginalPresent1 = (PFN_Present1)pVTable[22];
    if (!g_OriginalResizeBuffers1)
        g_OriginalResizeBuffers1 = (PFN_ResizeBuffers1)pVTable[31];

    // Patch original VTable memory
    DWORD old;
    if (VirtualProtect(pVTable, 128 * sizeof(void*), PAGE_READWRITE, &old)) {
        pVTable[0] = (void*)HookedQueryInterface;
        pVTable[2] = (void*)HookedRelease;
        pVTable[8] = (void*)HookedPresent;
        pVTable[9] = (void*)HookedGetBuffer;
        pVTable[13] = (void*)HookedResizeBuffers;
        pVTable[21] = (void*)HookedPresent1;
        pVTable[22] = (void*)HookedPresent1;
        pVTable[31] = (void*)HookedResizeBuffers1;
        VirtualProtect(pVTable, 128 * sizeof(void*), old, &old);

        s_PatchedVTables.insert(pVTable);
        g_HookedVTables.insert(pSwapChain);
        Logger::info("DX Hooks D3D11: Global SwapChain VTable entries patched (STEALTH MODE)");
    } else {
        Logger::error("DX Hooks D3D11: FAILED to VirtualProtect VTable for stealth patching!");
    }
}

HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(
    IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
    if (g_InHook)
        return g_OriginalCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
    ScopedRecursionGuard guard;

    Logger::info("DX Factory D3D11: CreateSwapChain triggered");
    HRESULT hr = g_OriginalCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        Logger::info("DX Factory D3D11: New SwapChain created at " + std::to_string((uintptr_t)*ppSwapChain));
        ApplySwapChainHooks(*ppSwapChain);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(IDXGIFactory2* pFactory, IUnknown* pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain) {
    if (g_InHook)
        return g_OriginalCreateSwapChainForHwnd(pFactory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    ScopedRecursionGuard guard;

    Logger::info("DX Factory D3D11: CreateSwapChainForHwnd triggered (HWND=" + std::to_string((uintptr_t)hWnd) + ")");
    HRESULT hr = g_OriginalCreateSwapChainForHwnd(pFactory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        Logger::info("DX Factory D3D11: New SwapChain (Hwnd) created at " + std::to_string((uintptr_t)*ppSwapChain));
        ApplySwapChainHooks(*ppSwapChain);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForComposition(IDXGIFactory2* pFactory, IUnknown* pDevice,
    const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    if (g_InHook)
        return g_OriginalCreateSwapChainForComposition(pFactory, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
    ScopedRecursionGuard guard;

    Logger::info("DX Factory D3D11: CreateSwapChainForComposition triggered");
    HRESULT hr = g_OriginalCreateSwapChainForComposition(pFactory, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        Logger::info("DX Factory D3D11: New SwapChain (Composition) created at " + std::to_string((uintptr_t)*ppSwapChain));
        ApplySwapChainHooks(*ppSwapChain);
    }
    return hr;
}

void HandleNewFactory(IUnknown* pFactory) {
    if (!pFactory)
        return;
    void** pFactoryVTable = *(void***)pFactory;

    MH_CreateHook(pFactoryVTable[10], (LPVOID)HookedCreateSwapChain, (LPVOID*)&g_OriginalCreateSwapChain);

    IDXGIFactory2* factory2 = nullptr;
    if (SUCCEEDED(pFactory->QueryInterface(__uuidof(IDXGIFactory2), (void**)&factory2))) {
        void** pFactory2VTable = *(void***)factory2;
        MH_CreateHook(pFactory2VTable[15], (LPVOID)HookedCreateSwapChainForHwnd, (LPVOID*)&g_OriginalCreateSwapChainForHwnd);
        MH_CreateHook(pFactory2VTable[16], (LPVOID)HookedCreateSwapChainForComposition, (LPVOID*)&g_OriginalCreateSwapChainForComposition);
        factory2->Release();
    }
    MH_EnableHook(MH_ALL_HOOKS);
}

HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void** ppFactory) {
    if (g_InHook)
        return g_OriginalCreateDXGIFactory(riid, ppFactory);
    ScopedRecursionGuard guard;
    Logger::info("DX Hooks D3D11: CreateDXGIFactory called");
    HRESULT hr = g_OriginalCreateDXGIFactory(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory)
        HandleNewFactory((IUnknown*)*ppFactory);
    return hr;
}

HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void** ppFactory) {
    if (g_InHook)
        return g_OriginalCreateDXGIFactory1(riid, ppFactory);
    ScopedRecursionGuard guard;
    Logger::info("DX Hooks D3D11: CreateDXGIFactory1 called");
    HRESULT hr = g_OriginalCreateDXGIFactory1(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory)
        HandleNewFactory((IUnknown*)*ppFactory);
    return hr;
}

HRESULT WINAPI HookedCreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    if (g_InHook)
        return g_OriginalCreateDXGIFactory2(Flags, riid, ppFactory);
    ScopedRecursionGuard guard;
    Logger::info("DX Hooks D3D11: CreateDXGIFactory2 called (Flags=" + std::to_string(Flags) + ")");
    HRESULT hr = g_OriginalCreateDXGIFactory2(Flags, riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory)
        HandleNewFactory((IUnknown*)*ppFactory);
    return hr;
}

void HookDXGIFactories() {
    HMODULE dxgi = GetModuleHandle("dxgi.dll");
    if (dxgi) {
        void* pCreateFactory = GetProcAddress(dxgi, "CreateDXGIFactory");
        void* pCreateFactory1 = GetProcAddress(dxgi, "CreateDXGIFactory1");
        void* pCreateFactory2 = GetProcAddress(dxgi, "CreateDXGIFactory2");

        if (pCreateFactory)
            MH_CreateHook(pCreateFactory, (LPVOID)HookedCreateDXGIFactory, (LPVOID*)&g_OriginalCreateDXGIFactory);
        if (pCreateFactory1)
            MH_CreateHook(pCreateFactory1, (LPVOID)HookedCreateDXGIFactory1, (LPVOID*)&g_OriginalCreateDXGIFactory1);
        if (pCreateFactory2)
            MH_CreateHook(pCreateFactory2, (LPVOID)HookedCreateDXGIFactory2, (LPVOID*)&g_OriginalCreateDXGIFactory2);

        MH_EnableHook(MH_ALL_HOOKS);
        Logger::info("DX Hooks D3D11: Global DXGI Factory entry points hooked");
    }
}

} // namespace GamePlug
