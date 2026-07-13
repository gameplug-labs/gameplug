#include "d3d9_proxy_texture.h"
#include "d3d9_proxy_device.h"
#include "d3d9_proxy_surface.h"
#include "logger.h"
#include <cstdio>

static uint32_t CalculateCRC32(const uint8_t* data, size_t size, uint32_t initial = 0xFFFFFFFF) {
    uint32_t crc = initial;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc;
}

ProxyTexture9::ProxyTexture9(
    IDirect3DTexture9* pReal, IDirect3DDevice9* pProxyDevice, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : m_pReal(pReal)
    , m_pReplaced(nullptr)
    , m_pProxyDevice(pProxyDevice)
    , m_refCount(1)
    , m_width(w)
    , m_height(h)
    , m_levels(levels)
    , m_usage(usage)
    , m_format(fmt)
    , m_pool(pool)
    , m_hash(0)
    , m_hashComputed(false)
    , m_replacementChecked(false) {

    if (m_pReal)
        m_pReal->AddRef();

    m_locks.resize(m_levels);

    // TextureReplacer::Get().OnTextureCreated(this);
}

ProxyTexture9::~ProxyTexture9() {
    // TextureReplacer::Get().OnTextureDestroyed(this);

    FreeReplacedTexture();

    if (m_pReal) {
        m_pReal->Release();
        m_pReal = nullptr;
    }
}

void ProxyTexture9::PropagateFrom(ProxyTexture9* pSrc) {
    if (!pSrc)
        return;

    {
        // std::lock_guard<std::recursive_mutex> lock(TextureReplacer::Get().GetMutex());
        m_hash = pSrc->m_hash;
        m_hashComputed = pSrc->m_hashComputed;

        if (pSrc->m_pReplaced) {
            FreeReplacedTexture();
            m_pReplaced = pSrc->m_pReplaced;
            m_pReplaced->AddRef();
            m_replacementChecked = true;
        }
    }

    if (m_hashComputed) {
        /*
        if (TextureReplacer::Get().IsAutoDumpEnabled()) {
            TextureReplacer::Get().AutoDumpTexture(this);
        }
        */
        if (!m_replacementChecked) {
            CheckAndApplyReplacement();
        }
    }
}

void ProxyTexture9::FreeReplacedTexture() {
    IDirect3DTexture9* pOld = nullptr;
    {
        // std::lock_guard<std::recursive_mutex> lock(TextureReplacer::Get().GetMutex());
        if (m_pReplaced) {
            pOld = m_pReplaced;
            m_pReplaced = nullptr;
        }
        m_replacementChecked = false;
    }
    if (pOld) {
        pOld->Release();
    }
}

void ProxyTexture9::CheckAndApplyReplacement() {
    /*
    if (!TextureReplacer::Get().IsReplacementEnabled()) {
        return;
    }

    IDirect3DTexture9* pRepl = nullptr;
    std::filesystem::path replPath;
    IDirect3DDevice9* pRealDevice = nullptr;

    {
        std::lock_guard<std::recursive_mutex> lock(TextureReplacer::Get().GetMutex());
        if (m_replacementChecked) {
            return;
        }

        m_replacementChecked = true;

        if (!m_hashComputed) {
            return;
        }

        char hashStr[16];
        sprintf_s(hashStr, sizeof(hashStr), "%08X", m_hash);
        replPath = TextureReplacer::Get().GetReplacementsDir() / (std::string(hashStr) + ".dds");

        ProxyDirect3DDevice9* pProxyDev = (ProxyDirect3DDevice9*)m_pProxyDevice;
        pRealDevice = pProxyDev->GetRealDevice();
    }

    std::error_code ec;
    if (!replPath.empty() && std::filesystem::exists(replPath, ec)) {
        pRepl = TextureReplacer::Get().LoadDDS(pRealDevice, replPath);
    }

    if (pRepl) {
        IDirect3DTexture9* pOld = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(TextureReplacer::Get().GetMutex());
            pOld = m_pReplaced;
            m_pReplaced = pRepl;
            m_replacementChecked = true;
            Logger::info("ProxyTexture9: Loaded replacement for {:08X} from {}", m_hash, replPath.string());
        }
        if (pOld) {
            pOld->Release();
        }
    }
    */
}

void ProxyTexture9::RecordLock(UINT level, D3DLOCKED_RECT lockedRect, DWORD flags, const RECT* pRect) {
    if (level < m_locks.size()) {
        m_locks[level].locked = true;
        m_locks[level].lockedRect = lockedRect;
        m_locks[level].flags = flags;
        if (pRect) {
            m_locks[level].rect = *pRect;
        } else {
            m_locks[level].rect = {0, 0, (LONG)m_width, (LONG)m_height};
        }
    }
}

void ProxyTexture9::OnLevelUnlocked(UINT level) {
    if (level != 0 || !m_locks[0].locked) {
        return;
    }

    m_locks[0].locked = false;
    return; // Early exit, do not compute hashes or replace textures
}

/*
    UINT lockedW = m_locks[0].rect.right - m_locks[0].rect.left;
    UINT lockedH = m_locks[0].rect.bottom - m_locks[0].rect.top;
    if (lockedW == 0 || lockedH == 0) {
        return;
    }

    // Determine row size and row count for level 0
    UINT rowBytes = 0;
    UINT rowCount = lockedH;

    switch (m_format) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
        rowBytes = lockedW * 4;
        break;
    case D3DFMT_R8G8B8:
        rowBytes = lockedW * 3;
        break;
    case D3DFMT_A1R5G5B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_R5G6B5:
        rowBytes = lockedW * 2;
        break;
    case D3DFMT_A8:
    case D3DFMT_L8:
        rowBytes = lockedW;
        break;
    case D3DFMT_DXT1:
        rowBytes = ((lockedW + 3) / 4) * 8;
        rowCount = (lockedH + 3) / 4;
        break;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        rowBytes = ((lockedW + 3) / 4) * 16;
        rowCount = (lockedH + 3) / 4;
        break;
    default:
        rowBytes = m_locks[0].lockedRect.Pitch;
        break;
    }

    if (m_locks[0].lockedRect.Pitch > 0 && rowBytes > (UINT)m_locks[0].lockedRect.Pitch) {
        rowBytes = m_locks[0].lockedRect.Pitch;
    }

    // Calculate CRC32 row by row (ignoring pitch padding)
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* src = (const uint8_t*)m_locks[0].lockedRect.pBits;
    for (UINT y = 0; y < rowCount; ++y) {
        crc = CalculateCRC32(src + y * m_locks[0].lockedRect.Pitch, rowBytes, crc);
    }
    m_hash = ~crc;
    m_hashComputed = true;

    Logger::info("ProxyTexture9: Computed CRC32 hash for {}x{} texture (format {}): {:08X}", m_width, m_height, (int)m_format, m_hash);

    // Auto Dump via shared helper (only full texture locks to avoid garbage/partial dumps)
    if (TextureReplacer::Get().IsAutoDumpEnabled() && lockedW == m_width && lockedH == m_height) {
        TextureReplacer::Get().AutoDumpTexture(this);
    }

    // Check replacement
    CheckAndApplyReplacement();
}
*/

// IUnknown methods
STDMETHODIMP ProxyTexture9::QueryInterface(REFIID riid, void** ppvObj) {
    if (riid == IID_IDirect3DTexture9 || riid == IID_IDirect3DBaseTexture9 || riid == IID_IDirect3DResource9 || riid == IID_IUnknown) {
        *ppvObj = (IDirect3DTexture9*)this;
        AddRef();
        return S_OK;
    }
    return m_pReal->QueryInterface(riid, ppvObj);
}

STDMETHODIMP_(ULONG) ProxyTexture9::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) ProxyTexture9::Release() {
    ULONG res = InterlockedDecrement(&m_refCount);
    if (res == 0) {
        delete this;
    }
    return res;
}

