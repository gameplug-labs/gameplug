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

    return g_OriginalCreateTexture2D(pDevice, &desc, pInitialData, ppTexture2D);
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
    DXUpscalerManager& mgr = DXUpscalerManager::Get();
    ID3D11Texture2D* fakeBB = mgr.GetFakeBackBuffer();
    if (!fakeBB)
        return false;

    ID3D11RenderTargetView* pRTV = nullptr;
    pCtx->OMGetRenderTargets(1, &pRTV, nullptr);
    if (!pRTV)
        return false;

    // Get the texture backing the currently bound RTV
    ID3D11Resource* pRes = nullptr;
    pRTV->GetResource(&pRes);
    pRTV->Release();

    bool isFake = (pRes == (ID3D11Resource*)fakeBB);
    if (pRes)
        pRes->Release();
    return isFake;
}

static std::unordered_map<void**, PFN_RSSetViewports> g_OriginalRSSetViewportsMap;
static std::unordered_map<void**, PFN_RSSetScissorRects> g_OriginalRSSetScissorRectsMap;
static std::unordered_map<void**, PFN_OMSetRenderTargets> g_OriginalOMSetRenderTargetsMap;
static std::unordered_map<void**, PFN_ClearRenderTargetView> g_OriginalClearRenderTargetViewMap;
static std::unordered_map<void**, PFN_QueryInterface> g_OriginalContextQIMap;

