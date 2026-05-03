#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <string>
#include <cstring>
#include <stdarg.h>

#include "logger.h"
#include "config.h"
#include "upscaler_manager.h"
#include "dx_overlay.h"
#include "MinHook.h"

using namespace GamePlug;

// Forward declarations
class ProxyDirect3D9;
class ProxyDirect3DDevice9;
class ProxyDirect3DSwapChain9;
class ProxySurface9;

// --- Proxy Surface ---
class ProxySurface9 : public IDirect3DSurface9 {
    IDirect3DSurface9* m_pReal;
    IDirect3DDevice9* m_pDevice;
    uint32_t m_virtualW;
    uint32_t m_virtualH;
    ULONG m_refCount;

public:
    ProxySurface9(IDirect3DSurface9* pReal, IDirect3DDevice9* pDev, uint32_t vw, uint32_t vh) 
        : m_pReal(pReal), m_pDevice(pDev), m_virtualW(vw), m_virtualH(vh), m_refCount(1) {
        if (m_pReal) m_pReal->AddRef();
    }

    void SetInternalSurface(IDirect3DSurface9* pNew) {
        if (pNew) pNew->AddRef();
        if (m_pReal) m_pReal->Release();
        m_pReal = pNew;
    }

    IDirect3DSurface9* GetInternalSurface() { return m_pReal; }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) override {
        if (riid == IID_IDirect3DSurface9 || riid == IID_IDirect3DResource9 || riid == IID_IUnknown) {
            *ppvObj = (IDirect3DSurface9*)this;
            AddRef();
            return S_OK;
        }
        return m_pReal->QueryInterface(riid, ppvObj);
    }
    STDMETHOD_(ULONG, AddRef)() override {
        return InterlockedIncrement(&m_refCount);
    }
    STDMETHOD_(ULONG, Release)() override {
        ULONG res = InterlockedDecrement(&m_refCount);
        if (res == 0) {
            if (m_pReal) m_pReal->Release();
            delete this;
        }
        return res;
    }

    // IDirect3DResource9
    STDMETHOD(GetDevice)(IDirect3DDevice9** ppD) override { *ppD = m_pDevice; m_pDevice->AddRef(); return S_OK; }
    STDMETHOD(SetPrivateData)(REFGUID r, const void* pD, DWORD dS, DWORD f) override { return m_pReal->SetPrivateData(r, pD, dS, f); }
    STDMETHOD(GetPrivateData)(REFGUID r, void* pD, DWORD* pS) override { return m_pReal->GetPrivateData(r, pD, pS); }
    STDMETHOD(FreePrivateData)(REFGUID r) override { return m_pReal->FreePrivateData(r); }
    STDMETHOD_(DWORD, SetPriority)(DWORD pN) override { return m_pReal->SetPriority(pN); }
    STDMETHOD_(DWORD, GetPriority)() override { return m_pReal->GetPriority(); }
    STDMETHOD_(void, PreLoad)() override { m_pReal->PreLoad(); }
    STDMETHOD_(D3DRESOURCETYPE, GetType)() override { return m_pReal->GetType(); }

    // IDirect3DSurface9
    STDMETHOD(GetContainer)(REFIID r, void** ppC) override { return m_pReal->GetContainer(r, ppC); }
    STDMETHOD(GetDesc)(D3DSURFACE_DESC* pD) override { 
        HRESULT hr = m_pReal->GetDesc(pD);
        if (SUCCEEDED(hr)) {
            pD->Width = m_virtualW;
            pD->Height = m_virtualH;
        }
        return hr;
    }
    STDMETHOD(LockRect)(D3DLOCKED_RECT* pL, const RECT* pR, DWORD f) override { return m_pReal->LockRect(pL, pR, f); }
    STDMETHOD(UnlockRect)() override { return m_pReal->UnlockRect(); }
    STDMETHOD(GetDC)(HDC* p) override { return m_pReal->GetDC(p); }
    STDMETHOD(ReleaseDC)(HDC p) override { return m_pReal->ReleaseDC(p); }
};

// Global pointer to the real D3D9 DLL
HMODULE g_hRealDll = NULL;

typedef IDirect3D9* (WINAPI* PFN_Direct3DCreate9)(UINT SDKVersion);
typedef HRESULT (WINAPI* PFN_Direct3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex** ppDirect3D9Ex);

PFN_Direct3DCreate9 Real_Direct3DCreate9 = NULL;
PFN_Direct3DCreate9Ex Real_Direct3DCreate9Ex = NULL;

typedef int (WINAPI* PFN_D3DPERF_BeginEvent)(D3DCOLOR col, LPCWSTR wszName);
typedef int (WINAPI* PFN_D3DPERF_EndEvent)(void);
typedef DWORD (WINAPI* PFN_D3DPERF_GetStatus)(void);
typedef BOOL (WINAPI* PFN_D3DPERF_QueryRepeatFrame)(void);
typedef void (WINAPI* PFN_D3DPERF_SetMarker)(D3DCOLOR col, LPCWSTR wszName);
typedef void (WINAPI* PFN_D3DPERF_SetOptions)(DWORD dwOptions);
typedef void (WINAPI* PFN_D3DPERF_SetRegion)(D3DCOLOR col, LPCWSTR wszName);

PFN_D3DPERF_BeginEvent Real_D3DPERF_BeginEvent = NULL;
PFN_D3DPERF_EndEvent Real_D3DPERF_EndEvent = NULL;
PFN_D3DPERF_GetStatus Real_D3DPERF_GetStatus = NULL;
PFN_D3DPERF_QueryRepeatFrame Real_D3DPERF_QueryRepeatFrame = NULL;
PFN_D3DPERF_SetMarker Real_D3DPERF_SetMarker = NULL;
PFN_D3DPERF_SetOptions Real_D3DPERF_SetOptions = NULL;
PFN_D3DPERF_SetRegion Real_D3DPERF_SetRegion = NULL;

// --- DirectInput8 Forwarding ---
typedef HRESULT (WINAPI* PFN_DirectInput8Create)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter);
PFN_DirectInput8Create Real_DirectInput8Create = NULL;
HMODULE g_hRealDInput8 = NULL;

void EnsureRealDInput8Loaded() {
    if (g_hRealDInput8) return;
    char sysPath[MAX_PATH];
    GetSystemDirectoryA(sysPath, MAX_PATH); strcat(sysPath, "\\dinput8.dll");
    g_hRealDInput8 = LoadLibraryA(sysPath);
    if (g_hRealDInput8) {
        Real_DirectInput8Create = (PFN_DirectInput8Create)GetProcAddress(g_hRealDInput8, "DirectInput8Create");
        Logger::info("Loaded system dinput8.dll for forwarding.");
    }
}

// Global flag to bypass scaling for upscaler passes
bool g_InUpscalerPass = false;

// --- Helper Functions ---
void GetScaledResolution(int& outW, int& outH) {
    int gameW = outW;
    int gameH = outH;
    try {
        Config& cfg = Config::Get();
        // Poll current target from config (refreshed by overlay UI)
        int targetWidth = cfg.GetTargetWidth();
        int targetHeight = cfg.GetTargetHeight();
        
        if (targetWidth <= 0 || targetHeight <= 0) {
            targetWidth = gameW;
            targetHeight = gameH;
        }
        
        if (targetWidth > 0 && targetHeight > 0) {
            UpscalerManager& mgr = UpscalerManager::Get();
            bool nativeRendering = mgr.IsNativeRenderingEnabled();
            int quality = mgr.GetUpscaleQuality();
            bool upscaling = mgr.IsUpscalingEnabled();
            
            static int lastQ = -1;
            static int lastN = -1;
            static int lastU = -1;
            if (quality != lastQ || (int)nativeRendering != lastN || (int)upscaling != lastU) {
                Logger::info("GetScaledResolution: mgr={:p}, quality={}, native={}, upscaling={}", (void*)&mgr, quality, nativeRendering, upscaling);
                lastQ = quality; lastN = (int)nativeRendering; lastU = (int)upscaling;
            }

            if (!nativeRendering && upscaling) {
                float scale = 1.0f;
                switch (quality) {
                    case 0: scale = 1.2f; break; // Ultra Ultra Quality
                    case 1: scale = 1.3f; break; // Ultra Quality
                    case 2: scale = 1.5f; break; // Quality
                    case 3: scale = 1.7f; break; // Balanced
                    case 4: scale = 2.0f; break; // Performance
                    case 5: scale = 3.0f; break; // Ultra Performance
                    default: scale = 1.3f; break;
                }
                
                outW = (int)(targetWidth / scale + 0.5f);
                outH = (int)(targetHeight / scale + 0.5f);
            } else {
                outW = targetWidth;
                outH = targetHeight;
            }
        }
    } catch (...) {}
}

