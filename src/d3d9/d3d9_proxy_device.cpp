#include "d3d9_proxy_device.h"
#include "config.h"
#include "d3d9_proxy_surface.h"
#include "d3d9_proxy_swapchain.h"
#include "d3d9_proxy_texture.h"
#include "texture_replacer.h"
#include "upscaler_interface.h"
#include "upscaler_manager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>



static IDirect3DBaseTexture9* GetRealTexture(IDirect3DBaseTexture9* pTex) {
    if (!pTex)
        return nullptr;
    if (GamePlug::TextureReplacer::Get().IsProxyTexture(pTex)) {
        return ((ProxyTexture9*)pTex)->GetReplacedReal();
    }
    return pTex;
}

static IDirect3DSurface9* GetRealSurface(IDirect3DSurface9* pSurf) {
    if (pSurf && GamePlug::TextureReplacer::Get().IsProxySurface(pSurf)) {
        return ((ProxySurface9*)pSurf)->GetInternalSurface();
    }
    return pSurf;
}

static void LogActiveDefaultPoolResources() {
    auto& replacer = GamePlug::TextureReplacer::Get();
    std::lock_guard<std::recursive_mutex> lock(replacer.GetMutex());

    Logger::info("--- Active Default/Managed Pool Resources (Before Reset) ---");

    const auto& activeTextures = replacer.GetActiveTextures();
    for (auto* pTex : activeTextures) {
        if (pTex->GetPool() == D3DPOOL_DEFAULT) {
            IDirect3DTexture9* pReal = pTex->GetRealTexture();
            ULONG realRef = 0;
            if (pReal) {
                pReal->AddRef();
                realRef = pReal->Release();
            }
            ULONG wrapRef = pTex->AddRef();
            pTex->Release();

            Logger::info("Active Texture: {:p} (Real: {:p}), Size: {}x{}, Format: {}, WrapperRef: {}, RealRef: {}", (void*)pTex,
                (void*)pReal, pTex->GetWidth(), pTex->GetHeight(), (int)pTex->GetFormat(), wrapRef, realRef);
        }
    }

    const auto& activeSurfaces = replacer.GetActiveSurfaces();
    for (auto* pSurf : activeSurfaces) {
        IDirect3DSurface9* pReal = pSurf->GetInternalSurface();
        D3DSURFACE_DESC desc = {};
        D3DPOOL pool = (D3DPOOL)-1;
        if (pReal) {
            pReal->GetDesc(&desc);
            pool = desc.Pool;
        }
        if (pool == D3DPOOL_DEFAULT) {
            ULONG realRef = 0;
            if (pReal) {
                pReal->AddRef();
                realRef = pReal->Release();
            }
            ULONG wrapRef = pSurf->AddRef();
            pSurf->Release();

            Logger::info("Active Surface: {:p} (Real: {:p}), Size: {}x{}, Format: {}, WrapperRef: {}, RealRef: {}", (void*)pSurf,
                (void*)pReal, desc.Width, desc.Height, (int)desc.Format, wrapRef, realRef);
        }
    }
    Logger::info("------------------------------------------------------------");
}

void ProxyDirect3DDevice9::UpdateScaledResolution() {
    int sw = m_displayW;
    int sh = m_displayH;
    UpscalerManager::Get().GetScaledResolution(sw, sh);
    if (sw != (int)m_renderW || sh != (int)m_renderH) {
        Logger::info("Proxy: Render resolution changed real-time: {}x{} -> {}x{}", m_renderW, m_renderH, sw, sh);
        m_renderW = (uint32_t)sw;
        m_renderH = (uint32_t)sh;

        if (m_isUpscaling) {
            UpscalerManager::Get().CreateFakeBackBuffer(m_pReal, m_displayW, m_displayH, D3DFMT_A8R8G8B8);
            IDirect3DSurface9* pRealSurf = UpscalerManager::Get().GetFakeBackBufferSurface();
            if (pRealSurf) {
                if (!m_pFakeBackBuffer) {
                    m_pFakeBackBuffer = new ProxySurface9(pRealSurf, this, m_displayW, m_displayH);
                } else {
                    m_pFakeBackBuffer->SetInternalSurface(pRealSurf);
                }

                m_pReal->SetRenderTarget(0, pRealSurf);
                Logger::info("Proxy: Fake backbuffer re-created at {}x{}", m_renderW, m_renderH);
            }
        }
    }
}

void ProxyDirect3DDevice9::UpdateJitterAndFrameIndex() {
    GamePlug::UpscalerManager::Get().UpdateJitterAndFrameIndex();
}

void ProxyDirect3DDevice9::PerformDepthDownsampling() {
    // Removed to allow Vulkan-side downsampling
}

ProxyDirect3DDevice9::ProxyDirect3DDevice9(
    IDirect3DDevice9* pReal, IDirect3D9* pParent, HWND hFocusWindow, uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh, bool upscale)
    : m_pReal(pReal)
    , m_pParent(pParent)
    , m_hFocusWindow(hFocusWindow)
    , m_renderW(rw)
    , m_renderH(rh)
    , m_displayW(dw)
    , m_displayH(dh)
    , m_isUpscaling(upscale)
    , m_pFakeBackBuffer(nullptr) {

    if (SUCCEEDED(m_pReal->QueryInterface(IID_IDirect3DDevice9Ex, (void**)&m_pRealEx))) {
        m_pRealEx->Release();
    }

    Logger::info("ProxyDirect3DDevice9: Created. Render: {}x{}, Display: {}x{}, Upscale: {}, RealEx: {}", m_renderW, m_renderH, m_displayW,
        m_displayH, m_isUpscaling, (void*)m_pRealEx);

    OverlayRenderer::Get().Init((IDirect3DDevice9*)this);

    if (m_isUpscaling) {
        bool upscalerReady = false;
        if (!Config::Get().GetBool("VKUpscaler", true)) {
            if (UpscalerManager::Get().LoadUpscaler()) {
                UpscalerManager::Get().InitUpscaler((void*)m_pReal);
                upscalerReady = true;
            }
        } else {
            upscalerReady = true;
        }

        if (upscalerReady) {
            UpscalerManager::Get().CreateFakeBackBuffer(m_pReal, m_displayW, m_displayH, D3DFMT_A8R8G8B8);
            IDirect3DSurface9* pRealSurf = UpscalerManager::Get().GetFakeBackBufferSurface();
            if (pRealSurf) {
                m_pFakeBackBuffer = new ProxySurface9(pRealSurf, this, m_displayW, m_displayH);
                m_pReal->SetRenderTarget(0, pRealSurf);
                Logger::info("Proxy: Fake backbuffer created at native {}x{}", m_renderW, m_renderH);
            }
        }
    }
}

ProxyDirect3DDevice9::~ProxyDirect3DDevice9() {
    if (m_pFakeBackBuffer)
        m_pFakeBackBuffer->Release();
    for (auto const& [surf, tex] : m_depthSurfaceToTextureMap) {
        if (tex)
            tex->Release();
    }
    m_depthSurfaceToTextureMap.clear();
    if (m_downsampledDepthTexINTZ) {
        m_downsampledDepthTexINTZ->Release();
        m_downsampledDepthTexINTZ = nullptr;
    }
    UpscalerManager::Get().DestroyFakeBackBuffer();
}

