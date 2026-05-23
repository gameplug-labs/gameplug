#include "hooks_common.h"

namespace GamePlug {

// Shared Global Hook Pointers Definition
PFN_QueryInterface g_OriginalQueryInterface = nullptr;
PFN_Release g_OriginalRelease = nullptr;
PFN_Present g_OriginalPresent = nullptr;
PFN_Present1 g_OriginalPresent1 = nullptr;
PFN_ResizeBuffers g_OriginalResizeBuffers = nullptr;
PFN_ResizeBuffers1 g_OriginalResizeBuffers1 = nullptr;
PFN_ExecuteCommandLists g_OriginalExecuteCommandLists = nullptr;
PFN_Signal g_OriginalSignal = nullptr;
PFN_ResourceBarrier g_OriginalResourceBarrier = nullptr;
PFN_RSSetViewports g_OriginalRSSetViewports = nullptr;
PFN_RSSetScissorRects g_OriginalRSSetScissorRects = nullptr;
PFN_OMSetRenderTargets g_OriginalOMSetRenderTargets = nullptr;
PFN_CreateCommittedResource g_OriginalCreateCommittedResource = nullptr;
PFN_CreatePlacedResource g_OriginalCreatePlacedResource = nullptr;

PFN_CreateSwapChain g_OriginalCreateSwapChain = nullptr;
PFN_CreateSwapChainForHwnd g_OriginalCreateSwapChainForHwnd = nullptr;
PFN_CreateSwapChainForComposition g_OriginalCreateSwapChainForComposition = nullptr;
PFN_CreateRenderTargetView g_OriginalCreateRenderTargetView = nullptr;
PFN_CreateDXGIFactory g_OriginalCreateDXGIFactory = nullptr;
PFN_CreateDXGIFactory g_OriginalCreateDXGIFactory1 = nullptr;
PFN_CreateDXGIFactory2 g_OriginalCreateDXGIFactory2 = nullptr;

// Shared Global Variables Definition
extern ID3D12Device* g_pd3d12Device;
extern ID3D12CommandQueue* g_pd3dCommandQueue;
uint64_t g_frameCount = 0;
bool g_IsResizing = false;
thread_local bool g_InHook = false;

std::unordered_map<SIZE_T, ID3D12Resource*> g_RTVToResource;
std::unordered_map<ID3D12GraphicsCommandList*, ID3D12Resource*> g_CommandListTargets;
std::set<ID3D12Resource*> g_OverriddenResources;
std::set<ID3D12Resource*> g_NativeResources;
std::mutex g_TrackingMtx;
std::recursive_mutex g_HooksMtx;
std::map<ID3D12GraphicsCommandList*, std::pair<uint32_t, uint32_t>> g_CommandListRTSize;
std::mutex g_HookMtx;
std::set<void*> g_HookedVTables;
uint32_t g_CommandQueueOffset = 0;
void* g_LastHookedSwapChain = nullptr;

void InstallDXGIHooks() {
    Logger::info("InstallDXGIHooks: Start (Signal-Sync Logic Active)");

    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "GamePlugDummy", NULL};
    RegisterClassEx(&wc);
    HWND hwnd =
        CreateWindow("GamePlugDummy", "GamePlug Dummy Window", WS_OVERLAPPEDWINDOW, 100, 100, 100, 100, NULL, NULL, wc.hInstance, NULL);

    ID3D12Device* d3d12Device = nullptr;
    ID3D12CommandQueue* commandQueue = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    IDXGIFactory4* dxgiFactory = nullptr;
    ID3D12CommandAllocator* commandAllocator = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;

    HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&d3d12Device);
    if (FAILED(hr)) {
        Logger::error("DX Hooks: D3D12CreateDevice failed");
        // Don't goto cleanup yet, we might still want DX11
    }

    if (!d3d12Device)
        goto cleanup;

    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr = d3d12Device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), (void**)&commandQueue);
        if (FAILED(hr))
            goto cleanup;

        void** pQueueVTable = *(void***)commandQueue;
        MH_CreateHook(pQueueVTable[12], (LPVOID)HookedExecuteCommandLists, (LPVOID*)&g_OriginalExecuteCommandLists);
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

        hr = dxgiFactory->CreateSwapChain(commandQueue, &sd, &swapChain);
    }

    if (swapChain) {
        void** pVTable = *(void***)swapChain;
        void** pFactoryVTable = *(void***)dxgiFactory;

        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            Logger::error("DX Hooks: MinHook failed to initialize");
        } else {
            // SwapChain hooks are now Instance-based shadow VTable patching
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

            // Phase 17: Global Queue Hooks deactivated to restore 'Previous' stability

            // Create a dummy command list to get the VTable for D3D12 Graphics methods
            hr = d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
            if (SUCCEEDED(hr)) {
                hr = d3d12Device->CreateCommandList(
                    0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
                if (SUCCEEDED(hr)) {
                    void** pListVTable = *(void***)commandList;

                    // Hook RSSetViewports (21) and RSSetScissorRects (22) for centering fixes
                    MH_CreateHook(pListVTable[21], (LPVOID)HookedRSSetViewports, (LPVOID*)&g_OriginalRSSetViewports);
                    MH_CreateHook(pListVTable[22], (LPVOID)HookedRSSetScissorRects, (LPVOID*)&g_OriginalRSSetScissorRects);

                    // Hook ResourceBarrier (Index 26) - Most stable for discovery
                    MH_CreateHook(pListVTable[26], (LPVOID)HookedResourceBarrier, (LPVOID*)&g_OriginalResourceBarrier);

                    MH_CreateHook(pListVTable[33], (LPVOID)HookedOMSetRenderTargets, (LPVOID*)&g_OriginalOMSetRenderTargets);

                    Logger::info(
                        "DX Hooks: Hooked RSSetViewports (21), RSSetScissorRects (22), ResourceBarrier (26), OMSetRenderTargets (33)");
                }
            }

            if (d3d12Device) {
                void** pDeviceVTable = *(void***)d3d12Device;
                MH_CreateHook(pDeviceVTable[7], (LPVOID)HookedCreateRenderTargetView, (LPVOID*)&g_OriginalCreateRenderTargetView);
                MH_CreateHook(pDeviceVTable[8], (LPVOID)HookedCreatePlacedResource, (LPVOID*)&g_OriginalCreatePlacedResource);
                MH_CreateHook(pDeviceVTable[27], (LPVOID)HookedCreateCommittedResource, (LPVOID*)&g_OriginalCreateCommittedResource);
                Logger::info(
                    "DX Hooks: Hook ID3D12Device::CreateCommittedResource(27), CreatePlacedResource(8), CreateRenderTargetView(7)");
            }

            MH_EnableHook(MH_ALL_HOOKS);
        }
    }

    HookDXGIFactories();

cleanup:
    if (commandList)
        commandList->Release();
    if (commandAllocator)
        commandAllocator->Release();
    if (swapChain)
        swapChain->Release();
    if (dxgiFactory)
        dxgiFactory->Release();
    if (commandQueue)
        commandQueue->Release();
    if (d3d12Device)
        d3d12Device->Release();

    DestroyWindow(hwnd);
    UnregisterClass("GamePlugDummy", wc.hInstance);
    Logger::info("InstallDXGIHooks: End");
}

} // namespace GamePlug