// --- Proxy SwapChain ---
class ProxyDirect3DSwapChain9 : public IDirect3DSwapChain9 {
    IDirect3DSwapChain9* m_pReal;
    IDirect3DDevice9* m_pProxyDevice;
public:
    ProxyDirect3DSwapChain9(IDirect3DSwapChain9* pReal, IDirect3DDevice9* pProxyDevice) 
        : m_pReal(pReal), m_pProxyDevice(pProxyDevice) {}
    
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) override {
        if (riid == IID_IDirect3DSwapChain9 || riid == IID_IUnknown) {
            *ppvObj = (IDirect3DSwapChain9*)this; AddRef(); return S_OK;
        }
        return m_pReal->QueryInterface(riid, ppvObj);
    }
    STDMETHOD_(ULONG, AddRef)() override { return m_pReal->AddRef(); }
    STDMETHOD_(ULONG, Release)() override {
        ULONG res = m_pReal->Release();
        if (res == 0) delete this;
        return res;
    }
    STDMETHOD(Present)(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags) override {
        return m_pReal->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
    }
    STDMETHOD(GetFrontBufferData)(IDirect3DSurface9* pDestSurface) override { return m_pReal->GetFrontBufferData(pDestSurface); }
    STDMETHOD(GetBackBuffer)(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBackBuffer) override { return m_pReal->GetBackBuffer(iBackBuffer, Type, ppBackBuffer); }
    STDMETHOD(GetRasterStatus)(D3DRASTER_STATUS* pRasterStatus) override { return m_pReal->GetRasterStatus(pRasterStatus); }
    STDMETHOD(GetDisplayMode)(D3DDISPLAYMODE* pMode) override { return m_pReal->GetDisplayMode(pMode); }
    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) override {
        if (ppDevice) {
            *ppDevice = m_pProxyDevice;
            m_pProxyDevice->AddRef();
            return S_OK;
        }
        return D3DERR_INVALIDCALL;
    }
    STDMETHOD(GetPresentParameters)(D3DPRESENT_PARAMETERS* pPresentationParameters) override { return m_pReal->GetPresentParameters(pPresentationParameters); }
};

// --- Combined Proxy Device (Supports Ex) ---
class ProxyDirect3DDevice9 : public IDirect3DDevice9Ex {
    IDirect3DDevice9* m_pReal;
    IDirect3DDevice9Ex* m_pRealEx;
    IDirect3D9* m_pParent;
    ProxySurface9* m_pFakeBackBuffer;
    IDirect3DTexture9* m_pFakeBackBufferTex;
    uint32_t m_renderW, m_renderH;
    uint32_t m_displayW, m_displayH;
    bool m_isUpscaling;
    void UpdateScaledResolution() {
        int sw = m_displayW;
        int sh = m_displayH;
        GetScaledResolution(sw, sh);
        if (sw != (int)m_renderW || sh != (int)m_renderH) {
            Logger::info("Proxy: Render resolution changed real-time: {}x{} -> {}x{}", m_renderW, m_renderH, sw, sh);
            m_renderW = (uint32_t)sw;
            m_renderH = (uint32_t)sh;

            // Re-create fake backbuffer for performance gain (better cache locality)
            if (m_isUpscaling) {
                if (m_pFakeBackBufferTex) { m_pFakeBackBufferTex->Release(); m_pFakeBackBufferTex = nullptr; }
                
                if (SUCCEEDED(m_pReal->CreateTexture(m_renderW, m_renderH, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_pFakeBackBufferTex, nullptr))) {
                    IDirect3DSurface9* pRealSurf = nullptr;
                    m_pFakeBackBufferTex->GetSurfaceLevel(0, &pRealSurf);
                    
                    if (!m_pFakeBackBuffer) {
                        m_pFakeBackBuffer = new ProxySurface9(pRealSurf, this, m_displayW, m_displayH);
                    } else {
                        m_pFakeBackBuffer->SetInternalSurface(pRealSurf);
                    }
                    
                    if (pRealSurf) pRealSurf->Release(); // Proxy holds a ref
                    Logger::info("Proxy: Fake backbuffer re-created at {}x{}", m_renderW, m_renderH);
                }
            }
        }
    }

public:
    ProxyDirect3DDevice9(IDirect3DDevice9* pReal, IDirect3D9* pParent, uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh, bool upscale) 
        : m_pReal(pReal), m_pParent(pParent), m_renderW(rw), m_renderH(rh), m_displayW(dw), m_displayH(dh), m_isUpscaling(upscale), m_pFakeBackBuffer(nullptr), m_pFakeBackBufferTex(nullptr) {
        
        m_pReal->QueryInterface(IID_IDirect3DDevice9Ex, (void**)&m_pRealEx);

        Logger::info("ProxyDirect3DDevice9: Created. Render: {}x{}, Display: {}x{}, Upscale: {}, RealEx: {}", m_renderW, m_renderH, m_displayW, m_displayH, m_isUpscaling, (void*)m_pRealEx);
        
        OverlayRenderer::Get().Init((IDirect3DDevice9*)this);

        if (m_isUpscaling) {
            if (UpscalerManager::Get().LoadUpscaler()) {
                UpscalerManager::Get().InitUpscaler((void*)m_pReal);
                if (SUCCEEDED(m_pReal->CreateTexture(m_renderW, m_renderH, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_pFakeBackBufferTex, nullptr))) {
                    IDirect3DSurface9* pRealSurf = nullptr;
                    m_pFakeBackBufferTex->GetSurfaceLevel(0, &pRealSurf);
                    m_pFakeBackBuffer = new ProxySurface9(pRealSurf, this, m_displayW, m_displayH);
                    if (pRealSurf) pRealSurf->Release();
                    Logger::info("Proxy: Fake backbuffer created at native {}x{}", m_renderW, m_renderH);
                }
            }
        }
    }

