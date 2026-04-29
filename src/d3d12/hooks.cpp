#include "common.h"
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include "minhook.h"
#include "upscaler_manager.h"
#include "config.h"
#include <mutex>
#include <set>
#include <map>
#include <vector>
#include <unordered_map>

namespace GamePlug {

typedef HRESULT (STDMETHODCALLTYPE* PFN_QueryInterface)(IUnknown* pUnk, REFIID riid, void** ppvObject);
typedef ULONG (STDMETHODCALLTYPE* PFN_Release)(IUnknown* pUnk);
typedef HRESULT (STDMETHODCALLTYPE* PFN_Present)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef HRESULT (STDMETHODCALLTYPE* PFN_Present1)(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
typedef HRESULT (STDMETHODCALLTYPE* PFN_ResizeBuffers)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
typedef HRESULT (STDMETHODCALLTYPE* PFN_ResizeBuffers1)(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pNodeMask, IUnknown* const* ppPresentQueue);
typedef void (STDMETHODCALLTYPE* PFN_ExecuteCommandLists)(ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
typedef HRESULT (STDMETHODCALLTYPE* PFN_Signal)(ID3D12CommandQueue* pQueue, ID3D12Fence* pFence, UINT64 Value);
typedef void (STDMETHODCALLTYPE* PFN_ResourceBarrier)(ID3D12GraphicsCommandList* pList, UINT NumBarriers, const D3D12_RESOURCE_BARRIER* pBarriers);
typedef void (STDMETHODCALLTYPE* PFN_RSSetViewports)(ID3D12GraphicsCommandList* pList, UINT NumViewports, const D3D12_VIEWPORT* pViewports);
typedef void (STDMETHODCALLTYPE* PFN_RSSetScissorRects)(ID3D12GraphicsCommandList* pList, UINT NumRects, const D3D12_RECT* pRects);
typedef void (STDMETHODCALLTYPE* PFN_OMSetRenderTargets)(ID3D12GraphicsCommandList* pList, UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors, BOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor);

typedef HRESULT (STDMETHODCALLTYPE* PFN_CreateCommittedResource)(
    ID3D12Device* pDevice,
    const D3D12_HEAP_PROPERTIES* pHeapProperties,
    D3D12_HEAP_FLAGS HeapFlags,
    const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialResourceState,
    const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    REFIID riidResource,
    void** ppvResource);

typedef HRESULT (STDMETHODCALLTYPE* PFN_CreatePlacedResource)(
    ID3D12Device* pDevice,
    ID3D12Heap* pHeap,
    UINT64 HeapOffset,
    const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialResourceState,
    const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    REFIID riidResource,
    void** ppvResource);

typedef HRESULT (STDMETHODCALLTYPE* PFN_CreateTexture2D)(
    ID3D11Device* pDevice,
    const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture2D** ppTexture2D);

typedef HRESULT (STDMETHODCALLTYPE* PFN_CreateSwapChain)(IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain);
typedef HRESULT (STDMETHODCALLTYPE* PFN_CreateSwapChainForHwnd)(IDXGIFactory2* pFactory, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);
typedef HRESULT (STDMETHODCALLTYPE* PFN_CreateSwapChainForComposition)(IDXGIFactory2* pFactory, IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);

PFN_QueryInterface g_OriginalQueryInterface = nullptr;
PFN_Release g_OriginalRelease = nullptr;
PFN_Present g_OriginalPresent = nullptr;
PFN_Present1 g_OriginalPresent1 = nullptr;
PFN_ResizeBuffers g_OriginalResizeBuffers = nullptr;
PFN_ResizeBuffers1 g_OriginalResizeBuffers1 = nullptr;
PFN_ExecuteCommandLists g_OriginalExecuteCommandLists = nullptr;
PFN_Signal g_OriginalSignal = nullptr;
PFN_ResourceBarrier g_OriginalResourceBarrier = nullptr;
PFN_RSSetViewports g_OriginalRSSetViewports = nullptr;
PFN_RSSetScissorRects g_OriginalRSSetScissorRects = nullptr;
PFN_OMSetRenderTargets g_OriginalOMSetRenderTargets = nullptr;
PFN_CreateCommittedResource g_OriginalCreateCommittedResource = nullptr;
PFN_CreatePlacedResource g_OriginalCreatePlacedResource = nullptr;

extern ID3D12Device* g_pd3d12Device;
extern ID3D12CommandQueue* g_pd3dCommandQueue;
static uint64_t g_frameCount = 0;
bool g_IsResizing = false; // Phase 10: Surgical Suppression

void SyncAllDX12Queues();
void ClearActiveQueues();
PFN_CreateTexture2D g_OriginalCreateTexture2D = nullptr;

PFN_CreateSwapChain g_OriginalCreateSwapChain = nullptr;
PFN_CreateSwapChainForHwnd g_OriginalCreateSwapChainForHwnd = nullptr;
PFN_CreateSwapChainForComposition g_OriginalCreateSwapChainForComposition = nullptr;

static thread_local bool g_InHook = false;

// Forward Declarations of engine-integration functions
extern void OnDXPresent(IDXGISwapChain* pSwapChain);
extern void OnDXResize(IDXGISwapChain* pSwapChain);
extern void SetDX12CommandQueue(ID3D12CommandQueue* pQueue);
extern void SetDX12CommandQueueOffset(uint32_t offset);
extern ID3D12CommandList* OnDXExecute(ID3D12CommandQueue* pQueue, bool isSignal);

// Resource Tracking for Viewport/Scissor Scaling
extern std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> g_ResourceStates;
static std::unordered_map<SIZE_T, ID3D12Resource*> g_RTVToResource;
static std::unordered_map<ID3D12GraphicsCommandList*, ID3D12Resource*> g_CommandListTargets;
static std::set<ID3D12Resource*> g_OverriddenResources;
static std::set<ID3D12Resource*> g_NativeResources; // SwapChain backbuffers
static std::mutex g_TrackingMtx;

typedef void (STDMETHODCALLTYPE* PFN_CreateRenderTargetView)(ID3D12Device* pDevice, ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
static PFN_CreateRenderTargetView g_OriginalCreateRenderTargetView = nullptr;

struct ScopedRecursionGuard {
    ScopedRecursionGuard() { g_InHook = true; }
    ~ScopedRecursionGuard() { g_InHook = false; }
};


// Forward Declarations
void STDMETHODCALLTYPE HookedRSSetViewports(ID3D12GraphicsCommandList* pList, UINT NumViewports, const D3D12_VIEWPORT* pViewports);
void STDMETHODCALLTYPE HookedRSSetScissorRects(ID3D12GraphicsCommandList* pList, UINT NumRects, const D3D12_RECT* pRects);
void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D12GraphicsCommandList* pList, UINT NumRTs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRTVs, BOOL singleHandle, const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV);
HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
HRESULT STDMETHODCALLTYPE HookedPresent1(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
HRESULT STDMETHODCALLTYPE HookedResizeBuffers1(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pNodeMask, IUnknown* const* ppPresentQueue);
ULONG STDMETHODCALLTYPE HookedRelease(IUnknown* pUnk);
HRESULT STDMETHODCALLTYPE HookedQueryInterface(IUnknown* pUnk, REFIID riid, void** ppvObject);
void RegisterNativeResources(IDXGISwapChain* pSwapChain);

static std::recursive_mutex g_HooksMtx;
std::map<ID3D12GraphicsCommandList*, std::pair<uint32_t, uint32_t>> g_CommandListRTSize;

void SetCommandListRTSize(ID3D12GraphicsCommandList* pList, uint32_t w, uint32_t h) {
    std::lock_guard<std::recursive_mutex> lock(g_HooksMtx);
    g_CommandListRTSize[pList] = { w, h };
}

std::pair<uint32_t, uint32_t> GetCommandListRTSize(ID3D12GraphicsCommandList* pList) {
    std::lock_guard<std::recursive_mutex> lock(g_HooksMtx);
    auto it = g_CommandListRTSize.find(pList);
    if (it != g_CommandListRTSize.end()) return it->second;
    return { 0, 0 };
}

void STDMETHODCALLTYPE HookedRSSetViewports(ID3D12GraphicsCommandList* pList, UINT Count, const D3D12_VIEWPORT* pViewports) {
    if (g_InHook) {
        g_OriginalRSSetViewports(pList, Count, pViewports);
        return;
    }
    ScopedRecursionGuard guard;

    if (Count > 0 && pViewports && DXUpscalerManager::Get().IsUpscalingEnabled()) {
        // Stability Fix: Bypass scaling during first 50 frames or if RT not ready
        if (g_frameCount < 50 || !DXUpscalerManager::Get().HasValidRT()) {
            g_OriginalRSSetViewports(pList, Count, pViewports);
            return;
        }

        uint32_t rw = DXUpscalerManager::Get().GetRenderWidth();
        uint32_t rh = DXUpscalerManager::Get().GetRenderHeight();
        if (g_frameCount < 50 || !DXUpscalerManager::Get().HasValidRT()) {
            g_OriginalRSSetViewports(pList, Count, pViewports);
            return;
        }
        uint32_t dw = DXUpscalerManager::Get().GetDisplayWidth();
        uint32_t dh = DXUpscalerManager::Get().GetDisplayHeight();

        float scaleX = (float)rw / (float)dw;
        float scaleY = (float)rh / (float)dh;

        // NEW LOGIC: Check target classification
        bool isScaledTarget = false;
        bool isNativeTarget = false;
        ID3D12Resource* pTarget = nullptr;

        {
            std::lock_guard<std::mutex> lock(g_TrackingMtx);
            if (g_CommandListTargets.count(pList)) {
                pTarget = g_CommandListTargets[pList];
                if (g_OverriddenResources.count(pTarget)) {
                    isScaledTarget = true;
                }
                if (g_NativeResources.count(pTarget)) {
                    isNativeTarget = true;
                }
            }
        }

        // Viewport Check for Fallback
        bool isFullDisplayViewport = (pViewports[0].Width >= (float)dw * 0.95f) && (pViewports[0].Height >= (float)dh * 0.95f);

        bool shouldScale = false;
        const char* reason = "Unknown";

        if (isNativeTarget) {
            shouldScale = false;
            reason = "Native SwapChain Target";
        } else if (isScaledTarget) {
            shouldScale = true;
            reason = "Overridden Engine Target";
        } else if (pTarget == nullptr) {
            // Fallback: If we don't know the target, scale ONLY if it's a full-screen viewport
            if (isFullDisplayViewport) {
                shouldScale = true;
                reason = "Unknown Target (Full Screen Fallback)";
            } else {
                shouldScale = false;
                reason = "Unknown Target (Partial Viewport)";
            }
        } else {
            // We know the target, but it's neither overridden nor native. 
            // This is likely a small utility buffer (MIP, shadow, etc.)
            shouldScale = false;
            reason = "Normal Internal Buffer (Small)";
        }

        static uint32_t s_scaleCount = 0;
        if (!shouldScale) {
            if (s_scaleCount < 100 && isFullDisplayViewport) {
                Logger::info("RSSetViewports: SKIP scaling (" + std::string(reason) + ") @ " + std::to_string((int)pViewports[0].Width) + "x" + std::to_string((int)pViewports[0].Height));
                s_scaleCount++;
            }
            g_OriginalRSSetViewports(pList, Count, pViewports);
            return;
        }

        std::vector<D3D12_VIEWPORT> scaled(Count);
        for (UINT i = 0; i < Count; i++) {
            scaled[i] = pViewports[i];
            scaled[i].TopLeftX *= scaleX;
            scaled[i].TopLeftY *= scaleY;
            scaled[i].Width *= scaleX;
            scaled[i].Height *= scaleY;
        }

        static uint32_t s_logCount = 0;
        if (s_logCount < 500) {
            Logger::info("DX12: SCALING VIEWPORT [Reason=" + std::string(reason) + "] " +
                         std::to_string((int)pViewports[0].Width) + "x" + std::to_string((int)pViewports[0].Height) + 
                         " @ (" + std::to_string((int)pViewports[0].TopLeftX) + "," + std::to_string((int)pViewports[0].TopLeftY) + ") -> " +
                         std::to_string((int)scaled[0].Width) + "x" + std::to_string((int)scaled[0].Height));
            s_logCount++;
        }

        g_OriginalRSSetViewports(pList, Count, scaled.data());
        return;
    }

    g_OriginalRSSetViewports(pList, Count, pViewports);
}

void STDMETHODCALLTYPE HookedRSSetScissorRects(ID3D12GraphicsCommandList* pList, UINT Count, const D3D12_RECT* pRects) {
    if (g_InHook) {
        g_OriginalRSSetScissorRects(pList, Count, pRects);
        return;
    }
    ScopedRecursionGuard guard;

    if (Count > 0 && pRects && DXUpscalerManager::Get().IsUpscalingEnabled()) {
        if (g_frameCount < 50 || !DXUpscalerManager::Get().HasValidRT()) {
            g_OriginalRSSetScissorRects(pList, Count, pRects);
            return;
        }

        uint32_t rw = DXUpscalerManager::Get().GetRenderWidth();
        uint32_t rh = DXUpscalerManager::Get().GetRenderHeight();
        uint32_t dw = DXUpscalerManager::Get().GetDisplayWidth();
        uint32_t dh = DXUpscalerManager::Get().GetDisplayHeight();

        float scaleX = (float)rw / (float)dw;
        float scaleY = (float)rh / (float)dh;

        bool shouldScale = false;
        {
            std::lock_guard<std::mutex> lock(g_TrackingMtx);
            if (g_CommandListTargets.count(pList)) {
                ID3D12Resource* pTarget = g_CommandListTargets[pList];
                // Scale if it's an overridden buffer OR if it's unknown but large
                if (g_OverriddenResources.count(pTarget)) {
                    shouldScale = true;
                } else if (!g_NativeResources.count(pTarget)) {
                    // Fallback for scissors: if it's a huge scissor rect, it's likely for a scene RT we missed
                    if ((pRects[0].right - pRects[0].left) >= (LONG)dw * 0.9f) {
                        shouldScale = true;
                    }
                }
            }
        }

        if (!shouldScale) {
            g_OriginalRSSetScissorRects(pList, Count, pRects);
            return;
        }

        std::vector<D3D12_RECT> scaled(Count);
        bool needsScaling = false;

        for (UINT i = 0; i < Count; i++) {
            scaled[i] = pRects[i];

            // Handle massive values (like -32768, 32767) or 1080p values
            if (scaled[i].left < 0 || scaled[i].top < 0 || 
                (scaled[i].right - scaled[i].left) > (LONG)rw || 
                (scaled[i].bottom - scaled[i].top) > (LONG)rh) {
                
                // If it's a "Global" scissor, just clamp to target
                if (scaled[i].left <= -10000 || scaled[i].right >= 20000) {
                    scaled[i].left = 0;
                    scaled[i].top = 0;
                    scaled[i].right = (LONG)rw;
                    scaled[i].bottom = (LONG)rh;
                } else {
                    // Normal coordinate scaling
                    scaled[i].left = (LONG)((float)scaled[i].left * scaleX);
                    scaled[i].top = (LONG)((float)scaled[i].top * scaleY);
                    scaled[i].right = (LONG)((float)scaled[i].right * scaleX);
                    scaled[i].bottom = (LONG)((float)scaled[i].bottom * scaleY);
                }
                needsScaling = true;
            }
        }

        if (needsScaling) {
            static int s_scallCount = 0;
            if (s_scallCount < 500 && s_scallCount % 100 == 0) {
                Logger::warn("DX12: SCALING SCISSOR [" + std::to_string(Count) + "] (" + 
                             std::to_string(pRects[0].left) + "," + std::to_string(pRects[0].top) + " - " + 
                             std::to_string(pRects[0].right) + "," + std::to_string(pRects[0].bottom) + ") -> (" +
                             std::to_string(scaled[0].left) + "," + std::to_string(scaled[0].top) + " - " + 
                             std::to_string(scaled[0].right) + "," + std::to_string(scaled[0].bottom) + ")");
            }
            s_scallCount++;
            g_OriginalRSSetScissorRects(pList, Count, scaled.data());
            return;
        }
    }

    g_OriginalRSSetScissorRects(pList, Count, pRects);
}

void STDMETHODCALLTYPE HookedOMSetRenderTargets(
    ID3D12GraphicsCommandList* pList,
    UINT NumRTs,
    const D3D12_CPU_DESCRIPTOR_HANDLE* pRTVs,
    BOOL singleHandle,
    const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV)
{
    static uint32_t s_logCount = 0;
    if (s_logCount < 100) {
        std::string targetInfo = "None";
        if (NumRTs > 0 && pRTVs) {
            std::lock_guard<std::mutex> lock(g_TrackingMtx);
            if (g_RTVToResource.count(pRTVs[0].ptr)) {
                targetInfo = std::to_string((uintptr_t)g_RTVToResource[pRTVs[0].ptr]);
            }
        }
        Logger::info("DX12: HookedOMSetRenderTargets triggered (NumRTs=" + std::to_string(NumRTs) + " Target=" + targetInfo + ")");
        s_logCount++;
    }

    if (g_InHook) {
        g_OriginalOMSetRenderTargets(pList, NumRTs, pRTVs, singleHandle, pDSV);
        return;
    }
    ScopedRecursionGuard guard;

    if (NumRTs > 0 && pRTVs) {
        std::lock_guard<std::mutex> lock(g_TrackingMtx);
        // We only care about the first RT for viewport scaling purposes
        if (g_RTVToResource.count(pRTVs[0].ptr)) {
            g_CommandListTargets[pList] = g_RTVToResource[pRTVs[0].ptr];
        } else {
            // If we don't know this RTV, clear the target for safety
            g_CommandListTargets.erase(pList);
        }
    }

    g_OriginalOMSetRenderTargets(pList, NumRTs, pRTVs, singleHandle, pDSV);
}

void STDMETHODCALLTYPE HookedCreateRenderTargetView(ID3D12Device* pDevice, ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    static uint32_t s_logCount = 0;
    if (s_logCount < 100) {
        Logger::info("DX12: HookedCreateRenderTargetView triggered [Res=" + std::to_string((uintptr_t)pResource) + "]");
        s_logCount++;
    }

    if (g_InHook) {
        g_OriginalCreateRenderTargetView(pDevice, pResource, pDesc, DestDescriptor);
        return;
    }
    ScopedRecursionGuard guard;

    if (pResource) {
        std::lock_guard<std::mutex> lock(g_TrackingMtx);
        g_RTVToResource[DestDescriptor.ptr] = pResource;
    }

    g_OriginalCreateRenderTargetView(pDevice, pResource, pDesc, DestDescriptor);
}

static std::mutex g_HookMtx;
static std::set<void*> g_HookedVTables;

// OFFSET SCANNER
static uint32_t g_CommandQueueOffset = 0;
static void* g_LastHookedSwapChain = nullptr;
extern void SetLastEngineRenderTarget(ID3D12Resource* pRes);

void ApplySwapChainHooks(void* pSwapChain);

void STDMETHODCALLTYPE HookedExecuteCommandLists(ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
    if (g_InHook) {
        g_OriginalExecuteCommandLists(pQueue, NumCommandLists, ppCommandLists);
        return;
    }
    ScopedRecursionGuard guard;

    // Discovery & Synchronization
    SetDX12CommandQueue(pQueue);

    // Call original first to let the engine finish its work
    g_OriginalExecuteCommandLists(pQueue, NumCommandLists, ppCommandLists);

    // Now signal that FSR is ready to run in the Present pass
    DXUpscalerManager::Get().MarkFSRReady();
}
void HookCommandQueue(ID3D12CommandQueue* pQueue) {
    if (!pQueue) return;
    std::lock_guard<std::mutex> lock(g_HookMtx);
    
    // Safety check: is this instance already tracked, or is the GLOBAL VTable already patched?
    static bool s_QueueVTablePatched = false;
    if (g_HookedVTables.count(pQueue) || s_QueueVTablePatched) {
        g_HookedVTables.insert(pQueue); // Track instance anyway
        return;
    }

    void** pVTable = *(void***)pQueue;
    
    // Index 10 is ExecuteCommandLists
    if (!g_OriginalExecuteCommandLists) g_OriginalExecuteCommandLists = (PFN_ExecuteCommandLists)pVTable[10];
    if (!g_OriginalSignal) g_OriginalSignal = (PFN_Signal)pVTable[13];

    DWORD old;
    if (VirtualProtect(pVTable, 16 * sizeof(void*), PAGE_READWRITE, &old)) {
        pVTable[10] = (void*)HookedExecuteCommandLists;
        VirtualProtect(pVTable, 16 * sizeof(void*), old, &old);
        
        s_QueueVTablePatched = true;
        g_HookedVTables.insert(pQueue);
        Logger::warn("ExecuteCommandLists HOOK INSTALLED on Queue ID: " + std::to_string((uintptr_t)pQueue));
    }
}


void SyncAllDX12Queues() {
    if (g_pd3dCommandQueue) {
        // Phase 17: Minimize to only the active SwapChain queue
        ID3D12Fence* fence = nullptr;
        if (SUCCEEDED(g_pd3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            HANDLE event = CreateEvent(NULL, FALSE, FALSE, NULL);
            g_pd3dCommandQueue->Signal(fence, 1);
            fence->SetEventOnCompletion(1, event);
            WaitForSingleObject(event, INFINITE);
            CloseHandle(event);
            fence->Release();
        }
    }
    Logger::info(" - Engine Main Queue Synchronized");
}

void ClearActiveQueues() {
    // Phase 17: Global tracking deactivated
}

ULONG STDMETHODCALLTYPE HookedRelease(IUnknown* pUnk) {
    if (g_InHook) return g_OriginalRelease(pUnk);
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

// Phase 17: Global Pulse Hooks deactivated for maximum stability in RE4

void STDMETHODCALLTYPE HookedResourceBarrier(ID3D12GraphicsCommandList* pList, UINT NumBarriers, const D3D12_RESOURCE_BARRIER* pBarriers) {
    if (g_InHook) {
        g_OriginalResourceBarrier(pList, NumBarriers, pBarriers);
        return;
    }
    ScopedRecursionGuard guard;

    static int s_logCounter = 0;
    for (UINT i = 0; i < NumBarriers; i++) {
        if (pBarriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            auto& t = pBarriers[i].Transition;
            ID3D12Resource* pRes = t.pResource;

            // ALWAYS track resource states for all transitions
            g_ResourceStates[pRes] = t.StateAfter;

            D3D12_RESOURCE_DESC d = pRes->GetDesc();

            uint32_t rw = DXUpscalerManager::Get().GetRenderWidth();
            uint32_t rh = DXUpscalerManager::Get().GetRenderHeight();

            // Safety: If upscaler haven't determined resolution yet, skip discovery to avoid picking noise
            if (rw == 0 || rh == 0) continue;

            // TIGHTENED LOGIC: Source buffer MUST be very close to our internal render resolution
            // We allow a small margin (5%) for engine padding/alignment
            bool isValidSize = (d.Width >= rw * 0.95f && d.Width <= rw * 1.05f) && 
                               (d.Height >= rh * 0.95f && d.Height <= rh * 1.05f);
            
            bool isRT = (t.StateAfter & D3D12_RESOURCE_STATE_RENDER_TARGET);

            // Optimization: If we HAVE specifically overridden this resource, it's a near-certain match
            bool isOurResource = false;
            {
                std::lock_guard<std::mutex> lock(g_TrackingMtx);
                if (g_OverriddenResources.count(pRes)) {
                    isOurResource = true;
                }
            }

            if (!isOurResource) {
                if (!isValidSize || !isRT)
                    continue;
            }

            // Accept ONLY real scene buffers (HDR or high-quality SDR)
            bool isSupportedFormat = 
                (d.Format == DXGI_FORMAT_R11G11B10_FLOAT) || 
                (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) || 
                (d.Format == DXGI_FORMAT_R8G8B8A8_UNORM);

            if (!isSupportedFormat)
                continue;

            // SAFE to use captured final scene-like RT
            SetLastEngineRenderTarget(pRes);
            DXUpscalerManager::Get().SetHasValidRT(true);

            if (s_logCounter < 500 && s_logCounter % 100 == 0) {
                Logger::warn(
                    "FINAL RT CANDIDATE: " +
                    std::to_string(d.Width) + "x" +
                    std::to_string(d.Height) + " fmt=" + std::to_string(d.Format) + " PTR=" + std::to_string((uintptr_t)pRes)
                );
            }
            s_logCounter++;
        }
    }
    g_OriginalResourceBarrier(pList, NumBarriers, pBarriers);
}

// FSR1 Render Target Overriding Logic

bool ShouldOverrideD3D12(const D3D12_RESOURCE_DESC& desc) {
    if (!Config::Get().GetBool("ReGame", false)) return false;
    if (!DXUpscalerManager::Get().IsUpscalingEnabled()) return false;

    uint32_t dw = DXUpscalerManager::Get().GetDisplayWidth();
    uint32_t dh = DXUpscalerManager::Get().GetDisplayHeight();

    // Core Check: Does it match the target display resolution exactly?
    if (desc.Width != dw || desc.Height != dh) return false;

    // Check if it's a primary render resource
    bool isRenderTarget = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    bool isUAV = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    bool isDepth = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    // Some engines use simple textures and blit to them. 
    // If it's 1080p and has ANY of these flags, it's a candidate for downsampling.
    if (isRenderTarget || isUAV || isDepth) {
        return true;
    }

    return false;
}

HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource(
    ID3D12Device* pDevice,
    const D3D12_HEAP_PROPERTIES* pHeapProperties,
    D3D12_HEAP_FLAGS HeapFlags,
    const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialResourceState,
    const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    REFIID riidResource,
    void** ppvResource)
{
    if (g_InHook) return g_OriginalCreateCommittedResource(pDevice, pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource);
    ScopedRecursionGuard guard;

    D3D12_RESOURCE_DESC desc = *pDesc;
    bool overridden = false;

    if (ShouldOverrideD3D12(desc)) {
        uint32_t renderW = DXUpscalerManager::Get().GetRenderWidth();
        uint32_t renderH = DXUpscalerManager::Get().GetRenderHeight();

        if (renderW > 0 && renderH > 0) {
            Logger::info("FSR1: Override DX12 RT " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " -> " + std::to_string(renderW) + "x" + std::to_string(renderH));
            desc.Width = renderW;
            desc.Height = renderH;
            overridden = true;
        }
    }

    ID3D12Resource* pRes = nullptr;
    HRESULT hr = g_OriginalCreateCommittedResource(pDevice, pHeapProperties, HeapFlags, &desc, InitialResourceState, pOptimizedClearValue, riidResource, (void**)&pRes);

    if (SUCCEEDED(hr) && pRes) {
        if (overridden) {
            std::lock_guard<std::mutex> lock(g_TrackingMtx);
            g_OverriddenResources.insert(pRes);
        }
        if (ppvResource) *ppvResource = pRes;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource(
    ID3D12Device* pDevice,
    ID3D12Heap* pHeap,
    UINT64 HeapOffset,
    const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialResourceState,
    const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    REFIID riidResource,
    void** ppvResource)
{
    if (g_InHook) return g_OriginalCreatePlacedResource(pDevice, pHeap, HeapOffset, pDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource);
    ScopedRecursionGuard guard;
    Logger::info("HookedCreatePlacedResource: CreatePlacedResource");
    D3D12_RESOURCE_DESC desc = *pDesc;
    bool overridden = false;

    if (ShouldOverrideD3D12(desc)) {
        uint32_t renderW = DXUpscalerManager::Get().GetRenderWidth();
        uint32_t renderH = DXUpscalerManager::Get().GetRenderHeight();

        if (renderW > 128 && renderH > 128) { // Extra safety for tiny textures
            Logger::info("FSR1: Override DX12 Placed RT " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " -> " + std::to_string(renderW) + "x" + std::to_string(renderH));
            desc.Width = renderW;
            desc.Height = renderH;
            overridden = true;
        }
    }

    ID3D12Resource* pRes = nullptr;
    HRESULT hr = g_OriginalCreatePlacedResource(pDevice, pHeap, HeapOffset, &desc, InitialResourceState, pOptimizedClearValue, riidResource, (void**)&pRes);

    if (SUCCEEDED(hr) && pRes) {
        if (overridden) {
            std::lock_guard<std::mutex> lock(g_TrackingMtx);
            g_OverriddenResources.insert(pRes);
        }
        if (ppvResource) *ppvResource = pRes;
    }
    return hr;
}

bool ShouldOverrideD3D11(const D3D11_TEXTURE2D_DESC& desc) {
    return false;
}

HRESULT STDMETHODCALLTYPE HookedCreateTexture2D(
    ID3D11Device* pDevice,
    const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture2D** ppTexture2D)
{
    if (g_InHook) return g_OriginalCreateTexture2D(pDevice, pDesc, pInitialData, ppTexture2D);
    ScopedRecursionGuard guard;

    D3D11_TEXTURE2D_DESC desc = *pDesc;
    if (ShouldOverrideD3D11(desc)) {
        uint32_t renderW = DXUpscalerManager::Get().GetRenderWidth();
        uint32_t renderH = DXUpscalerManager::Get().GetRenderHeight();

        if (renderW > 0 && renderH > 0) {
            Logger::info("FSR1: Override DX11 RT " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " -> " + std::to_string(renderW) + "x" + std::to_string(renderH));
            desc.Width = renderW;
            desc.Height = renderH;
        }
    }

    return g_OriginalCreateTexture2D(pDevice, &desc, pInitialData, ppTexture2D);
}

HRESULT STDMETHODCALLTYPE HookedQueryInterface(IUnknown* pUnk, REFIID riid, void** ppvObject) {
    if (g_InHook) return g_OriginalQueryInterface(pUnk, riid, ppvObject);
    ScopedRecursionGuard guard;

    HRESULT hr = g_OriginalQueryInterface(pUnk, riid, ppvObject);
    if (SUCCEEDED(hr) && ppvObject && *ppvObject) {
        bool isSwapChain = false;
        std::string iidName = "Unknown";
        
        if (riid == __uuidof(IDXGISwapChain)) { isSwapChain = true; iidName = "IDXGISwapChain"; }
        else if (riid == __uuidof(IDXGISwapChain1)) { isSwapChain = true; iidName = "IDXGISwapChain1"; }
        else if (riid == __uuidof(IDXGISwapChain2)) { isSwapChain = true; iidName = "IDXGISwapChain2"; }
        else if (riid == __uuidof(IDXGISwapChain3)) { isSwapChain = true; iidName = "IDXGISwapChain3"; }
        else if (riid == __uuidof(IDXGISwapChain4)) { isSwapChain = true; iidName = "IDXGISwapChain4"; }
        
        if (isSwapChain) {
            Logger::info("DX Hooks: QueryInterface returned " + iidName + " interface at " + std::to_string((uintptr_t)*ppvObject));
            ApplySwapChainHooks(*ppvObject);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (g_InHook) return g_OriginalPresent(pSwapChain, SyncInterval, Flags);
    ScopedRecursionGuard guard;

    g_frameCount++;
    Logger::info("HookedPresent Entry [SC=" + std::to_string((uintptr_t)pSwapChain) + " VT=" + std::to_string((uintptr_t)(*(void***)pSwapChain)) + "]");
    OnDXPresent(pSwapChain);
    return g_OriginalPresent(pSwapChain, SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE HookedPresent1(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    if (g_InHook) return g_OriginalPresent1(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
    ScopedRecursionGuard guard;

    static uint64_t s_present1Calls = 0;
    if (s_present1Calls++ % 60 == 0) {
        Logger::info("HookedPresent1 Entry [" + std::to_string(s_present1Calls) + "] SC=" + std::to_string((uintptr_t)pSwapChain));
    }
    g_frameCount++;
    OnDXPresent(pSwapChain);
    return g_OriginalPresent1(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    if (g_InHook) return g_OriginalResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    ScopedRecursionGuard guard;

    Logger::info("HookedResizeBuffers Entry [SC=" + std::to_string((uintptr_t)pSwapChain) + "]");
    g_IsResizing = true;
    DXUpscalerManager::Get().UpdateDimensions(Width, Height);
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
        char hex[16]; sprintf(hex, "0x%08X", (unsigned int)hr);
        Logger::error("HookedResizeBuffers: FAILED with HR=" + std::string(hex));
    } else {
        Logger::info("HookedResizeBuffers: Success");
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers1(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pNodeMask, IUnknown* const* ppPresentQueue) {
    if (g_InHook) return g_OriginalResizeBuffers1(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags, pNodeMask, ppPresentQueue);
    ScopedRecursionGuard guard;

    Logger::info("HookedResizeBuffers1 Entry [SC=" + std::to_string((uintptr_t)pSwapChain) + "]");
    g_IsResizing = true;
    DXUpscalerManager::Get().UpdateDimensions(Width, Height);
    OnDXResize(pSwapChain);

    // Phase 12 & 15: Total Integrity Restoration
    MH_DisableHook(MH_ALL_HOOKS);
    
    void** vtable = *(void***)pSwapChain;
    DWORD old;
    if (VirtualProtect(vtable, 128 * sizeof(void*), PAGE_READWRITE, &old)) {
        vtable[0] = (void*)g_OriginalQueryInterface;
        vtable[2] = (void*)g_OriginalRelease;
        vtable[8] = (void*)g_OriginalPresent;
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
        char hex[16]; sprintf(hex, "0x%08X", (unsigned int)hr);
        Logger::error("HookedResizeBuffers1: FAILED with HR=" + std::string(hex));
    } else {
        Logger::info("HookedResizeBuffers1: Success");
    }
    return hr;
}

void RegisterNativeResources(IDXGISwapChain* pSwapChain) {
    if (!pSwapChain) return;
    
    // Get Buffer Count
    DXGI_SWAP_CHAIN_DESC desc = {};
    if (FAILED(pSwapChain->GetDesc(&desc))) return;

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

static ID3D12CommandQueue* s_lastSeenQueue = nullptr;

void ApplySwapChainHooks(void* pSwapChain) {
    std::lock_guard<std::mutex> lock(g_HookMtx);
    if (g_HookedVTables.count(pSwapChain)) return;

    void** pVTable = *(void***)pSwapChain;
    Logger::info("DX Hooks: Patching Instance VTable for SC=" + std::to_string((uintptr_t)pSwapChain) + " [VT=" + std::to_string((uintptr_t)pVTable) + "]");

    // Track native resources (Phase 18: Logo/UI Full-size fix)
    RegisterNativeResources((IDXGISwapChain*)pSwapChain);

    // SCAN FOR QUEUE OFFSET (Only if not found yet and we have a hint)
    // pHintQueue is passed from CreateSwapChain hooks
    if (g_CommandQueueOffset == 0 && s_lastSeenQueue) {
        for (uint32_t i = 0; i < 1024; i += 8) {
            void* ptr = (void*)((uintptr_t)pSwapChain + i);
            if (IsBadReadPtr(ptr, 8)) continue;
            if (*(void**)ptr == s_lastSeenQueue) {
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
    if (!g_OriginalQueryInterface) g_OriginalQueryInterface = (PFN_QueryInterface)pVTable[0];
    if (!g_OriginalRelease) g_OriginalRelease = (PFN_Release)pVTable[2];
    if (!g_OriginalPresent) g_OriginalPresent = (PFN_Present)pVTable[8];
    if (!g_OriginalResizeBuffers) g_OriginalResizeBuffers = (PFN_ResizeBuffers)pVTable[13];
    if (!g_OriginalPresent1) g_OriginalPresent1 = (PFN_Present1)pVTable[22]; 
    if (!g_OriginalResizeBuffers1) g_OriginalResizeBuffers1 = (PFN_ResizeBuffers1)pVTable[31];

    // Patch original VTable memory
    DWORD old;
    if (VirtualProtect(pVTable, 128 * sizeof(void*), PAGE_READWRITE, &old)) {
        pVTable[0] = (void*)HookedQueryInterface;
        pVTable[2] = (void*)HookedRelease;
        pVTable[8] = (void*)HookedPresent;
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

static ID3D12CommandQueue* s_PendingQueue = nullptr;
void ApplySwapChainHooksWithQueue(void* pSwapChain, ID3D12CommandQueue* pQueue) {
    s_lastSeenQueue = pQueue;
    ApplySwapChainHooks(pSwapChain);
}

HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
    if (g_InHook) return g_OriginalCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
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

HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(IDXGIFactory2* pFactory, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    if (g_InHook) return g_OriginalCreateSwapChainForHwnd(pFactory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
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

HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForComposition(IDXGIFactory2* pFactory, IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    if (g_InHook) return g_OriginalCreateSwapChainForComposition(pFactory, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
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

// Global hook IDs for all factory versions
typedef HRESULT (WINAPI* PFN_CreateDXGIFactory)(REFIID riid, void** ppFactory);
typedef HRESULT (WINAPI* PFN_CreateDXGIFactory2)(UINT Flags, REFIID riid, void** ppFactory);

PFN_CreateDXGIFactory g_OriginalCreateDXGIFactory = nullptr;
PFN_CreateDXGIFactory g_OriginalCreateDXGIFactory1 = nullptr;
PFN_CreateDXGIFactory2 g_OriginalCreateDXGIFactory2 = nullptr;

void HandleNewFactory(IUnknown* pFactory) {
    if (!pFactory) return;
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
    if (g_InHook) return g_OriginalCreateDXGIFactory(riid, ppFactory);
    ScopedRecursionGuard guard;
    Logger::info("DX Hooks: CreateDXGIFactory called");
    HRESULT hr = g_OriginalCreateDXGIFactory(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory) HandleNewFactory((IUnknown*)*ppFactory);
    return hr;
}

HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void** ppFactory) { 
    if (g_InHook) return g_OriginalCreateDXGIFactory1(riid, ppFactory);
    ScopedRecursionGuard guard;
    Logger::info("DX Hooks: CreateDXGIFactory1 called");
    HRESULT hr = g_OriginalCreateDXGIFactory1(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory) HandleNewFactory((IUnknown*)*ppFactory);
    return hr;
}

HRESULT WINAPI HookedCreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) { 
    if (g_InHook) return g_OriginalCreateDXGIFactory2(Flags, riid, ppFactory);
    ScopedRecursionGuard guard;
    Logger::info("DX Hooks: CreateDXGIFactory2 called (Flags=" + std::to_string(Flags) + ")");
    HRESULT hr = g_OriginalCreateDXGIFactory2(Flags, riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory) HandleNewFactory((IUnknown*)*ppFactory);
    return hr;
}

void HookDXGIFactories() {
    // BROAD NET: Hook factory creation functions in DXGI.dll
    HMODULE dxgi = GetModuleHandle("dxgi.dll");
    if (dxgi) {
        void* pCreateFactory = GetProcAddress(dxgi, "CreateDXGIFactory");
        void* pCreateFactory1 = GetProcAddress(dxgi, "CreateDXGIFactory1");
        void* pCreateFactory2 = GetProcAddress(dxgi, "CreateDXGIFactory2");

        if (pCreateFactory) MH_CreateHook(pCreateFactory, (LPVOID)HookedCreateDXGIFactory, (LPVOID*)&g_OriginalCreateDXGIFactory);
        if (pCreateFactory1) MH_CreateHook(pCreateFactory1, (LPVOID)HookedCreateDXGIFactory1, (LPVOID*)&g_OriginalCreateDXGIFactory1);
        if (pCreateFactory2) MH_CreateHook(pCreateFactory2, (LPVOID)HookedCreateDXGIFactory2, (LPVOID*)&g_OriginalCreateDXGIFactory2);
        
        MH_EnableHook(MH_ALL_HOOKS);
        Logger::info("DX Hooks: Global DXGI Factory entry points hooked");
    }
}

void InstallDXGIHooks() {
    Logger::info("InstallDXGIHooks: Start (Signal-Sync Logic Active)");
    DXUpscalerManager::Get().EarlyInit();
    
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "GamePlugDummy", NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow("GamePlugDummy", "GamePlug Dummy Window", WS_OVERLAPPEDWINDOW, 100, 100, 100, 100, NULL, NULL, wc.hInstance, NULL);

    ID3D12Device* d3d12Device = nullptr;
    ID3D12CommandQueue* commandQueue = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    IDXGIFactory4* dxgiFactory = nullptr;
    ID3D12CommandAllocator* commandAllocator = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;

    HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&d3d12Device);
    if (FAILED(hr)) {
        Logger::error("DX Hooks: D3D12CreateDevice failed");
        // Don't goto cleanup yet, we might still want DX11
    }

    ID3D11Device* d3d11Device = nullptr;
    ID3D11DeviceContext* d3d11Context = nullptr;
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &d3d11Device, &featureLevel, &d3d11Context);
    if (FAILED(hr)) {
        Logger::error("DX Hooks: D3D11CreateDevice failed");
    }

    if (!d3d12Device && !d3d11Device) goto cleanup;

    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr = d3d12Device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), (void**)&commandQueue);
        if (FAILED(hr)) goto cleanup;

        void** pQueueVTable = *(void***)commandQueue;
        MH_CreateHook(pQueueVTable[12], (LPVOID)HookedExecuteCommandLists, (LPVOID*)&g_OriginalExecuteCommandLists);
    }

    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&dxgiFactory);
    if (FAILED(hr)) goto cleanup;

    {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        hr = dxgiFactory->CreateSwapChain(commandQueue, &sd, &swapChain);
    }

    if (swapChain) {
        void** pVTable = *(void***)swapChain;
        void** pFactoryVTable = *(void***)dxgiFactory;

        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            Logger::error("DX Hooks: MinHook failed to initialize");
        } else {
            // SwapChain hooks are now Instance-based shadow VTable patching
            ApplySwapChainHooks(swapChain);

            MH_CreateHook(pFactoryVTable[10], (LPVOID)HookedCreateSwapChain, (LPVOID*)&g_OriginalCreateSwapChain);
            
            IDXGIFactory2* factory2 = nullptr;
            if (SUCCEEDED(dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), (void**)&factory2))) {
                void** pFactory2VTable = *(void***)factory2;
                MH_CreateHook(pFactory2VTable[15], (LPVOID)HookedCreateSwapChainForHwnd, (LPVOID*)&g_OriginalCreateSwapChainForHwnd);
                MH_CreateHook(pFactory2VTable[16], (LPVOID)HookedCreateSwapChainForComposition, (LPVOID*)&g_OriginalCreateSwapChainForComposition);
                factory2->Release();
            }

            // Phase 17: Global Queue Hooks deactivated to restore 'Previous' stability

            // Create a dummy command list to get the VTable for D3D12 Graphics methods
            hr = d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
            if (SUCCEEDED(hr)) {
                hr = d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
                if (SUCCEEDED(hr)) {
                    void** pListVTable = *(void***)commandList;
                    
                    // Hook RSSetViewports (21) and RSSetScissorRects (22) for centering fixes
                    MH_CreateHook(pListVTable[21], (LPVOID)HookedRSSetViewports, (LPVOID*)&g_OriginalRSSetViewports);
                    MH_CreateHook(pListVTable[22], (LPVOID)HookedRSSetScissorRects, (LPVOID*)&g_OriginalRSSetScissorRects);
                    
                    // Hook ResourceBarrier (Index 26) - Most stable for discovery
                    MH_CreateHook(pListVTable[26], (LPVOID)HookedResourceBarrier, (LPVOID*)&g_OriginalResourceBarrier);

                    MH_CreateHook(pListVTable[33], (LPVOID)HookedOMSetRenderTargets, (LPVOID*)&g_OriginalOMSetRenderTargets);
                    
                    Logger::info("DX Hooks: Hooked RSSetViewports (21), RSSetScissorRects (22), ResourceBarrier (26), OMSetRenderTargets (33)");
                }
            }

            if (d3d12Device) {
                void** pDeviceVTable = *(void***)d3d12Device;
                MH_CreateHook(pDeviceVTable[7], (LPVOID)HookedCreateRenderTargetView, (LPVOID*)&g_OriginalCreateRenderTargetView);
                MH_CreateHook(pDeviceVTable[8], (LPVOID)HookedCreatePlacedResource, (LPVOID*)&g_OriginalCreatePlacedResource);
                MH_CreateHook(pDeviceVTable[27], (LPVOID)HookedCreateCommittedResource, (LPVOID*)&g_OriginalCreateCommittedResource);
                Logger::info("DX Hooks: Hook ID3D12Device::CreateCommittedResource(27), CreatePlacedResource(8), CreateRenderTargetView(7)");
            }

            if (d3d11Device) {
                void** pDevice11VTable = *(void***)d3d11Device;
                MH_CreateHook(pDevice11VTable[5], (LPVOID)HookedCreateTexture2D, (LPVOID*)&g_OriginalCreateTexture2D);
                Logger::info("DX Hooks: Hook ID3D11Device::CreateTexture2D (IDX 5): MH_OK");
            }

            MH_EnableHook(MH_ALL_HOOKS);
        }
    }

    HookDXGIFactories();

cleanup:
    if (commandList) commandList->Release();
    if (commandAllocator) commandAllocator->Release();
    if (swapChain) swapChain->Release();
    if (dxgiFactory) dxgiFactory->Release();
    if (commandQueue) commandQueue->Release();
    if (d3d12Device) d3d12Device->Release();
    if (d3d11Context) d3d11Context->Release();
    if (d3d11Device) d3d11Device->Release();
    DestroyWindow(hwnd);
    UnregisterClass("GamePlugDummy", wc.hInstance);
    Logger::info("InstallDXGIHooks: End");
}

} // namespace GamePlug
