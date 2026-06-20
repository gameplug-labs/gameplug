#pragma once
#include "common.h"
#include "config.h"
#include "minhook.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

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
typedef void(STDMETHODCALLTYPE* PFN_ExecuteCommandLists)(
    ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Signal)(ID3D12CommandQueue* pQueue, ID3D12Fence* pFence, UINT64 Value);
typedef void(STDMETHODCALLTYPE* PFN_ResourceBarrier)(
    ID3D12GraphicsCommandList* pList, UINT NumBarriers, const D3D12_RESOURCE_BARRIER* pBarriers);
typedef void(STDMETHODCALLTYPE* PFN_RSSetViewports)(ID3D12GraphicsCommandList* pList, UINT NumViewports, const D3D12_VIEWPORT* pViewports);
typedef void(STDMETHODCALLTYPE* PFN_RSSetScissorRects)(ID3D12GraphicsCommandList* pList, UINT NumRects, const D3D12_RECT* pRects);
typedef void(STDMETHODCALLTYPE* PFN_OMSetRenderTargets)(ID3D12GraphicsCommandList* pList, UINT NumRenderTargetDescriptors,
    const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors, BOOL RTsSingleHandleToDescriptorRange,
    const D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor);

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateCommandQueue)(
    ID3D12Device* pDevice, const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateCommittedResource)(ID3D12Device* pDevice, const D3D12_HEAP_PROPERTIES* pHeapProperties,
    D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState,
    const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource, void** ppvResource);

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreatePlacedResource)(ID3D12Device* pDevice, ID3D12Heap* pHeap, UINT64 HeapOffset,
    const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    REFIID riidResource, void** ppvResource);

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChain)(
    IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChainForHwnd)(IDXGIFactory2* pFactory, IUnknown* pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChainForComposition)(IDXGIFactory2* pFactory, IUnknown* pDevice,
    const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);

typedef void(STDMETHODCALLTYPE* PFN_CreateRenderTargetView)(ID3D12Device* pDevice, ID3D12Resource* pResource,
    const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_CreateDepthStencilView)(ID3D12Device* pDevice, ID3D12Resource* pResource,
    const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_CopyDescriptorsSimple)(ID3D12Device* pDevice, UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
typedef void(STDMETHODCALLTYPE* PFN_CopyDescriptors)(ID3D12Device* pDevice, UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts, const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts, const UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);

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
extern PFN_ExecuteCommandLists g_OriginalExecuteCommandLists;
extern PFN_Signal g_OriginalSignal;
extern PFN_ResourceBarrier g_OriginalResourceBarrier;
extern PFN_RSSetViewports g_OriginalRSSetViewports;
extern PFN_RSSetScissorRects g_OriginalRSSetScissorRects;
extern PFN_OMSetRenderTargets g_OriginalOMSetRenderTargets;
extern PFN_CreateCommandQueue g_OriginalCreateCommandQueue;
extern PFN_CreateCommittedResource g_OriginalCreateCommittedResource;
extern PFN_CreatePlacedResource g_OriginalCreatePlacedResource;

extern PFN_CreateSwapChain g_OriginalCreateSwapChain;
extern PFN_CreateSwapChainForHwnd g_OriginalCreateSwapChainForHwnd;
extern PFN_CreateSwapChainForComposition g_OriginalCreateSwapChainForComposition;
extern PFN_CreateRenderTargetView g_OriginalCreateRenderTargetView;
extern PFN_CreateDepthStencilView g_OriginalCreateDepthStencilView;
extern PFN_CopyDescriptorsSimple g_OriginalCopyDescriptorsSimple;
extern PFN_CopyDescriptors g_OriginalCopyDescriptors;
extern PFN_CreateDXGIFactory g_OriginalCreateDXGIFactory;
extern PFN_CreateDXGIFactory g_OriginalCreateDXGIFactory1;
extern PFN_CreateDXGIFactory2 g_OriginalCreateDXGIFactory2;

// Shared Variables
extern ID3D12Device* g_pd3d12Device;
extern ID3D12CommandQueue* g_pd3dCommandQueue;
extern uint64_t g_frameCount;
extern bool g_IsResizing;
extern thread_local bool g_InHook;

extern std::unordered_map<SIZE_T, ID3D12Resource*> g_RTVToResource;
extern std::unordered_map<SIZE_T, ID3D12Resource*> g_DSVToResource;
extern std::unordered_map<ID3D12GraphicsCommandList*, ID3D12Resource*> g_CommandListTargets;
extern std::set<ID3D12Resource*> g_OverriddenResources;
extern std::set<ID3D12Resource*> g_NativeResources;
extern std::set<ID3D12CommandQueue*> g_AllTrackedQueues;
extern std::mutex g_TrackingMtx;
extern std::recursive_mutex g_HooksMtx;
extern std::map<ID3D12GraphicsCommandList*, std::pair<uint32_t, uint32_t>> g_CommandListRTSize;
extern std::map<ID3D12GraphicsCommandList*, bool> g_CommandListShouldScale;
extern std::mutex g_HookMtx;
extern std::set<void*> g_HookedVTables;
extern uint32_t g_CommandQueueOffset;
extern void* g_LastHookedSwapChain;

// Forward Declarations of engine-integration functions
extern void OnDXPresent(IDXGISwapChain* pSwapChain);
extern void OnDXResize(IDXGISwapChain* pSwapChain);
extern void SetDX12CommandQueue(ID3D12CommandQueue* pQueue);
extern void SetDX12CommandQueueOffset(uint32_t offset);
extern ID3D12CommandList* OnDXExecute(ID3D12CommandQueue* pQueue, bool isSignal);
extern std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> g_ResourceStates;

D3D12_GPU_DESCRIPTOR_HANDLE GetImGuiSRVForResource(ID3D12Resource* pRes);

// Recursion guard helper
struct ScopedRecursionGuard {
    ScopedRecursionGuard() { g_InHook = true; }
    ~ScopedRecursionGuard() { g_InHook = false; }
};

// Forward Declarations of Hook Functions and Helpers
void STDMETHODCALLTYPE HookedRSSetViewports(ID3D12GraphicsCommandList* pList, UINT NumViewports, const D3D12_VIEWPORT* pViewports);
void STDMETHODCALLTYPE HookedRSSetScissorRects(ID3D12GraphicsCommandList* pList, UINT NumRects, const D3D12_RECT* pRects);
void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D12GraphicsCommandList* pList, UINT NumRTs, const D3D12_CPU_DESCRIPTOR_HANDLE* pRTVs,
    BOOL singleHandle, const D3D12_CPU_DESCRIPTOR_HANDLE* pDSV);