    virtual ~ProxyDirect3DDevice9() {
        if (m_pFakeBackBuffer) m_pFakeBackBuffer->Release();
        if (m_pFakeBackBufferTex) m_pFakeBackBufferTex->Release();
        if (m_pRealEx) m_pRealEx->Release();
    }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) override {
        if (riid == IID_IDirect3DDevice9 || riid == IID_IUnknown) {
            *ppvObj = (IDirect3DDevice9*)this; AddRef(); return S_OK;
        }
        if (riid == IID_IDirect3DDevice9Ex) {
            if (m_pRealEx) {
                *ppvObj = (IDirect3DDevice9Ex*)this; AddRef(); return S_OK;
            }
            return E_NOINTERFACE;
        }
        return m_pReal->QueryInterface(riid, ppvObj);
    }
    STDMETHOD_(ULONG, AddRef)() override { return m_pReal->AddRef(); }
    STDMETHOD_(ULONG, Release)() override {
        ULONG res = m_pReal->Release();
        if (res == 0) delete this;
        return res;
    }

    // IDirect3DDevice9 methods
    STDMETHOD(TestCooperativeLevel)() override { return m_pReal->TestCooperativeLevel(); }
    STDMETHOD_(UINT, GetAvailableTextureMem)() override { return m_pReal->GetAvailableTextureMem(); }
    STDMETHOD(EvictManagedResources)() override { return m_pReal->EvictManagedResources(); }
    STDMETHOD(GetDirect3D)(IDirect3D9** ppD3D9) override { 
        if (ppD3D9) { *ppD3D9 = m_pParent; m_pParent->AddRef(); return S_OK; }
        return D3DERR_INVALIDCALL;
    }
    STDMETHOD(GetDeviceCaps)(D3DCAPS9* pCaps) override { return m_pReal->GetDeviceCaps(pCaps); }
    STDMETHOD(GetDisplayMode)(UINT iSwapChain, D3DDISPLAYMODE* pMode) override { return m_pReal->GetDisplayMode(iSwapChain, pMode); }
    STDMETHOD(GetCreationParameters)(D3DDEVICE_CREATION_PARAMETERS* pParameters) override { return m_pReal->GetCreationParameters(pParameters); }
    STDMETHOD(SetCursorProperties)(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override { return m_pReal->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap); }
    STDMETHOD_(void, SetCursorPosition)(int X, int Y, DWORD Flags) override { m_pReal->SetCursorPosition(X, Y, Flags); }
    STDMETHOD_(BOOL, ShowCursor)(BOOL bShow) override { return m_pReal->ShowCursor(bShow); }
    STDMETHOD(CreateAdditionalSwapChain)(D3DPRESENT_PARAMETERS* pPP, IDirect3DSwapChain9** ppSC) override { 
        HRESULT hr = m_pReal->CreateAdditionalSwapChain(pPP, ppSC);
        if (SUCCEEDED(hr) && ppSC && *ppSC) *ppSC = new ProxyDirect3DSwapChain9(*ppSC, (IDirect3DDevice9*)this);
        return hr;
    }
    STDMETHOD(GetSwapChain)(UINT iSC, IDirect3DSwapChain9** ppSC) override { 
        HRESULT hr = m_pReal->GetSwapChain(iSC, ppSC);
        if (SUCCEEDED(hr) && ppSC && *ppSC) *ppSC = new ProxyDirect3DSwapChain9(*ppSC, (IDirect3DDevice9*)this);
        return hr;
    }
    STDMETHOD_(UINT, GetNumberOfSwapChains)() override { return m_pReal->GetNumberOfSwapChains(); }
    STDMETHOD(Reset)(D3DPRESENT_PARAMETERS* pPP) override {
        OverlayRenderer::Get().OnReset();
        UpscalerManager::Get().OnReset();
        if (m_pFakeBackBuffer) { m_pFakeBackBuffer->Release(); m_pFakeBackBuffer = nullptr; }
        if (m_pFakeBackBufferTex) { m_pFakeBackBufferTex->Release(); m_pFakeBackBufferTex = nullptr; }
        
        int scaledW = pPP->BackBufferWidth;
        int scaledH = pPP->BackBufferHeight;
        GetScaledResolution(scaledW, scaledH);
        
        int nativeW = Config::Get().GetTargetWidth();
        int nativeH = Config::Get().GetTargetHeight();
        if (nativeW <= 0) nativeW = pPP->BackBufferWidth;
        if (nativeH <= 0) nativeH = pPP->BackBufferHeight;
        
        D3DPRESENT_PARAMETERS realPP = *pPP;
        realPP.BackBufferWidth = nativeW;
        realPP.BackBufferHeight = nativeH;
        
        m_renderW = scaledW; 
        m_renderH = scaledH;
        m_displayW = nativeW; 
        m_displayH = nativeH;
        m_isUpscaling = true; // Force enabled even if resolution matches
        
        Logger::info("Reset: Game requested {}x{}, Proxy created device at {}x{}, Game sees {}x{}", pPP->BackBufferWidth, pPP->BackBufferHeight, nativeW, nativeH, scaledW, scaledH);
        
        HRESULT hr = m_pReal->Reset(&realPP);
        if (SUCCEEDED(hr)) {
            pPP->BackBufferWidth = scaledW;
            pPP->BackBufferHeight = scaledH;
            if (m_isUpscaling) {
                if (m_pFakeBackBufferTex) { m_pFakeBackBufferTex->Release(); m_pFakeBackBufferTex = nullptr; }
                if (SUCCEEDED(m_pReal->CreateTexture(m_renderW, m_renderH, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_pFakeBackBufferTex, nullptr))) {
                    IDirect3DSurface9* pRealSurf = nullptr;
                    m_pFakeBackBufferTex->GetSurfaceLevel(0, &pRealSurf);
                    if (!m_pFakeBackBuffer) {
                        m_pFakeBackBuffer = new ProxySurface9(pRealSurf, this, m_displayW, m_displayH);
                    } else {
                        m_pFakeBackBuffer->SetInternalSurface(pRealSurf);
                    }
                    if (pRealSurf) pRealSurf->Release();
                    Logger::info("Proxy: Fake backbuffer re-created at {}x{} (Reset path)", m_renderW, m_renderH);
                }
            }
            OverlayRenderer::Get().OnPostReset();
        }
        return hr;
    }
    STDMETHOD(Present)(CONST RECT* pSR, CONST RECT* pDR, HWND hW, CONST RGNDATA* pR) override { 
        UpdateScaledResolution();
        OverlayRenderer::Get().NewFrame();
        IDirect3DSurface9* pRBB = nullptr;
        if (SUCCEEDED(m_pReal->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pRBB))) {
            if (m_isUpscaling && m_pFakeBackBuffer) {
                bool upscalerHandled = false;
                if (UpscalerManager::Get().IsUpscalingEnabled()) {
                    g_InUpscalerPass = true;
                    UpscalerManager::Get().RenderFrame((void*)m_pReal, (void*)m_pFakeBackBuffer->GetInternalSurface(), (void*)pRBB, m_displayW, m_displayH, m_renderW, m_renderH);
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
            if (pOldRT) pOldRT->Release();
            pRBB->Release();
        } else {
            OverlayRenderer::Get().Render(m_pReal, m_displayW, m_displayH);
        }
        // Force NULL rects to ensure the full upscaled 1080p buffer is presented
        return m_pReal->Present(NULL, NULL, hW, pR); 
    }
    STDMETHOD(GetBackBuffer)(UINT iSC, UINT iBB, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBB) override { 
        if (m_isUpscaling && m_pFakeBackBuffer && iSC == 0 && iBB == 0) { *ppBB = m_pFakeBackBuffer; m_pFakeBackBuffer->AddRef(); return S_OK; }
        return m_pReal->GetBackBuffer(iSC, iBB, Type, ppBB); 
    }
    STDMETHOD(GetRasterStatus)(UINT iSC, D3DRASTER_STATUS* pRS) override { return m_pReal->GetRasterStatus(iSC, pRS); }
    STDMETHOD(SetDialogBoxMode)(BOOL bE) override { return m_pReal->SetDialogBoxMode(bE); }
    STDMETHOD_(void, SetGammaRamp)(UINT iSC, DWORD F, CONST D3DGAMMARAMP* pR) override { m_pReal->SetGammaRamp(iSC, F, pR); }
    STDMETHOD_(void, GetGammaRamp)(UINT iSC, D3DGAMMARAMP* pR) override { m_pReal->GetGammaRamp(iSC, pR); }
    STDMETHOD(CreateTexture)(UINT W, UINT H, UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DTexture9** ppT, HANDLE* pS) override { return m_pReal->CreateTexture(W, H, L, U, F, P, ppT, pS); }
    STDMETHOD(CreateVolumeTexture)(UINT W, UINT H, UINT D, UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DVolumeTexture9** ppVT, HANDLE* pS) override { return m_pReal->CreateVolumeTexture(W, H, D, L, U, F, P, ppVT, pS); }
    STDMETHOD(CreateCubeTexture)(UINT E, UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DCubeTexture9** ppCT, HANDLE* pS) override { return m_pReal->CreateCubeTexture(E, L, U, F, P, ppCT, pS); }
    STDMETHOD(CreateVertexBuffer)(UINT L, DWORD U, DWORD F, D3DPOOL P, IDirect3DVertexBuffer9** ppVB, HANDLE* pS) override { return m_pReal->CreateVertexBuffer(L, U, F, P, ppVB, pS); }
    STDMETHOD(CreateIndexBuffer)(UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DIndexBuffer9** ppIB, HANDLE* pS) override { return m_pReal->CreateIndexBuffer(L, U, F, P, ppIB, pS); }
    STDMETHOD(CreateRenderTarget)(UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL L, IDirect3DSurface9** ppS, HANDLE* pS2) override { return m_pReal->CreateRenderTarget(W, H, F, M, MQ, L, ppS, pS2); }
    STDMETHOD(CreateDepthStencilSurface)(UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL D, IDirect3DSurface9** ppS, HANDLE* pS2) override { return m_pReal->CreateDepthStencilSurface(W, H, F, M, MQ, D, ppS, pS2); }
    STDMETHOD(UpdateSurface)(IDirect3DSurface9* pSS, CONST RECT* pSR, IDirect3DSurface9* pDS, CONST POINT* pDP) override { return m_pReal->UpdateSurface(pSS, pSR, pDS, pDP); }
    STDMETHOD(UpdateTexture)(IDirect3DBaseTexture9* pST, IDirect3DBaseTexture9* pDT) override { return m_pReal->UpdateTexture(pST, pDT); }
    STDMETHOD(GetRenderTargetData)(IDirect3DSurface9* pRT, IDirect3DSurface9* pDS) override { 
        IDirect3DSurface9* pActualRT = (pRT == m_pFakeBackBuffer) ? m_pFakeBackBuffer->GetInternalSurface() : pRT;
        return m_pReal->GetRenderTargetData(pActualRT, pDS); 
    }
    STDMETHOD(GetFrontBufferData)(UINT iSC, IDirect3DSurface9* pDS) override { return m_pReal->GetFrontBufferData(iSC, pDS); }
    STDMETHOD(StretchRect)(IDirect3DSurface9* pSS, CONST RECT* pSR, IDirect3DSurface9* pDS, CONST RECT* pDR, D3DTEXTUREFILTERTYPE F) override { 
        IDirect3DSurface9* pActualSS = (pSS == m_pFakeBackBuffer) ? m_pFakeBackBuffer->GetInternalSurface() : pSS;
        IDirect3DSurface9* pActualDS = (pDS == m_pFakeBackBuffer) ? m_pFakeBackBuffer->GetInternalSurface() : pDS;
        return m_pReal->StretchRect(pActualSS, pSR, pActualDS, pDR, F); 
    }
    STDMETHOD(ColorFill)(IDirect3DSurface9* pS, CONST RECT* pR, D3DCOLOR c) override { 
        IDirect3DSurface9* pActualS = (pS == m_pFakeBackBuffer) ? m_pFakeBackBuffer->GetInternalSurface() : pS;
        return m_pReal->ColorFill(pActualS, pR, c); 
    }
    STDMETHOD(CreateOffscreenPlainSurface)(UINT W, UINT H, D3DFORMAT F, D3DPOOL P, IDirect3DSurface9** ppS, HANDLE* pS2) override { return m_pReal->CreateOffscreenPlainSurface(W, H, F, P, ppS, pS2); }
    STDMETHOD(SetRenderTarget)(DWORD RTI, IDirect3DSurface9* pRT) override { 
        IDirect3DSurface9* pActual = (pRT == m_pFakeBackBuffer) ? m_pFakeBackBuffer->GetInternalSurface() : pRT;
        return m_pReal->SetRenderTarget(RTI, pActual); 
    }
    STDMETHOD(GetRenderTarget)(DWORD RTI, IDirect3DSurface9** ppRT) override { 
        HRESULT hr = m_pReal->GetRenderTarget(RTI, ppRT);
        if (SUCCEEDED(hr) && ppRT && *ppRT) {
            if (m_pFakeBackBuffer && *ppRT == m_pFakeBackBuffer->GetInternalSurface()) {
                (*ppRT)->Release();
                *ppRT = m_pFakeBackBuffer;
                (*ppRT)->AddRef();
            }
        }
        return hr;
    }
    STDMETHOD(SetDepthStencilSurface)(IDirect3DSurface9* pNZS) override { return m_pReal->SetDepthStencilSurface(pNZS); }
    STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9** ppZSS) override { return m_pReal->GetDepthStencilSurface(ppZSS); }
    STDMETHOD(BeginScene)() override { return m_pReal->BeginScene(); }
    STDMETHOD(EndScene)() override { return m_pReal->EndScene(); }
    STDMETHOD(Clear)(DWORD C, CONST D3DRECT* pR, DWORD F, D3DCOLOR C2, float Z, DWORD S) override { return m_pReal->Clear(C, pR, F, C2, Z, S); }
    STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE S, CONST D3DMATRIX* pM) override { return m_pReal->SetTransform(S, pM); }
    STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE S, D3DMATRIX* pM) override { return m_pReal->GetTransform(S, pM); }
    STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE S, CONST D3DMATRIX* pM) override { return m_pReal->MultiplyTransform(S, pM); }
    STDMETHOD(SetViewport)(CONST D3DVIEWPORT9* pV) override { 
        if (!pV) return D3DERR_INVALIDCALL;
        UpdateScaledResolution();
        
        // Check if we are rendering to our fake backbuffer or a full-screen surface
        IDirect3DSurface9* pRT = nullptr;
        m_pReal->GetRenderTarget(0, &pRT);
        D3DSURFACE_DESC rtDesc = {};
        if (pRT) pRT->GetDesc(&rtDesc);
        
        bool isFakeBB = (pRT && (pRT == m_pFakeBackBuffer || (m_pFakeBackBuffer && pRT == m_pFakeBackBuffer->GetInternalSurface())));
        bool isFullScreenRT = (rtDesc.Width == m_displayW && rtDesc.Height == m_displayH);
        if (pRT) pRT->Release();

        // Only scale if we are rendering to the fake backbuffer.
        // Internal RTs (like 512x256) or FullScreen RTs (1080p) should NOT be scaled
        // to avoid double scaling or misalignment.
        bool shouldScale = isFakeBB;

        static int logCount = 0;
        if (logCount++ % 100 == 0) {
            Logger::info("SetViewport: pV={}x{}, RT={}x{}, isFakeBB={}, isFullScreenRT={}, g_InUpscalerPass={}, shouldScale={}, BB={}x{}, Disp={}x{}", 
                pV->Width, pV->Height, rtDesc.Width, rtDesc.Height, isFakeBB, isFullScreenRT, g_InUpscalerPass, shouldScale, m_renderW, m_renderH, m_displayW, m_displayH);
        }

        // Always bypass if in upscaler pass or if we shouldn't scale
        if (g_InUpscalerPass || !shouldScale) return m_pReal->SetViewport(pV);

        // Even if on fake BB, if the viewport already matches the BB size, do not scale (prevent double scale)
        if (pV->Width == m_renderW && pV->Height == m_renderH) {
            return m_pReal->SetViewport(pV);
        }

        // Scale viewport to match render resolution
        D3DVIEWPORT9 scaledVp = *pV;
        if (m_displayW > 0 && m_displayH > 0) {
            scaledVp.X = (pV->X * m_renderW + m_displayW / 2) / m_displayW;
            scaledVp.Y = (pV->Y * m_renderH + m_displayH / 2) / m_displayH;
            scaledVp.Width = (pV->Width * m_renderW + m_displayW / 2) / m_displayW;
            scaledVp.Height = (pV->Height * m_renderH + m_displayH / 2) / m_displayH;
            
            // Ensure width/height are at least 1 if original was > 0
            if (pV->Width > 0 && scaledVp.Width == 0) scaledVp.Width = 1;
            if (pV->Height > 0 && scaledVp.Height == 0) scaledVp.Height = 1;
        }

        static int viewportlogCount = 0;
        if (viewportlogCount++ % 100 == 0) {
            Logger::info("SetViewport [Scaled]: Game {}x{} -> Scaled {}x{}", 
                pV->Width, pV->Height, scaledVp.Width, scaledVp.Height);
        }
        
        return m_pReal->SetViewport(&scaledVp); 
    }
    STDMETHOD(GetViewport)(D3DVIEWPORT9* pV) override { return m_pReal->GetViewport(pV); }
    STDMETHOD(SetMaterial)(CONST D3DMATERIAL9* pM) override { return m_pReal->SetMaterial(pM); }
    STDMETHOD(GetMaterial)(D3DMATERIAL9* pM) override { return m_pReal->GetMaterial(pM); }
    STDMETHOD(SetLight)(DWORD I, CONST D3DLIGHT9* pL) override { return m_pReal->SetLight(I, pL); }
    STDMETHOD(GetLight)(DWORD I, D3DLIGHT9* pL) override { return m_pReal->GetLight(I, pL); }
    STDMETHOD(LightEnable)(DWORD I, BOOL E) override { return m_pReal->LightEnable(I, E); }
    STDMETHOD(GetLightEnable)(DWORD I, BOOL* pE) override { return m_pReal->GetLightEnable(I, pE); }
    STDMETHOD(SetClipPlane)(DWORD I, CONST float* pP) override { return m_pReal->SetClipPlane(I, pP); }
    STDMETHOD(GetClipPlane)(DWORD I, float* pP) override { return m_pReal->GetClipPlane(I, pP); }
    STDMETHOD(SetRenderState)(D3DRENDERSTATETYPE S, DWORD V) override { return m_pReal->SetRenderState(S, V); }
    STDMETHOD(GetRenderState)(D3DRENDERSTATETYPE S, DWORD* pV) override { return m_pReal->GetRenderState(S, pV); }
    STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE T, IDirect3DStateBlock9** ppSB) override { return m_pReal->CreateStateBlock(T, ppSB); }
    STDMETHOD(BeginStateBlock)() override { return m_pReal->BeginStateBlock(); }
    STDMETHOD(EndStateBlock)(IDirect3DStateBlock9** ppSB) override { return m_pReal->EndStateBlock(ppSB); }
    STDMETHOD(SetClipStatus)(CONST D3DCLIPSTATUS9* pCS) override { return m_pReal->SetClipStatus(pCS); }
    STDMETHOD(GetClipStatus)(D3DCLIPSTATUS9* pCS) override { return m_pReal->GetClipStatus(pCS); }
    STDMETHOD(GetTexture)(DWORD S, IDirect3DBaseTexture9** ppT) override { return m_pReal->GetTexture(S, ppT); }
    STDMETHOD(SetTexture)(DWORD S, IDirect3DBaseTexture9* pT) override { return m_pReal->SetTexture(S, pT); }
    STDMETHOD(GetTextureStageState)(DWORD S, D3DTEXTURESTAGESTATETYPE T, DWORD* pV) override { return m_pReal->GetTextureStageState(S, T, pV); }
    STDMETHOD(SetTextureStageState)(DWORD S, D3DTEXTURESTAGESTATETYPE T, DWORD V) override { return m_pReal->SetTextureStageState(S, T, V); }
    STDMETHOD(GetSamplerState)(DWORD S, D3DSAMPLERSTATETYPE T, DWORD* pV) override { return m_pReal->GetSamplerState(S, T, pV); }
    STDMETHOD(SetSamplerState)(DWORD S, D3DSAMPLERSTATETYPE T, DWORD V) override { return m_pReal->SetSamplerState(S, T, V); }
    STDMETHOD(ValidateDevice)(DWORD* pNP) override { return m_pReal->ValidateDevice(pNP); }
    STDMETHOD(SetPaletteEntries)(UINT PN, CONST PALETTEENTRY* pE) override { return m_pReal->SetPaletteEntries(PN, pE); }
    STDMETHOD(GetPaletteEntries)(UINT PN, PALETTEENTRY* pE) override { return m_pReal->GetPaletteEntries(PN, pE); }
    STDMETHOD(SetCurrentTexturePalette)(UINT PN) override { return m_pReal->SetCurrentTexturePalette(PN); }
    STDMETHOD(GetCurrentTexturePalette)(UINT *PN) override { return m_pReal->GetCurrentTexturePalette(PN); }
    STDMETHOD(SetScissorRect)(CONST RECT* pR) override { 
        if (!pR) return D3DERR_INVALIDCALL;
        UpdateScaledResolution();

        IDirect3DSurface9* pRT = nullptr;
        m_pReal->GetRenderTarget(0, &pRT);
        D3DSURFACE_DESC rtDesc = {};
        if (pRT) pRT->GetDesc(&rtDesc);
        
        bool isFakeBB = (pRT && (pRT == m_pFakeBackBuffer || (m_pFakeBackBuffer && pRT == m_pFakeBackBuffer->GetInternalSurface())));
        bool isFullScreenRT = (rtDesc.Width == m_displayW && rtDesc.Height == m_displayH);
        if (pRT) pRT->Release();

        // Only scale if we are rendering to the fake backbuffer.
        bool shouldScale = isFakeBB;

        static int logCountScissor = 0;
        if (logCountScissor++ % 100 == 0) {
            Logger::info("SetScissorRect: pR={}x{}, RT={}x{}, isFakeBB={}, isFullScreenRT={}, g_InUpscalerPass={}, shouldScale={}", 
                pR->right - pR->left, pR->bottom - pR->top, rtDesc.Width, rtDesc.Height, isFakeBB, isFullScreenRT, g_InUpscalerPass, shouldScale);
        }

        if (g_InUpscalerPass || !shouldScale) return m_pReal->SetScissorRect(pR);

        UINT scW = pR->right - pR->left;
        UINT scH = pR->bottom - pR->top;

        if (scW == m_renderW && scH == m_renderH) {
            return m_pReal->SetScissorRect(pR);
        }

        RECT scaledR;
        if (m_displayW > 0 && m_displayH > 0) {
            scaledR.left = (pR->left * m_renderW + m_displayW / 2) / m_displayW;
            scaledR.top = (pR->top * m_renderH + m_displayH / 2) / m_displayH;
            scaledR.right = (pR->right * m_renderW + m_displayW / 2) / m_displayW;
            scaledR.bottom = (pR->bottom * m_renderH + m_displayH / 2) / m_displayH;
            
            // Safety: Ensure right/bottom are at least left+1 if original had size
            if (pR->right > pR->left && scaledR.right <= scaledR.left) scaledR.right = scaledR.left + 1;
            if (pR->bottom > pR->top && scaledR.bottom <= scaledR.top) scaledR.bottom = scaledR.top + 1;
        } else {
            scaledR = *pR;
        }

        static int logCount = 0;
        if (logCount++ % 100 == 0) {
            Logger::info("SetScissorRect [Scaled]: Game {}x{} -> Scaled {}x{}", 
                pR->right - pR->left, pR->bottom - pR->top, scaledR.right - scaledR.left, scaledR.bottom - scaledR.top);
        }

        return m_pReal->SetScissorRect(&scaledR); 
    }
    STDMETHOD(GetScissorRect)(RECT* pR) override { return m_pReal->GetScissorRect(pR); }
    STDMETHOD(SetSoftwareVertexProcessing)(BOOL bS) override { return m_pReal->SetSoftwareVertexProcessing(bS); }
    STDMETHOD_(BOOL, GetSoftwareVertexProcessing)() override { return m_pReal->GetSoftwareVertexProcessing(); }
    STDMETHOD(SetNPatchMode)(float nS) override { return m_pReal->SetNPatchMode(nS); }
    STDMETHOD_(float, GetNPatchMode)() override { return m_pReal->GetNPatchMode(); }
    STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE PT, UINT SV, UINT PC) override { return m_pReal->DrawPrimitive(PT, SV, PC); }
    STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE PT, INT BVI, UINT MVI, UINT NV, UINT SI, UINT PC) override { return m_pReal->DrawIndexedPrimitive(PT, BVI, MVI, NV, SI, PC); }
    STDMETHOD(DrawPrimitiveUP)(D3DPRIMITIVETYPE PT, UINT PC, CONST void* pV, UINT VS) override { return m_pReal->DrawPrimitiveUP(PT, PC, pV, VS); }
    STDMETHOD(DrawIndexedPrimitiveUP)(D3DPRIMITIVETYPE PT, UINT MVI, UINT NV, UINT PC, CONST void* pI, D3DFORMAT IF, CONST void* pV, UINT VS) override { return m_pReal->DrawIndexedPrimitiveUP(PT, MVI, NV, PC, pI, IF, pV, VS); }
    STDMETHOD(ProcessVertices)(UINT SSI, UINT DI, UINT VC, IDirect3DVertexBuffer9* pDB, IDirect3DVertexDeclaration9* pVD, DWORD F) override { return m_pReal->ProcessVertices(SSI, DI, VC, pDB, pVD, F); }
    STDMETHOD(CreateVertexDeclaration)(CONST D3DVERTEXELEMENT9* pVE, IDirect3DVertexDeclaration9** ppD) override { return m_pReal->CreateVertexDeclaration(pVE, ppD); }
    STDMETHOD(SetVertexDeclaration)(IDirect3DVertexDeclaration9* pD) override { return m_pReal->SetVertexDeclaration(pD); }
    STDMETHOD(GetVertexDeclaration)(IDirect3DVertexDeclaration9** ppD) override { return m_pReal->GetVertexDeclaration(ppD); }
    STDMETHOD(SetFVF)(DWORD FVF) override { return m_pReal->SetFVF(FVF); }
    STDMETHOD(GetFVF)(DWORD* pFVF) override { return m_pReal->GetFVF(pFVF); }
    STDMETHOD(CreateVertexShader)(CONST DWORD* pF, IDirect3DVertexShader9** ppS) override { return m_pReal->CreateVertexShader(pF, ppS); }
    STDMETHOD(SetVertexShader)(IDirect3DVertexShader9* pS) override { return m_pReal->SetVertexShader(pS); }
    STDMETHOD(GetVertexShader)(IDirect3DVertexShader9** ppS) override { return m_pReal->GetVertexShader(ppS); }
    STDMETHOD(SetVertexShaderConstantF)(UINT SR, CONST float* pCD, UINT V4C) override { return m_pReal->SetVertexShaderConstantF(SR, pCD, V4C); }
    STDMETHOD(GetVertexShaderConstantF)(UINT SR, float* pCD, UINT V4C) override { return m_pReal->GetVertexShaderConstantF(SR, pCD, V4C); }
    STDMETHOD(SetVertexShaderConstantI)(UINT SR, CONST int* pCD, UINT V4C) override { return m_pReal->SetVertexShaderConstantI(SR, pCD, V4C); }
    STDMETHOD(GetVertexShaderConstantI)(UINT SR, int* pCD, UINT V4C) override { return m_pReal->GetVertexShaderConstantI(SR, pCD, V4C); }
    STDMETHOD(SetVertexShaderConstantB)(UINT SR, CONST BOOL* pCD, UINT BC) override { return m_pReal->SetVertexShaderConstantB(SR, pCD, BC); }
    STDMETHOD(GetVertexShaderConstantB)(UINT SR, BOOL* pCD, UINT BC) override { return m_pReal->GetVertexShaderConstantB(SR, pCD, BC); }
    STDMETHOD(SetStreamSource)(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) override { return m_pReal->SetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride); }
    STDMETHOD(GetStreamSource)(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) override { return m_pReal->GetStreamSource(StreamNumber, ppStreamData, pOffsetInBytes, pStride); }
    STDMETHOD(SetStreamSourceFreq)(UINT SN, UINT S) override { return m_pReal->SetStreamSourceFreq(SN, S); }
    STDMETHOD(GetStreamSourceFreq)(UINT SN, UINT* pS) override { return m_pReal->GetStreamSourceFreq(SN, pS); }
    STDMETHOD(SetIndices)(IDirect3DIndexBuffer9* pID) override { return m_pReal->SetIndices(pID); }
    STDMETHOD(GetIndices)(IDirect3DIndexBuffer9** ppID) override { return m_pReal->GetIndices(ppID); }
    STDMETHOD(CreatePixelShader)(CONST DWORD* pF, IDirect3DPixelShader9** ppS) override { return m_pReal->CreatePixelShader(pF, ppS); }
    STDMETHOD(SetPixelShader)(IDirect3DPixelShader9* pS) override { return m_pReal->SetPixelShader(pS); }
    STDMETHOD(GetPixelShader)(IDirect3DPixelShader9** ppS) override { return m_pReal->GetPixelShader(ppS); }
    STDMETHOD(SetPixelShaderConstantF)(UINT SR, CONST float* pCD, UINT V4C) override { return m_pReal->SetPixelShaderConstantF(SR, pCD, V4C); }
    STDMETHOD(GetPixelShaderConstantF)(UINT SR, float* pCD, UINT V4C) override { return m_pReal->GetPixelShaderConstantF(SR, pCD, V4C); }
    STDMETHOD(SetPixelShaderConstantI)(UINT SR, CONST int* pCD, UINT V4C) override { return m_pReal->SetPixelShaderConstantI(SR, pCD, V4C); }
    STDMETHOD(GetPixelShaderConstantI)(UINT SR, int* pCD, UINT V4C) override { return m_pReal->GetPixelShaderConstantI(SR, pCD, V4C); }
    STDMETHOD(SetPixelShaderConstantB)(UINT SR, CONST BOOL* pCD, UINT BC) override { return m_pReal->SetPixelShaderConstantB(SR, pCD, BC); }
    STDMETHOD(GetPixelShaderConstantB)(UINT SR, BOOL* pCD, UINT BC) override { return m_pReal->GetPixelShaderConstantB(SR, pCD, BC); }
    STDMETHOD(DrawRectPatch)(UINT H, CONST float* pNS, CONST D3DRECTPATCH_INFO* pPI) override { return m_pReal->DrawRectPatch(H, pNS, pPI); }
    STDMETHOD(DrawTriPatch)(UINT H, CONST float* pNS, CONST D3DTRIPATCH_INFO* pPI) override { return m_pReal->DrawTriPatch(H, pNS, pPI); }
    STDMETHOD(DeletePatch)(UINT H) override { return m_pReal->DeletePatch(H); }
    STDMETHOD(CreateQuery)(D3DQUERYTYPE T, IDirect3DQuery9** ppQ) override { return m_pReal->CreateQuery(T, ppQ); }

    // IDirect3DDevice9Ex methods
    STDMETHOD(SetConvolutionMonoKernel)(UINT W, UINT H, float* R, float* C) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->SetConvolutionMonoKernel(W, H, R, C); }
    STDMETHOD(ComposeRects)(IDirect3DSurface9* pS, IDirect3DSurface9* pD, IDirect3DVertexBuffer9* pSRD, UINT NR, IDirect3DVertexBuffer9* pDRD, D3DCOMPOSERECTSOP O, int XO, int YO) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->ComposeRects(pS, pD, pSRD, NR, pDRD, O, XO, YO); }
    STDMETHOD(PresentEx)(CONST RECT* pSR, CONST RECT* pDR, HWND hW, CONST RGNDATA* pR, DWORD F) override { 
        UpdateScaledResolution();
        OverlayRenderer::Get().NewFrame();
        IDirect3DSurface9* pRBB = nullptr;
        if (SUCCEEDED(m_pReal->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pRBB))) {
            if (m_isUpscaling && m_pFakeBackBuffer) {
                bool upscalerHandled = false;
                if (UpscalerManager::Get().IsUpscalingEnabled()) {
                    g_InUpscalerPass = true;
                    UpscalerManager::Get().RenderFrame((void*)m_pReal, (void*)m_pFakeBackBuffer->GetInternalSurface(), (void*)pRBB, m_displayW, m_displayH, m_renderW, m_renderH);
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
            if (pOldRT) pOldRT->Release();
            pRBB->Release();
        } else {
            OverlayRenderer::Get().Render(m_pReal, m_displayW, m_displayH);
        }
        if (!m_pRealEx) return E_NOTIMPL; 
        // Force NULL rects to ensure the full upscaled 1080p buffer is presented
        return m_pRealEx->PresentEx(NULL, NULL, hW, pR, F); 
    }
    STDMETHOD(GetGPUThreadPriority)(INT* pP) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->GetGPUThreadPriority(pP); }
    STDMETHOD(SetGPUThreadPriority)(INT P) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->SetGPUThreadPriority(P); }
    STDMETHOD(WaitForVBlank)(UINT iSC) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->WaitForVBlank(iSC); }
    STDMETHOD(CheckResourceResidency)(IDirect3DResource9** pRA, UINT32 NR) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->CheckResourceResidency(pRA, NR); }
    STDMETHOD(SetMaximumFrameLatency)(UINT ML) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->SetMaximumFrameLatency(ML); }
    STDMETHOD(GetMaximumFrameLatency)(UINT* pML) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->GetMaximumFrameLatency(pML); }
    STDMETHOD(CheckDeviceState)(HWND hDW) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->CheckDeviceState(hDW); }
    STDMETHOD(CreateRenderTargetEx)(UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL L, IDirect3DSurface9** ppS, HANDLE* pS2, DWORD U) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->CreateRenderTargetEx(W, H, F, M, MQ, L, ppS, pS2, U); }
    STDMETHOD(CreateOffscreenPlainSurfaceEx)(UINT W, UINT H, D3DFORMAT F, D3DPOOL P, IDirect3DSurface9** ppS, HANDLE* pS2, DWORD U) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->CreateOffscreenPlainSurfaceEx(W, H, F, P, ppS, pS2, U); }
    STDMETHOD(CreateDepthStencilSurfaceEx)(UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL D, IDirect3DSurface9** ppS, HANDLE* pS2, DWORD U) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->CreateDepthStencilSurfaceEx(W, H, F, M, MQ, D, ppS, pS2, U); }
    STDMETHOD(ResetEx)(D3DPRESENT_PARAMETERS* pPP, D3DDISPLAYMODEEX* pFDM) override { 
        OverlayRenderer::Get().OnReset();
        
        int scaledW = pPP->BackBufferWidth;
        int scaledH = pPP->BackBufferHeight;
        GetScaledResolution(scaledW, scaledH);
        
        int nativeW = Config::Get().GetTargetWidth();
        int nativeH = Config::Get().GetTargetHeight();
        if (nativeW <= 0) nativeW = pPP->BackBufferWidth;
        if (nativeH <= 0) nativeH = pPP->BackBufferHeight;
        
        D3DPRESENT_PARAMETERS realPP = *pPP;
        realPP.BackBufferWidth = nativeW;
        realPP.BackBufferHeight = nativeH;
        
        if (!m_pRealEx) return E_NOTIMPL; 
        HRESULT hr = m_pRealEx->ResetEx(&realPP, pFDM);
        if (SUCCEEDED(hr)) {
            pPP->BackBufferWidth = scaledW;
            pPP->BackBufferHeight = scaledH;
            OverlayRenderer::Get().OnPostReset();
        }
        return hr;
    }
    STDMETHOD(GetDisplayModeEx)(UINT iSC, D3DDISPLAYMODEEX* pM, D3DDISPLAYROTATION* pR) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->GetDisplayModeEx(iSC, pM, pR); }
};

