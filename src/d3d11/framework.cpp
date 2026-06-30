#include "common.h"
#include "config.h"
#include "framework_export.h"
#ifdef FALLOUT4
#include "fallout4_bridge_api.h"
#include "upscaler_manager.h"
#endif
namespace GamePlug {
extern void InstallDXGIHooks();

#ifdef FALLOUT4
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


#ifdef FALLOUT4
extern "C" FRAMEWORK_API void GamePlug_SetFallout4Data(const GamePlugFallout4Data* data) {
    GamePlug::DXUpscalerManager::Get().SetFallout4Data(data);
}

extern "C" FRAMEWORK_API void GamePlug_GetFallout4ResolutionOverride(uint32_t outputW, uint32_t outputH, uint32_t* renderW, uint32_t* renderH) {
    if (renderW && renderH) {
        GamePlug::Logger::info(
            "GamePlug_GetFallout4ResolutionOverride called with outputW=" + std::to_string(outputW) + ", outputH=" + std::to_string(outputH));
        GamePlug::DXUpscalerManager::Get().SetFallout4Active(true);
        GamePlug::Logger::info("GamePlug_GetFallout4ResolutionOverride::GetCurrentDXSwapChain");
        IDXGISwapChain* swapChain = GamePlug::GetCurrentDXSwapChain();
        GamePlug::Logger::info("GamePlug_GetFallout4ResolutionOverride::swapChain");
        if (swapChain) {
            GamePlug::Logger::info("GamePlug_GetFallout4ResolutionOverride::swapChain->GetDesc");
            DXGI_SWAP_CHAIN_DESC sd;
            if (SUCCEEDED(swapChain->GetDesc(&sd))) {
                GamePlug::Logger::info("GamePlug_GetFallout4ResolutionOverride::swapChain->GetDesc success");
                outputW = sd.BufferDesc.Width;
                outputH = sd.BufferDesc.Height;
                GamePlug::Logger::info(
                    "GamePlug_GetFallout4ResolutionOverride: SwapChain detected. Overriding output resolution to SwapChain size: " +
                    std::to_string(outputW) + "x" + std::to_string(outputH));
            } else {
                GamePlug::Logger::info("GamePlug_GetFallout4ResolutionOverride::swapChain->GetDesc failed");
            }
        } else {
            uint32_t knownW = GamePlug::DXUpscalerManager::Get().GetDisplayWidth();
            uint32_t knownH = GamePlug::DXUpscalerManager::Get().GetDisplayHeight();
            if (knownW > 0 && knownH > 0) {
                outputW = knownW;
                outputH = knownH;
                GamePlug::Logger::info("GamePlug_GetFallout4ResolutionOverride: SwapChain NULL, using cached display: " +
                    std::to_string(outputW) + "x" + std::to_string(outputH));
            } else {
                GamePlug::Logger::warn("GamePlug_GetFallout4ResolutionOverride: SwapChain is NULL. Using passed resolution parameters.");
            }
        }
        GamePlug::Logger::info("GamePlug_GetFallout4ResolutionOverride::UpdateDimensions");
        GamePlug::DXUpscalerManager::Get().UpdateDimensions(outputW, outputH);
        *renderW = GamePlug::DXUpscalerManager::Get().GetRenderWidth();
        *renderH = GamePlug::DXUpscalerManager::Get().GetRenderHeight();

        GamePlug::Logger::info(
            "GamePlug_GetFallout4ResolutionOverride returning renderW=" + std::to_string(*renderW) + ", renderH=" + std::to_string(*renderH));

        if (swapChain) {
            GamePlug::DXUpscalerManager::Get().CreateFakeBackBuffer(swapChain);
        }
        GamePlug::Logger::info("GamePlug_GetFallout4ResolutionOverride::End");
    }
}

extern "C" FRAMEWORK_API bool GamePlug_IsFallout4OverlayVisible() {
    return GamePlug::IsOverlayVisible();
}
#endif
