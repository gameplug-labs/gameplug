#include "common.h"
#include "config.h"
#include "framework_export.h"
extern void InitializeHooks();

namespace GamePlug {

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
    GamePlug::InitDX();
}