void STDMETHODCALLTYPE HookedCreateRenderTargetView(ID3D12Device* pDevice, ID3D12Resource* pResource,
    const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
void STDMETHODCALLTYPE HookedCreateDepthStencilView(ID3D12Device* pDevice, ID3D12Resource* pResource,
    const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
void STDMETHODCALLTYPE HookedCopyDescriptorsSimple(ID3D12Device* pDevice, UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
void STDMETHODCALLTYPE HookedCopyDescriptors(ID3D12Device* pDevice, UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts, const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts, const UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
void STDMETHODCALLTYPE HookedExecuteCommandLists(
    ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
void HookCommandQueue(ID3D12CommandQueue* pQueue);
void SyncAllDX12Queues();
void ClearActiveQueues();
ULONG STDMETHODCALLTYPE HookedRelease(IUnknown* pUnk);
void STDMETHODCALLTYPE HookedResourceBarrier(ID3D12GraphicsCommandList* pList, UINT NumBarriers, const D3D12_RESOURCE_BARRIER* pBarriers);
bool ShouldOverrideD3D12(const D3D12_RESOURCE_DESC& desc);
HRESULT STDMETHODCALLTYPE HookedCreateCommandQueue(
    ID3D12Device* pDevice, const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue);

HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource(ID3D12Device* pDevice, const D3D12_HEAP_PROPERTIES* pHeapProperties,
    D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState,
    const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource, void** ppvResource);
HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource(ID3D12Device* pDevice, ID3D12Heap* pHeap, UINT64 HeapOffset,
    const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    REFIID riidResource, void** ppvResource);

HRESULT STDMETHODCALLTYPE HookedQueryInterface(IUnknown* pUnk, REFIID riid, void** ppvObject);
HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
HRESULT STDMETHODCALLTYPE HookedPresent1(
    IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
    IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
HRESULT STDMETHODCALLTYPE HookedResizeBuffers1(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
    DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pNodeMask, IUnknown* const* ppPresentQueue);
HRESULT STDMETHODCALLTYPE HookedGetBuffer(IDXGISwapChain* pSwapChain, UINT Buffer, REFIID riid, void** ppSurface);
void RegisterNativeResources(IDXGISwapChain* pSwapChain);
void ApplySwapChainHooks(void* pSwapChain);
void ApplySwapChainHooksWithQueue(void* pSwapChain, ID3D12CommandQueue* pQueue);
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

void SetCommandListRTSize(ID3D12GraphicsCommandList* pList, uint32_t w, uint32_t h);
std::pair<uint32_t, uint32_t> GetCommandListRTSize(ID3D12GraphicsCommandList* pList);

} // namespace GamePlug
