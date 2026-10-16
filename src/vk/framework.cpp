#include "common.h"
#include "config.h"
#include "types.h"
#include "plugin_manager.h"

namespace GamePlug {
FRAMEWORK_API void Init() {
    Logger::Get().Init("gameplug.log");
    Logger::info("Framework Initialized");

    Config::Get().Load();

    if (volkInitialize() != VK_SUCCESS) {
        Logger::error("Failed to initialize volk");
    } else {
        Logger::info("volk initialized successfully");
    }

    // Load plugins early so that all startup hooks are active and called
    PluginManager::Get().LoadPlugins();
}
} // namespace GamePlug
