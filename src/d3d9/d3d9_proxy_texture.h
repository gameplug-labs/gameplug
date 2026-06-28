#pragma once
#include "d3d9_proxy.h"
#include <vector>

class ProxyTexture9 : public IDirect3DTexture9 {
private:
    IDirect3DTexture9* m_pReal;
    IDirect3DTexture9* m_pReplaced;
    IDirect3DDevice9* m_pProxyDevice;

    ULONG m_refCount;

    UINT m_width;
    UINT m_height;
    UINT m_levels;
    DWORD m_usage;
    D3DFORMAT m_format;
    D3DPOOL m_pool;

    uint32_t m_hash;
    bool m_hashComputed;
    bool m_replacementChecked;

    // Keep track of active LockRect details
    struct LockInfo {
        bool locked = false;
        D3DLOCKED_RECT lockedRect = {};
        RECT rect = {};
        DWORD flags = 0;
    };
    std::vector<LockInfo> m_locks;

public:
    ProxyTexture9(
        IDirect3DTexture9* pReal, IDirect3DDevice9* pProxyDevice, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    virtual ~ProxyTexture9();

    IDirect3DTexture9* GetRealTexture() const { return m_pReal; }
    IDirect3DTexture9* GetReplacedReal() const { return m_pReplaced ? m_pReplaced : m_pReal; }

    UINT GetWidth() const { return m_width; }
    UINT GetHeight() const { return m_height; }
    D3DFORMAT GetFormat() const { return m_format; }
    uint32_t GetHash() const { return m_hash; }
    bool IsHashComputed() const { return m_hashComputed; }
    D3DPOOL GetPool() const { return m_pool; }
    DWORD GetUsage() const { return m_usage; }

    void PropagateFrom(ProxyTexture9* pSrc);
    void CheckAndApplyReplacement();
    void FreeReplacedTexture();
    void RecordLock(UINT level, D3DLOCKED_RECT lockedRect, DWORD flags, const RECT* pRect = nullptr);

    // Hashing and replacement trigger
    void OnLevelUnlocked(UINT level);

    // IUnknown methods
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IDirect3DResource9 methods
    STDMETHOD(GetDevice)(IDirect3DDevice9** ppDevice) override;
    STDMETHOD(SetPrivateData)(REFGUID refguid, const void* pData, DWORD SizeOfData, DWORD Flags) override;
    STDMETHOD(GetPrivateData)(REFGUID refguid, void* pData, DWORD* pSizeOfData) override;
    STDMETHOD(FreePrivateData)(REFGUID refguid) override;
    STDMETHOD_(DWORD, SetPriority)(DWORD PriorityNew) override;
    STDMETHOD_(DWORD, GetPriority)() override;
    STDMETHOD_(void, PreLoad)() override;
    STDMETHOD_(D3DRESOURCETYPE, GetType)() override;

    // IDirect3DBaseTexture9 methods
    STDMETHOD_(DWORD, SetLOD)(DWORD LODNew) override;
    STDMETHOD_(DWORD, GetLOD)() override;
    STDMETHOD_(DWORD, GetLevelCount)() override;
    STDMETHOD(SetAutoGenFilterType)(D3DTEXTUREFILTERTYPE FilterType) override;
    STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)() override;
    STDMETHOD_(void, GenerateMipSubLevels)() override;

    // IDirect3DTexture9 methods
    STDMETHOD(GetLevelDesc)(UINT Level, D3DSURFACE_DESC* pDesc) override;
    STDMETHOD(GetSurfaceLevel)(UINT Level, IDirect3DSurface9** ppSurfaceLevel) override;
    STDMETHOD(LockRect)(UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) override;
    STDMETHOD(UnlockRect)(UINT Level) override;
    STDMETHOD(AddDirtyRect)(const RECT* pDirtyRect) override;
};