// --- Proxy D3D9 ---
class ProxyDirect3D9 : public IDirect3D9Ex {
    IDirect3D9* m_pReal;
    IDirect3D9Ex* m_pRealEx;
public:
    ProxyDirect3D9(IDirect3D9* pReal) : m_pReal(pReal) {
        m_pReal->QueryInterface(IID_IDirect3D9Ex, (void**)&m_pRealEx);
    }
    virtual ~ProxyDirect3D9() { if (m_pRealEx) m_pRealEx->Release(); }

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) override {
        if (riid == IID_IDirect3D9 || riid == IID_IUnknown) { *ppvObj = (IDirect3D9*)this; AddRef(); return S_OK; }
        if (riid == IID_IDirect3D9Ex) {
            if (m_pRealEx) { *ppvObj = (IDirect3D9Ex*)this; AddRef(); return S_OK; }
            return E_NOINTERFACE;
        }
        return m_pReal->QueryInterface(riid, ppvObj);
    }
    STDMETHOD_(ULONG, AddRef)() override { return m_pReal->AddRef(); }
    STDMETHOD_(ULONG, Release)() override {
        ULONG res = m_pReal->Release();
        if (res == 0) delete this;
        return res;
    }
    STDMETHOD(RegisterSoftwareDevice)(void* pIF) override { return m_pReal->RegisterSoftwareDevice(pIF); }
    STDMETHOD_(UINT, GetAdapterCount)() override { return m_pReal->GetAdapterCount(); }
    STDMETHOD(GetAdapterIdentifier)(UINT A, DWORD F, D3DADAPTER_IDENTIFIER9* pI) override { return m_pReal->GetAdapterIdentifier(A, F, pI); }
    STDMETHOD_(UINT, GetAdapterModeCount)(UINT A, D3DFORMAT F) override { return m_pReal->GetAdapterModeCount(A, F); }
    STDMETHOD(EnumAdapterModes)(UINT A, D3DFORMAT F, UINT M, D3DDISPLAYMODE* pM) override { return m_pReal->EnumAdapterModes(A, F, M, pM); }
    STDMETHOD(GetAdapterDisplayMode)(UINT A, D3DDISPLAYMODE* pM) override { return m_pReal->GetAdapterDisplayMode(A, pM); }
    STDMETHOD(CheckDeviceType)(UINT A, D3DDEVTYPE DT, D3DFORMAT AF, D3DFORMAT BF, BOOL W) override { return m_pReal->CheckDeviceType(A, DT, AF, BF, W); }
    STDMETHOD(CheckDeviceFormat)(UINT A, D3DDEVTYPE DT, D3DFORMAT AF, DWORD U, D3DRESOURCETYPE RT, D3DFORMAT CF) override { return m_pReal->CheckDeviceFormat(A, DT, AF, U, RT, CF); }
    STDMETHOD(CheckDeviceMultiSampleType)(UINT A, D3DDEVTYPE DT, D3DFORMAT SF, BOOL W, D3DMULTISAMPLE_TYPE MT, DWORD* pQL) override { return m_pReal->CheckDeviceMultiSampleType(A, DT, SF, W, MT, pQL); }
    STDMETHOD(CheckDepthStencilMatch)(UINT A, D3DDEVTYPE DT, D3DFORMAT AF, D3DFORMAT RTF, D3DFORMAT DSF) override { return m_pReal->CheckDepthStencilMatch(A, DT, AF, RTF, DSF); }
    STDMETHOD(CheckDeviceFormatConversion)(UINT A, D3DDEVTYPE DT, D3DFORMAT SF, D3DFORMAT TF) override { return m_pReal->CheckDeviceFormatConversion(A, DT, SF, TF); }
    STDMETHOD(GetDeviceCaps)(UINT A, D3DDEVTYPE DT, D3DCAPS9* pC) override { return m_pReal->GetDeviceCaps(A, DT, pC); }
    STDMETHOD_(HMONITOR, GetAdapterMonitor)(UINT A) override { return m_pReal->GetAdapterMonitor(A); }
    STDMETHOD(CreateDevice)(UINT A, D3DDEVTYPE DT, HWND hFW, DWORD BF, D3DPRESENT_PARAMETERS* pPP, IDirect3DDevice9** ppRDI) override {
        if (!pPP) return D3DERR_INVALIDCALL;
        OverlayRenderer::Get().SetWindow(hFW);
        
        int scaledW = pPP->BackBufferWidth;
        int scaledH = pPP->BackBufferHeight;
        GetScaledResolution(scaledW, scaledH);
        
        int nativeW = Config::Get().GetTargetWidth();
        int nativeH = Config::Get().GetTargetHeight();
        if (nativeW <= 0) nativeW = pPP->BackBufferWidth;
        if (nativeH <= 0) nativeH = pPP->BackBufferHeight;
        
        D3DPRESENT_PARAMETERS realPP = *pPP;
        realPP.BackBufferWidth = nativeW;
        realPP.BackBufferHeight = nativeH;

        Logger::info("CreateDevice: Game requested {}x{}, Proxy creating device at {}x{}, Game will see {}x{}", pPP->BackBufferWidth, pPP->BackBufferHeight, nativeW, nativeH, scaledW, scaledH);
        
        HRESULT hr = m_pReal->CreateDevice(A, DT, hFW, BF, &realPP, ppRDI);
        if (SUCCEEDED(hr) && ppRDI && *ppRDI) {
            pPP->BackBufferWidth = scaledW;
            pPP->BackBufferHeight = scaledH;
            *ppRDI = (IDirect3DDevice9*)new ProxyDirect3DDevice9(*ppRDI, (IDirect3D9*)this, scaledW, scaledH, nativeW, nativeH, true);
        }
        return hr;
    }

    // IDirect3D9Ex methods
    STDMETHOD_(UINT, GetAdapterModeCountEx)(UINT A, CONST D3DDISPLAYMODEFILTER* pF) override { if (!m_pRealEx) return 0; return m_pRealEx->GetAdapterModeCountEx(A, pF); }
    STDMETHOD(EnumAdapterModesEx)(UINT A, CONST D3DDISPLAYMODEFILTER* pF, UINT M, D3DDISPLAYMODEEX* pMode) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->EnumAdapterModesEx(A, pF, M, pMode); }
    STDMETHOD(GetAdapterDisplayModeEx)(UINT A, D3DDISPLAYMODEEX* pM, D3DDISPLAYROTATION* pR) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->GetAdapterDisplayModeEx(A, pM, pR); }
    STDMETHOD(CreateDeviceEx)(UINT A, D3DDEVTYPE DT, HWND hFW, DWORD BF, D3DPRESENT_PARAMETERS* pPP, D3DDISPLAYMODEEX* pFDM, IDirect3DDevice9Ex** ppRDI) override {
        if (!pPP) return D3DERR_INVALIDCALL;
        OverlayRenderer::Get().SetWindow(hFW);
        
        int scaledW = pPP->BackBufferWidth;
        int scaledH = pPP->BackBufferHeight;
        GetScaledResolution(scaledW, scaledH);
        
        int nativeW = Config::Get().GetTargetWidth();
        int nativeH = Config::Get().GetTargetHeight();
        if (nativeW <= 0) nativeW = pPP->BackBufferWidth;
        if (nativeH <= 0) nativeH = pPP->BackBufferHeight;
        
        D3DPRESENT_PARAMETERS realPP = *pPP;
        realPP.BackBufferWidth = nativeW;
        realPP.BackBufferHeight = nativeH;

        if (!m_pRealEx) return E_NOTIMPL;
        HRESULT hr = m_pRealEx->CreateDeviceEx(A, DT, hFW, BF, &realPP, pFDM, ppRDI);
        if (SUCCEEDED(hr) && ppRDI && *ppRDI) {
            pPP->BackBufferWidth = scaledW;
            pPP->BackBufferHeight = scaledH;
            *ppRDI = new ProxyDirect3DDevice9(*ppRDI, (IDirect3D9*)this, scaledW, scaledH, nativeW, nativeH, true);
        }
        return hr;
    }
    STDMETHOD(GetAdapterLUID)(UINT A, LUID* pL) override { if (!m_pRealEx) return E_NOTIMPL; return m_pRealEx->GetAdapterLUID(A, pL); }
};

