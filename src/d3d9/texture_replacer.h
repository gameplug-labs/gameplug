#pragma once
#include <d3d9.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

class ProxyTexture9;
class ProxySurface9;

namespace GamePlug {

class TextureReplacer {
public:
    static TextureReplacer& Get();

    void Init();
    void OnTextureCreated(ProxyTexture9* pTex);
    void OnTextureDestroyed(ProxyTexture9* pTex);
    void OnTextureBound(ProxyTexture9* pTex);

    void OnSurfaceCreated(ProxySurface9* pSurf);
    void OnSurfaceDestroyed(ProxySurface9* pSurf);

    bool IsProxyTexture(IDirect3DBaseTexture9* pTex);
    bool IsProxySurface(IDirect3DSurface9* pSurf);

    ProxyTexture9* FindProxyByReal(IDirect3DBaseTexture9* pReal);
    ProxySurface9* FindProxySurfaceByReal(IDirect3DSurface9* pReal);

    void RenderUI(IDirect3DDevice9* pDevice);

    std::recursive_mutex& GetMutex() { return m_mtx; }
    const std::unordered_set<ProxyTexture9*>& GetActiveTextures() const { return m_activeTextures; }
    const std::unordered_set<ProxySurface9*>& GetActiveSurfaces() const { return m_activeSurfaces; }

    bool IsReplacementEnabled() const { return m_enableReplacement; }
    bool IsAutoDumpEnabled() const { return m_enableAutoDump; }

    std::filesystem::path GetDumpsDir() const { return m_dumpsDir; }
    std::filesystem::path GetReplacementsDir() const { return m_replacementsDir; }

    // DDS load/save
    IDirect3DTexture9* LoadDDS(IDirect3DDevice9* pDevice, const std::filesystem::path& path);
    bool SaveDDS(const std::filesystem::path& path, const void* pBits, int pitch, UINT width, UINT height, D3DFORMAT format);

    void ReloadReplacements(IDirect3DDevice9* pDevice);
    void AutoDumpTexture(ProxyTexture9* pTex);

private:
    TextureReplacer() = default;
    ~TextureReplacer() = default;

    std::recursive_mutex m_mtx;
    bool m_initialized = false;

    std::filesystem::path m_baseDir;
    std::filesystem::path m_dumpsDir;
    std::filesystem::path m_replacementsDir;

    bool m_enableReplacement = true; // Default to true
    bool m_enableAutoDump = false;

    // Set of all active texture proxies
    std::unordered_set<ProxyTexture9*> m_activeTextures;
    std::unordered_set<ProxySurface9*> m_activeSurfaces;

    // List of active textures sorted by binding order
    std::vector<ProxyTexture9*> m_browserTextures;
    int m_selectedIndex = -1;

    void OpenFolderInExplorer(const std::filesystem::path& path);
};

} // namespace GamePlug
