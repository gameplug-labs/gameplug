#pragma once
#include "framework_export.h"
#include "upscaler_interface.h"
#include <d3d11.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace GamePlug {

class FRAMEWORK_API DXUpscalerManager {
public:
    static DXUpscalerManager& Get() {
        static DXUpscalerManager instance;
        return instance;
    }

    ~DXUpscalerManager();

    void InitDX11(ID3D11Device* device, ID3D11DeviceContext* context);
    void UnloadUpscaler();
    void CleanupPlugin();
    void CreateFakeBackBuffer(IDXGISwapChain* swapChain);
    void DestroyFakeBackBuffer();
    ID3D11Texture2D* GetFakeBackBuffer() { return m_fakeBackBuffer; }
    ID3D11ShaderResourceView* GetFakeBackBufferSRV() { return m_fakeBackBufferSRV; }
    ID3D11Device* GetDevice() { return m_pd3dDevice; }

    void RenderUI(float fps, uint32_t width, uint32_t height);

    // DX11 Call
    void RenderFrameDX11(ID3D11DeviceContext* context, ID3D11ShaderResourceView* sourceSRV, ID3D11RenderTargetView* targetRTV,
        uint32_t width, uint32_t height);

    bool PresentFrameDX11(IDXGISwapChain* swapChain, uint32_t syncInterval, uint32_t flags);

    void ResetFrame();
    void NewFrame() { m_frameUpscaled = false; }
    bool WasUpscaledThisFrame() const { return m_frameUpscaled; }

    bool IsLoaded() const { return m_handle != nullptr; }
    bool IsUpscalingEnabled() const;
    bool IsShowDebugImageEnabled() const;
    void SetShowDebugImageEnabled(bool enabled);

    uint32_t GetRenderWidth() const { return m_renderWidth; }
    uint32_t GetRenderHeight() const { return m_renderHeight; }
    uint32_t GetTargetWidth() const { return m_width; }
    uint32_t GetTargetHeight() const { return m_height; }
    uint32_t GetDisplayWidth() const { return m_width; }
    uint32_t GetDisplayHeight() const { return m_height; }

    void UpdateDimensions(uint32_t width, uint32_t height);
    void GetTargetResolution(uint32_t width, uint32_t height, uint32_t& outW, uint32_t& outH);
    float GetScaleFactor() const;

    // Depth & Motion Vector tracking
    void TrackTexture(ID3D11Texture2D* texture, const D3D11_TEXTURE2D_DESC* desc);
    void ResetTracker();
    void RecordDepthClearValue(bool isInverted);
    bool SetPluginFieldBool(const std::string& name, bool value);
    bool SetPluginFieldFloat(const std::string& name, float value);
    void ScanProjectionMatrix(ID3D11DeviceContext* context);
    bool IsViewProjectionMatrix(const float* m, float& outFovY, float& outNear, float& outFar, bool& outInverted, bool& outIsRowMajor);

    // Native Jitter Extraction & Fake Jitter Injection
    ID3D11Buffer* GetKnownProjBuffer() const { return m_knownProjBuffer; }
    UINT GetKnownProjOffset() const { return m_knownProjOffset; }
    float GetGameJitterX() const { return m_gameJitterX; }
    float GetGameJitterY() const { return m_gameJitterY; }
    void UpdateGameJitterFromData(const float* data);
    bool InjectFakeJitterIntoMatrix(float* data, UINT numElements);
    bool TryDetectMatrix(ID3D11Buffer* buffer, const float* data, UINT dataSize);

private:
    DXUpscalerManager()
        : m_handle(nullptr)
        , m_pInterface(nullptr)
        , m_depthTexture(nullptr)
        , m_depthFormat(DXGI_FORMAT_UNKNOWN)
        , m_depthWidth(0)
        , m_depthHeight(0)
        , m_bestDepthScore(-1.0f)
        , m_mvTexture(nullptr)
        , m_mvFormat(DXGI_FORMAT_UNKNOWN)
        , m_mvWidth(0)
        , m_mvHeight(0)
        , m_bestMVScore(-1.0f)
        , m_jitterIndex(0)
        , m_debugPreviewIndex(0)
        , m_depthSRV(nullptr)
        , m_mvSRV(nullptr)
        , m_detectedInvertedDepth(false)
        , m_invertedDepthConfidence(0)
        , m_detectedHDR(false)
        , m_hdrConfidence(0)
        , m_projScanCounter(0)
        , m_cameraNear(0.1f)
        , m_cameraFar(1000.0f)
        , m_cameraFov(60.0f)
        , m_viewSpaceToMetersFactor(1.0f)
        , m_downsampleCS(nullptr)
        , m_downsampledDepthTex(nullptr)
        , m_downsampledDepthSRV(nullptr)
        , m_downsampledDepthUAV(nullptr)
        , m_downsampledMVTex(nullptr)
        , m_downsampledMVSRV(nullptr)
        , m_downsampledMVUAV(nullptr)
        , m_knownProjBuffer(nullptr)
        , m_knownProjOffset(0)
        , m_knownProjIsRowMajor(true)
        , m_gameJitterX(0.0f)
        , m_gameJitterY(0.0f)
        , m_fakeJitterX(0.0f)
        , m_fakeJitterY(0.0f) {}

    void LoadPlugin();

    HMODULE m_handle;
    GamePlugUpscalerInterface* m_pInterface;

    // DX11
    ID3D11Device* m_pd3dDevice = nullptr;
    ID3D11DeviceContext* m_pd3dDeviceContext = nullptr;
    ID3D11Texture2D* m_fakeBackBuffer = nullptr;
    ID3D11ShaderResourceView* m_fakeBackBufferSRV = nullptr;

    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_frameUpscaled = false;
    bool m_isShuttingDown = false;

    // Depth & Motion Vector tracked resources
    ID3D11Texture2D* m_depthTexture;
    DXGI_FORMAT m_depthFormat;
    uint32_t m_depthWidth;
    uint32_t m_depthHeight;
    float m_bestDepthScore;

    ID3D11Texture2D* m_mvTexture;
    DXGI_FORMAT m_mvFormat;
    uint32_t m_mvWidth;
    uint32_t m_mvHeight;
    float m_bestMVScore;

    int m_debugPreviewIndex;
    ID3D11ShaderResourceView* m_depthSRV;
    ID3D11ShaderResourceView* m_mvSRV;

    std::mutex m_trackerMtx;
    uint32_t m_jitterIndex;

    bool m_detectedInvertedDepth;
    int m_invertedDepthConfidence;
    bool m_detectedHDR;
    int m_hdrConfidence;
    uint32_t m_projScanCounter;

    float m_cameraNear;
    float m_cameraFar;
    float m_cameraFov;
    float m_viewSpaceToMetersFactor;

    // Downsampling compute shader & textures
    ID3D11ComputeShader* m_downsampleCS;
    ID3D11Texture2D* m_downsampledDepthTex;
    ID3D11ShaderResourceView* m_downsampledDepthSRV;
    ID3D11UnorderedAccessView* m_downsampledDepthUAV;
    ID3D11Texture2D* m_downsampledMVTex;
    ID3D11ShaderResourceView* m_downsampledMVSRV;
    ID3D11UnorderedAccessView* m_downsampledMVUAV;
    ID3D11Buffer* m_knownProjBuffer;
    UINT m_knownProjOffset;
    bool m_knownProjIsRowMajor;
    float m_gameJitterX;
    float m_gameJitterY;
    float m_fakeJitterX;
    float m_fakeJitterY;
};

} // namespace GamePlug
