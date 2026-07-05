#pragma once

#include "d3d9_proxy.h"
#include <map>

// Forward declarations
class ProxySurface9;
class ProxyDirect3DSwapChain9;

// --- Combined Proxy Device (Supports Ex) ---
class ProxyDirect3DDevice9 : public IDirect3DDevice9Ex {
    IDirect3DDevice9* m_pReal;
    IDirect3DDevice9Ex* m_pRealEx;
    IDirect3D9* m_pParent;
    HWND m_hFocusWindow;
    ProxySurface9* m_pFakeBackBuffer;
    uint32_t m_renderW, m_renderH;
    uint32_t m_displayW, m_displayH;
    bool m_isUpscaling;
    bool m_jitterReadyForFrame = false;
    std::map<IDirect3DSurface9*, IDirect3DTexture9*> m_depthSurfaceToTextureMap;
    IDirect3DTexture9* m_downsampledDepthTexINTZ = nullptr;

    void UpdateScaledResolution();
    void UpdateJitterAndFrameIndex();
    void PerformDepthDownsampling();

public:
    IDirect3DDevice9* GetRealDevice() const { return m_pReal; }
    bool IsUpscaling() const { return m_isUpscaling; }
    ProxySurface9* GetFakeBackBuffer() const { return m_pFakeBackBuffer; }
    ProxyDirect3DDevice9(
        IDirect3DDevice9* pReal, IDirect3D9* pParent, HWND hFocusWindow, uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh, bool upscale);
    virtual ~ProxyDirect3DDevice9();

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IDirect3DDevice9 methods
    STDMETHOD(TestCooperativeLevel)() override;
    STDMETHOD_(UINT, GetAvailableTextureMem)() override;
    STDMETHOD(EvictManagedResources)() override;
    STDMETHOD(GetDirect3D)(IDirect3D9** ppD3D9) override;
    STDMETHOD(GetDeviceCaps)(D3DCAPS9* pCaps) override;
    STDMETHOD(GetDisplayMode)(UINT iSwapChain, D3DDISPLAYMODE* pMode) override;
    STDMETHOD(GetCreationParameters)(D3DDEVICE_CREATION_PARAMETERS* pParameters) override;
    STDMETHOD(SetCursorProperties)(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9* pCursorBitmap) override;
    STDMETHOD_(void, SetCursorPosition)(int X, int Y, DWORD Flags) override;
    STDMETHOD_(BOOL, ShowCursor)(BOOL bShow) override;
    STDMETHOD(CreateAdditionalSwapChain)(D3DPRESENT_PARAMETERS* pPP, IDirect3DSwapChain9** ppSC) override;
    STDMETHOD(GetSwapChain)(UINT iSC, IDirect3DSwapChain9** ppSC) override;
    STDMETHOD_(UINT, GetNumberOfSwapChains)() override;
    STDMETHOD(Reset)(D3DPRESENT_PARAMETERS* pPP) override;
    STDMETHOD(Present)(CONST RECT* pSR, CONST RECT* pDR, HWND hW, CONST RGNDATA* pR) override;
    STDMETHOD(GetBackBuffer)(UINT iSC, UINT iBB, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9** ppBB) override;
    STDMETHOD(GetRasterStatus)(UINT iSC, D3DRASTER_STATUS* pRS) override;
    STDMETHOD(SetDialogBoxMode)(BOOL bE) override;
    STDMETHOD_(void, SetGammaRamp)(UINT iSC, DWORD F, CONST D3DGAMMARAMP* pR) override;
    STDMETHOD_(void, GetGammaRamp)(UINT iSC, D3DGAMMARAMP* pR) override;
    STDMETHOD(CreateTexture)(UINT W, UINT H, UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DTexture9** ppT, HANDLE* pS) override;
    STDMETHOD(CreateVolumeTexture)(
        UINT W, UINT H, UINT D, UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DVolumeTexture9** ppVT, HANDLE* pS) override;
    STDMETHOD(CreateCubeTexture)(UINT E, UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DCubeTexture9** ppCT, HANDLE* pS) override;
    STDMETHOD(CreateVertexBuffer)(UINT L, DWORD U, DWORD F, D3DPOOL P, IDirect3DVertexBuffer9** ppVB, HANDLE* pS) override;
    STDMETHOD(CreateIndexBuffer)(UINT L, DWORD U, D3DFORMAT F, D3DPOOL P, IDirect3DIndexBuffer9** ppIB, HANDLE* pS) override;
    STDMETHOD(CreateRenderTarget)(
        UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL L, IDirect3DSurface9** ppS, HANDLE* pS2) override;
    STDMETHOD(CreateDepthStencilSurface)(
        UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL D, IDirect3DSurface9** ppS, HANDLE* pS2) override;
    STDMETHOD(UpdateSurface)(IDirect3DSurface9* pSS, CONST RECT* pSR, IDirect3DSurface9* pDS, CONST POINT* pDP) override;
    STDMETHOD(UpdateTexture)(IDirect3DBaseTexture9* pST, IDirect3DBaseTexture9* pDT) override;
    STDMETHOD(GetRenderTargetData)(IDirect3DSurface9* pRT, IDirect3DSurface9* pDS) override;
    STDMETHOD(GetFrontBufferData)(UINT iSC, IDirect3DSurface9* pDS) override;
    STDMETHOD(StretchRect)(
        IDirect3DSurface9* pSS, CONST RECT* pSR, IDirect3DSurface9* pDS, CONST RECT* pDR, D3DTEXTUREFILTERTYPE F) override;
    STDMETHOD(ColorFill)(IDirect3DSurface9* pS, CONST RECT* pR, D3DCOLOR c) override;
    STDMETHOD(CreateOffscreenPlainSurface)(UINT W, UINT H, D3DFORMAT F, D3DPOOL P, IDirect3DSurface9** ppS, HANDLE* pS2) override;
    STDMETHOD(SetRenderTarget)(DWORD RTI, IDirect3DSurface9* pRT) override;
    STDMETHOD(GetRenderTarget)(DWORD RTI, IDirect3DSurface9** ppRT) override;
    STDMETHOD(SetDepthStencilSurface)(IDirect3DSurface9* pNZS) override;
    STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9** ppZSS) override;
    STDMETHOD(BeginScene)() override;
    STDMETHOD(EndScene)() override;
    STDMETHOD(Clear)(DWORD C, CONST D3DRECT* pR, DWORD F, D3DCOLOR C2, float Z, DWORD S) override;
    STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE S, CONST D3DMATRIX* pM) override;
    STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE S, D3DMATRIX* pM) override;
    STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE S, CONST D3DMATRIX* pM) override;
    STDMETHOD(SetViewport)(CONST D3DVIEWPORT9* pV) override;
    STDMETHOD(GetViewport)(D3DVIEWPORT9* pV) override;
    STDMETHOD(SetMaterial)(CONST D3DMATERIAL9* pM) override;
    STDMETHOD(GetMaterial)(D3DMATERIAL9* pM) override;
    STDMETHOD(SetLight)(DWORD I, CONST D3DLIGHT9* pL) override;
    STDMETHOD(GetLight)(DWORD I, D3DLIGHT9* pL) override;
    STDMETHOD(LightEnable)(DWORD I, BOOL E) override;
    STDMETHOD(GetLightEnable)(DWORD I, BOOL* pE) override;
    STDMETHOD(SetClipPlane)(DWORD I, CONST float* pP) override;
    STDMETHOD(GetClipPlane)(DWORD I, float* pP) override;
    STDMETHOD(SetRenderState)(D3DRENDERSTATETYPE S, DWORD V) override;
    STDMETHOD(GetRenderState)(D3DRENDERSTATETYPE S, DWORD* pV) override;
    STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE T, IDirect3DStateBlock9** ppSB) override;
    STDMETHOD(BeginStateBlock)() override;
    STDMETHOD(EndStateBlock)(IDirect3DStateBlock9** ppSB) override;
    STDMETHOD(SetClipStatus)(CONST D3DCLIPSTATUS9* pCS) override;
    STDMETHOD(GetClipStatus)(D3DCLIPSTATUS9* pCS) override;
    STDMETHOD(GetTexture)(DWORD S, IDirect3DBaseTexture9** ppT) override;
    STDMETHOD(SetTexture)(DWORD S, IDirect3DBaseTexture9* pT) override;
    STDMETHOD(GetTextureStageState)(DWORD S, D3DTEXTURESTAGESTATETYPE T, DWORD* pV) override;
    STDMETHOD(SetTextureStageState)(DWORD S, D3DTEXTURESTAGESTATETYPE T, DWORD V) override;
    STDMETHOD(GetSamplerState)(DWORD S, D3DSAMPLERSTATETYPE T, DWORD* pV) override;
    STDMETHOD(SetSamplerState)(DWORD S, D3DSAMPLERSTATETYPE T, DWORD V) override;
    STDMETHOD(ValidateDevice)(DWORD* pNP) override;
    STDMETHOD(SetPaletteEntries)(UINT PN, CONST PALETTEENTRY* pE) override;
    STDMETHOD(GetPaletteEntries)(UINT PN, PALETTEENTRY* pE) override;
    STDMETHOD(SetCurrentTexturePalette)(UINT PN) override;
    STDMETHOD(GetCurrentTexturePalette)(UINT* PN) override;
    STDMETHOD(SetScissorRect)(CONST RECT* pR) override;
    STDMETHOD(GetScissorRect)(RECT* pR) override;
    STDMETHOD(SetSoftwareVertexProcessing)(BOOL bS) override;
    STDMETHOD_(BOOL, GetSoftwareVertexProcessing)() override;
    STDMETHOD(SetNPatchMode)(float nS) override;
    STDMETHOD_(float, GetNPatchMode)() override;
    STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE PT, UINT SV, UINT PC) override;
    STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE PT, INT BVI, UINT MVI, UINT NV, UINT SI, UINT PC) override;
    STDMETHOD(DrawPrimitiveUP)(D3DPRIMITIVETYPE PT, UINT PC, CONST void* pV, UINT VS) override;
    STDMETHOD(DrawIndexedPrimitiveUP)(
        D3DPRIMITIVETYPE PT, UINT MVI, UINT NV, UINT PC, CONST void* pI, D3DFORMAT IF, CONST void* pV, UINT VS) override;
    STDMETHOD(ProcessVertices)(UINT SSI, UINT DI, UINT VC, IDirect3DVertexBuffer9* pDB, IDirect3DVertexDeclaration9* pVD, DWORD F) override;
    STDMETHOD(CreateVertexDeclaration)(CONST D3DVERTEXELEMENT9* pVE, IDirect3DVertexDeclaration9** ppD) override;
    STDMETHOD(SetVertexDeclaration)(IDirect3DVertexDeclaration9* pD) override;
    STDMETHOD(GetVertexDeclaration)(IDirect3DVertexDeclaration9** ppD) override;
    STDMETHOD(SetFVF)(DWORD FVF) override;
    STDMETHOD(GetFVF)(DWORD* pFVF) override;
    STDMETHOD(CreateVertexShader)(CONST DWORD* pF, IDirect3DVertexShader9** ppS) override;
    STDMETHOD(SetVertexShader)(IDirect3DVertexShader9* pS) override;
    STDMETHOD(GetVertexShader)(IDirect3DVertexShader9** ppS) override;
    STDMETHOD(SetVertexShaderConstantF)(UINT SR, CONST float* pCD, UINT V4C) override;
    STDMETHOD(GetVertexShaderConstantF)(UINT SR, float* pCD, UINT V4C) override;
    STDMETHOD(SetVertexShaderConstantI)(UINT SR, CONST int* pCD, UINT V4C) override;
    STDMETHOD(GetVertexShaderConstantI)(UINT SR, int* pCD, UINT V4C) override;
    STDMETHOD(SetVertexShaderConstantB)(UINT SR, CONST BOOL* pCD, UINT BC) override;
    STDMETHOD(GetVertexShaderConstantB)(UINT SR, BOOL* pCD, UINT BC) override;
    STDMETHOD(SetStreamSource)(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) override;
    STDMETHOD(GetStreamSource)(UINT StreamNumber, IDirect3DVertexBuffer9** ppStreamData, UINT* pOffsetInBytes, UINT* pStride) override;
    STDMETHOD(SetStreamSourceFreq)(UINT SN, UINT S) override;
    STDMETHOD(GetStreamSourceFreq)(UINT SN, UINT* pS) override;
    STDMETHOD(SetIndices)(IDirect3DIndexBuffer9* pID) override;
    STDMETHOD(GetIndices)(IDirect3DIndexBuffer9** ppID) override;
    STDMETHOD(CreatePixelShader)(CONST DWORD* pF, IDirect3DPixelShader9** ppS) override;
    STDMETHOD(SetPixelShader)(IDirect3DPixelShader9* pS) override;
    STDMETHOD(GetPixelShader)(IDirect3DPixelShader9** ppS) override;
    STDMETHOD(SetPixelShaderConstantF)(UINT SR, CONST float* pCD, UINT V4C) override;
    STDMETHOD(GetPixelShaderConstantF)(UINT SR, float* pCD, UINT V4C) override;
    STDMETHOD(SetPixelShaderConstantI)(UINT SR, CONST int* pCD, UINT V4C) override;
    STDMETHOD(GetPixelShaderConstantI)(UINT SR, int* pCD, UINT V4C) override;
    STDMETHOD(SetPixelShaderConstantB)(UINT SR, CONST BOOL* pCD, UINT BC) override;
    STDMETHOD(GetPixelShaderConstantB)(UINT SR, BOOL* pCD, UINT BC) override;
    STDMETHOD(DrawRectPatch)(UINT H, CONST float* pNS, CONST D3DRECTPATCH_INFO* pPI) override;
    STDMETHOD(DrawTriPatch)(UINT H, CONST float* pNS, CONST D3DTRIPATCH_INFO* pPI) override;
    STDMETHOD(DeletePatch)(UINT H) override;
    STDMETHOD(CreateQuery)(D3DQUERYTYPE T, IDirect3DQuery9** ppQ) override;

    // IDirect3DDevice9Ex methods
    STDMETHOD(SetConvolutionMonoKernel)(UINT W, UINT H, float* R, float* C) override;
    STDMETHOD(ComposeRects)(IDirect3DSurface9* pS, IDirect3DSurface9* pD, IDirect3DVertexBuffer9* pSRD, UINT NR,
        IDirect3DVertexBuffer9* pDRD, D3DCOMPOSERECTSOP O, int XO, int YO) override;
    STDMETHOD(PresentEx)(CONST RECT* pSR, CONST RECT* pDR, HWND hW, CONST RGNDATA* pR, DWORD F) override;
    STDMETHOD(GetGPUThreadPriority)(INT* pP) override;
    STDMETHOD(SetGPUThreadPriority)(INT P) override;
    STDMETHOD(WaitForVBlank)(UINT iSC) override;
    STDMETHOD(CheckResourceResidency)(IDirect3DResource9** pRA, UINT32 NR) override;
    STDMETHOD(SetMaximumFrameLatency)(UINT ML) override;
    STDMETHOD(GetMaximumFrameLatency)(UINT* pML) override;
    STDMETHOD(CheckDeviceState)(HWND hDW) override;
    STDMETHOD(CreateRenderTargetEx)(
        UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL L, IDirect3DSurface9** ppS, HANDLE* pS2, DWORD U) override;
    STDMETHOD(CreateOffscreenPlainSurfaceEx)(
        UINT W, UINT H, D3DFORMAT F, D3DPOOL P, IDirect3DSurface9** ppS, HANDLE* pS2, DWORD U) override;
    STDMETHOD(CreateDepthStencilSurfaceEx)(
        UINT W, UINT H, D3DFORMAT F, D3DMULTISAMPLE_TYPE M, DWORD MQ, BOOL D, IDirect3DSurface9** ppS, HANDLE* pS2, DWORD U) override;
    STDMETHOD(ResetEx)(D3DPRESENT_PARAMETERS* pPP, D3DDISPLAYMODEEX* pFDM) override;
    STDMETHOD(GetDisplayModeEx)(UINT iSC, D3DDISPLAYMODEEX* pM, D3DDISPLAYROTATION* pR) override;
};
