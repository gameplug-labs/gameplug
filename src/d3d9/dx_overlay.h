#pragma once

#include <windows.h>
#include <d3d9.h>
#include <chrono>

namespace GamePlug {

class OverlayRenderer {
public:
    static OverlayRenderer& Get();

    void SetWindow(HWND hWnd) { m_hWnd = hWnd; }
    void Init(IDirect3DDevice9* device);
    void Shutdown();

    void OnReset();
    void OnPostReset();

    void NewFrame();
    void Render(IDirect3DDevice9* device, uint32_t width = 0, uint32_t height = 0);

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

    static bool IsRenderingOverlay();
    static void SetIsRenderingOverlay(bool val);

private:
    OverlayRenderer() = default;
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hWnd = nullptr;
    IDirect3DDevice9* m_pDevice = nullptr;
    WNDPROC m_originalWndProc = nullptr;
    bool m_initialized = false;
    bool m_visible = true;
    bool m_showKeyWasPressed = false;
    bool m_uiRendered = false;
    
    std::chrono::steady_clock::time_point m_lastTime;
};

} // namespace GamePlug
