#include "hooks_common.h"
#include "upscaler_manager.h"
#include <unordered_map>

namespace GamePlug {

bool ShouldOverrideD3D11(const D3D11_TEXTURE2D_DESC& desc) {
    return false;
}

HRESULT STDMETHODCALLTYPE HookedCreateTexture2D(
    ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) {
    if (g_InHook)
        return g_OriginalCreateTexture2D(pDevice, pDesc, pInitialData, ppTexture2D);
    ScopedRecursionGuard guard;

    D3D11_TEXTURE2D_DESC desc = *pDesc;
    if (ShouldOverrideD3D11(desc)) {
        uint32_t renderW = desc.Width;
        uint32_t renderH = desc.Height;

        if (renderW > 0 && renderH > 0) {
            Logger::info("Override DX11 RT " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " -> " +
                         std::to_string(renderW) + "x" + std::to_string(renderH));
            desc.Width = renderW;
            desc.Height = renderH;
        }
    }

    HRESULT hr = g_OriginalCreateTexture2D(pDevice, &desc, pInitialData, ppTexture2D);
    if (SUCCEEDED(hr) && ppTexture2D && *ppTexture2D) {
        DXUpscalerManager::Get().TrackTexture(*ppTexture2D, &desc);
    }
    return hr;
}

// -------------------------------------------------------------------------
// RSSetViewports / RSSetScissorRects hooks
//
// When upscaling is active the game renders into our fake back-buffer which
// is at render resolution (e.g. 1280x720), but it still calls
// RSSetViewports / RSSetScissorRects with display-resolution values
// (e.g. 1920x1080).  Passing those large values to a smaller texture means
// the rasterizer clips everything past the texture boundary → the image is
// cropped / off-screen exactly as reported.
//
// The fix mirrors what the D3D9 proxy does in SetViewport: detect whether
// the current OM render target is our fake back-buffer, and if so scale the
// coordinates proportionally from display resolution to render resolution.
// -------------------------------------------------------------------------

static bool IsFakeBackBufferBound(ID3D11DeviceContext* pCtx) {
    ID3D11RenderTargetView* pRTV = nullptr;
    pCtx->OMGetRenderTargets(1, &pRTV, nullptr);
    if (!pRTV)
        return false;

    // Get the texture backing the currently bound RTV
    ID3D11Resource* pRes = nullptr;
    pRTV->GetResource(&pRes);
    pRTV->Release();

    if (!pRes)
        return false;

    // Custom GUID used to identify our fake back buffer
    // {F192E666-C0DE-4D11-8765-FCEB5A4B2BA1}
    static const GUID GUID_FakeBackBuffer = {0xf192e666, 0xc0de, 0x4d11, {0x87, 0x65, 0xfc, 0xeb, 0x5a, 0x4b, 0x2b, 0xa1}};

    UINT tag = 0;
    UINT size = sizeof(tag);
    bool isFake = SUCCEEDED(pRes->GetPrivateData(GUID_FakeBackBuffer, &size, &tag)) && tag == 1;

    // Direct pointer fallback
    if (!isFake) {
        ID3D11Texture2D* fakeBB = DXUpscalerManager::Get().GetFakeBackBuffer();
        if (fakeBB) {
            if (pRes == fakeBB) {
                isFake = true;
            } else {
                IUnknown* pUnkRes = nullptr;
                IUnknown* pUnkBB = nullptr;
                if (SUCCEEDED(pRes->QueryInterface(IID_IUnknown, (void**)&pUnkRes)) &&
                    SUCCEEDED(fakeBB->QueryInterface(IID_IUnknown, (void**)&pUnkBB))) {
                    if (pUnkRes == pUnkBB) {
                        isFake = true;
                    }
                }
                if (pUnkRes)
                    pUnkRes->Release();
                if (pUnkBB)
                    pUnkBB->Release();
            }
        }
    }

    // Texture dimension fallback (in case the texture is wrapped by Steam
    // Overlay/Reshade)
    if (!isFake) {
        ID3D11Texture2D* pTex = nullptr;
        if (SUCCEEDED(pRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pTex))) {
            D3D11_TEXTURE2D_DESC desc;
            pTex->GetDesc(&desc);
            pTex->Release();

            DXUpscalerManager& mgr = DXUpscalerManager::Get();
            if (desc.Width > 0 && desc.Width == mgr.GetRenderWidth() && desc.Height == mgr.GetRenderHeight()) {
                isFake = true;
            }
        }
    }

    pRes->Release();
    return isFake;
}

static std::unordered_map<void**, PFN_RSSetViewports> g_OriginalRSSetViewportsMap;
static std::unordered_map<void**, PFN_RSSetScissorRects> g_OriginalRSSetScissorRectsMap;
static std::unordered_map<void**, PFN_OMSetRenderTargets> g_OriginalOMSetRenderTargetsMap;
static std::unordered_map<void**, PFN_OMSetRenderTargetsAndUnorderedAccessViews> g_OriginalOMSetRenderTargetsAndUnorderedAccessViewsMap;
static std::unordered_map<void**, PFN_ClearDepthStencilView> g_OriginalClearDepthStencilViewMap;
static std::unordered_map<void**, PFN_UpdateSubresource> g_OriginalUpdateSubresourceMap;
static std::unordered_map<void**, PFN_Map> g_OriginalMapMap;
static std::unordered_map<void**, PFN_Unmap> g_OriginalUnmapMap;
static std::unordered_map<void**, PFN_QueryInterface> g_OriginalContextQIMap;

static std::unordered_map<ID3D11Resource*, void*> g_MappedResources;