// IUnknown
STDMETHODIMP ProxyDirect3DDevice9::QueryInterface(REFIID riid, void** ppvObj) {
    if (riid == IID_IDirect3DDevice9 || riid == IID_IUnknown) {
        *ppvObj = (IDirect3DDevice9*)this;
        AddRef();
        return S_OK;
    }
    if (riid == IID_IDirect3DDevice9Ex) {
        if (m_pRealEx) {
            *ppvObj = (IDirect3DDevice9Ex*)this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    return m_pReal->QueryInterface(riid, ppvObj);
}

STDMETHODIMP_(ULONG) ProxyDirect3DDevice9::AddRef() {
    return m_pReal->AddRef();
}

STDMETHODIMP_(ULONG) ProxyDirect3DDevice9::Release() {
    ULONG res = m_pReal->Release();
    if (res == 0)
        delete this;
    return res;
}

// IDirect3DDevice9 methods
STDMETHODIMP ProxyDirect3DDevice9::TestCooperativeLevel() {
    return m_pReal->TestCooperativeLevel();
}

STDMETHODIMP_(UINT) ProxyDirect3DDevice9::GetAvailableTextureMem() {
    return m_pReal->GetAvailableTextureMem();
}

STDMETHODIMP ProxyDirect3DDevice9::EvictManagedResources() {
    return m_pReal->EvictManagedResources();
}

STDMETHODIMP ProxyDirect3DDevice9::GetDirect3D(IDirect3D9** ppD3D9) {
    if (ppD3D9) {
        *ppD3D9 = m_pParent;
        m_pParent->AddRef();
        return S_OK;
    }
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP ProxyDirect3DDevice9::GetDeviceCaps(D3DCAPS9* pCaps) {
    return m_pReal->GetDeviceCaps(pCaps);
}

STDMETHODIMP ProxyDirect3DDevice9::GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) {
    return m_pReal->GetDisplayMode(iSwapChain, pMode);
}

STDMETHODIMP ProxyDirect3DDevice9::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* pParameters) {
    return m_pReal->GetCreationParameters(pParameters);
}

STDMETHODIMP ProxyDirect3DDevice9::SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) {
    return m_pReal->SetCursorProperties(XHotSpot, YHotSpot, GetRealSurface(pCursorBitmap));
}

STDMETHODIMP_(void) ProxyDirect3DDevice9::SetCursorPosition(int X, int Y, DWORD Flags) {
    m_pReal->SetCursorPosition(X, Y, Flags);
}

STDMETHODIMP_(BOOL) ProxyDirect3DDevice9::ShowCursor(BOOL bShow) {
    return m_pReal->ShowCursor(bShow);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPP, IDirect3DSwapChain9** ppSC) {
    HRESULT hr = m_pReal->CreateAdditionalSwapChain(pPP, ppSC);
    if (SUCCEEDED(hr) && ppSC && *ppSC)
        *ppSC = new ProxyDirect3DSwapChain9(*ppSC, (IDirect3DDevice9*)this, m_pReal->GetNumberOfSwapChains() - 1);
    return hr;
}

STDMETHODIMP ProxyDirect3DDevice9::GetSwapChain(UINT iSC, IDirect3DSwapChain9** ppSC) {
    HRESULT hr = m_pReal->GetSwapChain(iSC, ppSC);
    if (SUCCEEDED(hr) && ppSC && *ppSC)
        *ppSC = new ProxyDirect3DSwapChain9(*ppSC, (IDirect3DDevice9*)this, iSC);
    return hr;
}

STDMETHODIMP_(UINT) ProxyDirect3DDevice9::GetNumberOfSwapChains() {
    return m_pReal->GetNumberOfSwapChains();
}

static void LogPresentParameters(const char* prefix, const D3DPRESENT_PARAMETERS* pPP) {
    if (!pPP)
        return;
    Logger::info("{}: W={}, H={}, Fmt={}, Cnt={}, MSType={}, MSQual={}, Swap={}, hWnd={:p}, Wnd={}, Depth={}, DSFormat={}, Flags={}, "
                 "Refresh={}, Interval={}",
        prefix, pPP->BackBufferWidth, pPP->BackBufferHeight, (int)pPP->BackBufferFormat, pPP->BackBufferCount, (int)pPP->MultiSampleType,
        pPP->MultiSampleQuality, (int)pPP->SwapEffect, (void*)pPP->hDeviceWindow, pPP->Windowed, pPP->EnableAutoDepthStencil,
        (int)pPP->AutoDepthStencilFormat, pPP->Flags, pPP->FullScreen_RefreshRateInHz, pPP->PresentationInterval);
}

