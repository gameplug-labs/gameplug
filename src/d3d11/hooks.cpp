#include "hooks_common.h"

namespace GamePlug {

// Shared Global Hook Pointers Definition
PFN_QueryInterface g_OriginalQueryInterface = nullptr;
PFN_Release g_OriginalRelease = nullptr;
PFN_Present g_OriginalPresent = nullptr;
PFN_Present1 g_OriginalPresent1 = nullptr;
PFN_ResizeBuffers g_OriginalResizeBuffers = nullptr;
PFN_ResizeBuffers1 g_OriginalResizeBuffers1 = nullptr;
PFN_GetBuffer g_OriginalGetBuffer = nullptr;
PFN_CreateTexture2D g_OriginalCreateTexture2D = nullptr;
PFN_CreateRenderTargetView g_OriginalCreateRenderTargetView = nullptr;
PFN_RSSetViewports g_OriginalRSSetViewports = nullptr;
PFN_RSSetScissorRects g_OriginalRSSetScissorRects = nullptr;
PFN_OMSetRenderTargets g_OriginalOMSetRenderTargets = nullptr;
PFN_ClearRenderTargetView g_OriginalClearRenderTargetView = nullptr;
PFN_CreateDeferredContext g_OriginalCreateDeferredContext = nullptr;
PFN_GetImmediateContext g_OriginalGetImmediateContext = nullptr;
PFN_CreateSwapChain g_OriginalCreateSwapChain = nullptr;
PFN_CreateSwapChainForHwnd g_OriginalCreateSwapChainForHwnd = nullptr;
PFN_CreateSwapChainForComposition g_OriginalCreateSwapChainForComposition = nullptr;
PFN_CreateDXGIFactory g_OriginalCreateDXGIFactory = nullptr;
PFN_CreateDXGIFactory g_OriginalCreateDXGIFactory1 = nullptr;
PFN_CreateDXGIFactory2 g_OriginalCreateDXGIFactory2 = nullptr;

// Shared Global Variables Definition
uint64_t g_frameCount = 0;
bool g_IsResizing = false;
thread_local bool g_InHook = false;

std::mutex g_HookMtx;
std::set<void*> g_HookedVTables;

void InstallDXGIHooks() {
    Logger::info("InstallDXGIHooks (D3D11): Start");

    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "GamePlugDummyD3D11", NULL};
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(
        "GamePlugDummyD3D11", "GamePlug Dummy Window D3D11", WS_OVERLAPPEDWINDOW, 100, 100, 100, 100, NULL, NULL, wc.hInstance, NULL);

    IDXGISwapChain* swapChain = nullptr;
    IDXGIFactory4* dxgiFactory = nullptr;

    ID3D11Device* d3d11Device = nullptr;
    ID3D11DeviceContext* d3d11Context = nullptr;
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr =
        D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &d3d11Device, &featureLevel, &d3d11Context);
    if (FAILED(hr)) {
        Logger::error("DX Hooks D3D11: D3D11CreateDevice failed");
        goto cleanup;
    }

    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&dxgiFactory);
    if (FAILED(hr))
        goto cleanup;

    {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        hr = dxgiFactory->CreateSwapChain(d3d11Device, &sd, &swapChain);
    }

    if (swapChain) {
        void** pVTable = *(void***)swapChain;
        void** pFactoryVTable = *(void***)dxgiFactory;

        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            Logger::error("DX Hooks D3D11: MinHook failed to initialize");
        } else {
            ApplySwapChainHooks(swapChain);

            MH_CreateHook(pFactoryVTable[10], (LPVOID)HookedCreateSwapChain, (LPVOID*)&g_OriginalCreateSwapChain);

            IDXGIFactory2* factory2 = nullptr;
            if (SUCCEEDED(dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), (void**)&factory2))) {
                void** pFactory2VTable = *(void***)factory2;
                MH_CreateHook(pFactory2VTable[15], (LPVOID)HookedCreateSwapChainForHwnd, (LPVOID*)&g_OriginalCreateSwapChainForHwnd);
                MH_CreateHook(
                    pFactory2VTable[16], (LPVOID)HookedCreateSwapChainForComposition, (LPVOID*)&g_OriginalCreateSwapChainForComposition);
                factory2->Release();
            }

            if (d3d11Device) {
                void** pDevice11VTable = *(void***)d3d11Device;
                MH_CreateHook(pDevice11VTable[5], (LPVOID)HookedCreateTexture2D, (LPVOID*)&g_OriginalCreateTexture2D);
                Logger::info("DX Hooks D3D11: Hook ID3D11Device::CreateTexture2D (IDX 5): MH_OK");

                MH_CreateHook(pDevice11VTable[9], (LPVOID)HookedCreateRenderTargetView, (LPVOID*)&g_OriginalCreateRenderTargetView);
                Logger::info("DX Hooks D3D11: Hook ID3D11Device::CreateRenderTargetView (IDX 9): MH_OK");

                MH_CreateHook(pDevice11VTable[27], (LPVOID)HookedCreateDeferredContext, (LPVOID*)&g_OriginalCreateDeferredContext);
                Logger::info("DX Hooks D3D11: Hook ID3D11Device::CreateDeferredContext (IDX 27): MH_OK");

                MH_CreateHook(pDevice11VTable[40], (LPVOID)HookedGetImmediateContext, (LPVOID*)&g_OriginalGetImmediateContext);
                Logger::info("DX Hooks D3D11: Hook ID3D11Device::GetImmediateContext (IDX 40): MH_OK");
            }

            if (d3d11Context) {
                // Apply VTable patching directly to pre-initialize variables on the dummy context
                PatchDeviceContextVTable(d3d11Context);
            }

            MH_EnableHook(MH_ALL_HOOKS);
        }
    }

    HookDXGIFactories();

cleanup:
    if (swapChain)
        swapChain->Release();
    if (dxgiFactory)
        dxgiFactory->Release();
    if (d3d11Context)
        d3d11Context->Release();
    if (d3d11Device)
        d3d11Device->Release();
    DestroyWindow(hwnd);
    UnregisterClass("GamePlugDummyD3D11", wc.hInstance);
    Logger::info("InstallDXGIHooks (D3D11): End");
}

} // namespace GamePlug
