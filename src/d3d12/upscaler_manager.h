#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include "upscaler_interface.h"
#include "framework_export.h"
#include <optional>
#include <memory>
#include <unordered_map>
#include "upscaler_manager.h" 



namespace GamePlug {

class FRAMEWORK_API DXUpscalerManager {
public:
    static DXUpscalerManager& Get() {
        static DXUpscalerManager instance;
        return instance;
    }

    ~DXUpscalerManager();

    void InitDX11(ID3D11Device* device, ID3D11DeviceContext* context);
    void InitDX12(ID3D12Device* device, ID3D12CommandQueue* queue);
    void EarlyInit();
    void UnloadUpscaler();
    void CleanupPlugin(); // New: Cleanup resources without unloading DLL
    void CleanupDX12Resources(); // New: Specifically release DX12 internal objects

    void CreateFakeBackBuffer(IDXGISwapChain* swapChain);
    void DestroyFakeBackBuffer();
    ID3D12Resource* GetFakeBackBuffer() { return m_fakeBackBuffer; }

    void RenderUI(float fps, uint32_t width, uint32_t height);
    
    // DX12 Call
    void RenderFrame(ID3D12GraphicsCommandList* cmd, 
                     ID3D12Resource* source, 
                     ID3D12Resource* target, 
                     uint32_t width, 
                     uint32_t height);
    
    // DX11 Call
    void RenderFrameDX11(ID3D11DeviceContext* context,
                         ID3D11ShaderResourceView* sourceSRV,
                         ID3D11RenderTargetView* targetRTV,
                         uint32_t width,
                         uint32_t height);

    void ResetFrame();
    void NewFrame() { m_frameUpscaled = false; }
    bool WasUpscaledThisFrame() const { return m_frameUpscaled; }

    bool IsLoaded() const { return m_handle != nullptr; }
    bool IsUpscalingEnabled() const;
    
    uint32_t GetRenderWidth() const { return m_renderWidth; }
    uint32_t GetRenderHeight() const { return m_renderHeight; }
    uint32_t GetTargetWidth() const { return m_width; }
    uint32_t GetTargetHeight() const { return m_height; }
    uint32_t GetDisplayWidth() const { return m_width; }
    uint32_t GetDisplayHeight() const { return m_height; }

    void UpdateDimensions(uint32_t width, uint32_t height);
    void GetTargetResolution(uint32_t width, uint32_t height, uint32_t& outW, uint32_t& outH);
    float GetScaleFactor() const;

    void RunFSRPass(); // Core upscaling injection point
    void SetCurrentSwapChain(IDXGISwapChain* sc) { m_currentSwapChain = sc; }

    ID3D12Resource* GetFinalOutput();
    D3D12_CPU_DESCRIPTOR_HANDLE GetOrCreateRTV(ID3D12Resource* res);

    // FSR Timing Fix Support
    void MarkFSRReady();
    bool IsFSRReady() const;
    void SetHasValidRT(bool ready);
    bool HasValidRT() const;
    void SetActiveQueue(ID3D12CommandQueue* queue);

    // RE Engine Native Scaling Support
    void InitREEngineHooks();
    void UpdateREEngineHooks();
    void PerformPropertyInjection();
    
    // Size structure used by the engine
    struct via_Size { float w; float h; };
    
    static via_Size Hooked_get_Size_Native(void* scene_view); // Keep for potential debug but remove usage



private:
    DXUpscalerManager() : m_handle(nullptr), m_pInterface(nullptr) {}
    
    void LoadPlugin();

    HMODULE m_handle;
    GamePlugUpscalerInterface* m_pInterface;

     // RE Engine Hooks
    bool m_reHooksInitialized = false;



    // DX11
    ID3D11Device* m_pd3dDevice = nullptr;
    ID3D11DeviceContext* m_pd3dDeviceContext = nullptr;

    // DX12
    ID3D12Device* m_pd3d12Device = nullptr;
    ID3D12CommandQueue* m_pd3d12Queue = nullptr;

    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_frameUpscaled = false;
    bool m_isShuttingDown = false;
    bool m_fsrReady = false;
    bool m_hasValidRT = false;

    ID3D12Resource* m_fakeBackBuffer = nullptr;
    ID3D12Resource* m_finalOutput = nullptr;
    std::unordered_map<ID3D12Resource*, D3D12_CPU_DESCRIPTOR_HANDLE> m_rtvCache;
    ID3D12DescriptorHeap* m_rtvHeap = nullptr;
    uint32_t m_rtvDescriptorSize = 0;
    uint32_t m_nextRtvIndex = 0;

    // FSR Dedicated Resources (DX12)
    IDXGISwapChain* m_currentSwapChain = nullptr;
    ID3D12CommandAllocator* m_fsrAllocator = nullptr;
    ID3D12GraphicsCommandList* m_fsrCommandList = nullptr;
    ID3D12Fence* m_fsrFence = nullptr;
    UINT64 m_fsrFenceValue = 0;
};

} // namespace GamePlug