// --- Hooking Logic ---
PFN_Direct3DCreate9 Trampoline_Direct3DCreate9 = NULL;
PFN_Direct3DCreate9Ex Trampoline_Direct3DCreate9Ex = NULL;

IDirect3D9* WINAPI Hook_Direct3DCreate9(UINT SDKVersion) {
    static bool insideHook = false;
    if (insideHook) return Trampoline_Direct3DCreate9(SDKVersion);
    insideHook = true;
    Logger::info("Hook_Direct3DCreate9: Intercepted call.");
    IDirect3D9* pReal = Trampoline_Direct3DCreate9(SDKVersion);
    if (pReal) pReal = (IDirect3D9*)new ProxyDirect3D9(pReal);
    insideHook = false;
    return pReal;
}

HRESULT WINAPI Hook_Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppDirect3D9Ex) {
    static bool insideHook = false;
    if (insideHook) return Trampoline_Direct3DCreate9Ex(SDKVersion, ppDirect3D9Ex);
    insideHook = true;
    Logger::info("Hook_Direct3DCreate9Ex: Intercepted call.");
    HRESULT hr = Trampoline_Direct3DCreate9Ex(SDKVersion, ppDirect3D9Ex);
    if (SUCCEEDED(hr) && ppDirect3D9Ex && *ppDirect3D9Ex) {
        *ppDirect3D9Ex = (IDirect3D9Ex*)new ProxyDirect3D9(*ppDirect3D9Ex);
    }
    insideHook = false;
    return hr;
}

