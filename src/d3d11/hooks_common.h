#pragma once
#include "common.h"
#include "config.h"
#include "minhook.h"
#include <d3d11.h>
#include <dxgi1_5.h>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include <functional>
#include <optional>

namespace GamePlug {

// Typedefs
typedef HRESULT(STDMETHODCALLTYPE* PFN_QueryInterface)(IUnknown* pUnk, REFIID riid, void** ppvObject);
typedef ULONG(STDMETHODCALLTYPE* PFN_Release)(IUnknown* pUnk);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Present1)(
    IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
typedef HRESULT(STDMETHODCALLTYPE* PFN_ResizeBuffers)(
    IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
typedef HRESULT(STDMETHODCALLTYPE* PFN_ResizeBuffers1)(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
    DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pNodeMask, IUnknown* const* ppPresentQueue);
typedef HRESULT(STDMETHODCALLTYPE* PFN_GetBuffer)(IDXGISwapChain* pSwapChain, UINT Buffer, REFIID riid, void** ppSurface);

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateTexture2D)(
    ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D);

typedef void(STDMETHODCALLTYPE* PFN_RSSetViewports)(ID3D11DeviceContext* pCtx, UINT NumViewports, const D3D11_VIEWPORT* pViewports);
typedef void(STDMETHODCALLTYPE* PFN_RSSetScissorRects)(ID3D11DeviceContext* pCtx, UINT NumRects, const D3D11_RECT* pRects);
typedef void(STDMETHODCALLTYPE* PFN_OMSetRenderTargets)(ID3D11DeviceContext* pCtx, UINT NumViews,
    ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView);
typedef void(STDMETHODCALLTYPE* PFN_OMSetRenderTargetsAndUnorderedAccessViews)(ID3D11DeviceContext* pCtx,
    UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView,
    UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts);
typedef void(STDMETHODCALLTYPE* PFN_ClearDepthStencilView)(
    ID3D11DeviceContext* pCtx, ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateDeferredContext)(
    ID3D11Device* pDevice, UINT ContextFlags, ID3D11DeviceContext** ppDeferredContext);
typedef void(STDMETHODCALLTYPE* PFN_GetImmediateContext)(ID3D11Device* pDevice, ID3D11DeviceContext** ppImmediateContext);

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChain)(
    IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChainForHwnd)(IDXGIFactory2* pFactory, IUnknown* pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChainForComposition)(IDXGIFactory2* pFactory, IUnknown* pDevice,
    const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);

typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID riid, void** ppFactory);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT Flags, REFIID riid, void** ppFactory);

// Shared Global Hook Pointers
extern PFN_QueryInterface g_OriginalQueryInterface;
extern PFN_Release g_OriginalRelease;
extern PFN_Present g_OriginalPresent;
extern PFN_Present1 g_OriginalPresent1;
extern PFN_ResizeBuffers g_OriginalResizeBuffers;
extern PFN_ResizeBuffers1 g_OriginalResizeBuffers1;
extern PFN_GetBuffer g_OriginalGetBuffer;
extern PFN_CreateTexture2D g_OriginalCreateTexture2D;
extern PFN_RSSetViewports g_OriginalRSSetViewports;
extern PFN_RSSetScissorRects g_OriginalRSSetScissorRects;
extern PFN_OMSetRenderTargets g_OriginalOMSetRenderTargets;
extern PFN_OMSetRenderTargetsAndUnorderedAccessViews g_OriginalOMSetRenderTargetsAndUnorderedAccessViews;
extern PFN_ClearDepthStencilView g_OriginalClearDepthStencilView;
extern PFN_CreateDeferredContext g_OriginalCreateDeferredContext;
extern PFN_GetImmediateContext g_OriginalGetImmediateContext;
extern PFN_CreateSwapChain g_OriginalCreateSwapChain;
extern PFN_CreateSwapChainForHwnd g_OriginalCreateSwapChainForHwnd;
extern PFN_CreateSwapChainForComposition g_OriginalCreateSwapChainForComposition;
extern PFN_CreateDXGIFactory g_OriginalCreateDXGIFactory;
extern PFN_CreateDXGIFactory g_OriginalCreateDXGIFactory1;
extern PFN_CreateDXGIFactory2 g_OriginalCreateDXGIFactory2;

// Shared Variables
extern uint64_t g_frameCount;
extern bool g_IsResizing;
extern thread_local bool g_InHook;

extern std::mutex g_HookMtx;
extern std::set<void*> g_HookedVTables;

// Forward Declarations of engine-integration functions
extern void OnDXPresent(IDXGISwapChain* pSwapChain);
extern void OnDXResize(IDXGISwapChain* pSwapChain);

// Recursion guard helper
struct ScopedRecursionGuard {
    ScopedRecursionGuard() { g_InHook = true; }
    ~ScopedRecursionGuard() { g_InHook = false; }
};

// Forward Declarations of Hook Functions and Helpers
ULONG STDMETHODCALLTYPE HookedRelease(IUnknown* pUnk);
bool ShouldOverrideD3D11(const D3D11_TEXTURE2D_DESC& desc);
HRESULT STDMETHODCALLTYPE HookedCreateTexture2D(
    ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D);
void STDMETHODCALLTYPE HookedRSSetViewports(ID3D11DeviceContext* pCtx, UINT NumViewports, const D3D11_VIEWPORT* pViewports);
void STDMETHODCALLTYPE HookedRSSetScissorRects(ID3D11DeviceContext* pCtx, UINT NumRects, const D3D11_RECT* pRects);
void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D11DeviceContext* pCtx, UINT NumViews,
    ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView);
void STDMETHODCALLTYPE HookedOMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext* pCtx,
    UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView,
    UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts);
HRESULT STDMETHODCALLTYPE HookedQueryInterface(IUnknown* pUnk, REFIID riid, void** ppvObject);
HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
HRESULT STDMETHODCALLTYPE HookedPresent1(
    IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
    IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
HRESULT STDMETHODCALLTYPE HookedResizeBuffers1(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
    DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pNodeMask, IUnknown* const* ppPresentQueue);
HRESULT STDMETHODCALLTYPE HookedGetBuffer(IDXGISwapChain* pSwapChain, UINT Buffer, REFIID riid, void** ppSurface);
void ApplySwapChainHooks(void* pSwapChain);
HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(
    IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain);
HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(IDXGIFactory2* pFactory, IUnknown* pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain);
HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForComposition(IDXGIFactory2* pFactory, IUnknown* pDevice,
    const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);
void HandleNewFactory(IUnknown* pFactory);
HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void** ppFactory);
HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void** ppFactory);
HRESULT WINAPI HookedCreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory);
void HookDXGIFactories();
void PatchDeviceContextVTable(ID3D11DeviceContext* context);
HRESULT STDMETHODCALLTYPE HookedCreateDeferredContext(ID3D11Device* pDevice, UINT ContextFlags, ID3D11DeviceContext** ppDeferredContext);
void STDMETHODCALLTYPE HookedGetImmediateContext(ID3D11Device* pDevice, ID3D11DeviceContext** ppImmediateContext);
HRESULT STDMETHODCALLTYPE HookedContextQueryInterface(ID3D11DeviceContext* pCtx, REFIID riid, void** ppvObject);
void STDMETHODCALLTYPE HookedClearDepthStencilView(
    ID3D11DeviceContext* pCtx, ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil);

} // namespace GamePlug
