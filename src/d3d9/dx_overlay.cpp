#include "dx_overlay.h"
#include "config.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "imgui_overlay_shared.h"
#include "logger.h"
#include "plugin_manager.h"
#include "upscaler_manager.h"
#include "texture_replacer.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace GamePlug {

static thread_local bool g_isRenderingOverlay = false;

bool OverlayRenderer::IsRenderingOverlay() {
    return g_isRenderingOverlay;
}
void OverlayRenderer::SetIsRenderingOverlay(bool val) {
    g_isRenderingOverlay = val;
}

OverlayRenderer& OverlayRenderer::Get() {
    static OverlayRenderer instance;
    return instance;
}

void OverlayRenderer::Init(IDirect3DDevice9* device) {
    if (m_initialized)
        return;
    m_pDevice = device;

    Logger::info("OverlayRenderer::Init: Entry (device={:p})", (void*)device);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    Logger::info("OverlayRenderer::Init: Calling ImGui_ImplWin32_Init for HWND {:p}...", (void*)m_hWnd);
    if (!ImGui_ImplWin32_Init(m_hWnd)) {
        Logger::error("OverlayRenderer::Init: ImGui_ImplWin32_Init failed!");
        return;
    }

    Logger::info("OverlayRenderer::Init: Calling ImGui_ImplDX9_Init...");
    if (!ImGui_ImplDX9_Init(device)) {
        Logger::error("OverlayRenderer::Init: ImGui_ImplDX9_Init failed!");
        return;
    }

    Logger::info("OverlayRenderer::Init: Hooking WndProc...");
    m_originalWndProc = (WNDPROC)SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
    Logger::info("OverlayRenderer: Window hooked. Original WndProc={:p}", (void*)m_originalWndProc);

    m_lastTime = std::chrono::steady_clock::now();
    m_initialized = true;
    Logger::info("OverlayRenderer: Initialized Successfully. Loading plugins...");

    TextureReplacer::Get().Init();
    PluginManager::Get().LoadPlugins();
}

void OverlayRenderer::Shutdown() {
    if (!m_initialized)
        return;
    Logger::info("OverlayRenderer::Shutdown: Starting teardown...");

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    PluginManager::Get().UnloadPlugins();
    ImGui::DestroyContext();

    if (m_originalWndProc) {
        SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)m_originalWndProc);
    }

    m_initialized = false;
    Logger::info("OverlayRenderer::Shutdown: Done.");
}

void OverlayRenderer::OnReset() {
    if (!m_initialized)
        return;
    Logger::info("OverlayRenderer::OnReset: Invalidating device objects...");
    ImGui_ImplDX9_InvalidateDeviceObjects();
}

void OverlayRenderer::OnPostReset() {
    if (!m_initialized)
        return;
    Logger::info("OverlayRenderer::OnPostReset: Creating device objects...");
    ImGui_ImplDX9_CreateDeviceObjects();
}

void OverlayRenderer::NewFrame() {
    if (!m_initialized)
        return;
    m_uiRendered = false;

    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float, std::ratio<1, 1>>(currentTime - m_lastTime).count();
    m_lastTime = currentTime;

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = deltaTime > 0 ? deltaTime : 1.0f / 60.0f;

    // Toggle Visibility with Ctrl + HOME key or ` key (VK_OEM_3)
    bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool homePressed = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
    bool backtickPressed = (GetAsyncKeyState(VK_OEM_3) & 0x8000) != 0;
    bool keyCurrentlyPressed = (ctrlPressed && homePressed) || backtickPressed;

    if (keyCurrentlyPressed && !m_showKeyWasPressed) {
        m_visible = !m_visible;
        Logger::info("OverlayRenderer: Visibility toggled manually to: " + std::string(m_visible ? "ON" : "OFF"));
    }
    m_showKeyWasPressed = keyCurrentlyPressed;

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();

    if (m_visible && m_hWnd) {
        POINT cursorPos;
        if (GetCursorPos(&cursorPos)) {
            ScreenToClient(m_hWnd, &cursorPos);
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

    if (!m_visible) {
        io.ClearInputKeys();
    }

    ImGui::NewFrame();
}

void OverlayRenderer::Render(IDirect3DDevice9* device, uint32_t width, uint32_t height) {
    if (!m_initialized || m_uiRendered)
        return;

    static uint32_t renderCount = 0;
    if (renderCount++ % 600 == 0) {
        Logger::info("OverlayRenderer::Render called (Logged every 600 frames)");
    }

    m_uiRendered = true;

    // Use device viewport if width/height are zero
    if (width == 0 || height == 0) {
        D3DVIEWPORT9 vp;
        if (SUCCEEDED(device->GetViewport(&vp))) {
            width = vp.Width;
            height = vp.Height;
        }
    }

    if (m_visible) {
        g_isRenderingOverlay = true;
        ImGuiOverlayShared::DrawUI(width, height, [width, height, device]() {
            TextureReplacer::Get().RenderUI(device);
            ImGuiIO& io = ImGui::GetIO();
            UpscalerManager::Get().RenderUI(io.Framerate, width, height);
        });
    }

    ImGui::Render();

    if (m_visible) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g_isRenderingOverlay = false;
    }
}

LRESULT CALLBACK OverlayRenderer::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& renderer = OverlayRenderer::Get();

    if (renderer.m_visible) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return 1;
    }

    return CallWindowProc(renderer.m_originalWndProc, hWnd, msg, wParam, lParam);
}

} // namespace GamePlug
