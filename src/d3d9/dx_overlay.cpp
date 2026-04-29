#include "dx_overlay.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "logger.h"
#include "upscaler_manager.h"
#include "config.h"
#include "plugin_manager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace GamePlug {

static thread_local bool g_isRenderingOverlay = false;

bool OverlayRenderer::IsRenderingOverlay() { return g_isRenderingOverlay; }
void OverlayRenderer::SetIsRenderingOverlay(bool val) { g_isRenderingOverlay = val; }

OverlayRenderer& OverlayRenderer::Get() {
    static OverlayRenderer instance;
    return instance;
}

void OverlayRenderer::Init(IDirect3DDevice9* device) {
    if (m_initialized) return;
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
    
    PluginManager::Get().LoadPlugins();
}

void OverlayRenderer::Shutdown() {
    if (!m_initialized) return;
    Logger::info("OverlayRenderer::Shutdown: Starting teardown...");
    
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    if (m_originalWndProc) {
        SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)m_originalWndProc);
    }
    
    m_initialized = false;
    Logger::info("OverlayRenderer::Shutdown: Done.");
}

void OverlayRenderer::OnReset() {
    if (!m_initialized) return;
    Logger::info("OverlayRenderer::OnReset: Invalidating device objects...");
    ImGui_ImplDX9_InvalidateDeviceObjects();
}

void OverlayRenderer::OnPostReset() {
    if (!m_initialized) return;
    Logger::info("OverlayRenderer::OnPostReset: Creating device objects...");
    ImGui_ImplDX9_CreateDeviceObjects();
}

void OverlayRenderer::NewFrame() {
    if (!m_initialized) return;
    m_uiRendered = false;

    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float, std::ratio<1, 1>>(currentTime - m_lastTime).count();
    m_lastTime = currentTime;

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = deltaTime > 0 ? deltaTime : 1.0f / 60.0f;

    // Toggle Visibility with HOME key
    bool keyCurrentlyPressed = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
    if (keyCurrentlyPressed && !m_showKeyWasPressed) {
        m_visible = !m_visible;
        Logger::info("OverlayRenderer: Visibility toggled manually to: " + std::string(m_visible ? "ON" : "OFF"));
    }
    m_showKeyWasPressed = keyCurrentlyPressed;

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void OverlayRenderer::Render(IDirect3DDevice9* device, uint32_t width, uint32_t height) {
    if (!m_initialized || !m_visible || m_uiRendered) return;
    
    static uint32_t renderCount = 0;
    if (renderCount++ % 600 == 0) {
        Logger::info("OverlayRenderer::Render called (Logged every 600 frames)");
    }

    m_uiRendered = true;
    g_isRenderingOverlay = true;

    // Use device viewport if width/height are zero
    if (width == 0 || height == 0) {
        D3DVIEWPORT9 vp;
        if (SUCCEEDED(device->GetViewport(&vp))) {
            width = vp.Width;
            height = vp.Height;
        }
    }

    DrawUI(width, height);

    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    
    g_isRenderingOverlay = false;
}

void OverlayRenderer::DrawUI(uint32_t width, uint32_t height) {
    ImGuiIO& io = ImGui::GetIO();
    float uiScale = (std::max)(1.0f, (float)height / 720.0f);
    io.FontGlobalScale = uiScale;

    // Premium Styling
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f * uiScale;
    style.FrameRounding = 4.0f * uiScale;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.75f); 
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.29f, 0.48f, 0.90f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);

    ImGui::SetNextWindowPos(ImVec2(20.0f * uiScale, 20.0f * uiScale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f * uiScale, 150.0f * uiScale), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("GamePlug v0.1", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // 1. Master Toggle (at the top)
        Config::Get().RenderUI();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // 2. Render Upscaler UI
        UpscalerManager::Get().RenderUI(io.Framerate, width, height);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3. Render Plugins
        PluginManager::Get().RenderPlugins();
    }
    ImGui::End();
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
