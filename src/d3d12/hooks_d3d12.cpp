#include "hooks_common.h"
#include "upscaler_manager.h"

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

static bool IsFakeBackBufferBound(ID3D12GraphicsCommandList* pList) {
    ID3D12Resource* pRes = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_TrackingMtx);
        auto it = g_CommandListTargets.find(pList);
        if (it != g_CommandListTargets.end()) {
            pRes = it->second;
        }
    }

    if (!pRes)
        return false;

    static const GUID GUID_FakeBackBuffer = {0xf192e666, 0xc0de, 0x4d12, {0x87, 0x65, 0xfc, 0xeb, 0x5a, 0x4b, 0x2b, 0xa1}};
    UINT tag = 0;
    UINT size = sizeof(tag);
    if (SUCCEEDED(pRes->GetPrivateData(GUID_FakeBackBuffer, &size, &tag)) && tag == 1) {
        return true;
    }

    ID3D12Resource* fakeBB = DXUpscalerManager::Get().GetFakeBackBuffer();
    if (fakeBB && pRes == fakeBB) {
        return true;
    }

    return false;
}

void STDMETHODCALLTYPE HookedRSSetViewports(ID3D12GraphicsCommandList* pList, UINT Count, const D3D12_VIEWPORT* pViewports) {
    if (g_InHook || !pViewports || Count == 0) {
        g_OriginalRSSetViewports(pList, Count, pViewports);
        return;
    }
    ScopedRecursionGuard guard;

    DXUpscalerManager& mgr = DXUpscalerManager::Get();
    uint32_t dispW = mgr.GetDisplayWidth();
    uint32_t dispH = mgr.GetDisplayHeight();
    uint32_t rendW = mgr.GetRenderWidth();
    uint32_t rendH = mgr.GetRenderHeight();

    bool shouldScale = mgr.IsUpscalingEnabled() && dispW > 0 && dispH > 0 && rendW > 0 && rendH > 0 && (rendW != dispW || rendH != dispH) &&
                       IsFakeBackBufferBound(pList);

    if (!shouldScale) {
        g_OriginalRSSetViewports(pList, Count, pViewports);
        return;
    }

    if (pViewports[0].Width == (float)rendW && pViewports[0].Height == (float)rendH) {
        g_OriginalRSSetViewports(pList, Count, pViewports);
        return;
    }

    float scaleX = (float)rendW / (float)dispW;
    float scaleY = (float)rendH / (float)dispH;

    D3D12_VIEWPORT scaled[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT countToScale = (Count < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)
                     ? Count
                     : D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                     
    for (UINT i = 0; i < countToScale; ++i) {
        scaled[i] = pViewports[i];
        scaled[i].TopLeftX = pViewports[i].TopLeftX * scaleX;
        scaled[i].TopLeftY = pViewports[i].TopLeftY * scaleY;
        scaled[i].Width = pViewports[i].Width * scaleX;
        scaled[i].Height = pViewports[i].Height * scaleY;
    }

    static uint64_t s_vpLog = 0;
    if (s_vpLog++ % 100 == 0) {
        Logger::info("DX12 RSSetViewports [Scaled]: vp[0] pos=(" +
                     std::to_string((int)pViewports[0].TopLeftX) + "," + std::to_string((int)pViewports[0].TopLeftY) +
                     ") size=" +
                     std::to_string((int)pViewports[0].Width) + "x" + std::to_string((int)pViewports[0].Height) +
                     " -> pos=(" +
                     std::to_string((int)scaled[0].TopLeftX) + "," + std::to_string((int)scaled[0].TopLeftY) +
                     ") size=" +
                     std::to_string((int)scaled[0].Width) + "x" + std::to_string((int)scaled[0].Height) + "  [display " +
                     std::to_string(dispW) + "x" + std::to_string(dispH) + " render " + std::to_string(rendW) + "x" +
                     std::to_string(rendH) + "]");
    }

    g_OriginalRSSetViewports(pList, countToScale, scaled);
}

void STDMETHODCALLTYPE HookedRSSetScissorRects(ID3D12GraphicsCommandList* pList, UINT Count, const D3D12_RECT* pRects) {
    if (g_InHook || !pRects || Count == 0) {
        g_OriginalRSSetScissorRects(pList, Count, pRects);
        return;
    }
    ScopedRecursionGuard guard;

    DXUpscalerManager& mgr = DXUpscalerManager::Get();
    uint32_t dispW = mgr.GetDisplayWidth();
    uint32_t dispH = mgr.GetDisplayHeight();
    uint32_t rendW = mgr.GetRenderWidth();
    uint32_t rendH = mgr.GetRenderHeight();

    bool shouldScale = mgr.IsUpscalingEnabled() && dispW > 0 && dispH > 0 && rendW > 0 && rendH > 0 && (rendW != dispW || rendH != dispH) &&
                       IsFakeBackBufferBound(pList);

    if (!shouldScale) {
        g_OriginalRSSetScissorRects(pList, Count, pRects);
        return;
    }

    UINT scW = pRects[0].right - pRects[0].left;
    UINT scH = pRects[0].bottom - pRects[0].top;

    if (scW == rendW && scH == rendH) {
        g_OriginalRSSetScissorRects(pList, Count, pRects);
        return;
    }

    float scaleX = (float)rendW / (float)dispW;
    float scaleY = (float)rendH / (float)dispH;

    D3D12_RECT scaled[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT countToScale = (Count < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)
                     ? Count
                     : D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                     
    for (UINT i = 0; i < countToScale; ++i) {
        scaled[i].left = (LONG)(pRects[i].left * scaleX);
        scaled[i].top = (LONG)(pRects[i].top * scaleY);
        scaled[i].right = (LONG)(pRects[i].right * scaleX);
        scaled[i].bottom = (LONG)(pRects[i].bottom * scaleY);
    }

    static uint64_t s_srLog = 0;
    if (s_srLog++ % 100 == 0) {
        Logger::info("DX12 RSSetScissorRects [Scaled]: rect[0] (" +
                     std::to_string(pRects[0].left) + "," + std::to_string(pRects[0].top) + ")-(" + std::to_string(pRects[0].right) + "," +
                     std::to_string(pRects[0].bottom) +
                     ") size=" +
                     std::to_string(pRects[0].right - pRects[0].left) + "x" + std::to_string(pRects[0].bottom - pRects[0].top) +
                     " -> (" +
                     std::to_string(scaled[0].left) + "," + std::to_string(scaled[0].top) + ")-(" + std::to_string(scaled[0].right) + "," +
                     std::to_string(scaled[0].bottom) +
                     ") size=" +
                     std::to_string(scaled[0].right - scaled[0].left) + "x" + std::to_string(scaled[0].bottom - scaled[0].top) +
                     "  [display " + std::to_string(dispW) + "x" + std::to_string(dispH) + " render " + std::to_string(rendW) + "x" +
                     std::to_string(rendH) + "]");
    }

    g_OriginalRSSetScissorRects(pList, countToScale, scaled);
}

void STDMETHODCALLTYPE HookedCopyDescriptorsSimple(
    ID3D12Device* pDevice, UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
    D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) {
    
    if (g_InHook) {
        g_OriginalCopyDescriptorsSimple(pDevice, NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart, DescriptorHeapsType);
        return;
    }
    ScopedRecursionGuard guard;

    g_OriginalCopyDescriptorsSimple(pDevice, NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart, DescriptorHeapsType);

    if (DescriptorHeapsType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || DescriptorHeapsType == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {
        std::lock_guard<std::mutex> lock(g_TrackingMtx);
        UINT increment = pDevice->GetDescriptorHandleIncrementSize(DescriptorHeapsType);
        for (UINT i = 0; i < NumDescriptors; i++) {
            SIZE_T srcPtr = SrcDescriptorRangeStart.ptr + i * increment;
            SIZE_T dstPtr = DestDescriptorRangeStart.ptr + i * increment;
            if (DescriptorHeapsType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
                if (g_RTVToResource.count(srcPtr)) g_RTVToResource[dstPtr] = g_RTVToResource[srcPtr];
            } else {
                if (g_DSVToResource.count(srcPtr)) g_DSVToResource[dstPtr] = g_DSVToResource[srcPtr];
            }
        }
    }
}

void STDMETHODCALLTYPE HookedCopyDescriptors(
    ID3D12Device* pDevice, UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
    const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
    const UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) {

    if (g_InHook) {
        g_OriginalCopyDescriptors(pDevice, NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes, NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, DescriptorHeapsType);
        return;
    }
    ScopedRecursionGuard guard;

    g_OriginalCopyDescriptors(pDevice, NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes, NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, DescriptorHeapsType);

    if (DescriptorHeapsType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || DescriptorHeapsType == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {
        std::lock_guard<std::mutex> lock(g_TrackingMtx);
        UINT increment = pDevice->GetDescriptorHandleIncrementSize(DescriptorHeapsType);
        
        UINT srcRangeIdx = 0;
        UINT srcOffset = 0;
        
        for (UINT dstRangeIdx = 0; dstRangeIdx < NumDestDescriptorRanges; dstRangeIdx++) {
            UINT dstSize = pDestDescriptorRangeSizes ? pDestDescriptorRangeSizes[dstRangeIdx] : 1;
            SIZE_T dstStart = pDestDescriptorRangeStarts[dstRangeIdx].ptr;
            
            for (UINT dstOffset = 0; dstOffset < dstSize; dstOffset++) {
                if (srcRangeIdx >= NumSrcDescriptorRanges) break;
                UINT srcSize = pSrcDescriptorRangeSizes ? pSrcDescriptorRangeSizes[srcRangeIdx] : 1;
                SIZE_T srcStart = pSrcDescriptorRangeStarts[srcRangeIdx].ptr;
                
                SIZE_T srcPtr = srcStart + srcOffset * increment;
                SIZE_T dstPtr = dstStart + dstOffset * increment;
                
                if (DescriptorHeapsType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
                    if (g_RTVToResource.count(srcPtr)) g_RTVToResource[dstPtr] = g_RTVToResource[srcPtr];
                } else {
                    if (g_DSVToResource.count(srcPtr)) g_DSVToResource[dstPtr] = g_DSVToResource[srcPtr];
                }
                
                srcOffset++;
                if (srcOffset >= srcSize) {
                    srcOffset = 0;
                    srcRangeIdx++;
                }
            }
        }
    }
}

void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D12GraphicsCommandList* pList, UINT NumRTs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRTVs,
    BOOL singleHandle, const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV) {
    
    static uint64_t s_omLogCount = 0;
    if (s_omLogCount++ % 1000 == 0) {
        Logger::info("OMSetRenderTargets Trace DX12 (Frame " + std::to_string(s_omLogCount / 60) + "):");
        std::lock_guard<std::mutex> lock(g_TrackingMtx);
        for (UINT i = 0; i < NumRTs; ++i) {
            if (pRTVs && g_RTVToResource.count(pRTVs[i].ptr)) {
                ID3D12Resource* res = g_RTVToResource[pRTVs[i].ptr];
                D3D12_RESOURCE_DESC desc = res->GetDesc();
                Logger::info("  Bound RTV[" + std::to_string(i) + "]: size=" + std::to_string(desc.Width) + "x" +
                             std::to_string(desc.Height) + " format=" + std::to_string(desc.Format) +
                             " RTV=" + std::to_string(pRTVs[i].ptr));
            }
        }
        if (pDSV && g_DSVToResource.count(pDSV->ptr)) {
            ID3D12Resource* res = g_DSVToResource[pDSV->ptr];
            D3D12_RESOURCE_DESC desc = res->GetDesc();
            Logger::info("  Bound DSV: size=" + std::to_string(desc.Width) + "x" +
                         std::to_string(desc.Height) + " format=" + std::to_string(desc.Format) +
                         " DSV=" + std::to_string(pDSV->ptr));
        }
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

void STDMETHODCALLTYPE HookedCreateDepthStencilView(ID3D12Device* pDevice, ID3D12Resource* pResource,
    const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (g_InHook) {
        g_OriginalCreateDepthStencilView(pDevice, pResource, pDesc, DestDescriptor);
        return;
    }
    ScopedRecursionGuard guard;

    if (pResource) {
        std::lock_guard<std::mutex> lock(g_TrackingMtx);
        g_DSVToResource[DestDescriptor.ptr] = pResource;
    }

    g_OriginalCreateDepthStencilView(pDevice, pResource, pDesc, DestDescriptor);
}

void STDMETHODCALLTYPE HookedExecuteCommandLists(
    ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
    // Discovery & Synchronization (Always run this, even if re-entrant, to ensure we find the queue)
    SetDX12CommandQueue(pQueue);

    if (g_InHook) {
        g_OriginalExecuteCommandLists(pQueue, NumCommandLists, ppCommandLists);
        return;
    }
    ScopedRecursionGuard guard;

    // Call original first to let the engine finish its work
    g_OriginalExecuteCommandLists(pQueue, NumCommandLists, ppCommandLists);

    // Now signal that FSR is ready to run in the Present pass
    DXUpscalerManager::Get().MarkFSRReady();
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
        g_OriginalSignal = (PFN_Signal)pVTable[14];

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

            uint32_t rw = DXUpscalerManager::Get().GetRenderWidth();
            uint32_t rh = DXUpscalerManager::Get().GetRenderHeight();

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
    return false;
}

HRESULT STDMETHODCALLTYPE HookedCreateCommandQueue(
    ID3D12Device* pDevice, const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) {
    if (g_InHook)
        return g_OriginalCreateCommandQueue(pDevice, pDesc, riid, ppCommandQueue);
    ScopedRecursionGuard guard;

    HRESULT hr = g_OriginalCreateCommandQueue(pDevice, pDesc, riid, ppCommandQueue);
    if (SUCCEEDED(hr) && ppCommandQueue && *ppCommandQueue) {
        if (pDesc && pDesc->Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            ID3D12CommandQueue* pQueue = (ID3D12CommandQueue*)*ppCommandQueue;
            {
                std::lock_guard<std::mutex> lock(g_TrackingMtx);
                g_AllTrackedQueues.insert(pQueue);
            }
            Logger::info("DX Hooks: Tracked new D3D12CommandQueue at " + std::to_string((uintptr_t)pQueue));

            // Force registration immediately (bypass GetDesc ABI issues)
            extern ID3D12CommandQueue* g_pd3dCommandQueue;
            extern std::mutex g_QueueMtx;
            extern void HookCommandQueue(ID3D12CommandQueue * pQueue);

            {
                std::lock_guard<std::mutex> lock2(g_QueueMtx);
                if (g_pd3dCommandQueue != pQueue) {
                    Logger::info("DX12: Primary Graphics Queue identified directly at creation: " + std::to_string((uintptr_t)pQueue));
                    g_pd3dCommandQueue = pQueue;
                    HookCommandQueue(pQueue);
                }
            }
        }
    }
    return hr;
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
        DXUpscalerManager::Get().TrackTexture(pRes);
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
    // Logger::info("HookedCreatePlacedResource: CreatePlacedResource");
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
        DXUpscalerManager::Get().TrackTexture(pRes);
        if (ppvResource)
            *ppvResource = pRes;
    }
    return hr;
}

} // namespace GamePlug
