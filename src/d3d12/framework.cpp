#include "common.h"
#include "config.h"
#include "framework_export.h"

namespace GamePlug {
    extern void InstallDXGIHooks();

    void InitDX() {
        OutputDebugStringA("[GamePlug] framework.dll: InitDX starting...");
        Logger::Get().Init("gameplug.log");
        Logger::info("DirectX Framework Initialized");
        Config::Get().Load();
        
        InstallDXGIHooks();
        OutputDebugStringA("[GamePlug] framework.dll: InitDX complete (hooks installed).");
    }
}


extern "C" FRAMEWORK_API void StartFramework() {
    GamePlug::InitDX();
}
