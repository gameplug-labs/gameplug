#pragma once

#ifdef _WIN32
#define GamePlug_UPSCALER_API __declspec(dllexport)
#else
#define GamePlug_UPSCALER_API
#endif

#include <imgui.h>
#include <string>

#ifdef GamePlug_VULKAN
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <volk.h>
#endif

extern "C" {

struct GamePlugUpscalerInterface {
    // Current interface version.
    int InterfaceVersion; // v1: Specialized Upscaler Interface

    // Returns a human-readable name for the upscaler.
    const char*(__cdecl* GetName)();

    enum LogLevel { LOG_INFO, LOG_WARN, LOG_ERROR, LOG_DEBUG };

    // Called once when the upscaler is loaded.
    void(__cdecl* OnInit)(ImGuiContext* imguiContext, void(__cdecl* log)(LogLevel level, const char* message, void* context),
        void* logContext, uintptr_t instance, uintptr_t physicalDevice, uintptr_t device, uintptr_t queue, uintptr_t queueFamilyIndex,
        const void* physicalDeviceMemoryProperties);

    // Called once per frame when the overlay is visible.
    void(__cdecl* OnImGuiRender)();

    // Called once before the upscaler is unloaded.
    void(__cdecl* OnShutdown)();

    // Called whenever any configuration field is modified.
    void(__cdecl* OnFieldsChanged)();

    // Specialized Rendering Hook for Upscalers
    // Includes depth, motion vectors, and jitter.
    void(__cdecl* OnRenderFrame)(uintptr_t commandBuffer, uint64_t source, uint64_t target, uint32_t targetFormat, uint32_t width,
        uint32_t height, uint32_t renderWidth, uint32_t renderHeight, uint64_t depth, uint32_t depthFormat, uint64_t motionVectors,
        uint32_t motionVectorFormat, float jitterX, float jitterY, float cameraNear, float cameraFar, float cameraFov,
        float viewSpaceToMetersFactor, bool invertedDepth, bool hdr);

    // Metadata for configuration fields
    struct FieldDescriptor {
        const char* Name;
        const char* Category;
        int Type; // Mapping to GamePlugPluginInterface::FieldType
        void* Data;
        int DataSize;
        const char* Options;
    };

    // Returns the number of fields and a pointer to an array of descriptors.
    int(__cdecl* GetFields)(FieldDescriptor** outFields);

    // Present callback
    bool(__cdecl* OnPresent)(uintptr_t swapChain, uint32_t syncInterval, uint32_t flags);
};

#define GamePlug_UPSCALER_INTERFACE_VERSION 1
#define GamePlug_GET_UPSCALER_INTERFACE_NAME "GamePlug_GetUpscalerInterface"

} // extern "C"

#ifdef __cplusplus
#if !defined(FRAMEWORK_EXPORTS) && !defined(GamePlug_INTERNAL_LOGGER) && !defined(GAMEPLUG_LOGGER_DEFINED)
namespace GamePlug {
typedef void(__cdecl* LogFuncPtr)(GamePlugUpscalerInterface::LogLevel, const char*, void*);

class Logger {
public:
    static void info(const char* msg) {
        if (get_log())
            get_log()(GamePlugUpscalerInterface::LOG_INFO, msg, get_ctx());
    }
    static void warn(const char* msg) {
        if (get_log())
            get_log()(GamePlugUpscalerInterface::LOG_WARN, msg, get_ctx());
    }
    static void error(const char* msg) {
        if (get_log())
            get_log()(GamePlugUpscalerInterface::LOG_ERROR, msg, get_ctx());
    }
    static void debug(const char* msg) {
        if (get_log())
            get_log()(GamePlugUpscalerInterface::LOG_DEBUG, msg, get_ctx());
    }

    static void info(const std::string& msg) { info(msg.c_str()); }
    static void warn(const std::string& msg) { warn(msg.c_str()); }
    static void error(const std::string& msg) { error(msg.c_str()); }
    static void debug(const std::string& msg) { debug(msg.c_str()); }

    static void set_log(LogFuncPtr func, void* context) {
        get_log_ref() = func;
        get_ctx_ref() = context;
    }

private:
    static LogFuncPtr& get_log_ref() {
        static LogFuncPtr logFunc = nullptr;
        return logFunc;
    }
    static void*& get_ctx_ref() {
        static void* ctx = nullptr;
        return ctx;
    }
    static LogFuncPtr get_log() { return get_log_ref(); }
    static void* get_ctx() { return get_ctx_ref(); }
};
} // namespace GamePlug
#endif
#endif
