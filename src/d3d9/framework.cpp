#include "common.h"
#include "config.h"
#include "framework_export.h"
#include "dx_overlay.h"
#include "dx_upscaler_manager.h"

extern void InitializeHooks();

namespace GamePlug {

void InitDX() {
    OutputDebugStringA("[GamePlug] framework_d3d9: InitDX starting...");
    GamePlug::Logger::Get().Init("gameplug.log");
    GamePlug::Logger::info("DirectX 9 Framework Initialized");
    Config::Get().Load();

    ::InitializeHooks();
    OutputDebugStringA("[GamePlug] framework_d3d9: InitDX complete (hooks installed).");
}
} // namespace GamePlug

extern "C" FRAMEWORK_API void StartFramework() {
    GamePlug::InitDX();
}



struct GamePlugSkyrimData {
    uint64_t frameCount;
    void* depthBuffer;
    void* motionVectorBuffer;
    void* sourceBuffer;
    float projectionMatrix[4][4];
    float prevProjectionMatrix[4][4];
    float viewMatrix[4][4];
    float prevViewMatrix[4][4];
    float jitterX;
    float jitterY;
    float cameraNear;
    float cameraFar;
    float cameraFov;
    float viewSpaceToMetersFactor;
    bool isVR;
    uint32_t activeEye;
};

extern "C" {
    FRAMEWORK_API void GamePlug_SetSkyrimData(const GamePlugSkyrimData* data) {
        static uint64_t callCount = 0;
        if (callCount++ % 1000 == 0) {
            GamePlug::Logger::info("GamePlug_SetSkyrimData: FrameCount={}, activeEye={}", 
                data ? data->frameCount : 0, data ? data->activeEye : 0);
        }
    }

    FRAMEWORK_API void GamePlug_GetResolutionOverride(uint32_t outputW, uint32_t outputH, uint32_t* renderW, uint32_t* renderH) {
        GamePlug::Logger::info("GamePlug_GetResolutionOverride: Requested display resolution: {}x{}", outputW, outputH);
        IDirect3DDevice9* device = GamePlug::UpscalerManager::Get().GetDevice();
        if (device) {
            IDirect3DSurface9* pRBB = nullptr;
            if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pRBB))) {
                D3DSURFACE_DESC desc;
                if (SUCCEEDED(pRBB->GetDesc(&desc))) {
                    outputW = desc.Width;
                    outputH = desc.Height;
                    GamePlug::Logger::info("GamePlug_GetResolutionOverride: Retrieved resolution from backbuffer description: {}x{}", outputW, outputH);
                }
                pRBB->Release();
            }
        }else{

            IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
            if (pD3D) {
                D3DDISPLAYMODE dm;
                if (SUCCEEDED(pD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &dm))) {
                    outputW = dm.Width;
                    outputH = dm.Height;
                    GamePlug::Logger::info("GamePlug_GetResolutionOverride: Retrieved resolution from adapter display mode: {}x{}", outputW, outputH);
                }
                pD3D->Release();
            }
        }

        // Update dimensions in UpscalerManager with the game's target resolution
        int rw = (int)outputW;
        int rh = (int)outputH;
        if (!GamePlug::Config::Get().GetBool("VKUpscaler", true)) {
            GamePlug::UpscalerManager::Get().LoadUpscaler();
        }else{
            GamePlug::UpscalerManager::Get().UpdateFallbackConfig();
        }
        GamePlug::UpscalerManager::Get().GetScaledResolution(rw, rh);
        
        if (renderW) *renderW = (uint32_t)rw;
        if (renderH) *renderH = (uint32_t)rh;
        
        GamePlug::Logger::info("GamePlug_GetResolutionOverride: Overridden render resolution: {}x{}", 
            renderW ? *renderW : 0, renderH ? *renderH : 0);
    }

    FRAMEWORK_API bool GamePlug_IsOverlayVisible() {
        bool visible = GamePlug::OverlayRenderer::Get().IsVisible();
        static bool lastVisible = false;
        if (visible != lastVisible) {
            GamePlug::Logger::info("GamePlug_IsOverlayVisible: Overlay visibility changed to {}", visible);
            lastVisible = visible;
        }
        return visible;
    }
}

