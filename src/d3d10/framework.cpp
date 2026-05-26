#include "common.h"
#include "config.h"
#include "framework_export.h"

namespace GamePlug {
extern void InstallDXGIHooks();

void InitDX() {
    OutputDebugStringA("[GamePlug] framework.dll: InitDX starting (D3D10)...");
    Logger::Get().Init("gameplug.log");
    Logger::info("DirectX 10 Framework Initialized");
    Config::Get().Load();

    InstallDXGIHooks();
    OutputDebugStringA("[GamePlug] framework.dll: InitDX complete (D3D10 hooks installed).");
}
} // namespace GamePlug

extern "C" FRAMEWORK_API void StartFramework() {
    GamePlug::InitDX();
}
