#include "common.h"
#include "config.h"
#include "types.h"

namespace GamePlug {
FRAMEWORK_API void Init() {
    Logger::Get().Init("gameplug_vk.log");
    Logger::info("Framework Initialized");

    Config::Get().Load();

    if (volkInitialize() != VK_SUCCESS) {
        Logger::error("Failed to initialize volk");
    } else {
        Logger::info("volk initialized successfully");
    }
}
} // namespace GamePlug
