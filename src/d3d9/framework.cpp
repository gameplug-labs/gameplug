#include "common.h"
#include "config.h"
#include "framework_export.h"
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

extern void InitializeHooks();

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace GamePlug {

static void SetupVulkanLayers() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW((HINSTANCE)&__ImageBase, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    std::wstring selfDir(buffer);

    // 1. Set VK_LAYER_PATH to the loader's directory
    SetEnvironmentVariableW(L"VK_LAYER_PATH", selfDir.c_str());

    // 2. Add our directory to PATH so vklayer.dll can find framework.dll/dependencies
    wchar_t oldPath[4096];
    DWORD pathLen = GetEnvironmentVariableW(L"PATH", oldPath, 4096);
    if (pathLen > 0 && pathLen < 4096) {
        std::wstring newPath = selfDir + L";" + oldPath;
        SetEnvironmentVariableW(L"PATH", newPath.c_str());
    } else {
        SetEnvironmentVariableW(L"PATH", selfDir.c_str());
    }

    // 3. Enable the layer
    SetEnvironmentVariableW(L"VK_INSTANCE_LAYERS", L"VK_LAYER_GAMEPLUG");
}

void InitDX() {
    OutputDebugStringA("[GamePlug] framework_d3d9: InitDX starting...");
    GamePlug::Logger::Get().Init("gameplug.log");
    GamePlug::Logger::info("DirectX 9 Framework Initialized");
    Config::Get().Load();

    ::InitializeHooks();
    OutputDebugStringA("[GamePlug] framework_d3d9: InitDX complete (hooks installed).");
}
} // namespace GamePlug

extern "C" FRAMEWORK_API void StartFramework() {
    GamePlug::SetupVulkanLayers();
    GamePlug::InitDX();
}

