#include "texture_replacer.h"
#include "d3d9_proxy_device.h"
#include "d3d9_proxy_surface.h"
#include "d3d9_proxy_texture.h"
#include "imgui.h"
#include "logger.h"
#include <algorithm>
#include <fstream>
#include <shellapi.h>
#include <thread>
#include <windows.h>

namespace GamePlug {

struct DDS_PIXELFORMAT {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwABitMask;
};

struct DDS_HEADER {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    DWORD dwPitchOrLinearSize;
    DWORD dwDepth;
    DWORD dwMipMapCount;
    DWORD dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    DWORD dwCaps;
    DWORD dwCaps2;
    DWORD dwCaps3;
    DWORD dwCaps4;
    DWORD dwReserved2;
};

#define DDSD_CAPS 0x00000001
#define DDSD_HEIGHT 0x00000002
#define DDSD_WIDTH 0x00000004
#define DDSD_PITCH 0x00000008
#define DDSD_PIXELFORMAT 0x00001000
#define DDSD_MIPMAPCOUNT 0x00020000
#define DDSD_LINEARSIZE 0x00080000

#define DDPF_ALPHAPIXELS 0x00000001
#define DDPF_ALPHA 0x00000002
#define DDPF_FOURCC 0x00000004
#define DDPF_RGB 0x00000040

#define DDSCAPS_COMPLEX 0x00000008
#define DDSCAPS_TEXTURE 0x00001000
#define DDSCAPS_MIPMAP 0x00400000

TextureReplacer& TextureReplacer::Get() {
    static TextureReplacer instance;
    return instance;
}

void TextureReplacer::Init() {
    std::lock_guard<std::recursive_mutex> lock(m_mtx);
    if (m_initialized)
        return;

    // Get current module path
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path exePath(buf);
    m_baseDir = exePath.parent_path() / "GamePlug";
    m_dumpsDir = m_baseDir / "textures" / "dumps";
    m_replacementsDir = m_baseDir / "textures" / "replacements";

    std::error_code ec;
    std::filesystem::create_directories(m_dumpsDir, ec);
    std::filesystem::create_directories(m_replacementsDir, ec);

    m_initialized = true;
    Logger::info("TextureReplacer: Initialized. Base dir: {}", m_baseDir.string());
}

void TextureReplacer::OnTextureCreated(ProxyTexture9* pTex) {
    std::lock_guard<std::recursive_mutex> lock(m_mtx);
    m_activeTextures.insert(pTex);
}

void TextureReplacer::OnTextureDestroyed(ProxyTexture9* pTex) {
    std::lock_guard<std::recursive_mutex> lock(m_mtx);
    m_activeTextures.erase(pTex);

    auto it = std::find(m_browserTextures.begin(), m_browserTextures.end(), pTex);
    if (it != m_browserTextures.end()) {
        m_browserTextures.erase(it);
        if (m_selectedIndex >= (int)m_browserTextures.size()) {
            m_selectedIndex = (int)m_browserTextures.size() - 1;
        }
        if (m_browserTextures.empty()) {
            m_selectedIndex = -1;
        }
    }
}

void TextureReplacer::OnTextureBound(ProxyTexture9* pTex) {
    if (!pTex)
        return;
    if (pTex->GetUsage() & D3DUSAGE_RENDERTARGET) {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(m_mtx);
    auto it = std::find(m_browserTextures.begin(), m_browserTextures.end(), pTex);
    if (it == m_browserTextures.end()) {
        m_browserTextures.push_back(pTex);
        if (m_selectedIndex == -1) {
            m_selectedIndex = 0;
        }
    }

    if (m_enableAutoDump) {
        AutoDumpTexture(pTex);
    }
}

void TextureReplacer::OnSurfaceCreated(ProxySurface9* pSurf) {
    std::lock_guard<std::recursive_mutex> lock(m_mtx);
    m_activeSurfaces.insert(pSurf);
}

void TextureReplacer::OnSurfaceDestroyed(ProxySurface9* pSurf) {
    std::lock_guard<std::recursive_mutex> lock(m_mtx);
    m_activeSurfaces.erase(pSurf);
}

bool TextureReplacer::IsProxyTexture(IDirect3DBaseTexture9* pTex) {
    if (!pTex)
        return false;
    std::lock_guard<std::recursive_mutex> lock(m_mtx);
    return m_activeTextures.count((ProxyTexture9*)pTex) > 0;
}

bool TextureReplacer::IsProxySurface(IDirect3DSurface9* pSurf) {
    if (!pSurf)
        return false;
    std::lock_guard<std::recursive_mutex> lock(m_mtx);
    return m_activeSurfaces.count((ProxySurface9*)pSurf) > 0;
}

ProxySurface9* TextureReplacer::FindProxySurfaceByReal(IDirect3DSurface9* pReal) {
    if (!pReal)
        return nullptr;
    std::lock_guard<std::recursive_mutex> lock(m_mtx);
    for (auto* pProxy : m_activeSurfaces) {
        if (pProxy->GetInternalSurface() == pReal) {
            pProxy->AddRef();
            return pProxy;
        }
    }
    return nullptr;
}

ProxyTexture9* TextureReplacer::FindProxyByReal(IDirect3DBaseTexture9* pReal) {
    std::lock_guard<std::recursive_mutex> lock(m_mtx);
    for (auto* pProxy : m_activeTextures) {
        if (pProxy->GetRealTexture() == pReal || pProxy->GetReplacedReal() == pReal) {
            pProxy->AddRef();
            return pProxy;
        }
    }
    return nullptr;
}

void TextureReplacer::AutoDumpTexture(ProxyTexture9* pTex) {
    if (!pTex || !pTex->IsHashComputed())
        return;

    char hashStr[16];
    sprintf_s(hashStr, sizeof(hashStr), "%08X", pTex->GetHash());
    std::filesystem::path dumpPath = m_dumpsDir / (std::string(hashStr) + ".dds");

    std::error_code ec;
    if (std::filesystem::exists(dumpPath, ec))
        return; // already dumped

    IDirect3DTexture9* pReal = pTex->GetRealTexture();
    D3DLOCKED_RECT locked;
    if (SUCCEEDED(pReal->LockRect(0, &locked, NULL, D3DLOCK_READONLY))) {
        if (SaveDDS(dumpPath, locked.pBits, locked.Pitch, pTex->GetWidth(), pTex->GetHeight(), pTex->GetFormat())) {
            Logger::info("AutoDump: Saved {}x{} texture to {}", pTex->GetWidth(), pTex->GetHeight(), dumpPath.string());
        } else {
            Logger::error("AutoDump: Failed to write DDS for hash {:08X}", pTex->GetHash());
        }
        pReal->UnlockRect(0);
    } else {
        Logger::error(
            "AutoDump: Failed to lock texture {:08X} (format={}, pool={})", pTex->GetHash(), (int)pTex->GetFormat(), (int)pTex->GetPool());
    }
}

void TextureReplacer::ReloadReplacements(IDirect3DDevice9* pDevice) {
    std::vector<ProxyTexture9*> texturesToReload;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mtx);
        texturesToReload.assign(m_activeTextures.begin(), m_activeTextures.end());
        for (auto* pTex : texturesToReload) {
            pTex->AddRef();
        }
    }

