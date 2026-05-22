#include "hooks_common.h"

namespace GamePlug {

extern std::recursive_mutex g_HooksMtx;
extern std::map<ID3D12GraphicsCommandList*, std::pair<uint32_t, uint32_t>> g_CommandListRTSize;

void SetCommandListRTSize(ID3D12GraphicsCommandList* pList, uint32_t w, uint32_t h) {
    std::lock_guard<std::recursive_mutex> lock(g_HooksMtx);
    g_CommandListRTSize[pList] = {w, h};
}

std::pair<uint32_t, uint32_t> GetCommandListRTSize(ID3D12GraphicsCommandList* pList) {
    std::lock_guard<std::recursive_mutex> lock(g_HooksMtx);
    auto it = g_CommandListRTSize.find(pList);
    if (it != g_CommandListRTSize.end())
        return it->second;
    return {0, 0};
}

void STDMETHODCALLTYPE HookedRSSetViewports(ID3D12GraphicsCommandList* pList, UINT Count, const D3D12_VIEWPORT* pViewports) {
    if (g_InHook) {
        g_OriginalRSSetViewports(pList, Count, pViewports);
        return;
    }
    ScopedRecursionGuard guard;

    if (Count > 0 && pViewports) {
        g_OriginalRSSetViewports(pList, Count, pViewports);
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

    if (Count > 0 && pRects) {
        g_OriginalRSSetScissorRects(pList, Count, pRects);
        return;
    }

    g_OriginalRSSetScissorRects(pList, Count, pRects);
}

void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D12GraphicsCommandList* pList, UINT NumRTs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRTVs,
    BOOL singleHandle, const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV) {
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

void STDMETHODCALLTYPE HookedCreateRenderTargetView(ID3D12Device* pDevice, ID3D12Resource* pResource,
    const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
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

void STDMETHODCALLTYPE HookedExecuteCommandLists(
    ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
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
}

void HookCommandQueue(ID3D12CommandQueue* pQueue) {
    if (!pQueue)
        return;
    std::lock_guard<std::mutex> lock(g_HookMtx);

    // Safety check: is this instance already tracked, or is the GLOBAL VTable already patched?
    static bool s_QueueVTablePatched = false;
    if (g_HookedVTables.count(pQueue) || s_QueueVTablePatched) {
        g_HookedVTables.insert(pQueue); // Track instance anyway
        return;
    }

    void** pVTable = *(void***)pQueue;

    // Index 10 is ExecuteCommandLists
    if (!g_OriginalExecuteCommandLists)
        g_OriginalExecuteCommandLists = (PFN_ExecuteCommandLists)pVTable[10];
    if (!g_OriginalSignal)
        g_OriginalSignal = (PFN_Signal)pVTable[13];

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

            uint32_t rw = d.Width;
            uint32_t rh = d.Height;

            // Safety: If upscaler haven't determined resolution yet, skip discovery to avoid picking noise

            // TIGHTENED LOGIC: Source buffer MUST be very close to our internal render resolution
            // We allow a small margin (5%) for engine padding/alignment
            bool isValidSize = (d.Width >= rw * 0.95f && d.Width <= rw * 1.05f) && (d.Height >= rh * 0.95f && d.Height <= rh * 1.05f);

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
            bool isSupportedFormat = (d.Format == DXGI_FORMAT_R11G11B10_FLOAT) || (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                                     (d.Format == DXGI_FORMAT_R8G8B8A8_UNORM);

            if (!isSupportedFormat)
                continue;

            // SAFE to use captured final scene-like RT
            SetLastEngineRenderTarget(pRes);

            if (s_logCounter < 500 && s_logCounter % 100 == 0) {
                Logger::warn("FINAL RT CANDIDATE: " + std::to_string(d.Width) + "x" + std::to_string(d.Height) +
                             " fmt=" + std::to_string(d.Format) + " PTR=" + std::to_string((uintptr_t)pRes));
            }
            s_logCounter++;
        }
    }
    g_OriginalResourceBarrier(pList, NumBarriers, pBarriers);
}

bool ShouldOverrideD3D12(const D3D12_RESOURCE_DESC& desc) {
    if (!Config::Get().GetBool("ReGame", false))
        return false;

    uint32_t dw = desc.Width;
    uint32_t dh = desc.Height;

    // Core Check: Does it match the target display resolution exactly?
    if (desc.Width != dw || desc.Height != dh)
        return false;

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

HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource(ID3D12Device* pDevice, const D3D12_HEAP_PROPERTIES* pHeapProperties,
    D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState,
    const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource, void** ppvResource) {
    if (g_InHook)
        return g_OriginalCreateCommittedResource(
            pDevice, pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource);
    ScopedRecursionGuard guard;

    D3D12_RESOURCE_DESC desc = *pDesc;
    bool overridden = false;

    if (ShouldOverrideD3D12(desc)) {
        uint32_t renderW = desc.Width;
        uint32_t renderH = desc.Height;

        if (renderW > 0 && renderH > 0) {
            Logger::info("FSR1: Override DX12 RT " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " -> " +
                         std::to_string(renderW) + "x" + std::to_string(renderH));
            desc.Width = renderW;
            desc.Height = renderH;
            overridden = true;
        }
    }

    ID3D12Resource* pRes = nullptr;
    HRESULT hr = g_OriginalCreateCommittedResource(
        pDevice, pHeapProperties, HeapFlags, &desc, InitialResourceState, pOptimizedClearValue, riidResource, (void**)&pRes);

    if (SUCCEEDED(hr) && pRes) {
        if (overridden) {
            std::lock_guard<std::mutex> lock(g_TrackingMtx);
            g_OverriddenResources.insert(pRes);
        }
        if (ppvResource)
            *ppvResource = pRes;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource(ID3D12Device* pDevice, ID3D12Heap* pHeap, UINT64 HeapOffset,
    const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    REFIID riidResource, void** ppvResource) {
    if (g_InHook)
        return g_OriginalCreatePlacedResource(
            pDevice, pHeap, HeapOffset, pDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource);
    ScopedRecursionGuard guard;
    Logger::info("HookedCreatePlacedResource: CreatePlacedResource");
    D3D12_RESOURCE_DESC desc = *pDesc;
    bool overridden = false;

    if (ShouldOverrideD3D12(desc)) {
        uint32_t renderW = desc.Width;
        uint32_t renderH = desc.Height;

        if (renderW > 128 && renderH > 128) { // Extra safety for tiny textures
            Logger::info("FSR1: Override DX12 Placed RT " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " -> " +
                         std::to_string(renderW) + "x" + std::to_string(renderH));
            desc.Width = renderW;
            desc.Height = renderH;
            overridden = true;
        }
    }

    ID3D12Resource* pRes = nullptr;
    HRESULT hr = g_OriginalCreatePlacedResource(
        pDevice, pHeap, HeapOffset, &desc, InitialResourceState, pOptimizedClearValue, riidResource, (void**)&pRes);

    if (SUCCEEDED(hr) && pRes) {
        if (overridden) {
            std::lock_guard<std::mutex> lock(g_TrackingMtx);
            g_OverriddenResources.insert(pRes);
        }
        if (ppvResource)
            *ppvResource = pRes;
    }
    return hr;
}

} // namespace GamePlug