void InitializeHooks() {
    static bool hooksInitialized = false;
    if (hooksInitialized) return;
    
    if (MH_Initialize() != MH_OK) {
        Logger::error("InitializeHooks: MH_Initialize failed!");
        return;
    }

    HMODULE hD3D9 = GetModuleHandleA("d3d9.dll");
    if (hD3D9) {
        HMODULE hMe = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&InitializeHooks, &hMe);
        
        char myPath[MAX_PATH];
        GetModuleFileNameA(hMe, myPath, MAX_PATH);
        std::string pathStr = myPath;
        for (auto& c : pathStr) c = tolower(c);
        
        bool isDInput8 = (pathStr.find("dinput8.dll") != std::string::npos);

        if (isDInput8 && hD3D9 != hMe) {
            void* pDirect3DCreate9 = (void*)GetProcAddress(hD3D9, "Direct3DCreate9");
            void* pDirect3DCreate9Ex = (void*)GetProcAddress(hD3D9, "Direct3DCreate9Ex");

            if (pDirect3DCreate9) {
                if (MH_CreateHook(pDirect3DCreate9, (LPVOID)&Hook_Direct3DCreate9, (LPVOID*)&Trampoline_Direct3DCreate9) == MH_OK) {
                    MH_EnableHook(pDirect3DCreate9);
                    Logger::info("InitializeHooks: Hooked Direct3DCreate9 in d3d9.dll");
                }
            }
            if (pDirect3DCreate9Ex) {
                if (MH_CreateHook(pDirect3DCreate9Ex, (LPVOID)&Hook_Direct3DCreate9Ex, (LPVOID*)&Trampoline_Direct3DCreate9Ex) == MH_OK) {
                    MH_EnableHook(pDirect3DCreate9Ex);
                    Logger::info("InitializeHooks: Hooked Direct3DCreate9Ex in d3d9.dll");
                }
            }
        }
    }
    hooksInitialized = true;
}