STDMETHODIMP ProxyDirect3DDevice9::Reset(D3DPRESENT_PARAMETERS* pPP) {
    // Unbind any potentially bound resources on the real device to clear device state references
    IDirect3DSurface9* pOrigBB = nullptr;
    if (SUCCEEDED(m_pReal->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pOrigBB))) {
        m_pReal->SetRenderTarget(0, pOrigBB);
        pOrigBB->Release();
    }
    for (DWORD i = 1; i < 4; ++i) {
        m_pReal->SetRenderTarget(i, nullptr);
    }
    m_pReal->SetDepthStencilSurface(nullptr);

    for (DWORD i = 0; i < 16; ++i) {
        m_pReal->SetTexture(i, nullptr);
    }
    for (DWORD i = 0; i < 4; ++i) {
        m_pReal->SetTexture(D3DVERTEXTEXTURESAMPLER0 + i, nullptr);
    }

    for (DWORD i = 0; i < 16; ++i) {
        m_pReal->SetStreamSource(i, nullptr, 0, 0);
    }
    m_pReal->SetIndices(nullptr);

    for (auto const& [surf, tex] : m_depthSurfaceToTextureMap) {
        if (tex)
            tex->Release();
    }
    m_depthSurfaceToTextureMap.clear();
    if (m_downsampledDepthTexINTZ) {
        m_downsampledDepthTexINTZ->Release();
        m_downsampledDepthTexINTZ = nullptr;
    }

    OverlayRenderer::Get().OnReset();
    UpscalerManager::Get().OnReset();
    if (m_pFakeBackBuffer) {
        m_pFakeBackBuffer->SetInternalSurface(nullptr);
    }

    int requestedW = pPP->BackBufferWidth;
    int requestedH = pPP->BackBufferHeight;

    int nativeW = Config::Get().GetTargetWidth();
    int nativeH = Config::Get().GetTargetHeight();

    if (nativeW <= 0 || nativeH <= 0) {
        if (m_isUpscaling && requestedW == (int)m_renderW && requestedH == (int)m_renderH) {
            nativeW = m_displayW;
            nativeH = m_displayH;
        } else {
            D3DDISPLAYMODE dm;
            if (SUCCEEDED(m_pReal->GetDisplayMode(0, &dm))) {
                nativeW = dm.Width;
                nativeH = dm.Height;
            } else {
                nativeW = requestedW;
                nativeH = requestedH;
            }
        }
    }

    int scaledW = nativeW;
    int scaledH = nativeH;
    UpscalerManager::Get().GetScaledResolution(scaledW, scaledH);

    D3DPRESENT_PARAMETERS realPP = *pPP;
    realPP.BackBufferWidth = nativeW;
    realPP.BackBufferHeight = nativeH;
    if (realPP.hDeviceWindow == NULL) {
        realPP.hDeviceWindow = m_hFocusWindow;
    }

    m_renderW = scaledW;
    m_renderH = scaledH;
    m_displayW = nativeW;
    m_displayH = nativeH;
    m_isUpscaling = true;

    Logger::info("Reset: Game requested {}x{}, Proxy created device at {}x{}, Game sees {}x{}", requestedW, requestedH, nativeW, nativeH,
        scaledW, scaledH);

    {
        auto& mgr = GamePlug::UpscalerManager::Get();
        mgr.UpdateSharedHDR(GamePlug::UpscalerManager::IsHDRFormat(pPP->BackBufferFormat) || GamePlug::UpscalerManager::IsHDRFormat(realPP.BackBufferFormat));
    }

    LogPresentParameters("Reset (Game input)", pPP);
    LogPresentParameters("Reset (Real output)", &realPP);

    LogActiveDefaultPoolResources();

    HRESULT hr = m_pReal->Reset(&realPP);
    Logger::info("Reset: m_pReal->Reset returned hr={:08X}", (uint32_t)hr);
    if (SUCCEEDED(hr)) {
        pPP->BackBufferWidth = scaledW;
        pPP->BackBufferHeight = scaledH;
        if (m_isUpscaling) {
            UpscalerManager::Get().CreateFakeBackBuffer(m_pReal, m_displayW, m_displayH, D3DFMT_A8R8G8B8);
            IDirect3DSurface9* pRealSurf = UpscalerManager::Get().GetFakeBackBufferSurface();
            if (pRealSurf) {
                if (!m_pFakeBackBuffer) {
                    m_pFakeBackBuffer = new ProxySurface9(pRealSurf, this, m_displayW, m_displayH);
                } else {
                    m_pFakeBackBuffer->SetInternalSurface(pRealSurf);
                }
                m_pReal->SetRenderTarget(0, pRealSurf);
                Logger::info("Proxy: Fake backbuffer re-created at {}x{} (Reset path)", m_renderW, m_renderH);
            }
        }
        OverlayRenderer::Get().OnPostReset();
    }
    return hr;
}

STDMETHODIMP ProxyDirect3DDevice9::Present(CONST RECT* pSR, CONST RECT* pDR, HWND hW, CONST RGNDATA* pR) {
    UpdateScaledResolution();
    if (!m_jitterReadyForFrame) {
        UpdateJitterAndFrameIndex();
        m_jitterReadyForFrame = true;
    }
    OverlayRenderer::Get().NewFrame();
    IDirect3DSurface9* pRBB = nullptr;
    if (SUCCEEDED(m_pReal->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pRBB))) {
        D3DSURFACE_DESC bbDesc = {};
        if (SUCCEEDED(pRBB->GetDesc(&bbDesc))) {
            GamePlug::UpscalerManager::Get().UpdateSharedHDR(GamePlug::UpscalerManager::IsHDRFormat(bbDesc.Format));
        }
        if (m_isUpscaling && m_pFakeBackBuffer) {
            bool upscalerHandled = false;
            if (UpscalerManager::Get().IsUpscalingEnabled() && !Config::Get().GetBool("VKUpscaler", true)) {
                g_InUpscalerPass = true;
                UpscalerManager::Get().RenderFrame((void*)m_pReal, (void*)m_pFakeBackBuffer->GetInternalSurface(), (void*)pRBB, m_displayW,
                    m_displayH, m_renderW, m_renderH);
                g_InUpscalerPass = false;
                upscalerHandled = true;
            }

            if (!upscalerHandled) {
                m_pReal->StretchRect(m_pFakeBackBuffer->GetInternalSurface(), NULL, pRBB, NULL, D3DTEXF_LINEAR);
            }
        }
        IDirect3DSurface9* pOldRT = nullptr;
        m_pReal->GetRenderTarget(0, &pOldRT);
        m_pReal->SetRenderTarget(0, pRBB);
        OverlayRenderer::Get().Render(m_pReal, m_displayW, m_displayH);
        m_pReal->SetRenderTarget(0, pOldRT);
        if (pOldRT)
            pOldRT->Release();
        pRBB->Release();
    } else {
        OverlayRenderer::Get().Render(m_pReal, m_displayW, m_displayH);
    }
    m_jitterReadyForFrame = false;
    return m_pReal->Present(NULL, NULL, hW, pR);
}

STDMETHODIMP ProxyDirect3DDevice9::GetBackBuffer(UINT iSC, UINT iBB, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBB) {
    if (m_isUpscaling && m_pFakeBackBuffer && iSC == 0 && iBB == 0) {
        *ppBB = m_pFakeBackBuffer;
        m_pFakeBackBuffer->AddRef();
        return S_OK;
    }
    return m_pReal->GetBackBuffer(iSC, iBB, Type, ppBB);
}

STDMETHODIMP ProxyDirect3DDevice9::GetRasterStatus(UINT iSC, D3DRASTER_STATUS* pRS) {
    return m_pReal->GetRasterStatus(iSC, pRS);
}

STDMETHODIMP ProxyDirect3DDevice9::SetDialogBoxMode(BOOL bE) {
    return m_pReal->SetDialogBoxMode(bE);
}

STDMETHODIMP_(void) ProxyDirect3DDevice9::SetGammaRamp(UINT iSC, DWORD F, CONST D3DGAMMARAMP* pR) {
    m_pReal->SetGammaRamp(iSC, F, pR);
}

