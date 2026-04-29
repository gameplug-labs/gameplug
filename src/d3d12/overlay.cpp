#include "common.h"
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx12.h"
#include "config.h"
#include "plugin_manager.h"
#include "plugin_manager.h"
#include "upscaler_manager.h"
#include <mutex>
#include <unordered_map>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace GamePlug {
std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> g_ResourceStates;

enum class DXVersion {
    Unknown,
    DX11,
    DX12
};

static DXVersion g_DXVersion = DXVersion::Unknown;
static bool g_ImGuiInitialized = false;
static WNDPROC g_OriginalWndProc = nullptr;

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_ImGuiInitialized && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return CallWindowProc(g_OriginalWndProc, hWnd, msg, wParam, lParam);
}
static IDXGISwapChain* g_currentSwapChain = nullptr;
static HWND g_currentHWND = nullptr;
static DXGI_FORMAT g_currentFormat = DXGI_FORMAT_UNKNOWN;
static UINT g_bufferCount = 0;
static int g_confidenceCounter = 0;
static uint64_t g_totalPresentCalls = 0;
static uint64_t g_totalExecuteCalls = 0;
static std::recursive_mutex g_DXMtx;

// DX11 Resources
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// DX12 Resources
struct FrameContext {
    ID3D12CommandAllocator* CommandAllocator;
    D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle;
};

ID3D12Device* g_pd3d12Device = nullptr;
static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
static std::mutex g_QueueMtx;
ID3D12CommandQueue* g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList* g_pd3dCommandList = nullptr;
static ID3D12Resource* g_backBuffers[8] = {};
static FrameContext* g_frameContext = nullptr;

static ID3D12Fence* g_pFence = nullptr;
static ID3D12Fence* g_pFenceSync = nullptr;
static HANDLE g_FenceEvent = NULL;
static UINT64 g_FenceValue = 0;
static UINT64 g_FenceValueSync = 0;
ID3D12Resource* g_lastEngineRenderTarget = nullptr;
uint64_t g_lastRTFrame = 0;
static uint64_t g_frameCounter = 0;
static float g_realFPS = 0.0f;
static bool g_needsNewImGuiFrame = true;
static uint64_t g_lastInjectionFrame = 0xFFFFFFFFFFFFFFFF;
static bool g_InHook = false;
static uint32_t g_CommandQueueOffset = 0;
static bool g_pendingResize = false;
static uint64_t g_bestArea = 0;
static std::recursive_mutex g_TargetMtx;

static bool g_CleaningUp = false;

struct ScopedRecursionGuard {
    ScopedRecursionGuard() { g_InHook = true; }
    ~ScopedRecursionGuard() { g_InHook = false; }
};

static void LogDeviceRemovedReason(ID3D12Device* device, HRESULT hr) {
    if (hr != DXGI_ERROR_DEVICE_REMOVED && hr != DXGI_ERROR_DEVICE_RESET) return;
    HRESULT reason = device->GetDeviceRemovedReason();
    const char* reasonStr = "Unknown";
    switch (reason) {
        case DXGI_ERROR_DEVICE_HUNG: reasonStr = "DXGI_ERROR_DEVICE_HUNG"; break;
        case DXGI_ERROR_DEVICE_REMOVED: reasonStr = "DXGI_ERROR_DEVICE_REMOVED"; break;
        case DXGI_ERROR_DEVICE_RESET: reasonStr = "DXGI_ERROR_DEVICE_RESET"; break;
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR: reasonStr = "DXGI_ERROR_DRIVER_INTERNAL_ERROR"; break;
        case DXGI_ERROR_INVALID_CALL: reasonStr = "DXGI_ERROR_INVALID_CALL"; break;
    }
    char hex[16]; sprintf(hex, "0x%08X", (unsigned int)reason);
    Logger::error("DX12: Device Removed! Reason: " + std::string(reasonStr) + " (" + std::string(hex) + ")");
}

void SetDX12CommandQueueOffset(uint32_t offset) {
    g_CommandQueueOffset = offset;
}

void SyncDX12Capture(ID3D12CommandQueue* pQueue);

void SetLastEngineRenderTarget(ID3D12Resource* pRes) {
    std::lock_guard<std::recursive_mutex> lock(g_TargetMtx);

    // Handle explicit clearing
    if (!pRes) {
        if (g_lastEngineRenderTarget) g_lastEngineRenderTarget->Release();
        g_lastEngineRenderTarget = nullptr;
        g_bestArea = 0;
        return;
    }

    D3D12_RESOURCE_DESC desc = pRes->GetDesc();
    if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) return;

    uint32_t rw = DXUpscalerManager::Get().GetRenderWidth();
    uint32_t rh = DXUpscalerManager::Get().GetRenderHeight();

    bool matchesRenderSize =
        abs((int)desc.Width - (int)rw) < 16 &&
        abs((int)desc.Height - (int)rh) < 16;

    if (!matchesRenderSize)
        return;

    if (g_lastEngineRenderTarget != pRes) {
        // [FIX] First-Winner-Logic: If we already found a valid RT this frame, don't overwrite it
        // This prevents picking up bloom or other post-process buffers that happen later.
        if (g_lastEngineRenderTarget != nullptr && g_lastRTFrame == g_frameCounter) {
            return;
        }

        if (g_lastEngineRenderTarget) g_lastEngineRenderTarget->Release();
        g_lastEngineRenderTarget = pRes;
        g_lastEngineRenderTarget->AddRef();
        
        Logger::info("DX12: RT SELECTED [FIRST-MATCH] " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " Fmt=" + std::to_string(desc.Format));
    }

    g_lastRTFrame = g_frameCounter;
}


