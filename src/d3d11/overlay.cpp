#include "hooks_common.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_overlay_shared.h"
#include "plugin_manager.h"
#include "upscaler_manager.h"
#include <d3d11.h>
#include <mutex>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace GamePlug {

static bool g_ImGuiInitialized = false;
static bool g_Visible = true;
static bool g_ShowKeyWasPressed = false;
static WNDPROC g_OriginalWndProc = nullptr;

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_ImGuiInitialized && g_Visible) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;
    }
    return CallWindowProc(g_OriginalWndProc, hWnd, msg, wParam, lParam);
}

static IDXGISwapChain* g_currentSwapChain = nullptr;
static HWND g_currentHWND = nullptr;
static DXGI_FORMAT g_currentFormat = DXGI_FORMAT_UNKNOWN;
static int g_confidenceCounter = 0;
static uint64_t g_totalPresentCalls = 0;
static float g_realFPS = 0.0f;
static bool g_needsNewImGuiFrame = true;
static std::recursive_mutex g_DXMtx;

// DX11 Resources
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static bool g_pendingResize = false;

void CleanupDX11() {
    Logger::info("CleanupDX11: Start");
    DXUpscalerManager::Get().UnloadUpscaler();
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
        Logger::info("CleanupDX11: -> Released RTV");
    }
    if (g_ImGuiInitialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        PluginManager::Get().UnloadPlugins();
        if (ImGui::GetCurrentContext()) {
            ImGui::DestroyContext();
        }
        g_ImGuiInitialized = false;
        Logger::info("CleanupDX11: ImGui Shutdown Complete (Plugins Unloaded)");
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
    if (g_OriginalWndProc && g_currentHWND) {
        SetWindowLongPtr(g_currentHWND, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
        g_OriginalWndProc = nullptr;
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

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
    if (SUCCEEDED(g_OriginalGetBuffer(pSwapChain, 0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer))) {
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

    // Hook Input
    g_currentHWND = hWnd;
    if (g_currentHWND && !g_OriginalWndProc) {
        g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_currentHWND, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        Logger::info("DX11 Init: WndProc Hooked Successfully");
    }

    // Load Plugins
    PluginManager::Get().LoadPlugins();
    DXUpscalerManager::Get().InitDX11(g_pd3dDevice, g_pd3dDeviceContext);
    DXUpscalerManager::Get().CreateFakeBackBuffer(pSwapChain);

    Logger::info("DX11 Init: Success");
    return true;
}

void OnDXResize(IDXGISwapChain* pSwapChain) {
    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    Logger::info("OnDXResize (D3D11): Triggered");
    g_pendingResize = true;
    if (!g_ImGuiInitialized) {
        Logger::info(" - Not initialized, ignoring resize");
        return;
    }

    Logger::info(" - Invalidating DX11 objects");
    ImGui_ImplDX11_InvalidateDeviceObjects();
    CleanupDX11();
    g_ImGuiInitialized = false;
    Logger::info("OnDXResize (D3D11): Status reset to uninitialized");
}

IDXGISwapChain* GetCurrentDXSwapChain() {
    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    return g_currentSwapChain;
}

void OnDXPresent(IDXGISwapChain* pSwapChain) {
    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    DXUpscalerManager::Get().NewFrame();
    if (g_pendingResize) {
        Logger::info("DX11: Handling deferred resize safely");
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

    // Toggle Visibility with Ctrl + HOME key
    bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool homePressed = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
    bool keyCurrentlyPressed = ctrlPressed && homePressed;

    if (keyCurrentlyPressed && !g_ShowKeyWasPressed) {
        g_Visible = !g_Visible;
        Logger::info("DX Overlay D3D11: Visibility toggled manually to: " + std::string(g_Visible ? "ON" : "OFF"));
    }
    g_ShowKeyWasPressed = keyCurrentlyPressed;

    g_needsNewImGuiFrame = true;
    g_totalPresentCalls++;

    DXGI_SWAP_CHAIN_DESC desc = {};
    HRESULT hr = pSwapChain->GetDesc(&desc);
    if (FAILED(hr)) {
        Logger::error("OnDXPresent D3D11: pSwapChain->GetDesc FAILED (HR=" + std::to_string(hr) + ")");
        return;
    }

    if (g_currentSwapChain != pSwapChain || g_currentHWND != desc.OutputWindow) {
        if (g_ImGuiInitialized) {
            Logger::info("DX11: Hot-swap detected! Re-locking to new rendering target...");
            ImGui_ImplDX11_InvalidateDeviceObjects();
            CleanupDX11();
            g_ImGuiInitialized = false;
        }

        g_currentSwapChain = pSwapChain;
        g_currentHWND = desc.OutputWindow;
        g_currentFormat = desc.BufferDesc.Format;
        g_confidenceCounter = 0;
        Logger::info("DX11: Found target " + std::to_string((uintptr_t)pSwapChain) +
                     " (HWND=" + std::to_string((uintptr_t)desc.OutputWindow) + "). Resetting confidence counter.");
    }

    // Accumulate confidence on a stable target
    if (!g_ImGuiInitialized) {
        g_confidenceCounter++;

        if (g_confidenceCounter % 30 == 1) {
            Logger::info("DX11: Accumulating Confidence (" + std::to_string(g_confidenceCounter) + "/5)...");
        }

        if (g_confidenceCounter < 1)
            return;

        Logger::info("DX11: Target Found & Stable. Starting Setup...");

        if (InitImGuiDX11(pSwapChain)) {
            g_ImGuiInitialized = true;
            Logger::info("DX11 Init Check: FINAL SUCCESS (DX11)");
        } else {
            Logger::error("DX11 Init Check: FINAL FAILURE (DX11 Init Failed)");
        }
    }

    // Detect Changes (Every 300 frames to further reduce overhead)
    static int changeCheckCounter = 0;
    if (changeCheckCounter++ % 300 == 0) {
        DXGI_SWAP_CHAIN_DESC currentDesc;
        if (SUCCEEDED(pSwapChain->GetDesc(&currentDesc))) {
            bool changed = false;
            if (g_currentSwapChain != pSwapChain) {
                Logger::info("DX11 Adapt: SwapChain pointer changed.");
                changed = true;
            } else if (g_currentHWND != currentDesc.OutputWindow) {
                Logger::info("DX11 Adapt: Output Window changed.");
                changed = true;
            } else if (g_currentFormat != currentDesc.BufferDesc.Format) {
                Logger::info("DX11 Adapt: Buffer format changed: " + std::to_string(g_currentFormat) + " -> " +
                             std::to_string(currentDesc.BufferDesc.Format));
                changed = true;
            }

            if (changed) {
                Logger::info("DX11 Adapt: Re-initializing renderer due to engine property shift");
                ImGui_ImplDX11_InvalidateDeviceObjects();
                CleanupDX11();
                g_ImGuiInitialized = false;
                g_confidenceCounter = 0;
                return;
            }

            g_currentSwapChain = pSwapChain;
            g_currentHWND = currentDesc.OutputWindow;
            g_currentFormat = currentDesc.BufferDesc.Format;
        }
    }

    if (!g_ImGuiInitialized)
        return;

    // DX11 BackBuffer Safety
    if (!g_mainRenderTargetView) {
        Logger::info("DX11 Present: Re-creating missing RTV");
        ID3D11Texture2D* pBackBuffer;
        if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer))) {
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
            pBackBuffer->Release();
            Logger::info("DX11 Present: RTV re-created successfully");
        }
    }

    // Rendering Path
    try {
        static int frameCount11 = 0;
        bool shouldLog = (frameCount11 < 5);
        if (shouldLog)
            Logger::info("DX11 Frame " + std::to_string(frameCount11));
        
        ImGuiIO& io = ImGui::GetIO();

        ImGuiIO& io = ImGui::GetIO();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        io.DisplaySize = ImVec2((float)desc.BufferDesc.Width, (float)desc.BufferDesc.Height);

        // io.MouseDrawCursor = g_Visible;
        if (!g_Visible) {
            io.ClearInputKeys();
        }

        ImGui::NewFrame();

        if (g_Visible) {
            ImGuiOverlayShared::DrawUI(desc.BufferDesc.Width, desc.BufferDesc.Height,
                [desc]() { DXUpscalerManager::Get().RenderUI(g_realFPS, desc.BufferDesc.Width, desc.BufferDesc.Height); });
        }

        ImGui::Render();

        if (g_mainRenderTargetView) {
            if (shouldLog)
                Logger::info("DX11 Frame Trace: Entry Render Objects");

            bool upscaled = false;
            if (DXUpscalerManager::Get().IsUpscalingEnabled()) {
                ID3D11ShaderResourceView* srcSRV = DXUpscalerManager::Get().GetFakeBackBufferSRV();
                if (srcSRV) {
                    DXUpscalerManager::Get().RenderFrameDX11(
                        g_pd3dDeviceContext, srcSRV, g_mainRenderTargetView, desc.BufferDesc.Width, desc.BufferDesc.Height);
                    upscaled = true;
                }
            }
            if (!upscaled) {
                ID3D11Texture2D* fakeBuffer = DXUpscalerManager::Get().GetFakeBackBuffer();
                if (fakeBuffer) {
                    ID3D11Texture2D* realBuffer = nullptr;
                    if (SUCCEEDED(g_OriginalGetBuffer(pSwapChain, 0, __uuidof(ID3D11Texture2D), (void**)&realBuffer))) {
                        D3D11_TEXTURE2D_DESC fakeDesc, realDesc;
                        fakeBuffer->GetDesc(&fakeDesc);
                        realBuffer->GetDesc(&realDesc);
                        if (fakeDesc.Width == realDesc.Width && fakeDesc.Height == realDesc.Height) {
                            g_pd3dDeviceContext->CopyResource(realBuffer, fakeBuffer);
                        } else {
                            static uint64_t s_warnCount = 0;
                            if (s_warnCount++ % 60 == 0) {
                                Logger::warn("DX11 Present: Cannot copy fake backbuffer, size mismatch (" + std::to_string(fakeDesc.Width) +
                                             "x" + std::to_string(fakeDesc.Height) + " vs " + std::to_string(realDesc.Width) + "x" +
                                             std::to_string(realDesc.Height) + ")");
                            }
                        }
                        realBuffer->Release();
                    }
                }
            }
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
            if (g_Visible) {
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }
            if (shouldLog)
                Logger::info("DX11 Frame Trace: RenderDrawData Complete");
        } else if (shouldLog) {
            Logger::error("DX11 Frame Trace: RTV IS NULL, Skipping draw!");
        }

        frameCount11++;
    } catch (const std::exception& e) {
        Logger::error("OnDXPresent D3D11: Caught exception: " + std::string(e.what()));
    } catch (...) {
        Logger::error("OnDXPresent D3D11: Caught unknown aggregate exception");
    }
}

} // namespace GamePlug
