#include "common.h"
#include "types.h"
#include "config.h"

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
    }
}