void STDMETHODCALLTYPE HookedRSSetViewports(ID3D11DeviceContext* pCtx, UINT NumViewports, const D3D11_VIEWPORT* pViewports) {
    PFN_RSSetViewports originalFn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (pCtx) {
            void** pVTable = *(void***)pCtx;
            auto it = g_OriginalRSSetViewportsMap.find(pVTable);
            if (it != g_OriginalRSSetViewportsMap.end()) {
                originalFn = it->second;
            }
        }
    }
    if (!originalFn) {
        originalFn = g_OriginalRSSetViewports;
    }

    if (g_InHook || !pViewports || NumViewports == 0) {
        if (originalFn) {
            originalFn(pCtx, NumViewports, pViewports);
        }
        return;
    }
    ScopedRecursionGuard guard;
    DXUpscalerManager& mgr = DXUpscalerManager::Get();
    uint32_t dispW = mgr.GetDisplayWidth();
    uint32_t dispH = mgr.GetDisplayHeight();
    uint32_t rendW = mgr.GetRenderWidth();
    uint32_t rendH = mgr.GetRenderHeight();

    bool shouldScale = mgr.IsUpscalingEnabled() && dispW > 0 && dispH > 0 && rendW > 0 && rendH > 0 && (rendW != dispW || rendH != dispH) &&
                       IsFakeBackBufferBound(pCtx);

    if (!shouldScale) {
        if (originalFn) {
            originalFn(pCtx, NumViewports, pViewports);
        }
        return;
    }

    if (pViewports[0].Width == (float)rendW && pViewports[0].Height == (float)rendH) {
        if (originalFn) {
            originalFn(pCtx, NumViewports, pViewports);
        }
        return;
    }

    // Scale each viewport proportionally
    float scaleX = (float)rendW / (float)dispW;
    float scaleY = (float)rendH / (float)dispH;

    D3D11_VIEWPORT scaled[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT count = (NumViewports < D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)
                     ? NumViewports
                     : D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    for (UINT i = 0; i < count; ++i) {
        scaled[i] = pViewports[i];
        scaled[i].TopLeftX = pViewports[i].TopLeftX * scaleX;
        scaled[i].TopLeftY = pViewports[i].TopLeftY * scaleY;
        scaled[i].Width = pViewports[i].Width * scaleX;
        scaled[i].Height = pViewports[i].Height * scaleY;
        // MinDepth / MaxDepth stay the same
    }

    static uint64_t s_vpLog = 0;
    if (s_vpLog++ % 100 == 0) {
        Logger::info("RSSetViewports [Scaled]: "
                     "vp[0] pos=(" +
                     std::to_string((int)pViewports[0].TopLeftX) + "," + std::to_string((int)pViewports[0].TopLeftY) +
                     ")"
                     " size=" +
                     std::to_string((int)pViewports[0].Width) + "x" + std::to_string((int)pViewports[0].Height) +
                     " -> "
                     "pos=(" +
                     std::to_string((int)scaled[0].TopLeftX) + "," + std::to_string((int)scaled[0].TopLeftY) +
                     ")"
                     " size=" +
                     std::to_string((int)scaled[0].Width) + "x" + std::to_string((int)scaled[0].Height) + "  [display " +
                     std::to_string(dispW) + "x" + std::to_string(dispH) + " render " + std::to_string(rendW) + "x" +
                     std::to_string(rendH) + "]");
    }

    if (originalFn) {
        originalFn(pCtx, count, scaled);
    }
}

void STDMETHODCALLTYPE HookedRSSetScissorRects(ID3D11DeviceContext* pCtx, UINT NumRects, const D3D11_RECT* pRects) {
    PFN_RSSetScissorRects originalFn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (pCtx) {
            void** pVTable = *(void***)pCtx;
            auto it = g_OriginalRSSetScissorRectsMap.find(pVTable);
            if (it != g_OriginalRSSetScissorRectsMap.end()) {
                originalFn = it->second;
            }
        }
    }
    if (!originalFn) {
        originalFn = g_OriginalRSSetScissorRects;
    }

    if (g_InHook || !pRects || NumRects == 0) {
        if (originalFn) {
            originalFn(pCtx, NumRects, pRects);
        }
        return;
    }
    ScopedRecursionGuard guard;
    DXUpscalerManager& mgr = DXUpscalerManager::Get();
    uint32_t dispW = mgr.GetDisplayWidth();
    uint32_t dispH = mgr.GetDisplayHeight();
    uint32_t rendW = mgr.GetRenderWidth();
    uint32_t rendH = mgr.GetRenderHeight();

    bool shouldScale = mgr.IsUpscalingEnabled() && dispW > 0 && dispH > 0 && rendW > 0 && rendH > 0 && (rendW != dispW || rendH != dispH) &&
                       IsFakeBackBufferBound(pCtx);

    if (!shouldScale) {
        if (originalFn) {
            originalFn(pCtx, NumRects, pRects);
        }
        return;
    }

    UINT scW = pRects[0].right - pRects[0].left;
    UINT scH = pRects[0].bottom - pRects[0].top;

    if (scW == rendW && scH == rendH) {
        if (originalFn) {
            originalFn(pCtx, NumRects, pRects);
        }
        return;
    }

    float scaleX = (float)rendW / (float)dispW;
    float scaleY = (float)rendH / (float)dispH;

    D3D11_RECT scaled[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT count = (NumRects < D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)
                     ? NumRects
                     : D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    for (UINT i = 0; i < count; ++i) {
        scaled[i].left = (LONG)(pRects[i].left * scaleX);
        scaled[i].top = (LONG)(pRects[i].top * scaleY);
        scaled[i].right = (LONG)(pRects[i].right * scaleX);
        scaled[i].bottom = (LONG)(pRects[i].bottom * scaleY);
    }

    static uint64_t s_srLog = 0;
    if (s_srLog++ % 100 == 0) {
        Logger::info("RSSetScissorRects [Scaled]: "
                     "rect[0] (" +
                     std::to_string(pRects[0].left) + "," + std::to_string(pRects[0].top) + ")-(" + std::to_string(pRects[0].right) + "," +
                     std::to_string(pRects[0].bottom) +
                     ")"
                     " size=" +
                     std::to_string(pRects[0].right - pRects[0].left) + "x" + std::to_string(pRects[0].bottom - pRects[0].top) +
                     " -> "
                     "(" +
                     std::to_string(scaled[0].left) + "," + std::to_string(scaled[0].top) + ")-(" + std::to_string(scaled[0].right) + "," +
                     std::to_string(scaled[0].bottom) +
                     ")"
                     " size=" +
                     std::to_string(scaled[0].right - scaled[0].left) + "x" + std::to_string(scaled[0].bottom - scaled[0].top) +
                     "  [display " + std::to_string(dispW) + "x" + std::to_string(dispH) + " render " + std::to_string(rendW) + "x" +
                     std::to_string(rendH) + "]");
    }

    if (originalFn) {
        originalFn(pCtx, count, scaled);
    }
}