    Logger::info("TextureReplacer: Reloading replacements for {} active textures...", texturesToReload.size());
    for (auto* pTex : texturesToReload) {
        pTex->FreeReplacedTexture();
        pTex->CheckAndApplyReplacement();
        pTex->Release();
    }
}

void TextureReplacer::OpenFolderInExplorer(const std::filesystem::path& path) {
    // ShellExecuteA must NOT be called on the D3D9 render thread — it initializes
    // COM and pumps window messages which can crash the render thread.
    std::string pathStr = path.string();
    std::thread([pathStr]() { ShellExecuteA(NULL, "open", pathStr.c_str(), NULL, NULL, SW_SHOWNORMAL); }).detach();
}

bool TextureReplacer::SaveDDS(const std::filesystem::path& path, const void* pBits, int pitch, UINT width, UINT height, D3DFORMAT format) {
    // Build header and validate format BEFORE creating the file
    DDS_HEADER header = {};
    header.dwSize = sizeof(DDS_HEADER);
    header.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
    header.dwHeight = height;
    header.dwWidth = width;
    header.dwCaps = DDSCAPS_TEXTURE;

    UINT rowBytes = 0;
    UINT rowCount = height;

    if (format == D3DFMT_A8R8G8B8) {
        header.ddspf.dwSize = sizeof(DDS_PIXELFORMAT);
        header.ddspf.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
        header.ddspf.dwRGBBitCount = 32;
        header.ddspf.dwRBitMask = 0x00FF0000;
        header.ddspf.dwGBitMask = 0x0000FF00;
        header.ddspf.dwBBitMask = 0x000000FF;
        header.ddspf.dwABitMask = 0xFF000000;
        rowBytes = width * 4;
    } else if (format == D3DFMT_X8R8G8B8) {
        header.ddspf.dwSize = sizeof(DDS_PIXELFORMAT);
        header.ddspf.dwFlags = DDPF_RGB;
        header.ddspf.dwRGBBitCount = 32;
        header.ddspf.dwRBitMask = 0x00FF0000;
        header.ddspf.dwGBitMask = 0x0000FF00;
        header.ddspf.dwBBitMask = 0x000000FF;
        header.ddspf.dwABitMask = 0x00000000;
        rowBytes = width * 4;
    } else if (format == D3DFMT_DXT1) {
        header.ddspf.dwSize = sizeof(DDS_PIXELFORMAT);
        header.ddspf.dwFlags = DDPF_FOURCC;
        header.ddspf.dwFourCC = D3DFMT_DXT1;
        header.dwFlags |= DDSD_LINEARSIZE;
        rowBytes = ((width + 3) / 4) * 8;
        rowCount = (height + 3) / 4;
        header.dwPitchOrLinearSize = rowBytes * rowCount;
    } else if (format == D3DFMT_DXT3) {
        header.ddspf.dwSize = sizeof(DDS_PIXELFORMAT);
        header.ddspf.dwFlags = DDPF_FOURCC;
        header.ddspf.dwFourCC = D3DFMT_DXT3;
        header.dwFlags |= DDSD_LINEARSIZE;
        rowBytes = ((width + 3) / 4) * 16;
        rowCount = (height + 3) / 4;
        header.dwPitchOrLinearSize = rowBytes * rowCount;
    } else if (format == D3DFMT_DXT5) {
        header.ddspf.dwSize = sizeof(DDS_PIXELFORMAT);
        header.ddspf.dwFlags = DDPF_FOURCC;
        header.ddspf.dwFourCC = D3DFMT_DXT5;
        header.dwFlags |= DDSD_LINEARSIZE;
        rowBytes = ((width + 3) / 4) * 16;
        rowCount = (height + 3) / 4;
        header.dwPitchOrLinearSize = rowBytes * rowCount;
    } else {
        // Unsupported format — do NOT create the file
        return false;
    }

    // Format is valid — now open the file and write
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;

    DWORD magic = 0x20534444; // "DDS "
    f.write((const char*)&magic, sizeof(magic));
    f.write((const char*)&header, sizeof(header));

    const uint8_t* src = (const uint8_t*)pBits;
    for (UINT y = 0; y < rowCount; ++y) {
        f.write((const char*)(src + y * pitch), rowBytes);
    }

    return true;
}

