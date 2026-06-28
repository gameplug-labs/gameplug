#pragma once

#include "d3d9_proxy.h"

class ProxyTexture9;

// --- Proxy Surface ---
class ProxySurface9 : public IDirect3DSurface9 {
    IDirect3DSurface9* m_pReal;
    IDirect3DDevice9* m_pDevice;
    ProxyTexture9* m_pParentTexture;
    UINT m_level;
    uint32_t m_virtualW;
    uint32_t m_virtualH;
    ULONG m_refCount;

public:
    ProxySurface9(IDirect3DSurface9* pReal, IDirect3DDevice9* pDev, uint32_t vw, uint32_t vh, ProxyTexture9* pParentTexture = nullptr,
        UINT level = 0);
    virtual ~ProxySurface9();

    void SetInternalSurface(IDirect3DSurface9* pNew);
    IDirect3DSurface9* GetInternalSurface();
    ProxyTexture9* GetParentTexture() const { return m_pParentTexture; }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IDirect3DResource9
    STDMETHOD(GetDevice)(IDirect3DDevice9** ppD) override;
    STDMETHOD(SetPrivateData)(REFGUID r, const void* pD, DWORD dS, DWORD f) override;
    STDMETHOD(GetPrivateData)(REFGUID r, void* pD, DWORD* pS) override;
    STDMETHOD(FreePrivateData)(REFGUID r) override;
    STDMETHOD_(DWORD, SetPriority)(DWORD pN) override;
    STDMETHOD_(DWORD, GetPriority)() override;
    STDMETHOD_(void, PreLoad)() override;
    STDMETHOD_(D3DRESOURCETYPE, GetType)() override;

    // IDirect3DSurface9
    STDMETHOD(GetContainer)(REFIID r, void** ppC) override;
    STDMETHOD(GetDesc)(D3DSURFACE_DESC* pD) override;
    STDMETHOD(LockRect)(D3DLOCKED_RECT* pL, const RECT* pR, DWORD f) override;
    STDMETHOD(UnlockRect)() override;
    STDMETHOD(GetDC)(HDC* p) override;
    STDMETHOD(ReleaseDC)(HDC p) override;
};
