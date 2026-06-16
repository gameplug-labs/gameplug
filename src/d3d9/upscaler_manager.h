#pragma once

#include <windows.h>
#include <string>
#include <filesystem>
#include "upscaler_interface.h"

namespace GamePlug {

class UpscalerManager {
public:
    static UpscalerManager& Get();

    bool LoadUpscaler(const std::string& name = "upscaler_d3d9.dll");
    void UnloadUpscaler();

    void InitUpscaler(void* device);
    void OnReset();
    void RenderFrame(void* device, void* source, void* target, uint32_t w, uint32_t h, uint32_t rw, uint32_t rh);

    bool IsUpscalingEnabled() const;
    bool IsNativeRenderingEnabled() const;
    int GetUpscaleQuality() const;

    void RenderUI(float fps, uint32_t width, uint32_t height);

    GamePlugUpscalerInterface* GetInterface() { return m_pInterface; }
    
    uint32_t GetVirtualWidth() const;
    uint32_t GetVirtualHeight() const;

private:
    UpscalerManager();
    ~UpscalerManager();

    HMODULE m_handle = nullptr;
    GamePlugUpscalerInterface* m_pInterface = nullptr;
    bool m_isShuttingDown = false;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;

    struct FallbackConfig {
        int type = 0;
        int quality = 2; // Default to Quality
        bool native = false;
    };
    mutable FallbackConfig m_fallbackCfg;
    mutable std::filesystem::file_time_type m_lastWriteTime;
    mutable bool m_hasCheckedWriteTime = false;
    void UpdateFallbackConfig() const;
};

} // namespace GamePlug