void STDMETHODCALLTYPE HookedUpdateSubresource(
    ID3D11DeviceContext* pCtx, ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) {
    PFN_UpdateSubresource originalFn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (pCtx) {
            void** pVTable = *(void***)pCtx;
            auto it = g_OriginalUpdateSubresourceMap.find(pVTable);
            if (it != g_OriginalUpdateSubresourceMap.end()) {
                originalFn = it->second;
            }
        }
    }
    if (!originalFn) originalFn = g_OriginalUpdateSubresource;

    if (!g_InHook && pDstResource) {
        DXUpscalerManager& mgr = DXUpscalerManager::Get();
        if (!mgr.GetKnownProjBuffer()) {
            if (pSrcData && pDstResource) {
                D3D11_RESOURCE_DIMENSION dim;
                pDstResource->GetType(&dim);
                if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
                    D3D11_BUFFER_DESC desc;
                    ((ID3D11Buffer*)pDstResource)->GetDesc(&desc);
                    UINT dataSize = pDstBox ? (pDstBox->right - pDstBox->left) : desc.ByteWidth;
                    mgr.TryDetectMatrix((ID3D11Buffer*)pDstResource, (const float*)pSrcData, dataSize);
                }
            }
        }
        
        if (pDstResource == (ID3D11Resource*)mgr.GetKnownProjBuffer()) {
            ScopedRecursionGuard guard;
            
            UINT dataSize = 0;
            if (pDstBox) {
                dataSize = pDstBox->right - pDstBox->left;
            } else {
                D3D11_BUFFER_DESC desc;
                ((ID3D11Buffer*)pDstResource)->GetDesc(&desc);
                dataSize = desc.ByteWidth;
            }

            if (dataSize > 0) {
                std::vector<uint8_t> tempData(dataSize);
                memcpy(tempData.data(), pSrcData, dataSize);
                float* fData = (float*)tempData.data();
                
                UINT offsetFloats = mgr.GetKnownProjOffset() / 4;
                if ((offsetFloats + 16) * sizeof(float) <= dataSize) {
                    mgr.UpdateGameJitterFromData(&fData[offsetFloats]);
                    mgr.InjectFakeJitterIntoMatrix(&fData[offsetFloats], 16);
                }

                if (originalFn) {
                    originalFn(pCtx, pDstResource, DstSubresource, pDstBox, tempData.data(), SrcRowPitch, SrcDepthPitch);
                }
                return;
            }
        }
    }

    if (originalFn) {
        originalFn(pCtx, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
    }
}

HRESULT STDMETHODCALLTYPE HookedMap(
    ID3D11DeviceContext* pCtx, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource) {
    PFN_Map originalFn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (pCtx) {
            void** pVTable = *(void***)pCtx;
            auto it = g_OriginalMapMap.find(pVTable);
            if (it != g_OriginalMapMap.end()) {
                originalFn = it->second;
            }
        }
    }
    if (!originalFn) originalFn = g_OriginalMap;

    HRESULT hr = originalFn ? originalFn(pCtx, pResource, Subresource, MapType, MapFlags, pMappedResource) : E_FAIL;

    if (SUCCEEDED(hr) && !g_InHook && pResource && pMappedResource && pMappedResource->pData) {
        DXUpscalerManager& mgr = DXUpscalerManager::Get();
        
        bool shouldTrack = false;
        if (pResource == (ID3D11Resource*)mgr.GetKnownProjBuffer()) {
            shouldTrack = true;
        } else if (!mgr.GetKnownProjBuffer()) {
            D3D11_RESOURCE_DIMENSION dim;
            pResource->GetType(&dim);
            if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
                shouldTrack = true;
            }
        }

        if (shouldTrack) {
            std::lock_guard<std::mutex> lock(g_HookMtx);
            g_MappedResources[pResource] = pMappedResource->pData;
        }
    }
    return hr;
}

void STDMETHODCALLTYPE HookedUnmap(
    ID3D11DeviceContext* pCtx, ID3D11Resource* pResource, UINT Subresource) {
    PFN_Unmap originalFn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (pCtx) {
            void** pVTable = *(void***)pCtx;
            auto it = g_OriginalUnmapMap.find(pVTable);
            if (it != g_OriginalUnmapMap.end()) {
                originalFn = it->second;
            }
        }
    }
    if (!originalFn) originalFn = g_OriginalUnmap;

    if (!g_InHook && pResource) {
        DXUpscalerManager& mgr = DXUpscalerManager::Get();
        
        bool isKnown = (pResource == (ID3D11Resource*)mgr.GetKnownProjBuffer());
        bool isUnknownBuffer = false;
        
        if (!isKnown && !mgr.GetKnownProjBuffer()) {
            D3D11_RESOURCE_DIMENSION dim;
            pResource->GetType(&dim);
            if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
                isUnknownBuffer = true;
            }
        }

        if (isKnown || isUnknownBuffer) {
            ScopedRecursionGuard guard;
            void* pData = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_HookMtx);
                auto it = g_MappedResources.find(pResource);
                if (it != g_MappedResources.end()) {
                    pData = it->second;
                    g_MappedResources.erase(it);
                }
            }
            if (pData) {
                float* data = (float*)pData;
                
                if (isUnknownBuffer) {
                    D3D11_BUFFER_DESC desc;
                    ((ID3D11Buffer*)pResource)->GetDesc(&desc);
                    mgr.TryDetectMatrix((ID3D11Buffer*)pResource, data, desc.ByteWidth);
                    // Update isKnown in case it was just detected
                    isKnown = (pResource == (ID3D11Resource*)mgr.GetKnownProjBuffer());
                }

                if (isKnown) {
                    UINT offsetFloats = mgr.GetKnownProjOffset() / 4;
                    mgr.UpdateGameJitterFromData(&data[offsetFloats]);
                    mgr.InjectFakeJitterIntoMatrix(&data[offsetFloats], 16);
                }
            }
        }
    }

    if (originalFn) {
        originalFn(pCtx, pResource, Subresource);
    }
}

