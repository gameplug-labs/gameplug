#pragma once

#include "d3d9_proxy.h"

// --- Proxy SwapChain ---
class ProxyDirect3DSwapChain9 : public IDirect3DSwapChain9 {
    IDirect3DSwapChain9* m_pReal;
    IDirect3DDevice9* m_pProxyDevice;
    UINT m_swapChainIndex;

public:
    ProxyDirect3DSwapChain9(IDirect3DSwapChain9* pReal, IDirect3DDevice9* pProxyDevice, UINT swapChainIndex);

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(Present)(
        CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags) override;
    STDMETHOD(GetFrontBufferData)(IDirect3DSurface9* pDestSurface) override;
    STDMETHOD(GetBackBuffer)(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override;
    STDMETHOD(GetRasterStatus)(D3DRASTER_STATUS* pRasterStatus) override;
    STDMETHOD(GetDisplayMode)(D3DDISPLAYMODE* pMode) override;
    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) override;
    STDMETHOD(GetPresentParameters)(D3DPRESENT_PARAMETERS* pPresentationParameters) override;
};
