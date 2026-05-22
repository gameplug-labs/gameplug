#pragma once

#include "d3d9_proxy.h"

// --- Proxy D3D9 ---
class ProxyDirect3D9 : public IDirect3D9Ex {
    IDirect3D9* m_pReal;
    IDirect3D9Ex* m_pRealEx;

public:
    ProxyDirect3D9(IDirect3D9* pReal);
    virtual ~ProxyDirect3D9();

    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(RegisterSoftwareDevice)(void* pIF) override;
    STDMETHOD_(UINT, GetAdapterCount)() override;
    STDMETHOD(GetAdapterIdentifier)(UINT A, DWORD F, D3DADAPTER_IDENTIFIER9* pI) override;
    STDMETHOD_(UINT, GetAdapterModeCount)(UINT A, D3DFORMAT F) override;
    STDMETHOD(EnumAdapterModes)(UINT A, D3DFORMAT F, UINT M, D3DDISPLAYMODE* pM) override;
    STDMETHOD(GetAdapterDisplayMode)(UINT A, D3DDISPLAYMODE* pM) override;
    STDMETHOD(CheckDeviceType)(UINT A, D3DDEVTYPE DT, D3DFORMAT AF, D3DFORMAT BF, BOOL W) override;
    STDMETHOD(CheckDeviceFormat)(UINT A, D3DDEVTYPE DT, D3DFORMAT AF, DWORD U, D3DRESOURCETYPE RT, D3DFORMAT CF) override;
    STDMETHOD(CheckDeviceMultiSampleType)(UINT A, D3DDEVTYPE DT, D3DFORMAT SF, BOOL W, D3DMULTISAMPLE_TYPE MT, DWORD* pQL) override;
    STDMETHOD(CheckDepthStencilMatch)(UINT A, D3DDEVTYPE DT, D3DFORMAT AF, D3DFORMAT RTF, D3DFORMAT DSF) override;
    STDMETHOD(CheckDeviceFormatConversion)(UINT A, D3DDEVTYPE DT, D3DFORMAT SF, D3DFORMAT TF) override;
    STDMETHOD(GetDeviceCaps)(UINT A, D3DDEVTYPE DT, D3DCAPS9* pC) override;
    STDMETHOD_(HMONITOR, GetAdapterMonitor)(UINT A) override;
    STDMETHOD(CreateDevice)(UINT A, D3DDEVTYPE DT, HWND hFW, DWORD BF, D3DPRESENT_PARAMETERS* pPP, IDirect3DDevice9** ppRDI) override;

    // IDirect3D9Ex methods
    STDMETHOD_(UINT, GetAdapterModeCountEx)(UINT A, CONST D3DDISPLAYMODEFILTER* pF) override;
    STDMETHOD(EnumAdapterModesEx)(UINT A, CONST D3DDISPLAYMODEFILTER* pF, UINT M, D3DDISPLAYMODEEX* pMode) override;
    STDMETHOD(GetAdapterDisplayModeEx)(UINT A, D3DDISPLAYMODEEX* pM, D3DDISPLAYROTATION* pR) override;
    STDMETHOD(CreateDeviceEx)(
        UINT A, D3DDEVTYPE DT, HWND hFW, DWORD BF, D3DPRESENT_PARAMETERS* pPP, D3DDISPLAYMODEEX* pFDM, IDirect3DDevice9Ex** ppRDI) override;
    STDMETHOD(GetAdapterLUID)(UINT A, LUID* pL) override;
};
