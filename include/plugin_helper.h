#pragma once

#include "plugin_interface.h"
#include <string>
#include <vector>

namespace GamePlug {

/**
 * @brief Base class for GamePlug plugins.
 *
 * Inherit from this class and use REGISTER_GAMEPLUG_PLUGIN(YourClass)
 * to create a C++ friendly plugin.
 */
class Plugin {
public:
    virtual ~Plugin() = default;

    /**
     * @brief Returns the display name of the plugin.
     */
    virtual const char* GetName() const = 0;

    /**
     * @brief Called when the plugin is loaded.
     * Default implementation sets up ImGui context and the GamePlug logger.
     */
    virtual void OnInit(
        ImGuiContext* context, void (*LogFunc)(GamePlugPluginInterface::PluginLogLevel, const char*, void*), void* logContext) {
        ImGui::SetCurrentContext(context);
        GamePlug::Logger::set_log(LogFunc, logContext);
    }

    /**
     * @brief Called every frame when the overlay is active.
     */
    virtual void OnImGuiRender() {}

    /**
     * @brief Called before the plugin is unloaded.
     */
    virtual void OnShutdown() {}

    /**
     * @brief Called when fields are modified in the UI.
     */
    virtual void OnFieldsChanged() {}

    /**
     * @brief Returns the field descriptors for the plugin.
     * @param outFields Pointer to store the address of the fields array.
     * @return Number of fields.
     */
    virtual int GetFields(GamePlugPluginInterface::FieldDescriptor** outFields) {
        if (outFields)
            *outFields = nullptr;
        return 0;
    }

    /**
     * @brief Internal helper to populate the C interface struct.
     */
    static GamePlugPluginInterface* GetInterface(Plugin* instance) {
        static Plugin* s_Instance = nullptr;
        static GamePlugPluginInterface s_Interface = {};

        s_Instance = instance;

        s_Interface.InterfaceVersion = 8;
        s_Interface.GetName = []() -> const char* { return s_Instance->GetName(); };
        s_Interface.OnInit = [](ImGuiContext* ctx, void (*log)(GamePlugPluginInterface::PluginLogLevel, const char*, void*), void* lctx) {
            s_Instance->OnInit(ctx, log, lctx);
        };
        s_Interface.OnImGuiRender = []() { s_Instance->OnImGuiRender(); };
        s_Interface.OnShutdown = []() { s_Instance->OnShutdown(); };
        s_Interface.OnFieldsChanged = []() { s_Instance->OnFieldsChanged(); };
        s_Interface.GetFields = [](GamePlugPluginInterface::FieldDescriptor** out) -> int { return s_Instance->GetFields(out); };

        return &s_Interface;
    }
};

} // namespace GamePlug

/**
 * @brief Macro to register a GamePlug plugin.
 * Place this at the bottom of your .cpp file.
 */
#define REGISTER_GAMEPLUG_PLUGIN(ClassName)                                                 \
    extern "C" GamePlug_PLUGIN_API GamePlugPluginInterface* GamePlug_GetPluginInterface() { \
        static ClassName instance;                                                          \
        return GamePlug::Plugin::GetInterface(&instance);                                   \
    }