STDMETHODIMP_(void) ProxyDirect3DDevice9::GetGammaRamp(UINT iSC, D3DGAMMARAMP* pR) {
    m_pReal->GetGammaRamp(iSC, pR);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateTexture(
    UINT W, UINT H, UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DTexture9** ppT, HANDLE* pS) {
    if (!ppT)
        return D3DERR_INVALIDCALL;

    IDirect3DTexture9* pRealTex = nullptr;
    HRESULT hr = m_pReal->CreateTexture(W, H, L, U, F, P, &pRealTex, pS);
    if (SUCCEEDED(hr) && (U & D3DUSAGE_RENDERTARGET)) {
        GamePlug::UpscalerManager::Get().UpdateSharedHDR(GamePlug::UpscalerManager::IsHDRFormat(F));
    }
    if (SUCCEEDED(hr) && pRealTex) {
        UINT actualLevels = pRealTex->GetLevelCount();
        *ppT = new ProxyTexture9(pRealTex, this, W, H, actualLevels, U, F, P);
        pRealTex->Release();
    } else {
        *ppT = nullptr;
    }
    return hr;
}

STDMETHODIMP ProxyDirect3DDevice9::CreateVolumeTexture(
    UINT W, UINT H, UINT D, UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DVolumeTexture9** ppVT, HANDLE* pS) {
    return m_pReal->CreateVolumeTexture(W, H, D, L, U, F, P, ppVT, pS);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateCubeTexture(
    UINT E, UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DCubeTexture9** ppCT, HANDLE* pS) {
    return m_pReal->CreateCubeTexture(E, L, U, F, P, ppCT, pS);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateVertexBuffer(UINT L, DWORD U, DWORD F, D3DPOOL P, IDirect3DVertexBuffer9** ppVB, HANDLE* pS) {
    return m_pReal->CreateVertexBuffer(L, U, F, P, ppVB, pS);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateIndexBuffer(UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DIndexBuffer9** ppIB, HANDLE* pS) {
    return m_pReal->CreateIndexBuffer(L, U, F, P, ppIB, pS);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateRenderTarget(
    UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL L, IDirect3DSurface9** ppS, HANDLE* pS2) {
    HRESULT hr = m_pReal->CreateRenderTarget(W, H, F, M, MQ, L, ppS, pS2);
    if (SUCCEEDED(hr)) {
        GamePlug::UpscalerManager::Get().UpdateSharedHDR(GamePlug::UpscalerManager::IsHDRFormat(F));
    }
    return hr;
}

STDMETHODIMP ProxyDirect3DDevice9::CreateDepthStencilSurface(
    UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL D, IDirect3DSurface9** ppS, HANDLE* pS2) {
    return m_pReal->CreateDepthStencilSurface(W, H, F, M, MQ, D, ppS, pS2);
}

STDMETHODIMP ProxyDirect3DDevice9::UpdateSurface(IDirect3DSurface9* pSS, CONST RECT* pSR, IDirect3DSurface9* pDS, CONST POINT* pDP) {
    HRESULT hr = m_pReal->UpdateSurface(GetRealSurface(pSS), pSR, GetRealSurface(pDS), pDP);
    if (SUCCEEDED(hr) && pSS && pDS) {
        if (GamePlug::TextureReplacer::Get().IsProxySurface(pSS) && GamePlug::TextureReplacer::Get().IsProxySurface(pDS)) {
            ProxySurface9* pProxySrc = (ProxySurface9*)pSS;
            ProxySurface9* pProxyDst = (ProxySurface9*)pDS;
            ProxyTexture9* pSrcTex = pProxySrc->GetParentTexture();
            ProxyTexture9* pDstTex = pProxyDst->GetParentTexture();
            if (pSrcTex && pDstTex) {
                pDstTex->PropagateFrom(pSrcTex);
            }
        }
    }
    return hr;
}

STDMETHODIMP ProxyDirect3DDevice9::UpdateTexture(IDirect3DBaseTexture9* pST, IDirect3DBaseTexture9* pDT) {
    if (!pST || !pDT)
        return m_pReal->UpdateTexture(pST, pDT);

    ProxyTexture9* pProxySrc = GamePlug::TextureReplacer::Get().IsProxyTexture(pST) ? (ProxyTexture9*)pST : nullptr;
    ProxyTexture9* pProxyDst = GamePlug::TextureReplacer::Get().IsProxyTexture(pDT) ? (ProxyTexture9*)pDT : nullptr;

    IDirect3DBaseTexture9* pRealSrc = pProxySrc ? pProxySrc->GetRealTexture() : pST;
    IDirect3DBaseTexture9* pRealDst = pProxyDst ? pProxyDst->GetRealTexture() : pDT;

    HRESULT hr = m_pReal->UpdateTexture(pRealSrc, pRealDst);

    if (SUCCEEDED(hr) && pProxySrc && pProxyDst) {
        pProxyDst->PropagateFrom(pProxySrc);
    }
    return hr;
}

STDMETHODIMP ProxyDirect3DDevice9::GetRenderTargetData(IDirect3DSurface9* pRT, IDirect3DSurface9* pDS) {
    return m_pReal->GetRenderTargetData(GetRealSurface(pRT), GetRealSurface(pDS));
}

STDMETHODIMP ProxyDirect3DDevice9::GetFrontBufferData(UINT iSC, IDirect3DSurface9* pDS) {
    return m_pReal->GetFrontBufferData(iSC, GetRealSurface(pDS));
}

STDMETHODIMP ProxyDirect3DDevice9::StretchRect(
    IDirect3DSurface9* pSS, CONST RECT* pSR, IDirect3DSurface9* pDS, CONST RECT* pDR, D3DTEXTUREFILTERTYPE F) {
    return m_pReal->StretchRect(GetRealSurface(pSS), pSR, GetRealSurface(pDS), pDR, F);
}

STDMETHODIMP ProxyDirect3DDevice9::ColorFill(IDirect3DSurface9* pS, CONST RECT* pR, D3DCOLOR c) {
    return m_pReal->ColorFill(GetRealSurface(pS), pR, c);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateOffscreenPlainSurface(
    UINT W, UINT H, D3DFORMAT F, D3DPOOL P, IDirect3DSurface9** ppS, HANDLE* pS2) {
    return m_pReal->CreateOffscreenPlainSurface(W, H, F, P, ppS, pS2);
}

STDMETHODIMP ProxyDirect3DDevice9::SetRenderTarget(DWORD RTI, IDirect3DSurface9* pRT) {
    return m_pReal->SetRenderTarget(RTI, GetRealSurface(pRT));
}

STDMETHODIMP ProxyDirect3DDevice9::GetRenderTarget(DWORD RTI, IDirect3DSurface9** ppRT) {
    if (!ppRT)
        return D3DERR_INVALIDCALL;
    HRESULT hr = m_pReal->GetRenderTarget(RTI, ppRT);
    if (SUCCEEDED(hr) && *ppRT) {
        ProxySurface9* pProxy = GamePlug::TextureReplacer::Get().FindProxySurfaceByReal(*ppRT);
        if (pProxy) {
            (*ppRT)->Release();
            *ppRT = pProxy;
        }
    }
    return hr;
}

STDMETHODIMP ProxyDirect3DDevice9::SetDepthStencilSurface(IDirect3DSurface9* pNZS) {
    return m_pReal->SetDepthStencilSurface(GetRealSurface(pNZS));
}

STDMETHODIMP ProxyDirect3DDevice9::GetDepthStencilSurface(IDirect3DSurface9** ppZSS) {
    if (!ppZSS)
        return D3DERR_INVALIDCALL;
    HRESULT hr = m_pReal->GetDepthStencilSurface(ppZSS);
    if (SUCCEEDED(hr) && *ppZSS) {
        ProxySurface9* pProxy = GamePlug::TextureReplacer::Get().FindProxySurfaceByReal(*ppZSS);
        if (pProxy) {
            (*ppZSS)->Release();
            *ppZSS = pProxy;
        }
    }
    return hr;
}

STDMETHODIMP ProxyDirect3DDevice9::BeginScene() {
    if (!m_jitterReadyForFrame) {
        UpdateJitterAndFrameIndex();
        m_jitterReadyForFrame = true;
    }
    return m_pReal->BeginScene();
}

STDMETHODIMP ProxyDirect3DDevice9::EndScene() {
    return m_pReal->EndScene();
}

STDMETHODIMP ProxyDirect3DDevice9::Clear(DWORD C, CONST D3DRECT* pR, DWORD F, D3DCOLOR C2, float Z, DWORD S) {
    return m_pReal->Clear(C, pR, F, C2, Z, S);
}

STDMETHODIMP ProxyDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE S, CONST D3DMATRIX* pM) {
    if (S == D3DTS_PROJECTION && pM) {
        D3DMATRIX modifiedM;
        if (GamePlug::UpscalerManager::Get().ProcessSetTransform(S, pM, modifiedM)) {
            return m_pReal->SetTransform(S, &modifiedM);
        }
    }
    return m_pReal->SetTransform(S, pM);
}

STDMETHODIMP ProxyDirect3DDevice9::GetTransform(D3DTRANSFORMSTATETYPE S, D3DMATRIX* pM) {
    return m_pReal->GetTransform(S, pM);
}

STDMETHODIMP ProxyDirect3DDevice9::MultiplyTransform(D3DTRANSFORMSTATETYPE S, CONST D3DMATRIX* pM) {
    return m_pReal->MultiplyTransform(S, pM);
}

STDMETHODIMP ProxyDirect3DDevice9::SetViewport(CONST D3DVIEWPORT9* pV) {
    if (!pV)
        return D3DERR_INVALIDCALL;
    UpdateScaledResolution();

    IDirect3DSurface9* pRT = nullptr;
    m_pReal->GetRenderTarget(0, &pRT);

    D3DVIEWPORT9 scaledVp;
    IDirect3DSurface9* fakeSurf = m_pFakeBackBuffer ? m_pFakeBackBuffer->GetInternalSurface() : nullptr;
    bool modified = GamePlug::UpscalerManager::Get().ProcessSetViewport(pV, pRT, fakeSurf, g_InUpscalerPass, scaledVp);

    if (pRT)
        pRT->Release();

    if (modified) {
        return m_pReal->SetViewport(&scaledVp);
    }
    return m_pReal->SetViewport(pV);
}

STDMETHODIMP ProxyDirect3DDevice9::GetViewport(D3DVIEWPORT9* pV) {
    return m_pReal->GetViewport(pV);
}

STDMETHODIMP ProxyDirect3DDevice9::SetMaterial(CONST D3DMATERIAL9* pM) {
    return m_pReal->SetMaterial(pM);
}

STDMETHODIMP ProxyDirect3DDevice9::GetMaterial(D3DMATERIAL9* pM) {
    return m_pReal->GetMaterial(pM);
}

STDMETHODIMP ProxyDirect3DDevice9::SetLight(DWORD I, CONST D3DLIGHT9* pL) {
    return m_pReal->SetLight(I, pL);
}

STDMETHODIMP ProxyDirect3DDevice9::GetLight(DWORD I, D3DLIGHT9* pL) {
    return m_pReal->GetLight(I, pL);
}

STDMETHODIMP ProxyDirect3DDevice9::LightEnable(DWORD I, BOOL E) {
    return m_pReal->LightEnable(I, E);
}

STDMETHODIMP ProxyDirect3DDevice9::GetLightEnable(DWORD I, BOOL* pE) {
    return m_pReal->GetLightEnable(I, pE);
}

STDMETHODIMP ProxyDirect3DDevice9::SetClipPlane(DWORD I, CONST float* pP) {
    return m_pReal->SetClipPlane(I, pP);
}

STDMETHODIMP ProxyDirect3DDevice9::GetClipPlane(DWORD I, float* pP) {
    return m_pReal->GetClipPlane(I, pP);
}

STDMETHODIMP ProxyDirect3DDevice9::SetRenderState(D3DRENDERSTATETYPE S, DWORD V) {
    return m_pReal->SetRenderState(S, V);
}

STDMETHODIMP ProxyDirect3DDevice9::GetRenderState(D3DRENDERSTATETYPE S, DWORD* pV) {
    return m_pReal->GetRenderState(S, pV);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateStateBlock(D3DSTATEBLOCKTYPE T, IDirect3DStateBlock9** ppSB) {
    return m_pReal->CreateStateBlock(T, ppSB);
}

STDMETHODIMP ProxyDirect3DDevice9::BeginStateBlock() {
    return m_pReal->BeginStateBlock();
}

STDMETHODIMP ProxyDirect3DDevice9::EndStateBlock(IDirect3DStateBlock9** ppSB) {
    return m_pReal->EndStateBlock(ppSB);
}

STDMETHODIMP ProxyDirect3DDevice9::SetClipStatus(CONST D3DCLIPSTATUS9* pCS) {
    return m_pReal->SetClipStatus(pCS);
}

STDMETHODIMP ProxyDirect3DDevice9::GetClipStatus(D3DCLIPSTATUS9* pCS) {
    return m_pReal->GetClipStatus(pCS);
}

STDMETHODIMP ProxyDirect3DDevice9::GetTexture(DWORD S, IDirect3DBaseTexture9** ppT) {
    if (!ppT)
        return D3DERR_INVALIDCALL;
    IDirect3DBaseTexture9* pRealTex = nullptr;
    HRESULT hr = m_pReal->GetTexture(S, &pRealTex);
    if (SUCCEEDED(hr) && pRealTex) {
        ProxyTexture9* pProxy = TextureReplacer::Get().FindProxyByReal(pRealTex);
        if (pProxy) {
            *ppT = pProxy;
            // FindProxyByReal already incremented the reference count under the lock!
            pRealTex->Release();
        } else {
            *ppT = pRealTex;
        }
    } else {
        *ppT = nullptr;
    }
    return hr;
}

STDMETHODIMP ProxyDirect3DDevice9::SetTexture(DWORD S, IDirect3DBaseTexture9* pT) {
    if (pT && GamePlug::TextureReplacer::Get().IsProxyTexture(pT)) {
        ProxyTexture9* pProxyTex = (ProxyTexture9*)pT;
        TextureReplacer::Get().OnTextureBound(pProxyTex);

        IDirect3DTexture9* pRealTex = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(TextureReplacer::Get().GetMutex());
            pRealTex = pProxyTex->GetReplacedReal();
            if (pRealTex) {
                pRealTex->AddRef();
            }
        }

        HRESULT hr = m_pReal->SetTexture(S, pRealTex);

        if (pRealTex) {
            pRealTex->Release();
        }
        return hr;
    }
    return m_pReal->SetTexture(S, pT);
}

STDMETHODIMP ProxyDirect3DDevice9::GetTextureStageState(DWORD S, D3DTEXTURESTAGESTATETYPE T, DWORD* pV) {
    return m_pReal->GetTextureStageState(S, T, pV);
}

STDMETHODIMP ProxyDirect3DDevice9::SetTextureStageState(DWORD S, D3DTEXTURESTAGESTATETYPE T, DWORD V) {
    return m_pReal->SetTextureStageState(S, T, V);
}

STDMETHODIMP ProxyDirect3DDevice9::GetSamplerState(DWORD S, D3DSAMPLERSTATETYPE T, DWORD* pV) {
    return m_pReal->GetSamplerState(S, T, pV);
}

STDMETHODIMP ProxyDirect3DDevice9::SetSamplerState(DWORD S, D3DSAMPLERSTATETYPE T, DWORD V) {
    return m_pReal->SetSamplerState(S, T, V);
}

STDMETHODIMP ProxyDirect3DDevice9::ValidateDevice(DWORD* pNP) {
    return m_pReal->ValidateDevice(pNP);
}

STDMETHODIMP ProxyDirect3DDevice9::SetPaletteEntries(UINT PN, CONST PALETTEENTRY* pE) {
    return m_pReal->SetPaletteEntries(PN, pE);
}

STDMETHODIMP ProxyDirect3DDevice9::GetPaletteEntries(UINT PN, PALETTEENTRY* pE) {
    return m_pReal->GetPaletteEntries(PN, pE);
}

STDMETHODIMP ProxyDirect3DDevice9::SetCurrentTexturePalette(UINT PN) {
    return m_pReal->SetCurrentTexturePalette(PN);
}

STDMETHODIMP ProxyDirect3DDevice9::GetCurrentTexturePalette(UINT* PN) {
    return m_pReal->GetCurrentTexturePalette(PN);
}

STDMETHODIMP ProxyDirect3DDevice9::SetScissorRect(CONST RECT* pR) {
    Logger::info("ProxyDirect3DDevice9 SetScissorRect");
    if (!pR)
        return D3DERR_INVALIDCALL;
    UpdateScaledResolution();

    IDirect3DSurface9* pRT = nullptr;
    m_pReal->GetRenderTarget(0, &pRT);

    RECT scaledR;
    IDirect3DSurface9* fakeSurf = m_pFakeBackBuffer ? m_pFakeBackBuffer->GetInternalSurface() : nullptr;
    bool modified = GamePlug::UpscalerManager::Get().ProcessSetScissorRect(pR, pRT, fakeSurf, g_InUpscalerPass, scaledR);

    if (pRT)
        pRT->Release();

    if (modified) {
        return m_pReal->SetScissorRect(&scaledR);
    }
    return m_pReal->SetScissorRect(pR);
}

STDMETHODIMP ProxyDirect3DDevice9::GetScissorRect(RECT* pR) {
    return m_pReal->GetScissorRect(pR);
}

STDMETHODIMP ProxyDirect3DDevice9::SetSoftwareVertexProcessing(BOOL bS) {
    return m_pReal->SetSoftwareVertexProcessing(bS);
}

STDMETHODIMP_(BOOL) ProxyDirect3DDevice9::GetSoftwareVertexProcessing() {
    return m_pReal->GetSoftwareVertexProcessing();
}

STDMETHODIMP ProxyDirect3DDevice9::SetNPatchMode(float nS) {
    return m_pReal->SetNPatchMode(nS);
}

STDMETHODIMP_(float) ProxyDirect3DDevice9::GetNPatchMode() {
    return m_pReal->GetNPatchMode();
}

STDMETHODIMP ProxyDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE PT, UINT SV, UINT PC) {
    return m_pReal->DrawPrimitive(PT, SV, PC);
}

STDMETHODIMP ProxyDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE PT, INT BVI, UINT MVI, UINT NV, UINT SI, UINT PC) {
    return m_pReal->DrawIndexedPrimitive(PT, BVI, MVI, NV, SI, PC);
}

STDMETHODIMP ProxyDirect3DDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE PT, UINT PC, CONST void* pV, UINT VS) {
    return m_pReal->DrawPrimitiveUP(PT, PC, pV, VS);
}

STDMETHODIMP ProxyDirect3DDevice9::DrawIndexedPrimitiveUP(
    D3DPRIMITIVETYPE PT, UINT MVI, UINT NV, UINT PC, CONST void* pI, D3DFORMAT IF, CONST void* pV, UINT VS) {
    return m_pReal->DrawIndexedPrimitiveUP(PT, MVI, NV, PC, pI, IF, pV, VS);
}

STDMETHODIMP ProxyDirect3DDevice9::ProcessVertices(
    UINT SSI, UINT DI, UINT VC, IDirect3DVertexBuffer9* pDB, IDirect3DVertexDeclaration9* pVD, DWORD F) {
    return m_pReal->ProcessVertices(SSI, DI, VC, pDB, pVD, F);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVE, IDirect3DVertexDeclaration9** ppD) {
    return m_pReal->CreateVertexDeclaration(pVE, ppD);
}

STDMETHODIMP ProxyDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* pD) {
    return m_pReal->SetVertexDeclaration(pD);
}

STDMETHODIMP ProxyDirect3DDevice9::GetVertexDeclaration(IDirect3DVertexDeclaration9** ppD) {
    return m_pReal->GetVertexDeclaration(ppD);
}

STDMETHODIMP ProxyDirect3DDevice9::SetFVF(DWORD FVF) {
    return m_pReal->SetFVF(FVF);
}

STDMETHODIMP ProxyDirect3DDevice9::GetFVF(DWORD* pFVF) {
    return m_pReal->GetFVF(pFVF);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateVertexShader(CONST DWORD* pF, IDirect3DVertexShader9** ppS) {
    return m_pReal->CreateVertexShader(pF, ppS);
}

STDMETHODIMP ProxyDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9* pS) {
    return m_pReal->SetVertexShader(pS);
}

STDMETHODIMP ProxyDirect3DDevice9::GetVertexShader(IDirect3DVertexShader9** ppS) {
    return m_pReal->GetVertexShader(ppS);
}

STDMETHODIMP ProxyDirect3DDevice9::SetVertexShaderConstantF(UINT SR, CONST float* pCD, UINT V4C) {
    if (pCD && V4C >= 4) {
        std::vector<float> modifiedCD;
        if (GamePlug::UpscalerManager::Get().ProcessSetVertexShaderConstantF(SR, pCD, V4C, modifiedCD)) {
            return m_pReal->SetVertexShaderConstantF(SR, modifiedCD.data(), V4C);
        }
    }
    return m_pReal->SetVertexShaderConstantF(SR, pCD, V4C);
}

STDMETHODIMP ProxyDirect3DDevice9::GetVertexShaderConstantF(UINT SR, float* pCD, UINT V4C) {
    return m_pReal->GetVertexShaderConstantF(SR, pCD, V4C);
}

STDMETHODIMP ProxyDirect3DDevice9::SetVertexShaderConstantI(UINT SR, CONST int* pCD, UINT V4C) {
    return m_pReal->SetVertexShaderConstantI(SR, pCD, V4C);
}

STDMETHODIMP ProxyDirect3DDevice9::GetVertexShaderConstantI(UINT SR, int* pCD, UINT V4C) {
    return m_pReal->GetVertexShaderConstantI(SR, pCD, V4C);
}

STDMETHODIMP ProxyDirect3DDevice9::SetVertexShaderConstantB(UINT SR, CONST BOOL* pCD, UINT BC) {
    return m_pReal->SetVertexShaderConstantB(SR, pCD, BC);
}

STDMETHODIMP ProxyDirect3DDevice9::GetVertexShaderConstantB(UINT SR, BOOL* pCD, UINT BC) {
    return m_pReal->GetVertexShaderConstantB(SR, pCD, BC);
}

STDMETHODIMP ProxyDirect3DDevice9::SetStreamSource(
    UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) {
    return m_pReal->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
}

STDMETHODIMP ProxyDirect3DDevice9::GetStreamSource(
    UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) {
    return m_pReal->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride);
}

