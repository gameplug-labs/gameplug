#include "d3d9_proxy_surface.h"
#include "d3d9_proxy_texture.h"
#include "texture_replacer.h"

ProxySurface9::ProxySurface9(
    IDirect3DSurface9* pReal, IDirect3DDevice9* pDev, uint32_t vw, uint32_t vh, ProxyTexture9* pParentTexture, UINT level)
    : m_pReal(pReal)
    , m_pDevice(pDev)
    , m_pParentTexture(pParentTexture)
    , m_level(level)
    , m_virtualW(vw)
    , m_virtualH(vh)
    , m_refCount(1) {
    if (m_pReal)
        m_pReal->AddRef();
    GamePlug::TextureReplacer::Get().OnSurfaceCreated(this);
}

ProxySurface9::~ProxySurface9() {
    GamePlug::TextureReplacer::Get().OnSurfaceDestroyed(this);
}

void ProxySurface9::SetInternalSurface(IDirect3DSurface9* pNew) {
    if (pNew)
        pNew->AddRef();
    if (m_pReal)
        m_pReal->Release();
    m_pReal = pNew;
}

IDirect3DSurface9* ProxySurface9::GetInternalSurface() {
    return m_pReal;
}

// IUnknown
STDMETHODIMP ProxySurface9::QueryInterface(REFIID riid, void** ppvObj) {
    if (riid == IID_IDirect3DSurface9 || riid == IID_IDirect3DResource9 || riid == IID_IUnknown) {
        *ppvObj = (IDirect3DSurface9*)this;
        AddRef();
        return S_OK;
    }
    return m_pReal->QueryInterface(riid, ppvObj);
}

STDMETHODIMP_(ULONG) ProxySurface9::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) ProxySurface9::Release() {
    ULONG res = InterlockedDecrement(&m_refCount);
    if (res == 0) {
        if (m_pReal)
            m_pReal->Release();
        delete this;
    }
    return res;
}

// IDirect3DResource9
STDMETHODIMP ProxySurface9::GetDevice(IDirect3DDevice9** ppD) {
    *ppD = m_pDevice;
    m_pDevice->AddRef();
    return S_OK;
}

STDMETHODIMP ProxySurface9::SetPrivateData(REFGUID r, const void* pD, DWORD dS, DWORD f) {
    return m_pReal->SetPrivateData(r, pD, dS, f);
}

STDMETHODIMP ProxySurface9::GetPrivateData(REFGUID r, void* pD, DWORD* pS) {
    return m_pReal->GetPrivateData(r, pD, pS);
}

STDMETHODIMP ProxySurface9::FreePrivateData(REFGUID r) {
    return m_pReal->FreePrivateData(r);
}

STDMETHODIMP_(DWORD) ProxySurface9::SetPriority(DWORD pN) {
    return m_pReal->SetPriority(pN);
}

STDMETHODIMP_(DWORD) ProxySurface9::GetPriority() {
    return m_pReal->GetPriority();
}

STDMETHODIMP_(void) ProxySurface9::PreLoad() {
    m_pReal->PreLoad();
}

STDMETHODIMP_(D3DRESOURCETYPE) ProxySurface9::GetType() {
    return m_pReal->GetType();
}

// IDirect3DSurface9
STDMETHODIMP ProxySurface9::GetContainer(REFIID r, void** ppC) {
    if (m_pParentTexture &&
        (r == IID_IDirect3DTexture9 || r == IID_IDirect3DBaseTexture9 || r == IID_IUnknown || r == IID_IDirect3DResource9)) {
        *ppC = m_pParentTexture;
        m_pParentTexture->AddRef();
        return S_OK;
    }
    return m_pReal->GetContainer(r, ppC);
}

STDMETHODIMP ProxySurface9::GetDesc(D3DSURFACE_DESC* pD) {
    HRESULT hr = m_pReal->GetDesc(pD);
    if (SUCCEEDED(hr)) {
        pD->Width = m_virtualW;
        pD->Height = m_virtualH;
    }
    return hr;
}

STDMETHODIMP ProxySurface9::LockRect(D3DLOCKED_RECT* pL, const RECT* pR, DWORD f) {
    HRESULT hr = m_pReal->LockRect(pL, pR, f);
    if (SUCCEEDED(hr) && pL && m_pParentTexture) {
        m_pParentTexture->RecordLock(m_level, *pL, f, pR);
    }
    return hr;
}

STDMETHODIMP ProxySurface9::UnlockRect() {
    if (m_pParentTexture && m_level == 0) {
        m_pParentTexture->OnLevelUnlocked(0);
    }
    return m_pReal->UnlockRect();
}

STDMETHODIMP ProxySurface9::GetDC(HDC* p) {
    return m_pReal->GetDC(p);
}

STDMETHODIMP ProxySurface9::ReleaseDC(HDC p) {
    return m_pReal->ReleaseDC(p);
}