IDirect3DTexture9* TextureReplacer::LoadDDS(IDirect3DDevice9* pDevice, const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return nullptr;

    DWORD magic = 0;
    f.read((char*)&magic, sizeof(magic));
    if (magic != 0x20534444)
        return nullptr;

    DDS_HEADER header = {};
    f.read((char*)&header, sizeof(header));

    D3DFORMAT fmt = D3DFMT_UNKNOWN;

    if (header.ddspf.dwFlags & DDPF_FOURCC) {
        fmt = (D3DFORMAT)header.ddspf.dwFourCC;
    } else if (header.ddspf.dwFlags & DDPF_RGB) {
        if (header.ddspf.dwRGBBitCount == 32) {
            if (header.ddspf.dwABitMask == 0xFF000000) {
                fmt = D3DFMT_A8R8G8B8;
            } else {
                fmt = D3DFMT_X8R8G8B8;
            }
        }
    }

    if (fmt == D3DFMT_UNKNOWN)
        return nullptr;

    UINT mips = (header.dwFlags & DDSD_MIPMAPCOUNT) ? header.dwMipMapCount : 1;
    if (mips == 0)
        mips = 1;

    IDirect3DTexture9* pNewTex = nullptr;
    HRESULT hr = pDevice->CreateTexture(header.dwWidth, header.dwHeight, mips, 0, fmt, D3DPOOL_MANAGED, &pNewTex, NULL);
    if (FAILED(hr) || !pNewTex) {
        Logger::error("TextureReplacer: LoadDDS failed to create texture (w={}, h={}, mips={}, format={})", header.dwWidth, header.dwHeight,
            mips, (int)fmt);
        return nullptr;
    }

    // Read pixel data level by level
    for (UINT level = 0; level < mips; ++level) {
        UINT w = (std::max)((UINT)1, (UINT)(header.dwWidth >> level));
        UINT h = (std::max)((UINT)1, (UINT)(header.dwHeight >> level));

        UINT rowBytes = 0;
        UINT rowCount = h;

        if (fmt == D3DFMT_A8R8G8B8 || fmt == D3DFMT_X8R8G8B8) {
            rowBytes = w * 4;
        } else if (fmt == D3DFMT_DXT1) {
            rowBytes = ((w + 3) / 4) * 8;
            rowCount = (h + 3) / 4;
        } else if (fmt == D3DFMT_DXT3 || fmt == D3DFMT_DXT5) {
            rowBytes = ((w + 3) / 4) * 16;
            rowCount = (h + 3) / 4;
        } else {
            pNewTex->Release();
            return nullptr;
        }

        UINT levelSize = rowBytes * rowCount;
        std::vector<uint8_t> tempBuf(levelSize);
        f.read((char*)tempBuf.data(), levelSize);

        D3DLOCKED_RECT locked;
        if (SUCCEEDED(pNewTex->LockRect(level, &locked, NULL, 0))) {
            uint8_t* dst = (uint8_t*)locked.pBits;
            for (UINT y = 0; y < rowCount; ++y) {
                memcpy(dst + y * locked.Pitch, tempBuf.data() + y * rowBytes, rowBytes);
            }
            pNewTex->UnlockRect(level);
        } else {
            Logger::error("TextureReplacer: LoadDDS failed to lock level {}", level);
            pNewTex->Release();
            return nullptr;
        }
    }

    return pNewTex;
}