void PatchDeviceContextVTable(ID3D11DeviceContext* context) {
    if (!context)
        return;
    void** pVTable = *(void***)context;

    std::lock_guard<std::mutex> lock(g_HookMtx);

    // Check if already patched
    if (pVTable[44] == (void*)HookedRSSetViewports) {
        return;
    }

    // Save original pointers in maps
    if (g_OriginalRSSetViewportsMap.count(pVTable) == 0) {
        g_OriginalRSSetViewportsMap[pVTable] = (PFN_RSSetViewports)pVTable[44];
    }
    if (g_OriginalRSSetScissorRectsMap.count(pVTable) == 0) {
        g_OriginalRSSetScissorRectsMap[pVTable] = (PFN_RSSetScissorRects)pVTable[45];
    }
    if (g_OriginalOMSetRenderTargetsMap.count(pVTable) == 0) {
        g_OriginalOMSetRenderTargetsMap[pVTable] = (PFN_OMSetRenderTargets)pVTable[33];
    }
    if (g_OriginalOMSetRenderTargetsAndUnorderedAccessViewsMap.count(pVTable) == 0) {
        g_OriginalOMSetRenderTargetsAndUnorderedAccessViewsMap[pVTable] = (PFN_OMSetRenderTargetsAndUnorderedAccessViews)pVTable[34];
    }
    if (g_OriginalClearDepthStencilViewMap.count(pVTable) == 0) {
        g_OriginalClearDepthStencilViewMap[pVTable] = (PFN_ClearDepthStencilView)pVTable[53];
    }
    if (g_OriginalUpdateSubresourceMap.count(pVTable) == 0) {
        g_OriginalUpdateSubresourceMap[pVTable] = (PFN_UpdateSubresource)pVTable[48];
    }
    if (g_OriginalMapMap.count(pVTable) == 0) {
        g_OriginalMapMap[pVTable] = (PFN_Map)pVTable[14];
    }
    if (g_OriginalUnmapMap.count(pVTable) == 0) {
        g_OriginalUnmapMap[pVTable] = (PFN_Unmap)pVTable[15];
    }
    if (g_OriginalContextQIMap.count(pVTable) == 0) {
        g_OriginalContextQIMap[pVTable] = (PFN_QueryInterface)pVTable[0];
    }

    // Also save to global fallbacks
    if (!g_OriginalRSSetViewports) {
        g_OriginalRSSetViewports = (PFN_RSSetViewports)pVTable[44];
    }
    if (!g_OriginalRSSetScissorRects) {
        g_OriginalRSSetScissorRects = (PFN_RSSetScissorRects)pVTable[45];
    }
    if (!g_OriginalOMSetRenderTargets) {
        g_OriginalOMSetRenderTargets = (PFN_OMSetRenderTargets)pVTable[33];
    }
    if (!g_OriginalOMSetRenderTargetsAndUnorderedAccessViews) {
        g_OriginalOMSetRenderTargetsAndUnorderedAccessViews = (PFN_OMSetRenderTargetsAndUnorderedAccessViews)pVTable[34];
    }
    if (!g_OriginalClearDepthStencilView) {
        g_OriginalClearDepthStencilView = (PFN_ClearDepthStencilView)pVTable[53];
    }
    if (!g_OriginalUpdateSubresource) {
        g_OriginalUpdateSubresource = (PFN_UpdateSubresource)pVTable[48];
    }
    if (!g_OriginalMap) {
        g_OriginalMap = (PFN_Map)pVTable[14];
    }
    if (!g_OriginalUnmap) {
        g_OriginalUnmap = (PFN_Unmap)pVTable[15];
    }

    DWORD old;
    if (VirtualProtect(pVTable, 128 * sizeof(void*), PAGE_READWRITE, &old)) {
        pVTable[0] = (void*)HookedContextQueryInterface;
        pVTable[14] = (void*)HookedMap;
        pVTable[15] = (void*)HookedUnmap;
        pVTable[33] = (void*)HookedOMSetRenderTargets;
        pVTable[34] = (void*)HookedOMSetRenderTargetsAndUnorderedAccessViews;
        pVTable[44] = (void*)HookedRSSetViewports;
        pVTable[45] = (void*)HookedRSSetScissorRects;
        pVTable[48] = (void*)HookedUpdateSubresource;
        pVTable[53] = (void*)HookedClearDepthStencilView;
        VirtualProtect(pVTable, 128 * sizeof(void*), old, &old);
        Logger::info("DX Hooks D3D11: Patched DeviceContext VTable (" + std::to_string((uintptr_t)pVTable) + ")");
    } else {
        Logger::error("DX Hooks D3D11: FAILED to VirtualProtect DeviceContext VTable (" + std::to_string((uintptr_t)pVTable) + ")!");
    }
}

HRESULT STDMETHODCALLTYPE HookedCreateDeferredContext(ID3D11Device* pDevice, UINT ContextFlags, ID3D11DeviceContext** ppDeferredContext) {
    if (g_InHook)
        return g_OriginalCreateDeferredContext(pDevice, ContextFlags, ppDeferredContext);
    ScopedRecursionGuard guard;

    HRESULT hr = g_OriginalCreateDeferredContext(pDevice, ContextFlags, ppDeferredContext);
    if (SUCCEEDED(hr) && ppDeferredContext && *ppDeferredContext) {
        Logger::info("DX Hooks D3D11: CreateDeferredContext returned context at " + std::to_string((uintptr_t)*ppDeferredContext));
        PatchDeviceContextVTable(*ppDeferredContext);
    }
    return hr;
}

