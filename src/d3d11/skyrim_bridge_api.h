#pragma once
#include "framework_export.h"
#include <d3d11.h>
#include <stdint.h>

extern "C" {

struct GamePlugMatrix {
    float m[4][4];
};

struct GamePlugSkyrimData {
    uint64_t frameCount;

    ID3D11Texture2D* depthBuffer;
    ID3D11Texture2D* motionVectorBuffer;
    ID3D11Texture2D* sourceBuffer;

    GamePlugMatrix projectionMatrix;
    GamePlugMatrix prevProjectionMatrix;
    GamePlugMatrix viewMatrix;
    GamePlugMatrix prevViewMatrix;

    float jitterX;
    float jitterY;

    float cameraNear;
    float cameraFar;

    bool isVR;
    uint32_t activeEye; // 0 = Left/Flat, 1 = Right
};

FRAMEWORK_API void GamePlug_SetSkyrimData(const GamePlugSkyrimData* data);
FRAMEWORK_API void GamePlug_GetResolutionOverride(uint32_t outputW, uint32_t outputH, uint32_t* renderW, uint32_t* renderH);
FRAMEWORK_API bool GamePlug_IsOverlayVisible();
}