STDMETHODIMP ProxyDirect3DDevice9::SetStreamSourceFreq(UINT SN, UINT S) {
    return m_pReal->SetStreamSourceFreq(SN, S);
}

STDMETHODIMP ProxyDirect3DDevice9::GetStreamSourceFreq(UINT SN, UINT* pS) {
    return m_pReal->GetStreamSourceFreq(SN, pS);
}

STDMETHODIMP ProxyDirect3DDevice9::SetIndices(IDirect3DIndexBuffer9* pID) {
    return m_pReal->SetIndices(pID);
}

STDMETHODIMP ProxyDirect3DDevice9::GetIndices(IDirect3DIndexBuffer9** ppID) {
    return m_pReal->GetIndices(ppID);
}

STDMETHODIMP ProxyDirect3DDevice9::CreatePixelShader(CONST DWORD* pF, IDirect3DPixelShader9** ppS) {
    return m_pReal->CreatePixelShader(pF, ppS);
}

STDMETHODIMP ProxyDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9* pS) {
    return m_pReal->SetPixelShader(pS);
}

STDMETHODIMP ProxyDirect3DDevice9::GetPixelShader(IDirect3DPixelShader9** ppS) {
    return m_pReal->GetPixelShader(ppS);
}

STDMETHODIMP ProxyDirect3DDevice9::SetPixelShaderConstantF(UINT SR, CONST float* pCD, UINT V4C) {
    return m_pReal->SetPixelShaderConstantF(SR, pCD, V4C);
}