// --- Initialization ---
void EnsureRealDllLoaded() {
    if (g_hRealDll) return;
    g_hRealDll = LoadLibraryA("real_d3d9.dll");
    if (g_hRealDll) Logger::info("Loaded chained DLL: real_d3d9.dll");
    if (!g_hRealDll) {
        char sysPath[MAX_PATH];
        GetSystemDirectoryA(sysPath, MAX_PATH); strcat(sysPath, "\\d3d9.dll");
        g_hRealDll = LoadLibraryA(sysPath);
        if (g_hRealDll) Logger::info("Loaded system DLL: {}", sysPath);
    }
    if (g_hRealDll) {
        Real_Direct3DCreate9 = (PFN_Direct3DCreate9)GetProcAddress(g_hRealDll, "Direct3DCreate9");
        Real_Direct3DCreate9Ex = (PFN_Direct3DCreate9Ex)GetProcAddress(g_hRealDll, "Direct3DCreate9Ex");
        Real_D3DPERF_BeginEvent = (PFN_D3DPERF_BeginEvent)GetProcAddress(g_hRealDll, "D3DPERF_BeginEvent");
        Real_D3DPERF_EndEvent = (PFN_D3DPERF_EndEvent)GetProcAddress(g_hRealDll, "D3DPERF_EndEvent");
        Real_D3DPERF_GetStatus = (PFN_D3DPERF_GetStatus)GetProcAddress(g_hRealDll, "D3DPERF_GetStatus");
        Real_D3DPERF_QueryRepeatFrame = (PFN_D3DPERF_QueryRepeatFrame)GetProcAddress(g_hRealDll, "D3DPERF_QueryRepeatFrame");
        Real_D3DPERF_SetMarker = (PFN_D3DPERF_SetMarker)GetProcAddress(g_hRealDll, "D3DPERF_SetMarker");
        Real_D3DPERF_SetOptions = (PFN_D3DPERF_SetOptions)GetProcAddress(g_hRealDll, "D3DPERF_SetOptions");
        Real_D3DPERF_SetRegion = (PFN_D3DPERF_SetRegion)GetProcAddress(g_hRealDll, "D3DPERF_SetRegion");
    }
}

