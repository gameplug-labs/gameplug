#include "hooks_common.h"
#include "imgui.h"
#include "imgui_impl_dx10.h"
#include "imgui_impl_win32.h"
#include "imgui_overlay_shared.h"
#include "plugin_manager.h"
#include <d3d10_1.h>
#include <d3d10.h>
#include <mutex>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace GamePlug {

static bool g_ImGuiInitialized = false;
static bool g_Visible = true;
static bool g_ShowKeyWasPressed = false;
static WNDPROC g_OriginalWndProc = nullptr;

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_ImGuiInitialized && g_Visible && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
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

// DX10 Resources
static ID3D10Device* g_pd3dDevice = nullptr;
static ID3D10RenderTargetView* g_mainRenderTargetView = nullptr;
static bool g_pendingResize = false;

void CleanupDX10() {
    Logger::info("CleanupDX10: Start");
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
        Logger::info("CleanupDX10: -> Released RTV");
    }
    if (g_ImGuiInitialized) {
        ImGui_ImplDX10_Shutdown();
        ImGui_ImplWin32_Shutdown();
        PluginManager::Get().UnloadPlugins();
        if (ImGui::GetCurrentContext()) {
            ImGui::DestroyContext();
        }
        g_ImGuiInitialized = false;
        Logger::info("CleanupDX10: ImGui Shutdown Complete (Plugins Unloaded)");
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
    if (g_OriginalWndProc && g_currentHWND) {
        SetWindowLongPtr(g_currentHWND, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
        g_OriginalWndProc = nullptr;
    }

    Logger::info("CleanupDX10: End");
}

bool InitImGuiDX10(IDXGISwapChain* pSwapChain) {
    Logger::info("DX10 Init: Step 1 - GetDevice");
    if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D10Device), (void**)&g_pd3dDevice))) {
        Logger::error("DX10 Init: GetDevice failed");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd;
    pSwapChain->GetDesc(&sd);
    HWND hWnd = sd.OutputWindow;

    Logger::info("DX10 Init: Step 2 - Creating ImGui Context (HWND=" + std::to_string((uintptr_t)hWnd) + ")");
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Disable imgui.ini

    Logger::info("DX10 Init: Step 3 - ImGui_ImplWin32_Init");
    if (!ImGui_ImplWin32_Init(hWnd)) {
        Logger::error("DX10 Init: ImGui_ImplWin32_Init failed");
        return false;
    }

    Logger::info("DX10 Init: Step 4 - ImGui_ImplDX10_Init");
    if (!ImGui_ImplDX10_Init(g_pd3dDevice)) {
        Logger::error("DX10 Init: ImGui_ImplDX10_Init failed");
        return false;
    }

    Logger::info("DX10 Init: Step 5 - Creating BackBuffer RTV");
    ID3D10Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D10Texture2D), (LPVOID*)&pBackBuffer))) {
        Logger::info("DX10 Init:   - GetBuffer(0) succeeded");
        if (SUCCEEDED(g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView))) {
            Logger::info("DX10 Init:   - CreateRenderTargetView succeeded");
        } else {
            Logger::error("DX10 Init:   - CreateRenderTargetView FAILED");
        }
        pBackBuffer->Release();
    } else {
        Logger::error("DX10 Init:   - GetBuffer(0) failed");
        return false;
    }

    // Hook Input
    g_currentHWND = hWnd;
    if (g_currentHWND && !g_OriginalWndProc) {
        g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_currentHWND, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        Logger::info("DX10 Init: WndProc Hooked Successfully");
    }

    // Load Plugins
    PluginManager::Get().LoadPlugins();

    Logger::info("DX10 Init: Success");
    return true;
}

void OnDXResize(IDXGISwapChain* pSwapChain) {
    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    Logger::info("OnDXResize (D3D10): Triggered");
    g_pendingResize = true;
    if (!g_ImGuiInitialized) {
        Logger::info(" - Not initialized, ignoring resize");
        return;
    }

    Logger::info(" - Invalidating DX10 objects");
    ImGui_ImplDX10_InvalidateDeviceObjects();
    CleanupDX10();
    g_ImGuiInitialized = false;
    Logger::info("OnDXResize (D3D10): Status reset to uninitialized");
}

IDXGISwapChain* GetCurrentDXSwapChain() {
    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    return g_currentSwapChain;
}

