#include "hooks_common.h"

namespace GamePlug {

static ID3D12CommandQueue* s_lastSeenQueue = nullptr;
static ID3D12CommandQueue* s_PendingQueue = nullptr;

ULONG STDMETHODCALLTYPE HookedRelease(IUnknown* pUnk) {
    if (g_InHook)
        return g_OriginalRelease(pUnk);
    ScopedRecursionGuard guard;

    ULONG refCount = g_OriginalRelease(pUnk);
    if (refCount == 0) {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (g_HookedVTables.count(pUnk)) {
            g_HookedVTables.erase(pUnk);
            Logger::info("DX Hooks: Object " + std::to_string((uintptr_t)pUnk) + " destroyed. Removed from tracking.");
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
            Logger::info("DX Hooks: QueryInterface returned " + iidName + " interface at " + std::to_string((uintptr_t)*ppvObject));
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
    // Logger::info("HookedPresent Entry [SC=" + std::to_string((uintptr_t)pSwapChain) +
    //              " VT=" + std::to_string((uintptr_t)(*(void***)pSwapChain)) + "]");
    OnDXPresent(pSwapChain);
    return g_OriginalPresent(pSwapChain, SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE HookedPresent1(
    IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    if (g_InHook)
        return g_OriginalPresent1(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
    ScopedRecursionGuard guard;

    static uint64_t s_present1Calls = 0;
    if (s_present1Calls++ % 60 == 0) {
        Logger::info("HookedPresent1 Entry [" + std::to_string(s_present1Calls) + "] SC=" + std::to_string((uintptr_t)pSwapChain));
    }
    g_frameCount++;
    OnDXPresent(pSwapChain);
    return g_OriginalPresent1(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
    IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    if (g_InHook)
        return g_OriginalResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    ScopedRecursionGuard guard;

    Logger::info("HookedResizeBuffers Entry [SC=" + std::to_string((uintptr_t)pSwapChain) + "]");
    g_IsResizing = true;
    OnDXResize(pSwapChain);

    // Phase 12 & 15: Total Integrity Restoration
    // 1. Physically unpatch MinHook detours
    MH_DisableHook(MH_ALL_HOOKS);

    // 2. Physically restore Global VTable entries to original DirectX pointers
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

    // 3. Re-apply Stealth Patches after the engine succeeds
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

    // // 4. Re-enable MinHook detours
    MH_EnableHook(MH_ALL_HOOKS);

    if (SUCCEEDED(hr)) {
        RegisterNativeResources(pSwapChain);
    }

    g_IsResizing = false;
    if (FAILED(hr)) {
        char hex[16];
        sprintf(hex, "0x%08X", (unsigned int)hr);
        Logger::error("HookedResizeBuffers: FAILED with HR=" + std::string(hex));
    } else {
        Logger::info("HookedResizeBuffers: Success");
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers1(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
    DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pNodeMask, IUnknown* const* ppPresentQueue) {
    if (g_InHook)
        return g_OriginalResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags, pNodeMask, ppPresentQueue);
    ScopedRecursionGuard guard;

    Logger::info("HookedResizeBuffers1 Entry [SC=" + std::to_string((uintptr_t)pSwapChain) + "]");
    g_IsResizing = true;
    OnDXResize(pSwapChain);

    // Phase 12 & 15: Total Integrity Restoration
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

    if (SUCCEEDED(hr)) {
        RegisterNativeResources(pSwapChain);
    }

    g_IsResizing = false;
    if (FAILED(hr)) {
        char hex[16];
        sprintf(hex, "0x%08X", (unsigned int)hr);
        Logger::error("HookedResizeBuffers1: FAILED with HR=" + std::string(hex));
    } else {
        Logger::info("HookedResizeBuffers1: Success");
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedGetBuffer(IDXGISwapChain* pSwapChain, UINT Buffer, REFIID riid, void** ppSurface) {
    if (g_InHook)
        return g_OriginalGetBuffer(pSwapChain, Buffer, riid, ppSurface);
    ScopedRecursionGuard guard;

    if (Buffer == 0) {
        if (DXUpscalerManager::Get().IsUpscalingEnabled()) {
            if (!DXUpscalerManager::Get().GetFakeBackBuffer()) {
                Logger::info("HookedGetBuffer: Fake BackBuffer is NULL, creating...");
                DXUpscalerManager::Get().CreateFakeBackBuffer(pSwapChain);
            }
            ID3D12Resource* fakeBuffer = DXUpscalerManager::Get().GetFakeBackBuffer();
            if (fakeBuffer) {
                HRESULT hr = fakeBuffer->QueryInterface(riid, ppSurface);
                if (SUCCEEDED(hr)) {
                    static uint64_t s_logCount = 0;
                    if (s_logCount++ % 100 == 0) {
                        Logger::info(
                            "DX Hooks: Redirected GetBuffer(0) to Fake BackBuffer (" + std::to_string((uintptr_t)*ppSurface) + ")");
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

void RegisterNativeResources(IDXGISwapChain* pSwapChain) {
    if (!pSwapChain)
        return;

    // Get Buffer Count
    DXGI_SWAP_CHAIN_DESC desc = {};
    if (FAILED(pSwapChain->GetDesc(&desc)))
        return;

    std::lock_guard<std::mutex> lock(g_TrackingMtx);
    for (UINT i = 0; i < desc.BufferCount; i++) {
        ID3D12Resource* pRes = nullptr;
        if (SUCCEEDED(pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pRes)))) {
            if (pRes) {
                g_NativeResources.insert(pRes);
                pRes->Release(); // GetBuffer adds a ref
            }
        }
    }
}

void ApplySwapChainHooks(void* pSwapChain) {
    std::lock_guard<std::mutex> lock(g_HookMtx);
    if (g_HookedVTables.count(pSwapChain))
        return;

    void** pVTable = *(void***)pSwapChain;
    Logger::info("DX Hooks: Patching Instance VTable for SC=" + std::to_string((uintptr_t)pSwapChain) +
                 " [VT=" + std::to_string((uintptr_t)pVTable) + "]");

    // Track native resources (Phase 18: Logo/UI Full-size fix)
    RegisterNativeResources((IDXGISwapChain*)pSwapChain);

    // SCAN FOR QUEUE OFFSET (Only if not found yet and we have a hint)
    // pHintQueue is passed from CreateSwapChain hooks
    if (g_CommandQueueOffset == 0) {
        std::lock_guard<std::mutex> lock(g_TrackingMtx);
        for (uint32_t i = 0; i < 1024; i += 8) {
            void* ptr = (void*)((uintptr_t)pSwapChain + i);
            if (IsBadReadPtr(ptr, 8))
                continue;

            ID3D12CommandQueue* possibleQueue = (ID3D12CommandQueue*)*(void**)ptr;
            if ((s_lastSeenQueue && possibleQueue == s_lastSeenQueue) || g_AllTrackedQueues.count(possibleQueue)) {
                g_CommandQueueOffset = i;
                Logger::info("DX Hooks: Found Command Queue Offset via Scan: " + std::to_string(i));
                SetDX12CommandQueueOffset(i);
                break;
            }
        }
    }

    // STEALTH VTABLE PATCHING (REFramework Style)
    // Instead of replacing the vptr on the instance (which triggers INVALID_CALL in RE Engine),
    // we surgically overwrite the function pointers in the ORIGINAL DirectX VTable.
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
        Logger::info("DX Hooks: Global SwapChain VTable entries patched (STEALTH MODE)");
    } else {
        Logger::error("DX Hooks: FAILED to VirtualProtect VTable for stealth patching!");
    }
}

void ApplySwapChainHooksWithQueue(void* pSwapChain, ID3D12CommandQueue* pQueue) {
    s_lastSeenQueue = pQueue;
    ApplySwapChainHooks(pSwapChain);
}

HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(
    IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
    if (g_InHook)
        return g_OriginalCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
    ScopedRecursionGuard guard;

    Logger::info("DX Factory: CreateSwapChain triggered");
    HRESULT hr = g_OriginalCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        Logger::info("DX Factory: New SwapChain created at " + std::to_string((uintptr_t)*ppSwapChain));
        ID3D12CommandQueue* queue = nullptr;
        if (pDevice && SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D12CommandQueue), (void**)&queue))) {
            ApplySwapChainHooksWithQueue(*ppSwapChain, queue);
            queue->Release();
        } else {
            ApplySwapChainHooks(*ppSwapChain);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(IDXGIFactory2* pFactory, IUnknown* pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain) {
    if (g_InHook)
        return g_OriginalCreateSwapChainForHwnd(pFactory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    ScopedRecursionGuard guard;

    Logger::info("DX Factory: CreateSwapChainForHwnd triggered (HWND=" + std::to_string((uintptr_t)hWnd) + ")");
    HRESULT hr = g_OriginalCreateSwapChainForHwnd(pFactory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        Logger::info("DX Factory: New SwapChain (Hwnd) created at " + std::to_string((uintptr_t)*ppSwapChain));
        ID3D12CommandQueue* queue = nullptr;
        if (pDevice && SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D12CommandQueue), (void**)&queue))) {
            ApplySwapChainHooksWithQueue(*ppSwapChain, queue);
            queue->Release();
        } else {
            ApplySwapChainHooks(*ppSwapChain);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForComposition(IDXGIFactory2* pFactory, IUnknown* pDevice,
    const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    if (g_InHook)
        return g_OriginalCreateSwapChainForComposition(pFactory, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
    ScopedRecursionGuard guard;

    Logger::info("DX Factory: CreateSwapChainForComposition triggered");
    HRESULT hr = g_OriginalCreateSwapChainForComposition(pFactory, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        Logger::info("DX Factory: New SwapChain (Composition) created at " + std::to_string((uintptr_t)*ppSwapChain));
        ID3D12CommandQueue* queue = nullptr;
        if (pDevice && SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D12CommandQueue), (void**)&queue))) {
            ApplySwapChainHooksWithQueue(*ppSwapChain, queue);
            queue->Release();
        } else {
            ApplySwapChainHooks(*ppSwapChain);
        }
    }
    return hr;
}

void HandleNewFactory(IUnknown* pFactory) {
    if (!pFactory)
        return;
    void** pFactoryVTable = *(void***)pFactory;

    // Hook internal SwapChain creation on THIS factory object
    MH_CreateHook(pFactoryVTable[10], (LPVOID)HookedCreateSwapChain, (LPVOID*)&g_OriginalCreateSwapChain);

    // Factory2/4 support
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
    Logger::info("DX Hooks: CreateDXGIFactory called");
    HRESULT hr = g_OriginalCreateDXGIFactory(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory)
        HandleNewFactory((IUnknown*)*ppFactory);
    return hr;
}

HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void** ppFactory) {
    if (g_InHook)
        return g_OriginalCreateDXGIFactory1(riid, ppFactory);
    ScopedRecursionGuard guard;
    Logger::info("DX Hooks: CreateDXGIFactory1 called");
    HRESULT hr = g_OriginalCreateDXGIFactory1(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory)
        HandleNewFactory((IUnknown*)*ppFactory);
    return hr;
}

HRESULT WINAPI HookedCreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    if (g_InHook)
        return g_OriginalCreateDXGIFactory2(Flags, riid, ppFactory);
    ScopedRecursionGuard guard;
    Logger::info("DX Hooks: CreateDXGIFactory2 called (Flags=" + std::to_string(Flags) + ")");
    HRESULT hr = g_OriginalCreateDXGIFactory2(Flags, riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory)
        HandleNewFactory((IUnknown*)*ppFactory);
    return hr;
}

void HookDXGIFactories() {
    // BROAD NET: Hook factory creation functions in DXGI.dll
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
        Logger::info("DX Hooks: Global DXGI Factory entry points hooked");
    }
}

} // namespace GamePlug