extern "C" {
    HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
        Logger::Init(); 
        EnsureRealDInput8Loaded();
        InitializeHooks();
        if (!Real_DirectInput8Create) return E_FAIL;
        return Real_DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    }
    IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
        Logger::Init(); EnsureRealDllLoaded();
        if (!Real_Direct3DCreate9) return NULL;
        IDirect3D9* pReal = Real_Direct3DCreate9(SDKVersion);
        if (!pReal) return NULL;
        return (IDirect3D9*)new ProxyDirect3D9(pReal);
    }
    HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppDirect3D9Ex) {
        Logger::Init(); EnsureRealDllLoaded();
        if (!Real_Direct3DCreate9Ex) return D3DERR_INVALIDCALL;
        HRESULT hr = Real_Direct3DCreate9Ex(SDKVersion, ppDirect3D9Ex);
        if (SUCCEEDED(hr) && ppDirect3D9Ex && *ppDirect3D9Ex) *ppDirect3D9Ex = (IDirect3D9Ex*)new ProxyDirect3D9(*ppDirect3D9Ex);
        return hr;
    }
    int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) { EnsureRealDllLoaded(); if (Real_D3DPERF_BeginEvent) return Real_D3DPERF_BeginEvent(col, wszName); return 0; }
    int WINAPI D3DPERF_EndEvent(void) { EnsureRealDllLoaded(); if (Real_D3DPERF_EndEvent) return Real_D3DPERF_EndEvent(); return 0; }
    DWORD WINAPI D3DPERF_GetStatus(void) { EnsureRealDllLoaded(); if (Real_D3DPERF_GetStatus) return Real_D3DPERF_GetStatus(); return 0; }
    BOOL WINAPI D3DPERF_QueryRepeatFrame(void) { EnsureRealDllLoaded(); if (Real_D3DPERF_QueryRepeatFrame) return Real_D3DPERF_QueryRepeatFrame(); return FALSE; }
    void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) { EnsureRealDllLoaded(); if (Real_D3DPERF_SetMarker) Real_D3DPERF_SetMarker(col, wszName); }
    void WINAPI D3DPERF_SetOptions(DWORD dwOptions) { EnsureRealDllLoaded(); if (Real_D3DPERF_SetOptions) Real_D3DPERF_SetOptions(dwOptions); }
    void WINAPI D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) { EnsureRealDllLoaded(); if (Real_D3DPERF_SetRegion) Real_D3DPERF_SetRegion(col, wszName); }
    void WINAPI DebugSetLevel(void) {}
    void WINAPI DebugSetMute(void) {}
    int WINAPI Direct3D9EnableMaximizedWindowedModeShim(UINT a) { return 0; }
    void* WINAPI Direct3DShaderValidatorCreate9(void) { return NULL; }
    void WINAPI PSGPError(void* a, UINT b, UINT c) {}
    void WINAPI PSGPSampleTexture(void* a, UINT b, float(*const c)[4], UINT d, float(*const e)[4]) {}
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) { 
        Logger::Init(); 
        Config::Get().Load();
        InitializeHooks();
        Logger::info("--- Proxy Loaded (D3D9/DInput8) ---"); 
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        Logger::info("--- Proxy Unloaded ---");
    }
    return TRUE;
}