extern void HookCommandQueue(ID3D12CommandQueue* pQueue);

void SetDX12CommandQueue(ID3D12CommandQueue* pQueue) {
    if (!pQueue) return;

    // Verify queue type - we ONLY want the Graphics (Direct) queue for FSR
    D3D12_COMMAND_QUEUE_DESC qDesc = pQueue->GetDesc();
    if (qDesc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        return; // Ignore compute/copy queues
    }

    std::lock_guard<std::mutex> lock(g_QueueMtx);
    if (g_pd3dCommandQueue != pQueue) {
        Logger::info("DX12: Primary Graphics Queue identified: " + std::to_string((uintptr_t)pQueue));
        g_pd3dCommandQueue = pQueue;
        HookCommandQueue(pQueue);
    }
}

void SyncAllDX12Queues();
void ClearActiveQueues();

void CleanupDX12(bool isResize) {
    if (g_CleaningUp) return;
    g_CleaningUp = true;
    Logger::info("CleanupDX12: Start (Safen Mode) - isResize: " + std::to_string(isResize));

    try {
        // Ensure all engine queues are idle only if device is still valid
        if (g_pd3d12Device) {
            SyncAllDX12Queues();
        }

        Logger::info(" - Releasing Upscaler Resources");
        DXUpscalerManager::Get().CleanupPlugin();

        if (g_lastEngineRenderTarget) {
            std::lock_guard<std::recursive_mutex> lock(g_TargetMtx);
            g_lastEngineRenderTarget->Release(); 
            g_lastEngineRenderTarget = nullptr; 
            g_lastRTFrame = 0;
            Logger::info(" - Released Captured RT");
        }

        if (g_ImGuiInitialized) {
            if (g_DXVersion == DXVersion::DX12) {
                ImGui_ImplDX12_Shutdown();
            } else if (g_DXVersion == DXVersion::DX11) {
                ImGui_ImplDX11_Shutdown();
            }
            ImGui_ImplWin32_Shutdown();
            if (ImGui::GetCurrentContext()) {
                ImGui::DestroyContext();
            }
            g_ImGuiInitialized = false;
            Logger::info(" - ImGui Shutdown Complete");
        }

        // Safe Resource Draining
        if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
        if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = nullptr; }
        if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = nullptr; }

        if (g_pFence) { g_pFence->Release(); g_pFence = nullptr; }
        if (g_pFenceSync) { g_pFenceSync->Release(); g_pFenceSync = nullptr; }
        if (g_FenceEvent) { CloseHandle(g_FenceEvent); g_FenceEvent = nullptr; }
        
        if (g_frameContext) {
            for (UINT i = 0; i < g_bufferCount; i++) {
                if (g_frameContext[i].CommandAllocator) {
                    g_frameContext[i].CommandAllocator->Release();
                    g_frameContext[i].CommandAllocator = nullptr;
                }
                if (g_backBuffers[i]) {
                    g_backBuffers[i]->Release();
                    g_backBuffers[i] = nullptr;
                }
            }
            delete[] g_frameContext;
            g_frameContext = nullptr;
        }

        // If we're just resizing, do NOT release the device. The engine is still using it.
        // We only release it on absolute detachment or if we know for sure it's dead.
        if (g_pd3dCommandQueue) { g_pd3dCommandQueue = nullptr; }
        
        if (g_pd3d12Device) {
            if (!isResize) {
                Logger::info(" - Releasing pd3d12Device (Full Detach)");
                g_pd3d12Device->Release();
            }
            g_pd3d12Device = nullptr;
        }
    } catch (...) {
        Logger::error("CleanupDX12: Caught exception during cleanup loop!");
    }

    ClearActiveQueues();
    g_CleaningUp = false;
    Logger::info("CleanupDX12: End");
}

void CleanupDX11() {
    Logger::info("CleanupDX11: Start");
    if (g_mainRenderTargetView) { 
        g_mainRenderTargetView->Release(); 
        g_mainRenderTargetView = nullptr; 
        Logger::info("CleanupDX11: -> Released RTV");
    }
    Logger::info("CleanupDX11: End");
}