void OnDXPresent(IDXGISwapChain* pSwapChain) {
    std::lock_guard<std::recursive_mutex> lock(g_DXMtx);
    if (g_pendingResize) {
        Logger::info("DX10: Handling deferred resize safely");
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

    // Toggle Visibility with Ctrl + HOME key or ` key (VK_OEM_3)
    bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool homePressed = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
    bool backtickPressed = (GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0;
    bool keyCurrentlyPressed = (ctrlPressed && homePressed) || backtickPressed;

    if (keyCurrentlyPressed && !g_ShowKeyWasPressed) {
        g_Visible = !g_Visible;
        Logger::info("DX Overlay D3D10: Visibility toggled manually to: " + std::string(g_Visible ? "ON" : "OFF"));
    }
    g_ShowKeyWasPressed = keyCurrentlyPressed;

    g_needsNewImGuiFrame = true;
    g_totalPresentCalls++;

    DXGI_SWAP_CHAIN_DESC desc = {};
    HRESULT hr = pSwapChain->GetDesc(&desc);
    if (FAILED(hr)) {
        Logger::error("OnDXPresent D3D10: pSwapChain->GetDesc FAILED (HR=" + std::to_string(hr) + ")");
        return;
    }

    if (g_currentSwapChain != pSwapChain || g_currentHWND != desc.OutputWindow) {
        if (g_ImGuiInitialized) {
            Logger::info("DX10: Hot-swap detected! Re-locking to new rendering target...");
            ImGui_ImplDX10_InvalidateDeviceObjects();
            CleanupDX10();
            g_ImGuiInitialized = false;
        }

        g_currentSwapChain = pSwapChain;
        g_currentHWND = desc.OutputWindow;
        g_currentFormat = desc.BufferDesc.Format;
        g_confidenceCounter = 0;
        Logger::info("DX10: Found target " + std::to_string((uintptr_t)pSwapChain) +
                     " (HWND=" + std::to_string((uintptr_t)desc.OutputWindow) + "). Resetting confidence counter.");
    }

    // Accumulate confidence on a stable target
    if (!g_ImGuiInitialized) {
        g_confidenceCounter++;

        if (g_confidenceCounter % 30 == 1) {
            Logger::info("DX10: Accumulating Confidence (" + std::to_string(g_confidenceCounter) + "/5)...");
        }

        if (g_confidenceCounter < 1)
            return;

        Logger::info("DX10: Target Found & Stable. Starting Setup...");

        if (InitImGuiDX10(pSwapChain)) {
            g_ImGuiInitialized = true;
            Logger::info("DX10 Init Check: FINAL SUCCESS (DX10)");
        } else {
            Logger::error("DX10 Init Check: FINAL FAILURE (DX10 Init Failed)");
        }
    }

    // Detect Changes (Every 300 frames to further reduce overhead)
    static int changeCheckCounter = 0;
    if (changeCheckCounter++ % 300 == 0) {
        DXGI_SWAP_CHAIN_DESC currentDesc;
        if (SUCCEEDED(pSwapChain->GetDesc(&currentDesc))) {
            bool changed = false;
            if (g_currentSwapChain != pSwapChain) {
                Logger::info("DX10 Adapt: SwapChain pointer changed.");
                changed = true;
            } else if (g_currentHWND != currentDesc.OutputWindow) {
                Logger::info("DX10 Adapt: Output Window changed.");
                changed = true;
            } else if (g_currentFormat != currentDesc.BufferDesc.Format) {
                Logger::info("DX10 Adapt: Buffer format changed: " + std::to_string(g_currentFormat) + " -> " +
                             std::to_string(currentDesc.BufferDesc.Format));
                changed = true;
            }

            if (changed) {
                Logger::info("DX10 Adapt: Re-initializing renderer due to engine property shift");
                ImGui_ImplDX10_InvalidateDeviceObjects();
                CleanupDX10();
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

    // DX10 BackBuffer Safety
    if (!g_mainRenderTargetView) {
        Logger::info("DX10 Present: Re-creating missing RTV");
        ID3D10Texture2D* pBackBuffer;
        if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D10Texture2D), (LPVOID*)&pBackBuffer))) {
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
            pBackBuffer->Release();
            Logger::info("DX10 Present: RTV re-created successfully");
        }
    }

    // Rendering Path
    try {
        static int frameCount10 = 0;
        bool shouldLog = (frameCount10 < 5);
        if (shouldLog)
            Logger::info("DX10 Frame " + std::to_string(frameCount10));

        ImGuiIO& io = ImGui::GetIO();

        ImGui_ImplDX10_NewFrame();
        ImGui_ImplWin32_NewFrame();
        io.DisplaySize = ImVec2((float)desc.BufferDesc.Width, (float)desc.BufferDesc.Height);

        if (g_Visible && g_currentHWND) {
            POINT cursorPos;
            if (GetCursorPos(&cursorPos)) {
                ScreenToClient(g_currentHWND, &cursorPos);
                io.AddMousePosEvent((float)cursorPos.x, (float)cursorPos.y);
            }
            // Poll button state and feed it through the event queue
            io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
            io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
            io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
            // Draw ImGui's own cursor on top – mirrors Community Shaders behaviour.
            // The game cursor can be hidden in certain states (in-game, menus with
            // hardware cursor hidden), so ImGui's software cursor is more reliable.
            io.MouseDrawCursor = true;
        } else {
            io.MouseDrawCursor = false;
        }

        if (!g_Visible) {
            io.ClearInputKeys();
        }

        ImGui::NewFrame();

        if (g_Visible) {
            ImGuiOverlayShared::DrawUI(desc.BufferDesc.Width, desc.BufferDesc.Height);
        }

        ImGui::Render();

        if (g_mainRenderTargetView) {
            if (shouldLog)
                Logger::info("DX10 Frame Trace: Entry Render Objects");

            g_pd3dDevice->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
            ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());
            if (shouldLog)
                Logger::info("DX10 Frame Trace: RenderDrawData Complete");
        } else if (shouldLog) {
            Logger::error("DX10 Frame Trace: RTV IS NULL, Skipping draw!");
        }

        frameCount10++;
    } catch (const std::exception& e) {
        Logger::error("OnDXPresent D3D10: Caught exception: " + std::string(e.what()));
    } catch (...) {
        Logger::error("OnDXPresent D3D10: Caught unknown aggregate exception");
    }
}

} // namespace GamePlug
