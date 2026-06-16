#include "d3d9_proxy_d3d9.h"
#include "d3d9_proxy_device.h"
#include "upscaler_manager.h"

ProxyDirect3D9::ProxyDirect3D9(IDirect3D9* pReal)
    : m_pReal(pReal)
    , m_pRealEx(nullptr) {
    if (SUCCEEDED(m_pReal->QueryInterface(IID_IDirect3D9Ex, (void**)&m_pRealEx))) {
        m_pRealEx->Release();
    }
}

ProxyDirect3D9::~ProxyDirect3D9() {
}

STDMETHODIMP ProxyDirect3D9::QueryInterface(REFIID riid, void** ppvObj) {
    if (riid == IID_IDirect3D9 || riid == IID_IUnknown) {
        *ppvObj = (IDirect3D9*)this;
        AddRef();
        return S_OK;
    }
    if (riid == IID_IDirect3D9Ex) {
        if (m_pRealEx) {
            *ppvObj = (IDirect3D9Ex*)this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    return m_pReal->QueryInterface(riid, ppvObj);
}

STDMETHODIMP_(ULONG) ProxyDirect3D9::AddRef() {
    return m_pReal->AddRef();
}

STDMETHODIMP_(ULONG) ProxyDirect3D9::Release() {
    ULONG res = m_pReal->Release();
    if (res == 0)
        delete this;
    return res;
}

STDMETHODIMP ProxyDirect3D9::RegisterSoftwareDevice(void* pIF) {
    return m_pReal->RegisterSoftwareDevice(pIF);
}

STDMETHODIMP_(UINT) ProxyDirect3D9::GetAdapterCount() {
    return m_pReal->GetAdapterCount();
}

STDMETHODIMP ProxyDirect3D9::GetAdapterIdentifier(UINT A, DWORD F, D3DADAPTER_IDENTIFIER9* pI) {
    return m_pReal->GetAdapterIdentifier(A, F, pI);
}

STDMETHODIMP_(UINT) ProxyDirect3D9::GetAdapterModeCount(UINT A, D3DFORMAT F) {
    UINT realCount = m_pReal->GetAdapterModeCount(A, F);
    if (realCount > 0) {
        realCount += (UINT)Config::Get().GetExtraResolutions().size();
    }
    return realCount;
}

STDMETHODIMP ProxyDirect3D9::EnumAdapterModes(UINT A, D3DFORMAT F, UINT M, D3DDISPLAYMODE* pM) {
    UINT realCount = m_pReal->GetAdapterModeCount(A, F);
    if (M < realCount)
        return m_pReal->EnumAdapterModes(A, F, M, pM);

    if (realCount == 0)
        return D3DERR_INVALIDCALL;

    UINT extraIdx = M - realCount;
    const auto& extra = Config::Get().GetExtraResolutions();
    if (extraIdx < extra.size()) {
        if (pM) {
            pM->Width = extra[extraIdx].width;
            pM->Height = extra[extraIdx].height;
            pM->RefreshRate = 60;
            pM->Format = F;
        }
        return S_OK;
    }
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP ProxyDirect3D9::GetAdapterDisplayMode(UINT A, D3DDISPLAYMODE* pM) {
    return m_pReal->GetAdapterDisplayMode(A, pM);
}

STDMETHODIMP ProxyDirect3D9::CheckDeviceType(UINT A, D3DDEVTYPE DT, D3DFORMAT AF, D3DFORMAT BF, BOOL W) {
    return m_pReal->CheckDeviceType(A, DT, AF, BF, W);
}

STDMETHODIMP ProxyDirect3D9::CheckDeviceFormat(UINT A, D3DDEVTYPE DT, D3DFORMAT AF, DWORD U, D3DRESOURCETYPE RT, D3DFORMAT CF) {
    return m_pReal->CheckDeviceFormat(A, DT, AF, U, RT, CF);
}

STDMETHODIMP ProxyDirect3D9::CheckDeviceMultiSampleType(UINT A, D3DDEVTYPE DT, D3DFORMAT SF, BOOL W, D3DMULTISAMPLE_TYPE MT, DWORD* pQL) {
    return m_pReal->CheckDeviceMultiSampleType(A, DT, SF, W, MT, pQL);
}

STDMETHODIMP ProxyDirect3D9::CheckDepthStencilMatch(UINT A, D3DDEVTYPE DT, D3DFORMAT AF, D3DFORMAT RTF, D3DFORMAT DSF) {
    return m_pReal->CheckDepthStencilMatch(A, DT, AF, RTF, DSF);
}

STDMETHODIMP ProxyDirect3D9::CheckDeviceFormatConversion(UINT A, D3DDEVTYPE DT, D3DFORMAT SF, D3DFORMAT TF) {
    return m_pReal->CheckDeviceFormatConversion(A, DT, SF, TF);
}

STDMETHODIMP ProxyDirect3D9::GetDeviceCaps(UINT A, D3DDEVTYPE DT, D3DCAPS9* pC) {
    return m_pReal->GetDeviceCaps(A, DT, pC);
}

STDMETHODIMP_(HMONITOR) ProxyDirect3D9::GetAdapterMonitor(UINT A) {
    return m_pReal->GetAdapterMonitor(A);
}

static void LogPresentParameters(const char* prefix, const D3DPRESENT_PARAMETERS* pPP) {
    if (!pPP)
        return;
    Logger::info("{}: W={}, H={}, Fmt={}, Cnt={}, MSType={}, MSQual={}, Swap={}, "
                 "hWnd={:p}, Wnd={}, Depth={}, DSFormat={}, Flags={}, "
                 "Refresh={}, Interval={}",
        prefix, pPP->BackBufferWidth, pPP->BackBufferHeight, (int)pPP->BackBufferFormat, pPP->BackBufferCount, (int)pPP->MultiSampleType,
        pPP->MultiSampleQuality, (int)pPP->SwapEffect, (void*)pPP->hDeviceWindow, pPP->Windowed, pPP->EnableAutoDepthStencil,
        (int)pPP->AutoDepthStencilFormat, pPP->Flags, pPP->FullScreen_RefreshRateInHz, pPP->PresentationInterval);
}

STDMETHODIMP ProxyDirect3D9::CreateDevice(UINT A, D3DDEVTYPE DT, HWND hFW, DWORD BF, D3DPRESENT_PARAMETERS* pPP, IDirect3DDevice9** ppRDI) {
    if (!pPP)
        return D3DERR_INVALIDCALL;
    OverlayRenderer::Get().SetWindow(hFW);

    // Load the upscaler plugin early so GetScaledResolution can check its status correctly
    if (!Config::Get().GetBool("VKUpscaler", true)) {
        UpscalerManager::Get().LoadUpscaler();
    }

    int requestedW = pPP->BackBufferWidth;
    int requestedH = pPP->BackBufferHeight;

    int nativeW = Config::Get().GetTargetWidth();
    int nativeH = Config::Get().GetTargetHeight();

    // Fallback to desktop resolution if not specified
    if (nativeW <= 0 || nativeH <= 0) {
        D3DDISPLAYMODE dm;
        if (SUCCEEDED(m_pReal->GetAdapterDisplayMode(A, &dm))) {
            nativeW = dm.Width;
            nativeH = dm.Height;
        } else {
            nativeW = requestedW;
            nativeH = requestedH;
        }
    }

    int scaledW = nativeW;
    int scaledH = nativeH;
    GetScaledResolution(scaledW, scaledH);

    D3DPRESENT_PARAMETERS realPP = *pPP;
    realPP.BackBufferWidth = nativeW;
    realPP.BackBufferHeight = nativeH;

    Logger::info("CreateDevice: Game requested {}x{}, Proxy creating device at {}x{}, Game will see {}x{}", requestedW, requestedH, nativeW,
        nativeH, scaledW, scaledH);

    LogPresentParameters("CreateDevice (Game input)", pPP);
    LogPresentParameters("CreateDevice (Real output)", &realPP);

    HRESULT hr = m_pReal->CreateDevice(A, DT, hFW, BF, &realPP, ppRDI);
    if (SUCCEEDED(hr) && ppRDI && *ppRDI) {
        pPP->BackBufferWidth = scaledW;
        pPP->BackBufferHeight = scaledH;
        *ppRDI = (IDirect3DDevice9*)new ProxyDirect3DDevice9(*ppRDI, (IDirect3D9*)this, hFW, scaledW, scaledH, nativeW, nativeH, true);
    }
    return hr;
}

// IDirect3D9Ex methods
STDMETHODIMP_(UINT)
ProxyDirect3D9::GetAdapterModeCountEx(UINT A, CONST D3DDISPLAYMODEFILTER* pF) {
    if (!m_pRealEx)
        return 0;
    UINT realCount = m_pRealEx->GetAdapterModeCountEx(A, pF);
    if (realCount > 0) {
        realCount += (UINT)Config::Get().GetExtraResolutions().size();
    }
    return realCount;
}

STDMETHODIMP ProxyDirect3D9::EnumAdapterModesEx(UINT A, CONST D3DDISPLAYMODEFILTER* pF, UINT M, D3DDISPLAYMODEEX* pMode) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    UINT realCount = m_pRealEx->GetAdapterModeCountEx(A, pF);
    if (M < realCount)
        return m_pRealEx->EnumAdapterModesEx(A, pF, M, pMode);

    if (realCount == 0)
        return D3DERR_INVALIDCALL;

    UINT extraIdx = M - realCount;
    const auto& extra = Config::Get().GetExtraResolutions();
    if (extraIdx < extra.size()) {
        if (pMode) {
            pMode->Size = sizeof(D3DDISPLAYMODEEX);
            pMode->Width = extra[extraIdx].width;
            pMode->Height = extra[extraIdx].height;
            pMode->RefreshRate = 60;
            pMode->Format = (pF ? pF->Format : D3DFMT_X8R8G8B8);
            pMode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
        }
        return S_OK;
    }
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP ProxyDirect3D9::GetAdapterDisplayModeEx(UINT A, D3DDISPLAYMODEEX* pM, D3DDISPLAYROTATION* pR) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->GetAdapterDisplayModeEx(A, pM, pR);
}

STDMETHODIMP ProxyDirect3D9::CreateDeviceEx(
    UINT A, D3DDEVTYPE DT, HWND hFW, DWORD BF, D3DPRESENT_PARAMETERS* pPP, D3DDISPLAYMODEEX* pFDM, IDirect3DDevice9Ex** ppRDI) {
    if (!pPP)
        return D3DERR_INVALIDCALL;
    OverlayRenderer::Get().SetWindow(hFW);

    // Load the upscaler plugin early so GetScaledResolution can check its status correctly
    if (!Config::Get().GetBool("VKUpscaler", true)) {
        UpscalerManager::Get().LoadUpscaler();
    }

    int requestedW = pPP->BackBufferWidth;
    int requestedH = pPP->BackBufferHeight;

    int nativeW = Config::Get().GetTargetWidth();
    int nativeH = Config::Get().GetTargetHeight();

    // Fallback to desktop resolution if not specified
    if (nativeW <= 0 || nativeH <= 0) {
        D3DDISPLAYMODEEX mode;
        mode.Size = sizeof(D3DDISPLAYMODEEX);
        if (m_pRealEx && SUCCEEDED(m_pRealEx->GetAdapterDisplayModeEx(A, &mode, nullptr))) {
            nativeW = mode.Width;
            nativeH = mode.Height;
        } else {
            nativeW = requestedW;
            nativeH = requestedH;
        }
    }

    int scaledW = nativeW;
    int scaledH = nativeH;
    GetScaledResolution(scaledW, scaledH);

    D3DPRESENT_PARAMETERS realPP = *pPP;
    realPP.BackBufferWidth = nativeW;
    realPP.BackBufferHeight = nativeH;

    if (!m_pRealEx)
        return E_NOTIMPL;

    LogPresentParameters("CreateDeviceEx (Game input)", pPP);
    LogPresentParameters("CreateDeviceEx (Real output)", &realPP);

    HRESULT hr = m_pRealEx->CreateDeviceEx(A, DT, hFW, BF, &realPP, pFDM, ppRDI);
    if (SUCCEEDED(hr) && ppRDI && *ppRDI) {
        pPP->BackBufferWidth = scaledW;
        pPP->BackBufferHeight = scaledH;
        *ppRDI = new ProxyDirect3DDevice9(*ppRDI, (IDirect3D9*)this, hFW, scaledW, scaledH, nativeW, nativeH, true);
    }
    return hr;
}

STDMETHODIMP ProxyDirect3D9::GetAdapterLUID(UINT A, LUID* pL) {
    if (!m_pRealEx)
        return E_NOTIMPL;
    return m_pRealEx->GetAdapterLUID(A, pL);
}
