#include "plugin_interface.h"
#include <cstdio>

static int g_Counter = 0;

const char* Extra_GetName() {
    return "Extra Counter Plugin";
}

void Extra_OnInit(ImGuiContext* context, void (*LogFunc)(GamePlugPluginInterface::PluginLogLevel level, const char* message, void* context), void* logContext) {
    ImGui::SetCurrentContext(context);
    g_Counter = 0;
}



void Extra_OnImGuiRender() {
    ImGui::Text("Counter: %d", g_Counter);
    if (ImGui::Button("Increment")) {
        g_Counter++;
    }
}

static GamePlugPluginInterface::FieldDescriptor g_Fields[] = {
    { "Counter Value", "Statistics", GamePlugPluginInterface::TYPE_INT, &g_Counter, 0, nullptr }
};

int Extra_GetFields(GamePlugPluginInterface::FieldDescriptor** outFields) {
    if (outFields) {
        *outFields = g_Fields;
    }
    return sizeof(g_Fields) / sizeof(g_Fields[0]);
}

static GamePlugPluginInterface g_Interface = {
    8, // Version
    Extra_GetName,
    Extra_OnInit,
    Extra_OnImGuiRender,
    nullptr, // OnShutdown
    nullptr, // OnFieldsChanged
    Extra_GetFields
};





extern "C" GamePlug_PLUGIN_API GamePlugPluginInterface* GamePlug_GetPluginInterface() {
    return &g_Interface;
}
