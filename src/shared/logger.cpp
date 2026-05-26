#include "logger.h"
#include <DbgHelp.h>
#include <algorithm>
#include <fstream>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <vector>
#include <windows.h>

#pragma comment(lib, "Dbghelp.lib")

namespace GamePlug {

std::shared_ptr<spdlog::logger> Logger::s_logger = nullptr;

namespace {
bool IsDebugEnabled() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::string path = std::string(buf);
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        path = path.substr(0, lastSlash + 1) + "GamePlug.conf";
    } else {
        path = "GamePlug.conf";
    }

    std::ifstream f(path);
    if (!f.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(f, line)) {
        auto trim = [](const std::string& s) {
            std::string res = s;
            res.erase(res.begin(), std::find_if(res.begin(), res.end(), [](unsigned char ch) { return !std::isspace(ch); }));
            res.erase(std::find_if(res.rbegin(), res.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), res.end());
            return res;
        };
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue;

        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        if (key == "Debug") {
            return (value == "true" || value == "1" || value == "on");
        }
    }
    return false;
}
} // namespace

LONG WINAPI GamePlugCrashHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    char buf[1024];
    void* addr = pExceptionInfo->ExceptionRecord->ExceptionAddress;
    DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;

    HMODULE hModule = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &hModule);

    char moduleName[MAX_PATH] = "Unknown";
    if (hModule) {
        GetModuleFileNameA(hModule, moduleName, MAX_PATH);
    }

    sprintf_s(buf,
        "\n\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
        "CRASH DETECTED!\n"
        "Exception Code: 0x%08X\n"
        "Fault Address: 0x%p\n"
        "Module: %s\n"
        "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n",
        code, addr, moduleName);

    Logger::error(buf);

    return EXCEPTION_CONTINUE_SEARCH;
}

void Logger::SetupCrashHandler() {
    SetUnhandledExceptionFilter(GamePlugCrashHandler);
    Logger::info("Crash handler initialized.");
}

void Logger::Init(const std::string& filename) {
    if (s_logger)
        return;

    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, true);
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        std::vector<spdlog::sink_ptr> sinks{file_sink, console_sink};
        s_logger = std::make_shared<spdlog::logger>("GamePlug", sinks.begin(), sinks.end());

        s_logger->set_level(spdlog::level::info);

        // Log date and time only once at the top
        s_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        s_logger->info("--- Logger Initialized: {} ---", filename);

        // Remove date and time from subsequent logs
        s_logger->set_pattern("[%l] %v");

        s_logger->flush_on(spdlog::level::info);

        spdlog::register_logger(s_logger);

        SetupCrashHandler();

        if (!IsDebugEnabled()) {
            file_sink->set_level(spdlog::level::off);
        }
    } catch (const spdlog::spdlog_ex& ex) {
        // Fallback
    }
}

void Logger::LogBridge(GamePlugUpscalerInterface::LogLevel level, const char* message, void* context) {
    std::string formattedMsg = "[Bridge] " + std::string(message);
    switch (level) {
        case GamePlugUpscalerInterface::LOG_INFO:  Logger::info(formattedMsg); break;
        case GamePlugUpscalerInterface::LOG_WARN:  Logger::warn(formattedMsg); break;
        case GamePlugUpscalerInterface::LOG_ERROR: Logger::error(formattedMsg); break;
        case GamePlugUpscalerInterface::LOG_DEBUG: Logger::debug(formattedMsg); break;
    }
}

} // namespace GamePlug
