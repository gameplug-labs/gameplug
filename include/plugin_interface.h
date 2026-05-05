/**
 * @file plugin_interface.h
 * @brief Raw C-style interface for GamePlug plugins.
 * 
 * IMPORTANT: For C++ plugins, it is HIGHLY RECOMMENDED to include "plugin_helper.h" 
 * instead of this file. The helper provides a clean C++ base class and 
 * automatic initialization.
 */

#pragma once

#ifdef _WIN32
#define GamePlug_PLUGIN_API __declspec(dllexport)
#else
#define GamePlug_PLUGIN_API
#endif

#include <imgui.h>

extern "C" {

struct GamePlugPluginInterface {
    // The version of the interface. 
    // Increment this if the struct layout changes.
    int InterfaceVersion; // Current version: 8


    // Returns a human-readable name for the plugin.
    const char* (*GetName)();


    enum PluginLogLevel {
        LOG_INFO,
        LOG_WARN,
        LOG_ERROR,
        LOG_DEBUG
    };

    // Called once when the plugin is loaded. 
    // Pass the ImGui context and a logging function pointer from the host.
    void (*OnInit)(ImGuiContext* context, void (*LogFunc)(PluginLogLevel level, const char* message, void* context), void* logContext);



    // Called once per frame when the overlay is visible.
    // The plugin should issue ImGui calls here.
    void (*OnImGuiRender)();

    // Called once before the plugin is unloaded.
    void (*OnShutdown)();
    
    // Called whenever any typed field is modified by the framework UI.
    void (*OnFieldsChanged)();

    // Field Types
    enum FieldType {
        TYPE_BOOL,
        TYPE_INT,
        TYPE_FLOAT,
        TYPE_STRING,
        TYPE_ENUM
    };


    struct FieldDescriptor {
        const char* Name;
        const char* Category; // Field Category (Grouping)
        FieldType Type;
        void* Data;
        int DataSize; // Used for strings (max length)
        const char* Options; // Comma-separated options (for TYPE_ENUM)
    };



    // Returns the number of fields and a pointer to an array of descriptors.
    int (*GetFields)(FieldDescriptor** outFields);
};


// Plugins must export this function to be recognized by GamePlug.
// typedef GamePlugPluginInterface* (*GamePlug_GetPluginInterfaceFn)();
#define GamePlug_GET_INTERFACE_NAME "GamePlug_GetPluginInterface"

// Helper macros for plugins to use the provided LogFunc
// Usage: if (pLog) pLog(LOG_INFO, "Message"); 
#define GamePlug_LOG_INFO(msg)  if (g_Log) g_Log(GamePlugPluginInterface::LOG_INFO, msg)
#define GamePlug_LOG_WARN(msg)  if (g_Log) g_Log(GamePlugPluginInterface::LOG_WARN, msg)
#define GamePlug_LOG_ERROR(msg) if (g_Log) g_Log(GamePlugPluginInterface::LOG_ERROR, msg)
#define GamePlug_LOG_DEBUG(msg) if (g_Log) g_Log(GamePlugPluginInterface::LOG_DEBUG, msg)

} // extern "C"

#ifdef __cplusplus
#if !defined(FRAMEWORK_EXPORTS) && !defined(GamePlug_INTERNAL_LOGGER) && !defined(GAMEPLUG_LOGGER_DEFINED)
namespace GamePlug {
    // Hidden global for the Logger wrapper
    // The plugin must define this somewhere (usually sample_plugin.cpp)
    typedef void (*LogFuncPtr)(GamePlugPluginInterface::PluginLogLevel, const char*, void*);
    
    class Logger {
    public:
        static void info(const char* msg)  { if (get_log()) get_log()(GamePlugPluginInterface::LOG_INFO, msg, get_ctx()); }
        static void warn(const char* msg)  { if (get_log()) get_log()(GamePlugPluginInterface::LOG_WARN, msg, get_ctx()); }
        static void error(const char* msg) { if (get_log()) get_log()(GamePlugPluginInterface::LOG_ERROR, msg, get_ctx()); }
        static void debug(const char* msg) { if (get_log()) get_log()(GamePlugPluginInterface::LOG_DEBUG, msg, get_ctx()); }

        // Plugins must call this or set g_Log manually
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

}
#endif
#endif


