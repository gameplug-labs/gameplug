#pragma once
#include "framework_export.h"
#include <d3d11.h>
#include <stdint.h>

extern "C" {

struct GamePlugFallout4Matrix {
    float m[4][4];
};

struct GamePlugFallout4Data {
    uint64_t frameCount;

    ID3D11Texture2D* depthBuffer;
    ID3D11Texture2D* motionVectorBuffer;
    ID3D11Texture2D* sourceBuffer;

    GamePlugFallout4Matrix projectionMatrix;
    GamePlugFallout4Matrix prevProjectionMatrix;
    GamePlugFallout4Matrix viewMatrix;
    GamePlugFallout4Matrix prevViewMatrix;

    float jitterX;
    float jitterY;

    float cameraNear;
    float cameraFar;
    float cameraFov;
    float viewSpaceToMetersFactor;
};

FRAMEWORK_API void GamePlug_SetFallout4Data(const GamePlugFallout4Data* data);
FRAMEWORK_API void GamePlug_GetFallout4ResolutionOverride(uint32_t outputW, uint32_t outputH, uint32_t* renderW, uint32_t* renderH);
FRAMEWORK_API bool GamePlug_IsFallout4OverlayVisible();
}