void STDMETHODCALLTYPE HookedGetImmediateContext(ID3D11Device* pDevice, ID3D11DeviceContext** ppImmediateContext) {
    if (g_InHook) {
        g_OriginalGetImmediateContext(pDevice, ppImmediateContext);
        return;
    }
    ScopedRecursionGuard guard;

    g_OriginalGetImmediateContext(pDevice, ppImmediateContext);
    if (ppImmediateContext && *ppImmediateContext) {
        Logger::info("DX Hooks D3D11: GetImmediateContext returned context at " + std::to_string((uintptr_t)*ppImmediateContext));
        PatchDeviceContextVTable(*ppImmediateContext);
    }
}

static const IID IID_ID3D11DeviceContext1 = {0x79352328, 0x16f2, 0x4f81, {0x97, 0x46, 0x9c, 0x2e, 0x2c, 0xcd, 0x43, 0xcf}};
static const IID IID_ID3D11DeviceContext2 = {0x420d5b32, 0xb90c, 0x4da4, {0xbe, 0xf0, 0x35, 0x9f, 0x6a, 0x24, 0xa8, 0x3a}};
static const IID IID_ID3D11DeviceContext3 = {0xb4e3c01d, 0xe79e, 0x4637, {0x91, 0xb2, 0x51, 0x0e, 0x9f, 0x4c, 0x9b, 0x8f}};
static const IID IID_ID3D11DeviceContext4 = {0x9a4b737c, 0xc0a8, 0x4319, {0xa9, 0xca, 0x81, 0x72, 0xe9, 0x92, 0x77, 0x4d}};

HRESULT STDMETHODCALLTYPE HookedContextQueryInterface(ID3D11DeviceContext* pCtx, REFIID riid, void** ppvObject) {
    PFN_QueryInterface originalQI = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (pCtx) {
            void** pVTable = *(void***)pCtx;
            auto it = g_OriginalContextQIMap.find(pVTable);
            if (it != g_OriginalContextQIMap.end()) {
                originalQI = it->second;
            }
        }
    }

    HRESULT hr = originalQI ? originalQI(pCtx, riid, ppvObject) : E_FAIL;

    if (SUCCEEDED(hr) && ppvObject && *ppvObject) {
        if (riid == __uuidof(ID3D11DeviceContext) || riid == IID_ID3D11DeviceContext1 || riid == IID_ID3D11DeviceContext2 ||
            riid == IID_ID3D11DeviceContext3 || riid == IID_ID3D11DeviceContext4) {

            Logger::info("DX Hooks D3D11: Context QueryInterface returned context "
                         "interface at " +
                         std::to_string((uintptr_t)*ppvObject));
            PatchDeviceContextVTable((ID3D11DeviceContext*)*ppvObject);
        }
    }
    return hr;
}