bool InitImGuiDX11(IDXGISwapChain* pSwapChain) {
    Logger::info("DX11 Init: Step 1 - GetDevice");
    if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice))) {
        Logger::error("DX11 Init: GetDevice failed");
        return false;
    }

    Logger::info("DX11 Init: Step 2 - GetImmediateContext");
    g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);

    DXGI_SWAP_CHAIN_DESC sd;
    pSwapChain->GetDesc(&sd);
    HWND hWnd = sd.OutputWindow;

    Logger::info("DX11 Init: Step 3 - Creating ImGui Context (HWND=" + std::to_string((uintptr_t)hWnd) + ")");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Disable imgui.ini

    Logger::info("DX11 Init: Step 4 - ImGui_ImplWin32_Init");
    if (!ImGui_ImplWin32_Init(hWnd)) {
        Logger::error("DX11 Init: ImGui_ImplWin32_Init failed");
        return false;
    }

    Logger::info("DX11 Init: Step 5 - ImGui_ImplDX11_Init");
    if (!ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext)) {
        Logger::error("DX11 Init: ImGui_ImplDX11_Init failed");
        return false;
    }
    
    Logger::info("DX11 Init: Step 6 - Creating BackBuffer RTV");
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer))) {
        Logger::info("DX11 Init:   - GetBuffer(0) succeeded");
        if (SUCCEEDED(g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView))) {
            Logger::info("DX11 Init:   - CreateRenderTargetView succeeded");
        } else {
            Logger::error("DX11 Init:   - CreateRenderTargetView FAILED");
        }
        pBackBuffer->Release();
    } else {
        Logger::error("DX11 Init:   - GetBuffer(0) failed");
        return false;
    }

    Logger::info("DX11 Init: Success");
    DXUpscalerManager::Get().InitDX11(g_pd3dDevice, g_pd3dDeviceContext);
    DXUpscalerManager::Get().UpdateDimensions(sd.BufferDesc.Width, sd.BufferDesc.Height);
    return true;
}

bool InitImGuiDX12(IDXGISwapChain* pSwapChain, ID3D12CommandQueue* pQueue) {
    Logger::info("DX12 Init: Start");
    
    ID3D12Device* pNewDevice = nullptr;
    HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&pNewDevice);
    if (FAILED(hr)) {
        Logger::error("DX12 Init: GetDevice failed with HR=" + std::to_string(hr));
        return false;
    }

    if (g_pd3d12Device != pNewDevice) {
        if (g_pd3d12Device) {
            Logger::info("DX12 Init: Device changed, releasing old device...");
            g_pd3d12Device->Release();
        }
        g_pd3d12Device = pNewDevice;
    } else {
        pNewDevice->Release(); // Already have it
    }

    DXGI_SWAP_CHAIN_DESC sd;
    pSwapChain->GetDesc(&sd);
    g_bufferCount = sd.BufferCount;
    HWND hWnd = sd.OutputWindow;

    Logger::info("DX12 Init: Step 1 - Creating RTV Heap (Count=" + std::to_string(g_bufferCount) + ")");
    if (g_bufferCount == 0) {
        Logger::error("DX12 Init: Invalid BufferCount (0)");
        return false;
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = g_bufferCount;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 0; // Fix: Use 0 for single-GPU or default node
        
        hr = g_pd3d12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap));
        if (FAILED(hr)) {
            char hex[16]; sprintf(hex, "0x%08X", (unsigned int)hr);
            Logger::error("DX12 Init: CreateDescriptorHeap(RTV) failed with HR=" + std::string(hex));
            LogDeviceRemovedReason(g_pd3d12Device, hr);
            return false;
        }
    }

    Logger::info("DX12 Init: Step 2 - Creating SRV Heap");
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask = 0;
        
        hr = g_pd3d12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap));
        if (FAILED(hr)) {
            char hex[16]; sprintf(hex, "0x%08X", (unsigned int)hr);
            Logger::error("DX12 Init: CreateDescriptorHeap(SRV) failed with HR=" + std::string(hex));
            LogDeviceRemovedReason(g_pd3d12Device, hr);
            return false;
        }
    }

    Logger::info("DX12 Init: Step 3 - Creating Allocators, RTVs & Command List");
    {
        g_frameContext = new FrameContext[g_bufferCount];
        UINT rtvDescriptorSize = g_pd3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < g_bufferCount; i++) {
            // Command Allocator
            hr = g_pd3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator));
            if (FAILED(hr)) {
                Logger::error("DX12 Init: CreateCommandAllocator " + std::to_string(i) + " failed with HR=" + std::to_string(hr));
                LogDeviceRemovedReason(g_pd3d12Device, hr);
                return false;
            }

            // RTV Handle & Resource caching
            if (FAILED(pSwapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i])))) {
                Logger::error("DX12 Init: GetBuffer " + std::to_string(i) + " failed");
                return false;
            }
            
            g_frameContext[i].RtvHandle = rtvHandle;
            g_pd3d12Device->CreateRenderTargetView(g_backBuffers[i], nullptr, g_frameContext[i].RtvHandle);
            rtvHandle.ptr += rtvDescriptorSize;
        }

        if (FAILED(g_pd3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, NULL, IID_PPV_ARGS(&g_pd3dCommandList)))) {
            Logger::error("DX12 Init: CreateCommandList failed");
            return false;
        }
        g_pd3dCommandList->Close();
    }

    Logger::info("DX12 Init: Step 4 - Creating Fence & Event");
    if (FAILED(g_pd3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_pFence)))) {
        Logger::error("DX12 Init: CreateFence failed");
        return false;
    }
    if (FAILED(g_pd3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_pFenceSync)))) {
        Logger::error("DX12 Init: CreateSyncFence failed");
        return false;
    }
    g_FenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_FenceValue = 1;
    g_FenceValueSync = 1;

    g_currentHWND = hWnd;
    g_currentFormat = sd.BufferDesc.Format;

    Logger::info("DX12 Init: Step 5 - Creating ImGui Context");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; 
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    Logger::info("DX12 Init: Step 6 - Forcing Font Build");
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    Logger::info("DX12 Init: Step 7 - ImGui_ImplWin32_Init");
    if (!ImGui_ImplWin32_Init(hWnd)) {
        Logger::error("DX12 Init: ImGui_ImplWin32_Init failed");
        return false;
    }
    
    Logger::info("DX12 Init: Step 8 - ImGui_ImplDX12_Init");
    bool init = ImGui_ImplDX12_Init(g_pd3d12Device, g_bufferCount, 
        sd.BufferDesc.Format != DXGI_FORMAT_UNKNOWN ? sd.BufferDesc.Format : DXGI_FORMAT_R8G8B8A8_UNORM, 
        g_pd3dSrvDescHeap, 
        g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(), 
        g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

    if (!init) {
        Logger::error("DX12 Init: ImGui_ImplDX12_Init failed");
        return false;
    }
    Logger::info("DX12 Init:   - ImGui_ImplDX12_Init succeeded");

    // Hook Input
    if (g_currentHWND && !g_OriginalWndProc) {
        g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_currentHWND, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        Logger::info("DX12 Init: WndProc Hooked Successfully");
    }

    // Load Plugins
    PluginManager::Get().LoadPlugins();
    DXUpscalerManager::Get().InitDX12(g_pd3d12Device, pQueue);
    DXUpscalerManager::Get().UpdateDimensions(sd.BufferDesc.Width, sd.BufferDesc.Height);

    // Premium Styling Port from vk_overlay.cpp
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.75f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.29f, 0.48f, 0.90f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);

    Logger::info("DX12 Init: Success");
    return true;
}