STDMETHODIMP ProxyDirect3DDevice9::GetPixelShaderConstantF(UINT SR, float* pCD, UINT V4C) {
    return m_pReal->GetPixelShaderConstantF(SR, pCD, V4C);
}

STDMETHODIMP ProxyDirect3DDevice9::SetPixelShaderConstantI(UINT SR, CONST int* pCD, UINT V4C) {
    return m_pReal->SetPixelShaderConstantI(SR, pCD, V4C);
}

STDMETHODIMP ProxyDirect3DDevice9::GetPixelShaderConstantI(UINT SR, int* pCD, UINT V4C) {
    return m_pReal->GetPixelShaderConstantI(SR, pCD, V4C);
}

STDMETHODIMP ProxyDirect3DDevice9::SetPixelShaderConstantB(UINT SR, CONST BOOL* pCD, UINT BC) {
    return m_pReal->SetPixelShaderConstantB(SR, pCD, BC);
}

STDMETHODIMP ProxyDirect3DDevice9::GetPixelShaderConstantB(UINT SR, BOOL* pCD, UINT BC) {
    return m_pReal->GetPixelShaderConstantB(SR, pCD, BC);
}

STDMETHODIMP ProxyDirect3DDevice9::DrawRectPatch(UINT H, CONST float* pNS, CONST D3DRECTPATCH_INFO* pPI) {
    return m_pReal->DrawRectPatch(H, pNS, pPI);
}

