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

        // Use the SwapChain's actual size (the final presentation target) as the display resolution
        IDXGISwapChain* swapChain = GamePlug::GetCurrentDXSwapChain();
        if (swapChain) {
            DXGI_SWAP_CHAIN_DESC sd;
            if (SUCCEEDED(swapChain->GetDesc(&sd))) {
                outputW = sd.BufferDesc.Width;
                outputH = sd.BufferDesc.Height;
                GamePlug::Logger::info(
                    "GamePlug_GetResolutionOverride: SwapChain detected. Overriding output resolution to SwapChain size: " +
                    std::to_string(outputW) + "x" + std::to_string(outputH));
            }
        } else {
            GamePlug::Logger::warn("GamePlug_GetResolutionOverride: SwapChain is NULL. Using passed resolution parameters.");
        }

        GamePlug::DXUpscalerManager::Get().UpdateDimensions(outputW, outputH);
        *renderW = GamePlug::DXUpscalerManager::Get().GetRenderWidth();
        *renderH = GamePlug::DXUpscalerManager::Get().GetRenderHeight();

        GamePlug::Logger::info(
            "GamePlug_GetResolutionOverride returning renderW=" + std::to_string(*renderW) + ", renderH=" + std::to_string(*renderH));

        // Recreate the Fake BackBuffer to match the new overridden dimensions
        if (swapChain) {
            GamePlug::DXUpscalerManager::Get().CreateFakeBackBuffer(swapChain);
        }
    }
}

extern "C" FRAMEWORK_API bool GamePlug_IsOverlayVisible() {
    return GamePlug::IsOverlayVisible();
}
#endif