void OnDXResize(IDXGISwapChain* pSwapChain) {
    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    Logger::info("OnDXResize: Triggered");
    g_pendingResize = true;
    if (!g_ImGuiInitialized) { Logger::info(" - Not initialized, ignoring resize"); return; }

    if (g_DXVersion == DXVersion::DX11) {
        Logger::info(" - Invalidating DX11 objects");
        ImGui_ImplDX11_InvalidateDeviceObjects();
        CleanupDX11();
    } else if (g_DXVersion == DXVersion::DX12) {
        Logger::info(" - Invalidating DX12 objects (Minimalist Resize)");
        ImGui_ImplDX12_InvalidateDeviceObjects();
        CleanupDX12(true);
    }
    g_ImGuiInitialized = false; 
    Logger::info("OnDXResize: Status reset to uninitialized");
}


IDXGISwapChain* GetCurrentDXSwapChain() {
    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    return g_currentSwapChain;
}

void OnDXPresent(IDXGISwapChain* pSwapChain) {
    if (g_InHook) return;
    ScopedRecursionGuard guard;

    // [FIX] Execute FSR every frame when signaled by the engine
    if (DXUpscalerManager::Get().IsFSRReady() && DXUpscalerManager::Get().HasValidRT()) {
        Logger::warn("FSR RUNNING IN PRESENT");
        DXUpscalerManager::Get().RunFSRPass();
    }

    DXUpscalerManager::Get().ResetFrame();

    {
        std::lock_guard<std::recursive_mutex> lock(g_TargetMtx);
        g_bestArea = 0; // Reset capture threshold for new frame
    }

    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    if (g_pendingResize) { 
        Logger::info("DX12: Handling deferred resize safely"); 
        g_ImGuiInitialized = false; 
        g_pendingResize = false; 
    }
    // Stable FPS Calculation
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(currentTime - lastTime).count();
    if (dt > 0.0001f) {
        float instantFPS = 1.0f / dt;
        g_realFPS = (g_realFPS * 0.9f) + (instantFPS * 0.1f);
    }
    lastTime = currentTime;

    g_frameCounter++;
    
    // RE Engine Stability: Ensure hooks are applied as soon as a SceneView is available
    DXUpscalerManager::Get().UpdateREEngineHooks();
    DXUpscalerManager::Get().SetCurrentSwapChain(pSwapChain);

    g_needsNewImGuiFrame = true;
    g_totalPresentCalls++;
    
    // Only clear if too old (staleness check)
    if (g_lastRTFrame + 1 < g_frameCounter) {
        if (g_lastEngineRenderTarget) {
            g_lastEngineRenderTarget->Release();
            g_lastEngineRenderTarget = nullptr;
        }
    }

    void** vtable = *(void***)pSwapChain;
    if (g_totalPresentCalls < 200) {
        Logger::info("OnDXPresent [" + std::to_string(g_totalPresentCalls) + "] SC=" + std::to_string((uintptr_t)pSwapChain) + " VT=" + std::to_string((uintptr_t)vtable));
    } else if (g_totalPresentCalls % 60 == 0) {
        Logger::info("OnDXPresent Heartbeat: " + std::to_string(g_totalPresentCalls) + " calls (Confidence=" + std::to_string(g_confidenceCounter) + ")");
        
        // Final RT Verification Logging
        ID3D12Resource* finalRT = g_lastEngineRenderTarget;
        if (!finalRT) {
            Logger::warn("No valid final RT this frame");
        } else {
            Logger::warn("Using FINAL RT for FSR");
        }
    }
    

    // Safety: Quick check for SwapChain change
    DXGI_SWAP_CHAIN_DESC desc = {};
    HRESULT hr = pSwapChain->GetDesc(&desc);
    if (FAILED(hr)) {
        Logger::error("OnDXPresent: pSwapChain->GetDesc FAILED (HR=" + std::to_string(hr) + ")");
        return;
    }

    if (g_currentSwapChain != pSwapChain || g_currentHWND != desc.OutputWindow) {
        if (g_ImGuiInitialized) {
            Logger::info("DX: Hot-swap detected! Re-locking to new rendering target...");
            if (g_DXVersion == DXVersion::DX11) {
                ImGui_ImplDX11_InvalidateDeviceObjects();
                CleanupDX11();
            } else if (g_DXVersion == DXVersion::DX12) {
                ImGui_ImplDX12_InvalidateDeviceObjects();
                CleanupDX12(false); // Hot-swap needs FULL cleanup
            }
            g_ImGuiInitialized = false;
        }
        
        g_currentSwapChain = pSwapChain;
        g_currentHWND = desc.OutputWindow;
        g_currentFormat = desc.BufferDesc.Format;
        g_confidenceCounter = 0;
        Logger::info("DX: Found target " + std::to_string((uintptr_t)pSwapChain) + " (HWND=" + std::to_string((uintptr_t)desc.OutputWindow) + "). Resetting confidence counter.");
    }

    // Accumulate confidence on a stable target
    if (!g_ImGuiInitialized) {
        g_confidenceCounter++;
        
        if (g_confidenceCounter % 30 == 1) {
            Logger::info("DX: Accumulating Confidence (" + std::to_string(g_confidenceCounter) + "/5)...");
        }

        if (g_confidenceCounter < 1) return; // Immediate detection after first frame

        Logger::info("DX: Target Found & Stable. Starting Immediate Detection...");
        
        // Detect DX Version (PRIORITIZE DX12)
        ID3D12Device* d3d12Device = nullptr;
        ID3D11Device* d3d11Device = nullptr;

        Logger::info("DX Init Check: Step A - Testing DX12...");
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&d3d12Device))) {
            Logger::info("DX Init Check: SUCCESS (DX12). Registration starting...");
            g_DXVersion = DXVersion::DX12;
            
            // REFramework-style Discovery
            if (g_CommandQueueOffset != 0) {
                ID3D12CommandQueue* pQueue = *(ID3D12CommandQueue**)((uintptr_t)pSwapChain + g_CommandQueueOffset);
                if (pQueue) {
                    Logger::info("DX Init Check: Found Command Queue via Offset.");
                    SetDX12CommandQueue(pQueue);
                    if (InitImGuiDX12(pSwapChain, pQueue)) {
                        g_ImGuiInitialized = true;
                    }
                }
            } else if (g_pd3dCommandQueue) {
                if (InitImGuiDX12(pSwapChain, g_pd3dCommandQueue)) {
                    g_ImGuiInitialized = true;
                }
            }

            d3d12Device->Release();
        }
        else {
            Logger::info("DX Init Check: Step B - Testing DX11...");
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&d3d11Device))) {
                Logger::info("DX Init Check: SUCCESS (DX11). Starting ImGui Setup...");
                g_DXVersion = DXVersion::DX11;
                d3d11Device->Release();
                if (InitImGuiDX11(pSwapChain)) {
                    g_ImGuiInitialized = true;
                    Logger::info("DX Init Check: FINAL SUCCESS (DX11)");
                } else {
                    Logger::error("DX Init Check: FINAL FAILURE (DX11 Init Failed)");
                }
            } else {
                Logger::error("DX Init Check: Step C - FAILURE. No supported DX API detected on this SwapChain.");
            }
        }
    }

    // Detect Changes (Every 300 frames to further reduce overhead)
    static int changeCheckCounter = 0;
    if (changeCheckCounter++ % 300 == 0) {
        DXGI_SWAP_CHAIN_DESC currentDesc;
        if (SUCCEEDED(pSwapChain->GetDesc(&currentDesc))) {
            bool changed = false;
            if (g_currentSwapChain != pSwapChain) {
                Logger::info("DX Adapt: SwapChain pointer changed.");
                changed = true;
            } else if (g_currentHWND != currentDesc.OutputWindow) {
                Logger::info("DX Adapt: Output Window changed.");
                changed = true;
            } else if (g_currentFormat != currentDesc.BufferDesc.Format) {
                Logger::info("DX Adapt: Buffer format changed: " + std::to_string(g_currentFormat) + " -> " + std::to_string(currentDesc.BufferDesc.Format));
                changed = true;
            }

            if (changed) {
                Logger::info("DX Adapt: Re-initializing renderer due to engine property shift");
                if (g_DXVersion == DXVersion::DX11) {
                    ImGui_ImplDX11_InvalidateDeviceObjects();
                    CleanupDX11();
                } else if (g_DXVersion == DXVersion::DX12) {
                    ImGui_ImplDX12_InvalidateDeviceObjects();
                    CleanupDX12(false); // Property shift needs FULL cleanup
                }
                g_ImGuiInitialized = false;
                g_confidenceCounter = 0;
                return;
            }
            
            g_currentSwapChain = pSwapChain;
            g_currentHWND = currentDesc.OutputWindow;
            g_currentFormat = currentDesc.BufferDesc.Format;
        }
    }

    if (!g_ImGuiInitialized) return;

    // DX11 BackBuffer Safety
    if (g_DXVersion == DXVersion::DX11 && !g_mainRenderTargetView) {
        Logger::info("DX11 Present: Re-creating missing RTV");
        ID3D11Texture2D* pBackBuffer;
        if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer))) {
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
            pBackBuffer->Release();
            Logger::info("DX11 Present: RTV re-created successfully");
        }
    }

    // Rendering Paths
    try {
        if (g_DXVersion == DXVersion::DX11) {
        static int frameCount11 = 0;
        bool shouldLog = (frameCount11 < 5);
        if (shouldLog) Logger::info("DX11 Frame " + std::to_string(frameCount11));

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DXUpscalerManager::Get().NewFrame();

        float uiScale = (std::max)(1.0f, (float)desc.BufferDesc.Height / 720.0f);
        
        // Premium Styling Port from vk_overlay.cpp
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 8.0f * uiScale;
        style.FrameRounding = 4.0f * uiScale;
        style.WindowBorderSize = 1.0f;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.75f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.29f, 0.48f, 0.90f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);

        ImGui::SetNextWindowPos(ImVec2(20.0f * uiScale, 20.0f * uiScale), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300.0f * uiScale, 150.0f * uiScale), ImGuiCond_Always);

        ImGui::Begin("GamePlug v0.1", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        Config::Get().RenderUI();
        bool pluginsEnabled = Config::Get().GetBool("PluginEnabled", true);

        if (pluginsEnabled) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        
        DXUpscalerManager::Get().RenderUI(ImGui::GetIO().Framerate, desc.BufferDesc.Width, desc.BufferDesc.Height);

        if (pluginsEnabled && !PluginManager::Get().IsEmpty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            PluginManager::Get().RenderPlugins();
        }

        if (!DXUpscalerManager::Get().IsLoaded() && PluginManager::Get().IsEmpty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No modules or plugins loaded.");
        }

        ImGui::End();
        ImGui::Render();
        
        if (g_mainRenderTargetView) {
            if (shouldLog) Logger::info("DX11 Frame Trace: Entry Render Objects");
            // Diagnostic: Attempt upscaling. Note: sourceSRV is currently nullptr in DX11.
            DXUpscalerManager::Get().RenderFrameDX11(g_pd3dDeviceContext, nullptr, g_mainRenderTargetView, desc.BufferDesc.Width, desc.BufferDesc.Height);

            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if (shouldLog) Logger::info("DX11 Frame Trace: RenderDrawData Complete");
        } else if (shouldLog) {
            Logger::error("DX11 Frame Trace: RTV IS NULL, Skipping draw!");
        }
        
        frameCount11++;
    } else if (g_DXVersion == DXVersion::DX12) {
        // REFramework-style Discovery Refinement
        ID3D12CommandQueue* pQueue = nullptr;
        if (g_CommandQueueOffset != 0) {
            pQueue = *(ID3D12CommandQueue**)((uintptr_t)pSwapChain + g_CommandQueueOffset);
        }
        if (!pQueue) pQueue = g_pd3dCommandQueue;

        if (!g_ImGuiInitialized || !g_currentSwapChain || !pQueue) return;

        // Ensure we always have the queue set for RenderUI etc
        if (pQueue) SetDX12CommandQueue(pQueue);

        g_needsNewImGuiFrame = true;

        IDXGISwapChain3* sc3 = nullptr;
        if (SUCCEEDED(pSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3))) {
            UINT backBufferIdx = sc3->GetCurrentBackBufferIndex();
            sc3->Release();

            if (backBufferIdx < g_bufferCount) {
                FrameContext& ctx = g_frameContext[backBufferIdx];
                if (!ctx.CommandAllocator) return;

                // Use cached backbuffer
                ID3D12Resource* pBackBuffer = g_backBuffers[backBufferIdx];
                if (!pBackBuffer) {
                    // Fallback to GetBuffer if not cached (should not happen with stable init)
                    if (FAILED(pSwapChain->GetBuffer(backBufferIdx, IID_PPV_ARGS(&pBackBuffer)))) return;
                    g_backBuffers[backBufferIdx] = pBackBuffer;
                }

                // Step 4: Debug
                if (g_frameCounter % 600 == 0) {
                    Logger::info("Backbuffer Index: " + std::to_string(backBufferIdx));
                    Logger::info("Backbuffer Ptr: " + std::to_string((uintptr_t)pBackBuffer));
                }

                D3D12_RESOURCE_DESC desc = pBackBuffer->GetDesc();
                uint32_t width = (uint32_t)desc.Width;
                uint32_t height = (uint32_t)desc.Height;

                ctx.CommandAllocator->Reset();
                g_pd3dCommandList->Reset(ctx.CommandAllocator, NULL);

                // In DX12, the SwapChain back buffer MUST be in D3D12_RESOURCE_STATE_PRESENT
                // state when Present() is called. We can safely assume StateBefore is PRESENT.
                D3D12_RESOURCE_STATES currentBBState = D3D12_RESOURCE_STATE_PRESENT;

                D3D12_RESOURCE_BARRIER beginBarrier = {}; 
                beginBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; 
                beginBarrier.Transition.pResource = pBackBuffer; 
                beginBarrier.Transition.StateBefore = currentBBState; 
                beginBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET; 
                beginBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; 
                g_pd3dCommandList->ResourceBarrier(1, &beginBarrier);

                currentBBState = D3D12_RESOURCE_STATE_RENDER_TARGET;

                g_pd3dCommandList->OMSetRenderTargets(1, &ctx.RtvHandle, FALSE, NULL);
                g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);

                // UI Logic
                if (g_needsNewImGuiFrame) {
                    ImGui_ImplDX12_NewFrame();
                    ImGui_ImplWin32_NewFrame();
                    ImGui::NewFrame();
                    
                    float uiScale = (std::max)(1.0f, (float)height / 720.0f);
                    
                    // Premium Styling Port from vk_overlay.cpp
                    ImGuiStyle& style = ImGui::GetStyle();
                    style.WindowRounding = 8.0f * uiScale;
                    style.FrameRounding = 4.0f * uiScale;
                    style.WindowBorderSize = 1.0f;
                    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.75f);
                    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.29f, 0.48f, 0.90f);
                    style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);

                    ImGui::SetNextWindowPos(ImVec2(20.0f * uiScale, 20.0f * uiScale), ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowSize(ImVec2(300.0f * uiScale, 150.0f * uiScale), ImGuiCond_Always);
                    
                    ImGui::Begin("GamePlug v0.1", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
                    
                    Config::Get().RenderUI();
                    bool pluginsEnabled = Config::Get().GetBool("PluginEnabled", true);

                    if (pluginsEnabled) {
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }

                    DXUpscalerManager::Get().RenderUI(g_realFPS, width, height);

                    if (pluginsEnabled && !PluginManager::Get().IsEmpty()) {
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                        PluginManager::Get().RenderPlugins();
                    }

                    if (!DXUpscalerManager::Get().IsLoaded() && PluginManager::Get().IsEmpty()) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("No modules or plugins loaded.");
                    }

                    ImGui::End();
                    ImGui::Render();
                    g_needsNewImGuiFrame = false;
                }

                // FSR pass now occurs in ExecuteCommandLists hook


                // Overlay Draw - Ensure heaps and targets are set immediately before draw
                g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
                
                // FORCE TRANSITION: Ensure backbuffer is RENDER_TARGET before ImGui
                if (currentBBState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
                    D3D12_RESOURCE_BARRIER barrier = {};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource = pBackBuffer;
                    barrier.Transition.StateBefore = currentBBState;
                    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

                    g_pd3dCommandList->ResourceBarrier(1, &barrier);

                    currentBBState = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    g_ResourceStates[pBackBuffer] = currentBBState;
                }

                g_pd3dCommandList->OMSetRenderTargets(1, &ctx.RtvHandle, FALSE, NULL);

                ImDrawData* drawData = ImGui::GetDrawData();
                if (drawData && drawData->CmdListsCount > 0) {
                    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
                    D3D12_RECT sr = { 0, 0, (LONG)width, (LONG)height };
                    g_pd3dCommandList->RSSetViewports(1, &vp);
                    g_pd3dCommandList->RSSetScissorRects(1, &sr);
                    ImGui_ImplDX12_RenderDrawData(drawData, g_pd3dCommandList);
                } else if (g_frameCounter % 60 == 0) {
                    Logger::info("DX12: No ImGui DrawData for frame " + std::to_string(g_frameCounter));
                }

                if (currentBBState != D3D12_RESOURCE_STATE_PRESENT) {
                    D3D12_RESOURCE_BARRIER barrier = {};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource = pBackBuffer;
                    barrier.Transition.StateBefore = currentBBState;
                    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

                    g_pd3dCommandList->ResourceBarrier(1, &barrier);

                    currentBBState = D3D12_RESOURCE_STATE_PRESENT;
                    g_ResourceStates[pBackBuffer] = currentBBState;
                }

                // DX12 FSR1 Fix: Force full-screen viewport reset before present
                D3D12_VIEWPORT fullVp = {};
                fullVp.TopLeftX = 0;
                fullVp.TopLeftY = 0;
                fullVp.Width  = (float)width;
                fullVp.Height = (float)height;
                fullVp.MinDepth = 0.0f;
                fullVp.MaxDepth = 1.0f;
                g_pd3dCommandList->RSSetViewports(1, &fullVp);

                D3D12_RECT fullScissor = {};
                fullScissor.left = 0;
                fullScissor.top = 0;
                fullScissor.right  = (LONG)width;
                fullScissor.bottom = (LONG)height;
                g_pd3dCommandList->RSSetScissorRects(1, &fullScissor);

                g_pd3dCommandList->Close();


                ID3D12CommandList* lists[] = { g_pd3dCommandList };
                g_pd3dCommandQueue->ExecuteCommandLists(1, lists);
                
                // Sync to avoid race conditions with next frame
                SyncDX12Capture(g_pd3dCommandQueue);
                
                // Finalize Frame Capture: No Release needed for cached backbuffer
                if (g_lastEngineRenderTarget == pBackBuffer) {
                    SetLastEngineRenderTarget(nullptr);
                }

                g_lastInjectionFrame = g_frameCounter;
            }
        }
    }
    } catch (const std::exception& e) {
        Logger::error("OnDXPresent: Caught exception: " + std::string(e.what()));
    } catch (...) {
        Logger::error("OnDXPresent: Caught unknown aggregate exception (likely D3D12 internal)");
    }
}