static std::string DXGIFormatToString(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_UNKNOWN:
        return "DXGI_FORMAT_UNKNOWN";
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return "DXGI_FORMAT_R32G32B32A32_TYPELESS";
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return "DXGI_FORMAT_R32G32B32A32_FLOAT";
    case DXGI_FORMAT_R32G32B32A32_UINT:
        return "DXGI_FORMAT_R32G32B32A32_UINT";
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return "DXGI_FORMAT_R32G32B32A32_SINT";
    case DXGI_FORMAT_R32G32B32_TYPELESS:
        return "DXGI_FORMAT_R32G32B32_TYPELESS";
    case DXGI_FORMAT_R32G32B32_FLOAT:
        return "DXGI_FORMAT_R32G32B32_FLOAT";
    case DXGI_FORMAT_R32G32B32_UINT:
        return "DXGI_FORMAT_R32G32B32_UINT";
    case DXGI_FORMAT_R32G32B32_SINT:
        return "DXGI_FORMAT_R32G32B32_SINT";
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return "DXGI_FORMAT_R16G16B16A16_TYPELESS";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return "DXGI_FORMAT_R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_UNORM:
        return "DXGI_FORMAT_R16G16B16A16_UNORM";
    case DXGI_FORMAT_R16G16B16A16_UINT:
        return "DXGI_FORMAT_R16G16B16A16_UINT";
    case DXGI_FORMAT_R16G16B16A16_SNORM:
        return "DXGI_FORMAT_R16G16B16A16_SNORM";
    case DXGI_FORMAT_R16G16B16A16_SINT:
        return "DXGI_FORMAT_R16G16B16A16_SINT";
    case DXGI_FORMAT_R32G32_TYPELESS:
        return "DXGI_FORMAT_R32G32_TYPELESS";
    case DXGI_FORMAT_R32G32_FLOAT:
        return "DXGI_FORMAT_R32G32_FLOAT";
    case DXGI_FORMAT_R32G32_UINT:
        return "DXGI_FORMAT_R32G32_UINT";
    case DXGI_FORMAT_R32G32_SINT:
        return "DXGI_FORMAT_R32G32_SINT";
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        return "DXGI_FORMAT_R32G8X24_TYPELESS";
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return "DXGI_FORMAT_D32_FLOAT_S8X24_UINT";
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        return "DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS";
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return "DXGI_FORMAT_X32_TYPELESS_G8X24_UINT";
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return "DXGI_FORMAT_R10G10B10A2_TYPELESS";
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return "DXGI_FORMAT_R10G10B10A2_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return "DXGI_FORMAT_R10G10B10A2_UINT";
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return "DXGI_FORMAT_R11G11B10_FLOAT";
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return "DXGI_FORMAT_R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return "DXGI_FORMAT_R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_UINT:
        return "DXGI_FORMAT_R8G8B8A8_UINT";
    case DXGI_FORMAT_R8G8B8A8_SNORM:
        return "DXGI_FORMAT_R8G8B8A8_SNORM";
    case DXGI_FORMAT_R8G8B8A8_SINT:
        return "DXGI_FORMAT_R8G8B8A8_SINT";
    case DXGI_FORMAT_R16G16_TYPELESS:
        return "DXGI_FORMAT_R16G16_TYPELESS";
    case DXGI_FORMAT_R16G16_FLOAT:
        return "DXGI_FORMAT_R16G16_FLOAT";
    case DXGI_FORMAT_R16G16_UNORM:
        return "DXGI_FORMAT_R16G16_UNORM";
    case DXGI_FORMAT_R16G16_UINT:
        return "DXGI_FORMAT_R16G16_UINT";
    case DXGI_FORMAT_R16G16_SNORM:
        return "DXGI_FORMAT_R16G16_SNORM";
    case DXGI_FORMAT_R16G16_SINT:
        return "DXGI_FORMAT_R16G16_SINT";
    case DXGI_FORMAT_R32_TYPELESS:
        return "DXGI_FORMAT_R32_TYPELESS";
    case DXGI_FORMAT_D32_FLOAT:
        return "DXGI_FORMAT_D32_FLOAT";
    case DXGI_FORMAT_R32_FLOAT:
        return "DXGI_FORMAT_R32_FLOAT";
    case DXGI_FORMAT_R32_UINT:
        return "DXGI_FORMAT_R32_UINT";
    case DXGI_FORMAT_R32_SINT:
        return "DXGI_FORMAT_R32_SINT";
    case DXGI_FORMAT_R24G8_TYPELESS:
        return "DXGI_FORMAT_R24G8_TYPELESS";
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return "DXGI_FORMAT_D24_UNORM_S8_UINT";
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        return "DXGI_FORMAT_R24_UNORM_X8_TYPELESS";
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return "DXGI_FORMAT_X24_TYPELESS_G8_UINT";
    case DXGI_FORMAT_R8G8_TYPELESS:
        return "DXGI_FORMAT_R8G8_TYPELESS";
    case DXGI_FORMAT_R8G8_UNORM:
        return "DXGI_FORMAT_R8G8_UNORM";
    case DXGI_FORMAT_R8G8_UINT:
        return "DXGI_FORMAT_R8G8_UINT";
    case DXGI_FORMAT_R8G8_SNORM:
        return "DXGI_FORMAT_R8G8_SNORM";
    case DXGI_FORMAT_R8G8_SINT:
        return "DXGI_FORMAT_R8G8_SINT";
    case DXGI_FORMAT_R16_TYPELESS:
        return "DXGI_FORMAT_R16_TYPELESS";
    case DXGI_FORMAT_R16_FLOAT:
        return "DXGI_FORMAT_R16_FLOAT";
    case DXGI_FORMAT_D16_UNORM:
        return "DXGI_FORMAT_D16_UNORM";
    case DXGI_FORMAT_R16_UNORM:
        return "DXGI_FORMAT_R16_UNORM";
    case DXGI_FORMAT_R16_UINT:
        return "DXGI_FORMAT_R16_UINT";
    case DXGI_FORMAT_R16_SNORM:
        return "DXGI_FORMAT_R16_SNORM";
    case DXGI_FORMAT_R16_SINT:
        return "DXGI_FORMAT_R16_SINT";
    case DXGI_FORMAT_R8_TYPELESS:
        return "DXGI_FORMAT_R8_TYPELESS";
    case DXGI_FORMAT_R8_UNORM:
        return "DXGI_FORMAT_R8_UNORM";
    case DXGI_FORMAT_R8_UINT:
        return "DXGI_FORMAT_R8_UINT";
    case DXGI_FORMAT_R8_SNORM:
        return "DXGI_FORMAT_R8_SNORM";
    case DXGI_FORMAT_R8_SINT:
        return "DXGI_FORMAT_R8_SINT";
    case DXGI_FORMAT_A8_UNORM:
        return "DXGI_FORMAT_A8_UNORM";
    case DXGI_FORMAT_R1_UNORM:
        return "DXGI_FORMAT_R1_UNORM";
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        return "DXGI_FORMAT_R9G9B9E5_SHAREDEXP";
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
        return "DXGI_FORMAT_R8G8_B8G8_UNORM";
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
        return "DXGI_FORMAT_G8R8_G8B8_UNORM";
    case DXGI_FORMAT_BC1_TYPELESS:
        return "DXGI_FORMAT_BC1_TYPELESS";
    case DXGI_FORMAT_BC1_UNORM:
        return "DXGI_FORMAT_BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return "DXGI_FORMAT_BC1_UNORM_SRGB";
    case DXGI_FORMAT_BC2_TYPELESS:
        return "DXGI_FORMAT_BC2_TYPELESS";
    case DXGI_FORMAT_BC2_UNORM:
        return "DXGI_FORMAT_BC2_UNORM";
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        return "DXGI_FORMAT_BC2_UNORM_SRGB";
    case DXGI_FORMAT_BC3_TYPELESS:
        return "DXGI_FORMAT_BC3_TYPELESS";
    case DXGI_FORMAT_BC3_UNORM:
        return "DXGI_FORMAT_BC3_UNORM";
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return "DXGI_FORMAT_BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_TYPELESS:
        return "DXGI_FORMAT_BC4_TYPELESS";
    case DXGI_FORMAT_BC4_UNORM:
        return "DXGI_FORMAT_BC4_UNORM";
    case DXGI_FORMAT_BC4_SNORM:
        return "DXGI_FORMAT_BC4_SNORM";
    case DXGI_FORMAT_BC5_TYPELESS:
        return "DXGI_FORMAT_BC5_TYPELESS";
    case DXGI_FORMAT_BC5_UNORM:
        return "DXGI_FORMAT_BC5_UNORM";
    case DXGI_FORMAT_BC5_SNORM:
        return "DXGI_FORMAT_BC5_SNORM";
    case DXGI_FORMAT_B5G6R5_UNORM:
        return "DXGI_FORMAT_B5G6R5_UNORM";
    case DXGI_FORMAT_B5G5R5A1_UNORM:
        return "DXGI_FORMAT_B5G5R5A1_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return "DXGI_FORMAT_B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return "DXGI_FORMAT_B8G8R8X8_UNORM";
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return "DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM";
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return "DXGI_FORMAT_B8G8R8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return "DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return "DXGI_FORMAT_B8G8R8X8_TYPELESS";
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return "DXGI_FORMAT_B8G8R8X8_UNORM_SRGB";
    case DXGI_FORMAT_BC6H_TYPELESS:
        return "DXGI_FORMAT_BC6H_TYPELESS";
    case DXGI_FORMAT_BC6H_UF16:
        return "DXGI_FORMAT_BC6H_UF16";
    case DXGI_FORMAT_BC6H_SF16:
        return "DXGI_FORMAT_BC6H_SF16";
    case DXGI_FORMAT_BC7_TYPELESS:
        return "DXGI_FORMAT_BC7_TYPELESS";
    case DXGI_FORMAT_BC7_UNORM:
        return "DXGI_FORMAT_BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return "DXGI_FORMAT_BC7_UNORM_SRGB";
    case DXGI_FORMAT_AYUV:
        return "DXGI_FORMAT_AYUV";
    case DXGI_FORMAT_Y410:
        return "DXGI_FORMAT_Y410";
    case DXGI_FORMAT_Y416:
        return "DXGI_FORMAT_Y416";
    case DXGI_FORMAT_NV12:
        return "DXGI_FORMAT_NV12";
    case DXGI_FORMAT_P010:
        return "DXGI_FORMAT_P010";
    case DXGI_FORMAT_P016:
        return "DXGI_FORMAT_P016";
    case DXGI_FORMAT_420_OPAQUE:
        return "DXGI_FORMAT_420_OPAQUE";
    case DXGI_FORMAT_YUY2:
        return "DXGI_FORMAT_YUY2";
    case DXGI_FORMAT_Y210:
        return "DXGI_FORMAT_Y210";
    case DXGI_FORMAT_Y216:
        return "DXGI_FORMAT_Y216";
    case DXGI_FORMAT_NV11:
        return "DXGI_FORMAT_NV11";
    case DXGI_FORMAT_AI44:
        return "DXGI_FORMAT_AI44";
    case DXGI_FORMAT_IA44:
        return "DXGI_FORMAT_IA44";
    case DXGI_FORMAT_P8:
        return "DXGI_FORMAT_P8";
    case DXGI_FORMAT_A8P8:
        return "DXGI_FORMAT_A8P8";
    case DXGI_FORMAT_B4G4R4A4_UNORM:
        return "DXGI_FORMAT_B4G4R4A4_UNORM";
    default:
        return "DXGI_FORMAT_UNKNOWN(" + std::to_string((int)format) + ")";
    }
}