STDMETHODIMP ProxyDirect3DDevice9::DrawTriPatch(UINT H, CONST float* pNS, CONST D3DTRIPATCH_INFO* pPI) {
    return m_pReal->DrawTriPatch(H, pNS, pPI);
}

STDMETHODIMP ProxyDirect3DDevice9::DeletePatch(UINT H) {
    return m_pReal->DeletePatch(H);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateQuery(D3DQUERYTYPE T, IDirect3DQuery9** ppQ) {
    return m_pReal->CreateQuery(T, ppQ);
}

// IDirect3DDevice9Ex methods
STDMETHODIMP ProxyDirect3DDevice9::SetConvolutionMonoKernel(UINT W, UINT H, float* R, float* C) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->SetConvolutionMonoKernel(W, H, R, C);
}

STDMETHODIMP ProxyDirect3DDevice9::ComposeRects(IDirect3DSurface9* pS, IDirect3DSurface9* pD, IDirect3DVertexBuffer9* pSRD, UINT NR,
    IDirect3DVertexBuffer9* pDRD, D3DCOMPOSERECTSOP O, int XO, int YO) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->ComposeRects(GetRealSurface(pS), GetRealSurface(pD), pSRD, NR, pDRD, O, XO, YO);
}

STDMETHODIMP ProxyDirect3DDevice9::PresentEx(CONST RECT* pSR, CONST RECT* pDR, HWND hW, CONST RGNDATA* pR, DWORD F) {
    UpdateScaledResolution();
    if (!m_jitterReadyForFrame) {
        UpdateJitterAndFrameIndex();
        m_jitterReadyForFrame = true;
    }
    OverlayRenderer::Get().NewFrame();
    IDirect3DSurface9* pRBB = nullptr;
    if (SUCCEEDED(m_pReal->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pRBB))) {
        D3DSURFACE_DESC bbDesc = {};
        if (SUCCEEDED(pRBB->GetDesc(&bbDesc))) {
            GamePlug::UpscalerManager::Get().UpdateSharedHDR(GamePlug::UpscalerManager::IsHDRFormat(bbDesc.Format));
        }
        if (m_isUpscaling && m_pFakeBackBuffer) {
            bool upscalerHandled = false;
            if (UpscalerManager::Get().IsUpscalingEnabled() && !Config::Get().GetBool("VKUpscaler", true)) {
                g_InUpscalerPass = true;
                UpscalerManager::Get().RenderFrame((void*)m_pReal, (void*)m_pFakeBackBuffer->GetInternalSurface(), (void*)pRBB, m_displayW,
                    m_displayH, m_renderW, m_renderH);
                g_InUpscalerPass = false;
                upscalerHandled = true;
            }

            if (!upscalerHandled) {
                m_pReal->StretchRect(m_pFakeBackBuffer->GetInternalSurface(), NULL, pRBB, NULL, D3DTEXF_LINEAR);
            }
        }
        IDirect3DSurface9* pOldRT = nullptr;
        m_pReal->GetRenderTarget(0, &pOldRT);
        m_pReal->SetRenderTarget(0, pRBB);
        OverlayRenderer::Get().Render(m_pReal, m_displayW, m_displayH);
        m_pReal->SetRenderTarget(0, pOldRT);
        if (pOldRT)
            pOldRT->Release();
        pRBB->Release();
    } else {
        OverlayRenderer::Get().Render(m_pReal, m_displayW, m_displayH);
    }
    if (!m_pRealEx) {
        m_jitterReadyForFrame = false;
        return E_NOTIMPL;
    }
    m_jitterReadyForFrame = false;
    return m_pRealEx->PresentEx(NULL, NULL, hW, pR, F);
}

STDMETHODIMP ProxyDirect3DDevice9::GetGPUThreadPriority(INT* pP) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->GetGPUThreadPriority(pP);
}

STDMETHODIMP ProxyDirect3DDevice9::SetGPUThreadPriority(INT P) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->SetGPUThreadPriority(P);
}

STDMETHODIMP ProxyDirect3DDevice9::WaitForVBlank(UINT iSC) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->WaitForVBlank(iSC);
}