void TextureReplacer::RenderUI(IDirect3DDevice9* pDevice) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.7f, 1.0f, 1.0f));
    ImGui::Text("Texture Replacer & Dumper");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (ImGui::Checkbox("Enable Texture Replacement", &m_enableReplacement)) {
        if (!m_enableReplacement) {
            std::vector<ProxyTexture9*> texturesToFree;
            {
                std::lock_guard<std::recursive_mutex> lock(m_mtx);
                texturesToFree.assign(m_activeTextures.begin(), m_activeTextures.end());
                for (auto* pTex : texturesToFree) {
                    pTex->AddRef();
                }
            }
            for (auto* pTex : texturesToFree) {
                pTex->FreeReplacedTexture();
                pTex->Release();
            }
        }
    }

    if (ImGui::Checkbox("Auto Dump Textures to Disk", &m_enableAutoDump)) {
        if (m_enableAutoDump) {
            // Dump all currently tracked textures that already have hashes
            std::vector<ProxyTexture9*> toSnapshot;
            {
                std::lock_guard<std::recursive_mutex> lock(m_mtx);
                toSnapshot = m_browserTextures;
                for (auto* pTex : toSnapshot)
                    pTex->AddRef();
            }
            for (auto* pTex : toSnapshot) {
                AutoDumpTexture(pTex);
                pTex->Release();
            }
        }
    }
    ImGui::Spacing();

    if (ImGui::Button("Open Dumps Folder")) {
        OpenFolderInExplorer(m_dumpsDir);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Replacements Folder")) {
        OpenFolderInExplorer(m_replacementsDir);
    }

    if (ImGui::Button("Reload Replacements")) {
        ReloadReplacements(pDevice);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Texture Browser");

    ProxyTexture9* selected = nullptr;
    int currentSelectedIndex = -1;
    int totalTextures = 0;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mtx);
        totalTextures = (int)m_browserTextures.size();
        if (totalTextures > 0) {
            if (m_selectedIndex < 0 || m_selectedIndex >= totalTextures) {
                m_selectedIndex = 0;
            }
            currentSelectedIndex = m_selectedIndex;
            selected = m_browserTextures[currentSelectedIndex];
            selected->AddRef();
        }
    }

    if (!selected) {
        ImGui::TextDisabled("No active textures bound yet.");
        return;
    }

    if (ImGui::Button("Previous")) {
        std::lock_guard<std::recursive_mutex> lock(m_mtx);
        if (!m_browserTextures.empty()) {
            if (m_selectedIndex > 0)
                m_selectedIndex--;
            else
                m_selectedIndex = (int)m_browserTextures.size() - 1;
            currentSelectedIndex = m_selectedIndex;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Next")) {
        std::lock_guard<std::recursive_mutex> lock(m_mtx);
        if (!m_browserTextures.empty()) {
            if (m_selectedIndex < (int)m_browserTextures.size() - 1)
                m_selectedIndex++;
            else
                m_selectedIndex = 0;
            currentSelectedIndex = m_selectedIndex;
        }
    }

    ProxyTexture9* nextSelected = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mtx);
        totalTextures = (int)m_browserTextures.size();
        if (totalTextures > 0) {
            if (m_selectedIndex < 0 || m_selectedIndex >= totalTextures) {
                m_selectedIndex = 0;
            }
            currentSelectedIndex = m_selectedIndex;
            nextSelected = m_browserTextures[currentSelectedIndex];
            nextSelected->AddRef();
        }
    }

    selected->Release();
    selected = nextSelected;

    if (!selected) {
        ImGui::TextDisabled("No active textures bound yet.");
        return;
    }

    ImGui::Text("Index: %d / %d", currentSelectedIndex + 1, totalTextures);
    ImGui::Text("Dimensions: %d x %d (Mips: %d)", selected->GetWidth(), selected->GetHeight(), selected->GetLevelCount());

    const char* fmtStr = "Unknown";
    switch (selected->GetFormat()) {
    case D3DFMT_A8R8G8B8:
        fmtStr = "A8R8G8B8";
        break;
    case D3DFMT_X8R8G8B8:
        fmtStr = "X8R8G8B8";
        break;
    case D3DFMT_R8G8B8:
        fmtStr = "R8G8B8";
        break;
    case D3DFMT_A1R5G5B5:
        fmtStr = "A1R5G5B5";
        break;
    case D3DFMT_X1R5G5B5:
        fmtStr = "X1R5G5B5";
        break;
    case D3DFMT_R5G6B5:
        fmtStr = "R5G6B5";
        break;
    case D3DFMT_A8:
        fmtStr = "A8";
        break;
    case D3DFMT_L8:
        fmtStr = "L8";
        break;
    case D3DFMT_DXT1:
        fmtStr = "DXT1";
        break;
    case D3DFMT_DXT2:
        fmtStr = "DXT2";
        break;
    case D3DFMT_DXT3:
        fmtStr = "DXT3";
        break;
    case D3DFMT_DXT4:
        fmtStr = "DXT4";
        break;
    case D3DFMT_DXT5:
        fmtStr = "DXT5";
        break;
    }
    ImGui::Text("Format: %s", fmtStr);

    if (selected->IsHashComputed()) {
        ImGui::Text("Hash: %08X", selected->GetHash());

        if (ImGui::Button("Manual Dump")) {
            char hashStr[16];
            sprintf_s(hashStr, sizeof(hashStr), "%08X", selected->GetHash());
            std::filesystem::path dumpPath = m_dumpsDir / (std::string(hashStr) + ".dds");

            IDirect3DTexture9* pReal = selected->GetRealTexture();
            D3DLOCKED_RECT locked;
            if (SUCCEEDED(pReal->LockRect(0, &locked, NULL, D3DLOCK_READONLY))) {
                if (SaveDDS(dumpPath, locked.pBits, locked.Pitch, selected->GetWidth(), selected->GetHeight(), selected->GetFormat())) {
                    Logger::info("Manual Dump: Saved texture to {}", dumpPath.string());
                } else {
                    Logger::error("Manual Dump: Failed to write DDS file.");
                }
                pReal->UnlockRect(0);
            } else {
                Logger::error("Manual Dump: Failed to lock texture for reading.");
            }
        }
    } else {
        if (selected->GetUsage() & D3DUSAGE_RENDERTARGET) {
            ImGui::Text("Hash: Render Target (Dynamic)");
        } else if (selected->GetUsage() & D3DUSAGE_DYNAMIC) {
            ImGui::Text("Hash: Dynamic Texture");
        } else {
            ImGui::Text("Hash: Not Computed (Never Locked)");
        }
    }

    IDirect3DTexture9* imgTex = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mtx);
        imgTex = selected->GetReplacedReal();
        if (imgTex) {
            imgTex->AddRef();
        }
    }

    if (imgTex) {
        float maxDim = 200.0f;
        float w = (float)selected->GetWidth();
        float h = (float)selected->GetHeight();
        float aspect = w / h;
        ImVec2 displaySize;
        if (w > h) {
            displaySize.x = maxDim;
            displaySize.y = maxDim / aspect;
        } else {
            displaySize.x = maxDim * aspect;
            displaySize.y = maxDim;
        }
        ImGui::Image((ImTextureID)imgTex, displaySize);
        imgTex->Release();
    }

    selected->Release();
}

} // namespace GamePlug