static void LogRTVDesc(ID3D11RenderTargetView* rtv, int index) {
    if (!rtv)
        return;
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    if (res) {
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex))) {
            D3D11_TEXTURE2D_DESC desc;
            tex->GetDesc(&desc);
            Logger::info("  Bound RTV[" + std::to_string(index) + "]: size=" + std::to_string(desc.Width) + "x" +
                         std::to_string(desc.Height) + " format=" + DXGIFormatToString(desc.Format) +
                         " RTV=" + std::to_string((uintptr_t)rtv));
            tex->Release();
        }
        res->Release();
    }
}

static void LogDSVDesc(ID3D11DepthStencilView* dsv) {
    if (!dsv)
        return;
    ID3D11Resource* res = nullptr;
    dsv->GetResource(&res);
    if (res) {
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex))) {
            D3D11_TEXTURE2D_DESC desc;
            tex->GetDesc(&desc);
            Logger::info("  Bound DSV: size=" + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) +
                         " format=" + DXGIFormatToString(desc.Format) + " DSV=" + std::to_string((uintptr_t)dsv));
            tex->Release();
        }
        res->Release();
    }
}

static void ScaleViewportsAndScissorRectsIfFakeBound(ID3D11DeviceContext* pCtx, ID3D11RenderTargetView* pRTV) {
    if (!pCtx || !pRTV)
        return;

    ID3D11Resource* pRes = nullptr;
    pRTV->GetResource(&pRes);
    if (!pRes)
        return;

    // {F192E666-C0DE-4D11-8765-FCEB5A4B2BA1}
    static const GUID GUID_FakeBackBuffer = {0xf192e666, 0xc0de, 0x4d11, {0x87, 0x65, 0xfc, 0xeb, 0x5a, 0x4b, 0x2b, 0xa1}};

    UINT tag = 0;
    UINT size = sizeof(tag);
    bool isFake = SUCCEEDED(pRes->GetPrivateData(GUID_FakeBackBuffer, &size, &tag)) && tag == 1;

    // Direct pointer fallback
    if (!isFake) {
        ID3D11Texture2D* fakeBB = DXUpscalerManager::Get().GetFakeBackBuffer();
        if (fakeBB) {
            if (pRes == fakeBB) {
                isFake = true;
            } else {
                IUnknown* pUnkRes = nullptr;
                IUnknown* pUnkBB = nullptr;
                if (SUCCEEDED(pRes->QueryInterface(IID_IUnknown, (void**)&pUnkRes)) &&
                    SUCCEEDED(fakeBB->QueryInterface(IID_IUnknown, (void**)&pUnkBB))) {
                    if (pUnkRes == pUnkBB) {
                        isFake = true;
                    }
                }
                if (pUnkRes)
                    pUnkRes->Release();
                if (pUnkBB)
                    pUnkBB->Release();
            }
        }
    }

    // Dimension fallback
    if (!isFake) {
        ID3D11Texture2D* pTex = nullptr;
        if (SUCCEEDED(pRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pTex))) {
            D3D11_TEXTURE2D_DESC desc;
            pTex->GetDesc(&desc);
            pTex->Release();

            DXUpscalerManager& mgr = DXUpscalerManager::Get();
            if (desc.Width > 0 && desc.Width == mgr.GetRenderWidth() && desc.Height == mgr.GetRenderHeight()) {
                isFake = true;
            }
        }
    }

    pRes->Release();

    if (!isFake)
        return;

    DXUpscalerManager& mgr = DXUpscalerManager::Get();
    uint32_t dispW = mgr.GetDisplayWidth();
    uint32_t dispH = mgr.GetDisplayHeight();
    uint32_t rendW = mgr.GetRenderWidth();
    uint32_t rendH = mgr.GetRenderHeight();

    if (!mgr.IsUpscalingEnabled() || dispW == 0 || dispH == 0 || rendW == 0 || rendH == 0 || (rendW == dispW && rendH == dispH))
        return;

    // Scale Viewports
    UINT numVPs = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    pCtx->RSGetViewports(&numVPs, vps);

    if (numVPs > 0) {
        bool needScale = false;
        for (UINT i = 0; i < numVPs; ++i) {
            if (vps[i].Width > (float)rendW || (vps[i].Width == (float)dispW && vps[i].Height == (float)dispH)) {
                needScale = true;
                break;
            }
        }

        if (needScale) {
            float scaleX = (float)rendW / (float)dispW;
            float scaleY = (float)rendH / (float)dispH;
            for (UINT i = 0; i < numVPs; ++i) {
                vps[i].TopLeftX *= scaleX;
                vps[i].TopLeftY *= scaleY;
                vps[i].Width *= scaleX;
                vps[i].Height *= scaleY;
            }
            pCtx->RSSetViewports(numVPs, vps);
        }
    }

    // Scale Scissor Rects
    UINT numRects = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    pCtx->RSGetScissorRects(&numRects, rects);

    if (numRects > 0) {
        bool needScale = false;
        for (UINT i = 0; i < numRects; ++i) {
            UINT scW = rects[i].right - rects[i].left;
            UINT scH = rects[i].bottom - rects[i].top;
            if (scW > rendW || (scW == dispW && scH == dispH)) {
                needScale = true;
                break;
            }
        }

        if (needScale) {
            float scaleX = (float)rendW / (float)dispW;
            float scaleY = (float)rendH / (float)dispH;
            for (UINT i = 0; i < numRects; ++i) {
                rects[i].left = (LONG)(rects[i].left * scaleX);
                rects[i].top = (LONG)(rects[i].top * scaleY);
                rects[i].right = (LONG)(rects[i].right * scaleX);
                rects[i].bottom = (LONG)(rects[i].bottom * scaleY);
            }
            pCtx->RSSetScissorRects(numRects, rects);
        }
    }
}

