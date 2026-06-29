#include "common.h"
#include "config.h"
#include "framework_export.h"
#ifdef SKYRIM_AE
#include "skyrim_bridge_api.h"
#include "upscaler_manager.h"
#endif

namespace GamePlug {
extern void InstallDXGIHooks();
#ifdef SKYRIM_AE
extern IDXGISwapChain* GetCurrentDXSwapChain();
extern bool IsOverlayVisible();
#endif

void InitDX() {
    OutputDebugStringA("[GamePlug] framework.dll: InitDX starting...");
    Logger::Get().Init("gameplug.log");
    Logger::info("DirectX Framework Initialized");
    Config::Get().Load();

    InstallDXGIHooks();
    OutputDebugStringA("[GamePlug] framework.dll: InitDX complete (hooks installed).");
}
} // namespace GamePlug

extern "C" FRAMEWORK_API void StartFramework() {
    GamePlug::InitDX();
}

#ifdef SKYRIM_AE
extern "C" FRAMEWORK_API void GamePlug_SetSkyrimData(const GamePlugSkyrimData* data) {
    GamePlug::DXUpscalerManager::Get().SetSkyrimData(data);
}

extern "C" FRAMEWORK_API void GamePlug_GetResolutionOverride(uint32_t outputW, uint32_t outputH, uint32_t* renderW, uint32_t* renderH) {
    if (renderW && renderH) {
        GamePlug::Logger::info(
            "GamePlug_GetResolutionOverride called with outputW=" + std::to_string(outputW) + ", outputH=" + std::to_string(outputH));
        GamePlug::DXUpscalerManager::Get().SetSkyrimActive(true);
        GamePlug::Logger::info("GamePlug_GetResolutionOverride::GetCurrentDXSwapChain");
        // Use the SwapChain's actual size (the final presentation target) as the display resolution
        IDXGISwapChain* swapChain = GamePlug::GetCurrentDXSwapChain();
        GamePlug::Logger::info("GamePlug_GetResolutionOverride::swapChain");
        if (swapChain) {
            GamePlug::Logger::info("GamePlug_GetResolutionOverride::swapChain->GetDesc");
            DXGI_SWAP_CHAIN_DESC sd;
            if (SUCCEEDED(swapChain->GetDesc(&sd))) {
                GamePlug::Logger::info("GamePlug_GetResolutionOverride::swapChain->GetDesc success");
                outputW = sd.BufferDesc.Width;
                outputH = sd.BufferDesc.Height;
                GamePlug::Logger::info(
                    "GamePlug_GetResolutionOverride: SwapChain detected. Overriding output resolution to SwapChain size: " +
                    std::to_string(outputW) + "x" + std::to_string(outputH));
            } else {
                GamePlug::Logger::info("GamePlug_GetResolutionOverride::swapChain->GetDesc failed");
            }
        } else {
            // SwapChain not yet available — fall back to display dimensions already cached
            // by HookedGetBuffer/CreateFakeBackBuffer, which runs before this call.
            uint32_t knownW = GamePlug::DXUpscalerManager::Get().GetDisplayWidth();
            uint32_t knownH = GamePlug::DXUpscalerManager::Get().GetDisplayHeight();
            if (knownW > 0 && knownH > 0) {
                outputW = knownW;
                outputH = knownH;
                GamePlug::Logger::info("GamePlug_GetResolutionOverride: SwapChain NULL, using cached display: " +
                    std::to_string(outputW) + "x" + std::to_string(outputH));
            } else {
                GamePlug::Logger::warn("GamePlug_GetResolutionOverride: SwapChain is NULL. Using passed resolution parameters.");
            }
        }
        GamePlug::Logger::info("GamePlug_GetResolutionOverride::UpdateDimensions");
        GamePlug::DXUpscalerManager::Get().UpdateDimensions(outputW, outputH);
        *renderW = GamePlug::DXUpscalerManager::Get().GetRenderWidth();
        *renderH = GamePlug::DXUpscalerManager::Get().GetRenderHeight();

        GamePlug::Logger::info(
            "GamePlug_GetResolutionOverride returning renderW=" + std::to_string(*renderW) + ", renderH=" + std::to_string(*renderH));

        // Recreate the Fake BackBuffer to match the new overridden dimensions
        if (swapChain) {
            GamePlug::DXUpscalerManager::Get().CreateFakeBackBuffer(swapChain);
        }
        GamePlug::Logger::info("GamePlug_GetResolutionOverride::End");
    }
}

extern "C" FRAMEWORK_API bool GamePlug_IsOverlayVisible() {
    return GamePlug::IsOverlayVisible();
}

extern "C" FRAMEWORK_API ID3D11RenderTargetView* GamePlug_BeginUI() {
    return GamePlug::DXUpscalerManager::Get().BeginUI();
}

extern "C" FRAMEWORK_API void GamePlug_EndUI() {
    GamePlug::DXUpscalerManager::Get().EndUI();
}

namespace GamePlug {
    extern ID3D11RenderTargetView* g_mainRenderTargetView;
    extern ID3D11DeviceContext* g_pd3dDeviceContext;
}

extern "C" FRAMEWORK_API void GamePlug_TriggerUpscale() {
    static uint64_t s_triggerCount = 0;
    bool shouldLog = (s_triggerCount++ % 1000 == 0);

    if (shouldLog) {
        GamePlug::Logger::info("GamePlug_TriggerUpscale invoked [Count=" + std::to_string(s_triggerCount) + 
            "] RTV=" + std::to_string((uintptr_t)GamePlug::g_mainRenderTargetView) + 
            " Ctx=" + std::to_string((uintptr_t)GamePlug::g_pd3dDeviceContext) +
            " Enabled=" + std::to_string(GamePlug::DXUpscalerManager::Get().IsUpscalingEnabled()));
    }

    if (GamePlug::g_mainRenderTargetView && GamePlug::g_pd3dDeviceContext && GamePlug::DXUpscalerManager::Get().IsUpscalingEnabled()) {
        GamePlug::Logger::info("GamePlug_TriggerUpscale RenderFrameDX11");
        GamePlug::DXUpscalerManager::Get().RenderFrameDX11(
            GamePlug::g_pd3dDeviceContext,
            nullptr, // sourceSRV is overridden inside RenderFrameDX11 with m_skyrimSourceSRV
            GamePlug::g_mainRenderTargetView,
            GamePlug::DXUpscalerManager::Get().GetDisplayWidth(),
            GamePlug::DXUpscalerManager::Get().GetDisplayHeight()
        );
    }
}
#endif
