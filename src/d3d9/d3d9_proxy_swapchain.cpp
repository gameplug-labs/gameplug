#include "d3d9_proxy_swapchain.h"
#include "d3d9_proxy_device.h"
#include "d3d9_proxy_surface.h"

ProxyDirect3DSwapChain9::ProxyDirect3DSwapChain9(IDirect3DSwapChain9* pReal, IDirect3DDevice9* pProxyDevice, UINT swapChainIndex)
    : m_pReal(pReal)
    , m_pProxyDevice(pProxyDevice)
    , m_swapChainIndex(swapChainIndex) {
}

STDMETHODIMP ProxyDirect3DSwapChain9::QueryInterface(REFIID riid, void** ppvObj) {
    if (riid == IID_IDirect3DSwapChain9 || riid == IID_IUnknown) {
        *ppvObj = (IDirect3DSwapChain9*)this;
        AddRef();
        return S_OK;
    }
    return m_pReal->QueryInterface(riid, ppvObj);
}

STDMETHODIMP_(ULONG) ProxyDirect3DSwapChain9::AddRef() {
    return m_pReal->AddRef();
}

STDMETHODIMP_(ULONG) ProxyDirect3DSwapChain9::Release() {
    ULONG res = m_pReal->Release();
    if (res == 0)
        delete this;
    return res;
}

STDMETHODIMP ProxyDirect3DSwapChain9::Present(
    CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags) {
    return m_pReal->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

STDMETHODIMP ProxyDirect3DSwapChain9::GetFrontBufferData(IDirect3DSurface9* pDestSurface) {
    return m_pReal->GetFrontBufferData(pDestSurface);
}

STDMETHODIMP ProxyDirect3DSwapChain9::GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) {
    if (!ppBackBuffer)
        return D3DERR_INVALIDCALL;

    if (m_pProxyDevice) {
        ProxyDirect3DDevice9* pProxyDevice = static_cast<ProxyDirect3DDevice9*>(m_pProxyDevice);

        if (pProxyDevice->IsUpscaling() && pProxyDevice->GetFakeBackBuffer() && iBackBuffer == 0) {
            *ppBackBuffer = pProxyDevice->GetFakeBackBuffer();
            (*ppBackBuffer)->AddRef();
            return S_OK;
        }

        return pProxyDevice->GetBackBuffer(m_swapChainIndex, iBackBuffer, Type, ppBackBuffer);
    }

    return m_pReal->GetBackBuffer(iBackBuffer, Type, ppBackBuffer);
}

STDMETHODIMP ProxyDirect3DSwapChain9::GetRasterStatus(D3DRASTER_STATUS* pRasterStatus) {
    return m_pReal->GetRasterStatus(pRasterStatus);
}

STDMETHODIMP ProxyDirect3DSwapChain9::GetDisplayMode(D3DDISPLAYMODE* pMode) {
    return m_pReal->GetDisplayMode(pMode);
}

STDMETHODIMP ProxyDirect3DSwapChain9::GetDevice(IDirect3DDevice9** ppDevice) {
    if (ppDevice) {
        *ppDevice = m_pProxyDevice;
        m_pProxyDevice->AddRef();
        return S_OK;
    }
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP ProxyDirect3DSwapChain9::GetPresentParameters(D3DPRESENT_PARAMETERS* pPresentationParameters) {
    return m_pReal->GetPresentParameters(pPresentationParameters);
}