// IDirect3DResource9 methods
STDMETHODIMP ProxyTexture9::GetDevice(IDirect3DDevice9** ppDevice) {
    if (!ppDevice)
        return D3DERR_INVALIDCALL;
    *ppDevice = m_pProxyDevice;
    m_pProxyDevice->AddRef();
    return S_OK;
}

STDMETHODIMP ProxyTexture9::SetPrivateData(REFGUID refguid, const void* pData, DWORD SizeOfData, DWORD Flags) {
    return m_pReal->SetPrivateData(refguid, pData, SizeOfData, Flags);
}

STDMETHODIMP ProxyTexture9::GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) {
    return m_pReal->GetPrivateData(refguid, pData, pSizeOfData);
}

STDMETHODIMP ProxyTexture9::FreePrivateData(REFGUID refguid) {
    return m_pReal->FreePrivateData(refguid);
}

STDMETHODIMP_(DWORD) ProxyTexture9::SetPriority(DWORD PriorityNew) {
    return m_pReal->SetPriority(PriorityNew);
}

STDMETHODIMP_(DWORD) ProxyTexture9::GetPriority() {
    return m_pReal->GetPriority();
}

STDMETHODIMP_(void) ProxyTexture9::PreLoad() {
    m_pReal->PreLoad();
}