void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D11DeviceContext* pCtx, UINT NumViews,
    ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView) {
    PFN_OMSetRenderTargets originalFn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (pCtx) {
            void** pVTable = *(void***)pCtx;
            auto it = g_OriginalOMSetRenderTargetsMap.find(pVTable);
            if (it != g_OriginalOMSetRenderTargetsMap.end()) {
                originalFn = it->second;
            }
        }
    }
    if (!originalFn) {
        originalFn = g_OriginalOMSetRenderTargets;
    }

    if (g_InHook || NumViews == 0 || !ppRenderTargetViews) {
        if (originalFn) {
            originalFn(pCtx, NumViews, ppRenderTargetViews, pDepthStencilView);
        }
        return;
    }
    ScopedRecursionGuard guard;

    static uint64_t s_omLogCount = 0;
    if (s_omLogCount++ % 1000 == 0) {
        Logger::info("OMSetRenderTargets Trace (Frame " + std::to_string(s_omLogCount / 60) + "):");
        for (UINT i = 0; i < NumViews; ++i) {
            LogRTVDesc(ppRenderTargetViews[i], i);
        }
        LogDSVDesc(pDepthStencilView);
    }

    if (originalFn) {
        originalFn(pCtx, NumViews, ppRenderTargetViews, pDepthStencilView);
    }

    ScaleViewportsAndScissorRectsIfFakeBound(pCtx, ppRenderTargetViews[0]);
}

void STDMETHODCALLTYPE HookedOMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext* pCtx, UINT NumRTVs,
    ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs,
    ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts) {
    PFN_OMSetRenderTargetsAndUnorderedAccessViews originalFn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (pCtx) {
            void** pVTable = *(void***)pCtx;
            auto it = g_OriginalOMSetRenderTargetsAndUnorderedAccessViewsMap.find(pVTable);
            if (it != g_OriginalOMSetRenderTargetsAndUnorderedAccessViewsMap.end()) {
                originalFn = it->second;
            }
        }
    }
    if (!originalFn) {
        originalFn = g_OriginalOMSetRenderTargetsAndUnorderedAccessViews;
    }

    if (g_InHook) {
        if (originalFn) {
            originalFn(
                pCtx, NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
        }
        return;
    }
    ScopedRecursionGuard guard;

    if (originalFn) {
        originalFn(pCtx, NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
    }

    if (NumRTVs > 0 && ppRenderTargetViews && ppRenderTargetViews[0]) {
        ScaleViewportsAndScissorRectsIfFakeBound(pCtx, ppRenderTargetViews[0]);
    }
}

void STDMETHODCALLTYPE HookedClearDepthStencilView(
    ID3D11DeviceContext* pCtx, ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) {
    PFN_ClearDepthStencilView originalFn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (pCtx) {
            void** pVTable = *(void***)pCtx;
            auto it = g_OriginalClearDepthStencilViewMap.find(pVTable);
            if (it != g_OriginalClearDepthStencilViewMap.end()) {
                originalFn = it->second;
            }
        }
    }
    if (!originalFn) {
        originalFn = g_OriginalClearDepthStencilView;
    }

    if (g_InHook) {
        if (originalFn) {
            originalFn(pCtx, pDepthStencilView, ClearFlags, Depth, Stencil);
        }
        return;
    }
    ScopedRecursionGuard guard;

    if (ClearFlags & D3D11_CLEAR_DEPTH) {
        // Standard depth is usually cleared to 1.0, inverted depth is cleared to
        // 0.0
        bool isInverted = (Depth < 0.5f);
        Logger::info("DXUpscalerManager: Check depth detect " + std::string(isInverted ? "true" : "false") +
                     " Depth: " + std::to_string(Depth) + " Stencil: " + std::to_string(Stencil));
        DXUpscalerManager::Get().RecordDepthClearValue(isInverted);
    }

    if (originalFn) {
        originalFn(pCtx, pDepthStencilView, ClearFlags, Depth, Stencil);
    }
}

} // namespace GamePlug