// Fallback: Command-Stream Injection (The "Lurker" Strategy)
// Fallback: Command-Stream Injection (The "Lurker" Strategy)
ID3D12CommandList* OnDXExecute(ID3D12CommandQueue* pQueue, bool isSignal) {
    uint64_t currentCall = g_totalExecuteCalls++;
    if (currentCall < 10 || currentCall % 300 == 0) {
        Logger::info("DX12: OnDXExecute Entry [Signal=" + std::string(isSignal ? "True" : "False") + " TotalCalls=" + std::to_string(currentCall) + "]");
    }

    if (!pQueue || !isSignal) return nullptr;

    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    
    // Unified Initialization: Handle start-up stable init from Signal thread
    if (!g_ImGuiInitialized) {
        // Hybrid Detection: If we have a swapchain but version is still unknown, promote to DX12
        if (g_currentSwapChain && g_DXVersion == DXVersion::Unknown) {
            Logger::info("DX12 Init: Promoting Unknown version to DX12 via Signal pulse [SC=" + std::to_string((uintptr_t)g_currentSwapChain) + "]");
            g_DXVersion = DXVersion::DX12;
        }

        if (currentCall % 300 == 0) {
            Logger::info("DX12: OnDXExecute - Waiting for Init (SC=" + std::to_string((uintptr_t)g_currentSwapChain) + 
                         " Ver=" + std::to_string((int)g_DXVersion) + " HWND=" + std::to_string((uintptr_t)g_currentHWND) + ")");
        }

        if (!g_currentSwapChain) {
            if (currentCall % 300 == 0) Logger::error("DX12: OnDXExecute - Dropout: No Current SwapChain");
            return nullptr;
        }
        if (g_DXVersion != DXVersion::DX12) {
            if (currentCall % 300 == 0) Logger::error("DX12: OnDXExecute - Dropout: Version Mismatch (Expected DX12, Got " + std::to_string((int)g_DXVersion) + ")");
            return nullptr;
        }

        ID3D12Device* device = nullptr;
        if (currentCall % 300 == 0) {
            Logger::info("DX12 Init: Attempting g_currentSwapChain->GetDevice");
        }
        if (SUCCEEDED(g_currentSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&device))) {
            device->Release();
            if (currentCall % 300 == 0) {
                Logger::info("DX12 Init: InitImGuiDX12");
            }
            if (InitImGuiDX12(g_currentSwapChain, pQueue)) {
                g_ImGuiInitialized = true;
                Logger::info("DX12 Init: Initialized from Signal (STABLE)");
            }
        } else if (currentCall % 300 == 0) {
            Logger::error("DX12: OnDXExecute - FAILED to get device from SwapChain");
        }
        return nullptr; // Skip the initialization frame for ultimate stability
    }

    if (g_DXVersion != DXVersion::DX12 || !g_currentSwapChain) {
        if (currentCall % 300 == 0) Logger::info("DX12: OnDXExecute - SC/Version Mismatch Drop");
        return nullptr;
    }

    // Heartbeat logic only
    if (currentCall % 600 == 0) {
        Logger::info("DX12: OnDXExecute Pulse [Frame=" + std::to_string(g_frameCounter) + "]");
    }
    
    return nullptr;
}

void SyncDX12Capture(ID3D12CommandQueue* pQueue) {
    if (!pQueue) return;
    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    if (g_pFenceSync && g_FenceEvent) {
        UINT64 fenceVal = g_FenceValueSync++;
        pQueue->Signal(g_pFenceSync, fenceVal);
        if (g_pFenceSync->GetCompletedValue() < fenceVal) {
            g_pFenceSync->SetEventOnCompletion(fenceVal, g_FenceEvent);
            WaitForSingleObject(g_FenceEvent, INFINITE);
        }
    }
}

} // namespace GamePlug
