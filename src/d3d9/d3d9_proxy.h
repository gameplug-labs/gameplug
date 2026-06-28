#pragma once

#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <stdarg.h>
#include <string>
#include <windows.h>

#include "config.h"
#include "dx_overlay.h"
#include "logger.h"

using namespace GamePlug;

// Forward declarations
class ProxyDirect3D9;
class ProxyDirect3DDevice9;
class ProxyDirect3DSwapChain9;
class ProxySurface9;
class ProxyTexture9;
typedef IDirect3D9*(WINAPI* PFN_Direct3DCreate9)(UINT SDKVersion);
typedef HRESULT(WINAPI* PFN_Direct3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex** ppDirect3D9Ex);

// Global flag to bypass scaling for upscaler passes
extern bool g_InUpscalerPass;

// Helper Functions
void GetScaledResolution(int& outW, int& outH);
void InitializeHooks();