STDMETHODIMP_(D3DRESOURCETYPE) ProxyTexture9::GetType() {
    return D3DRTYPE_TEXTURE;
}

// IDirect3DBaseTexture9 methods
STDMETHODIMP_(DWORD) ProxyTexture9::SetLOD(DWORD LODNew) {
    return m_pReal->SetLOD(LODNew);
}

STDMETHODIMP_(DWORD) ProxyTexture9::GetLOD() {
    return m_pReal->GetLOD();
}

STDMETHODIMP_(DWORD) ProxyTexture9::GetLevelCount() {
    return m_levels;
}

STDMETHODIMP ProxyTexture9::SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) {
    return m_pReal->SetAutoGenFilterType(FilterType);
}

STDMETHODIMP_(D3DTEXTUREFILTERTYPE) ProxyTexture9::GetAutoGenFilterType() {
    return m_pReal->GetAutoGenFilterType();
}

STDMETHODIMP_(void) ProxyTexture9::GenerateMipSubLevels() {
    m_pReal->GenerateMipSubLevels();
}

// IDirect3DTexture9 methods
STDMETHODIMP ProxyTexture9::GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) {
    return m_pReal->GetLevelDesc(Level, pDesc);
}

STDMETHODIMP ProxyTexture9::GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) {
    if (!ppSurfaceLevel)
        return D3DERR_INVALIDCALL;

    IDirect3DSurface9* pRealSurf = nullptr;
    HRESULT hr = m_pReal->GetSurfaceLevel(Level, &pRealSurf);
    if (SUCCEEDED(hr) && pRealSurf) {
        D3DSURFACE_DESC desc;
        pRealSurf->GetDesc(&desc);

        *ppSurfaceLevel = new ProxySurface9(pRealSurf, m_pProxyDevice, desc.Width, desc.Height);
        pRealSurf->Release(); // Wrapper adds a reference, so release the extra real ref
    } else {
        *ppSurfaceLevel = nullptr;
    }
    return hr;
}

STDMETHODIMP ProxyTexture9::LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) {
    HRESULT hr = m_pReal->LockRect(Level, pLockedRect, pRect, Flags);
    if (SUCCEEDED(hr) && pLockedRect) {
        RecordLock(Level, *pLockedRect, Flags, pRect);
    }
    return hr;
}

STDMETHODIMP ProxyTexture9::UnlockRect(UINT Level) {
    if (Level == 0) {
        OnLevelUnlocked(0);
    }
    return m_pReal->UnlockRect(Level);
}

STDMETHODIMP ProxyTexture9::AddDirtyRect(const RECT* pDirtyRect) {
    return m_pReal->AddDirtyRect(pDirtyRect);
}
