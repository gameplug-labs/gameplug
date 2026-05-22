#pragma once
#define GAMEPLUG_LOGGER_DEFINED

#include "framework_export.h"
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

namespace GamePlug {
FRAMEWORK_API void Init();

class FRAMEWORK_API Logger {
public:
    static void Init(const std::string& filename = "GamePlug.log");
    static void SetupCrashHandler();

    template <typename... Args> static void info(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger)
            s_logger->info(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> static void warn(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger)
            s_logger->warn(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> static void error(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger)
            s_logger->error(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args> static void debug(fmt::format_string<Args...> fmt, Args&&... args) {
        if (s_logger)
            s_logger->debug(fmt, std::forward<Args>(args)...);
    }

    static void info(const std::string& msg) {
        if (s_logger)
            s_logger->info("{}", msg);
    }

    static void warn(const std::string& msg) {
        if (s_logger)
            s_logger->warn("{}", msg);
    }

    static void error(const std::string& msg) {
        if (s_logger)
            s_logger->error("{}", msg);
    }

    static void debug(const std::string& msg) {
        if (s_logger)
            s_logger->debug("{}", msg);
    }

    // Compatibility shim for old Logger::Get().Init()
    struct GetShim {
        void Init(const std::string& filename) { Logger::Init(filename); }
    };
    static GetShim Get() { return GetShim(); }

private:
    static std::shared_ptr<spdlog::logger> s_logger;
};

} // namespace GamePlug

// Helper macros removed as per user request to use Logger::info, etc.