STDMETHODIMP ProxyDirect3DDevice9::CheckResourceResidency(IDirect3DResource9** pRA, UINT32 NR) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->CheckResourceResidency(pRA, NR);
}

STDMETHODIMP ProxyDirect3DDevice9::SetMaximumFrameLatency(UINT ML) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->SetMaximumFrameLatency(ML);
}

STDMETHODIMP ProxyDirect3DDevice9::GetMaximumFrameLatency(UINT* pML) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->GetMaximumFrameLatency(pML);
}

STDMETHODIMP ProxyDirect3DDevice9::CheckDeviceState(HWND hDW) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->CheckDeviceState(hDW);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateRenderTargetEx(
    UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL L, IDirect3DSurface9** ppS, HANDLE* pS2, DWORD U) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->CreateRenderTargetEx(W, H, F, M, MQ, L, ppS, pS2, U);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateOffscreenPlainSurfaceEx(
    UINT W, UINT H, D3DFORMAT F, D3DPOOL P, IDirect3DSurface9** ppS, HANDLE* pS2, DWORD U) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->CreateOffscreenPlainSurfaceEx(W, H, F, P, ppS, pS2, U);
}

STDMETHODIMP ProxyDirect3DDevice9::CreateDepthStencilSurfaceEx(
    UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL D, IDirect3DSurface9** ppS, HANDLE* pS2, DWORD U) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->CreateDepthStencilSurfaceEx(W, H, F, M, MQ, D, ppS, pS2, U);
}

STDMETHODIMP ProxyDirect3DDevice9::ResetEx(D3DPRESENT_PARAMETERS* pPP, D3DDISPLAYMODEEX* pFDM) {
    if (!m_pRealEx)
        return E_NOTIMPL;

    // Unbind any potentially bound resources on the real device to clear device state references
    IDirect3DSurface9* pOrigBB = nullptr;
    if (SUCCEEDED(m_pRealEx->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pOrigBB))) {
        m_pRealEx->SetRenderTarget(0, pOrigBB);
        pOrigBB->Release();
    }
    for (DWORD i = 1; i < 4; ++i) {
        m_pRealEx->SetRenderTarget(i, nullptr);
    }
    m_pRealEx->SetDepthStencilSurface(nullptr);

    for (DWORD i = 0; i < 16; ++i) {
        m_pRealEx->SetTexture(i, nullptr);
    }
    for (DWORD i = 0; i < 4; ++i) {
        m_pRealEx->SetTexture(D3DVERTEXTEXTURESAMPLER0 + i, nullptr);
    }

    for (DWORD i = 0; i < 16; ++i) {
        m_pRealEx->SetStreamSource(i, nullptr, 0, 0);
    }
    m_pRealEx->SetIndices(nullptr);

    for (auto const& [surf, tex] : m_depthSurfaceToTextureMap) {
        if (tex)
            tex->Release();
    }
    m_depthSurfaceToTextureMap.clear();
    if (m_downsampledDepthTexINTZ) {
        m_downsampledDepthTexINTZ->Release();
        m_downsampledDepthTexINTZ = nullptr;
    }

    OverlayRenderer::Get().OnReset();
    UpscalerManager::Get().OnReset();
    if (m_pFakeBackBuffer) {
        m_pFakeBackBuffer->SetInternalSurface(nullptr);
        m_pFakeBackBuffer->Release();
        m_pFakeBackBuffer = nullptr;
    }


    int requestedW = pPP->BackBufferWidth;
    int requestedH = pPP->BackBufferHeight;

    int nativeW = Config::Get().GetTargetWidth();
    int nativeH = Config::Get().GetTargetHeight();

    if (nativeW <= 0 || nativeH <= 0) {
        if (m_isUpscaling && requestedW == (int)m_renderW && requestedH == (int)m_renderH) {
            nativeW = m_displayW;
            nativeH = m_displayH;
        } else {
            nativeW = requestedW;
            nativeH = requestedH;
        }
    }

    int scaledW = nativeW;
    int scaledH = nativeH;
    UpscalerManager::Get().GetScaledResolution(scaledW, scaledH);

    D3DPRESENT_PARAMETERS realPP = *pPP;
    realPP.BackBufferWidth = nativeW;
    realPP.BackBufferHeight = nativeH;
    if (realPP.hDeviceWindow == NULL) {
        realPP.hDeviceWindow = m_hFocusWindow;
    }

    m_renderW = scaledW;
    m_renderH = scaledH;
    m_displayW = nativeW;
    m_displayH = nativeH;
    m_isUpscaling = true;

    Logger::info("ResetEx: Game requested {}x{}, Proxy created device at {}x{}, Game sees {}x{}", pPP->BackBufferWidth,
        pPP->BackBufferHeight, nativeW, nativeH, scaledW, scaledH);

    {
        auto& mgr = GamePlug::UpscalerManager::Get();
        mgr.UpdateSharedHDR(GamePlug::UpscalerManager::IsHDRFormat(pPP->BackBufferFormat) || GamePlug::UpscalerManager::IsHDRFormat(realPP.BackBufferFormat));
    }

    LogPresentParameters("ResetEx (Game input)", pPP);
    LogPresentParameters("ResetEx (Real output)", &realPP);

    LogActiveDefaultPoolResources();

    m_renderW = scaledW;
    m_renderH = scaledH;
    m_displayW = nativeW;
    m_displayH = nativeH;
    m_isUpscaling = true;

    Logger::info("ResetEx: Game requested {}x{}, Proxy created device at {}x{}, Game sees {}x{}", requestedW, requestedH, nativeW, nativeH,
        scaledW, scaledH);

    if (!m_pRealEx)
        return E_NOTIMPL;
    HRESULT hr = m_pRealEx->ResetEx(&realPP, pFDM);
    Logger::info("ResetEx: m_pRealEx->ResetEx returned hr={:08X}", (uint32_t)hr);
    if (SUCCEEDED(hr)) {
        pPP->BackBufferWidth = scaledW;
        pPP->BackBufferHeight = scaledH;
        if (m_isUpscaling) {
            UpscalerManager::Get().CreateFakeBackBuffer(m_pReal, m_displayW, m_displayH, D3DFMT_A8R8G8B8);
            IDirect3DSurface9* pRealSurf = UpscalerManager::Get().GetFakeBackBufferSurface();
            if (pRealSurf) {
                if (!m_pFakeBackBuffer) {
                    m_pFakeBackBuffer = new ProxySurface9(pRealSurf, this, m_displayW, m_displayH);
                } else {
                    m_pFakeBackBuffer->SetInternalSurface(pRealSurf);
                }
                m_pReal->SetRenderTarget(0, pRealSurf);
                Logger::info("Proxy: Fake backbuffer re-created at {}x{} (ResetEx path)", m_renderW, m_renderH);
            }
        }
        OverlayRenderer::Get().OnPostReset();
    }
    return hr;
}

STDMETHODIMP ProxyDirect3DDevice9::GetDisplayModeEx(UINT iSC, D3DDISPLAYMODEEX* pM, D3DDISPLAYROTATION* pR) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->GetDisplayModeEx(iSC, pM, pR);
}