HRESULT STDMETHODCALLTYPE HookedCreateRenderTargetView(
    ID3D11Device* pDevice, ID3D11Resource* pResource, const D3D11_RENDER_TARGET_VIEW_DESC* pDesc, ID3D11RenderTargetView** ppRTView) {
    if (g_InHook)
        return g_OriginalCreateRenderTargetView(pDevice, pResource, pDesc, ppRTView);
    ScopedRecursionGuard guard;

    HRESULT hr = g_OriginalCreateRenderTargetView(pDevice, pResource, pDesc, ppRTView);
    if (SUCCEEDED(hr) && ppRTView && *ppRTView && pResource) {
        DXUpscalerManager& mgr = DXUpscalerManager::Get();
        if (pResource == mgr.GetRealBackBufferRes()) {
            Logger::info("HookedCreateRenderTargetView: Game created RTV on Real "
                         "BackBuffer: " +
                         std::to_string((uintptr_t)*ppRTView));
            mgr.SetGameBackBufferRTV(*ppRTView);
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

    DXUpscalerManager& mgr = DXUpscalerManager::Get();
    ID3D11RenderTargetView* gameRTV = mgr.GetGameBackBufferRTV();
    ID3D11RenderTargetView* fakeRTV = mgr.GetFakeBackBufferRTV();
    ID3D11Resource* realRes = mgr.GetRealBackBufferRes();
    ID3D11Resource* gameBackBufferRes = mgr.GetGameBackBufferRes();

    bool wantUpscale = !mgr.IsNativeRenderingEnabled();

    bool needsSwapping = false;
    if (fakeRTV && gameRTV && realRes && gameBackBufferRes) {
        for (UINT i = 0; i < NumViews; ++i) {
            if (ppRenderTargetViews[i]) {
                if (ppRenderTargetViews[i] == gameRTV) {
                    if (wantUpscale) {
                        needsSwapping = true;
                        break;
                    }
                } else if (ppRenderTargetViews[i] == fakeRTV) {
                    if (!wantUpscale) {
                        needsSwapping = true;
                        break;
                    }
                } else {
                    ID3D11Resource* pRes = nullptr;
                    ppRenderTargetViews[i]->GetResource(&pRes);
                    if (pRes) {
                        if ((wantUpscale && pRes == realRes) ||
                            (wantUpscale && pRes == gameBackBufferRes && ppRenderTargetViews[i] != fakeRTV) ||
                            (!wantUpscale && pRes == gameBackBufferRes)) {
                            needsSwapping = true;
                            pRes->Release();
                            break;
                        }
                        pRes->Release();
                    }
                }
            }
        }
    }

    if (needsSwapping) {
        std::vector<ID3D11RenderTargetView*> swappedViews(NumViews);
        for (UINT i = 0; i < NumViews; ++i) {
            swappedViews[i] = ppRenderTargetViews[i];
            if (ppRenderTargetViews[i]) {
                if (ppRenderTargetViews[i] == gameRTV) {
                    if (wantUpscale) {
                        swappedViews[i] = fakeRTV;
                    }
                } else if (ppRenderTargetViews[i] == fakeRTV) {
                    if (!wantUpscale) {
                        swappedViews[i] = gameRTV;
                    }
                } else {
                    ID3D11Resource* pRes = nullptr;
                    ppRenderTargetViews[i]->GetResource(&pRes);
                    if (pRes) {
                        if (wantUpscale && pRes == realRes) {
                            swappedViews[i] = fakeRTV;
                        } else if (wantUpscale && pRes == gameBackBufferRes && ppRenderTargetViews[i] != fakeRTV) {
                            swappedViews[i] = fakeRTV;
                        } else if (!wantUpscale && pRes == gameBackBufferRes) {
                            swappedViews[i] = gameRTV;
                        }
                        pRes->Release();
                    }
                }
            }
        }
        if (originalFn) {
            originalFn(pCtx, NumViews, swappedViews.data(), pDepthStencilView);
        }
    } else {
        if (originalFn) {
            originalFn(pCtx, NumViews, ppRenderTargetViews, pDepthStencilView);
        }
    }
}

void STDMETHODCALLTYPE HookedClearRenderTargetView(
    ID3D11DeviceContext* pCtx, ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4]) {
    PFN_ClearRenderTargetView originalFn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_HookMtx);
        if (pCtx) {
            void** pVTable = *(void***)pCtx;
            auto it = g_OriginalClearRenderTargetViewMap.find(pVTable);
            if (it != g_OriginalClearRenderTargetViewMap.end()) {
                originalFn = it->second;
            }
        }
    }
    if (!originalFn) {
        originalFn = g_OriginalClearRenderTargetView;
    }

    if (g_InHook || !pRenderTargetView) {
        if (originalFn) {
            originalFn(pCtx, pRenderTargetView, ColorRGBA);
        }
        return;
    }
    ScopedRecursionGuard guard;

    DXUpscalerManager& mgr = DXUpscalerManager::Get();
    ID3D11RenderTargetView* gameRTV = mgr.GetGameBackBufferRTV();
    ID3D11RenderTargetView* fakeRTV = mgr.GetFakeBackBufferRTV();
    ID3D11Resource* realRes = mgr.GetRealBackBufferRes();
    ID3D11Resource* gameBackBufferRes = mgr.GetGameBackBufferRes();

    bool wantUpscale = !mgr.IsNativeRenderingEnabled();

    if (fakeRTV && gameRTV && realRes && gameBackBufferRes) {
        if (pRenderTargetView == gameRTV) {
            if (wantUpscale) {
                if (originalFn) {
                    originalFn(pCtx, fakeRTV, ColorRGBA);
                }
                return;
            }
        } else if (pRenderTargetView == fakeRTV) {
            if (!wantUpscale) {
                if (originalFn) {
                    originalFn(pCtx, gameRTV, ColorRGBA);
                }
                return;
            }
        } else {
            ID3D11Resource* pRes = nullptr;
            pRenderTargetView->GetResource(&pRes);
            if (pRes) {
                if (wantUpscale && pRes == realRes) {
                    pRes->Release();
                    if (originalFn) {
                        originalFn(pCtx, fakeRTV, ColorRGBA);
                    }
                    return;
                } else if (wantUpscale && pRes == gameBackBufferRes && pRenderTargetView != fakeRTV) {
                    pRes->Release();
                    if (originalFn) {
                        originalFn(pCtx, fakeRTV, ColorRGBA);
                    }
                    return;
                } else if (!wantUpscale && pRes == gameBackBufferRes) {
                    pRes->Release();
                    if (originalFn) {
                        originalFn(pCtx, gameRTV, ColorRGBA);
                    }
                    return;
                }
                pRes->Release();
            }
        }
    }

    if (originalFn) {
        originalFn(pCtx, pRenderTargetView, ColorRGBA);
    }
}

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
    Logger::info("HookedRSSetViewports entry 1");
    DXUpscalerManager& mgr = DXUpscalerManager::Get();
    uint32_t dispW = mgr.GetDisplayWidth();
    uint32_t dispH = mgr.GetDisplayHeight();
    uint32_t rendW = mgr.GetRenderWidth();
    uint32_t rendH = mgr.GetRenderHeight();

    bool scalingActive = !mgr.IsNativeRenderingEnabled() && (rendW != dispW || rendH != dispH);
    bool shouldScale = scalingActive && dispW > 0 && dispH > 0 && rendW > 0 && rendH > 0 && IsFakeBackBufferBound(pCtx);

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

    D3D11_VIEWPORT
    scaled[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
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
    Logger::info("HookedRSSetScissorRects entry (g_InHook=" + std::to_string(g_InHook) + " pRects=" + std::to_string(pRects != nullptr) +
                 " NumRects=" + std::to_string(NumRects) + ")");

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

    bool scalingActive = !mgr.IsNativeRenderingEnabled() && (rendW != dispW || rendH != dispH);
    bool shouldScale = scalingActive && dispW > 0 && dispH > 0 && rendW > 0 && rendH > 0 && IsFakeBackBufferBound(pCtx);

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
    if (g_OriginalClearRenderTargetViewMap.count(pVTable) == 0) {
        g_OriginalClearRenderTargetViewMap[pVTable] = (PFN_ClearRenderTargetView)pVTable[50];
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
    if (!g_OriginalClearRenderTargetView) {
        g_OriginalClearRenderTargetView = (PFN_ClearRenderTargetView)pVTable[50];
    }

    DWORD old;
    if (VirtualProtect(pVTable, 128 * sizeof(void*), PAGE_READWRITE, &old)) {
        pVTable[0] = (void*)HookedContextQueryInterface;
        pVTable[33] = (void*)HookedOMSetRenderTargets;
        pVTable[44] = (void*)HookedRSSetViewports;
        pVTable[45] = (void*)HookedRSSetScissorRects;
        pVTable[50] = (void*)HookedClearRenderTargetView;
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

} // namespace GamePlug
