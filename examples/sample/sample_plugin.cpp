#include "plugin_interface.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>

// Typed config structure
struct Config {
    bool EnableFeatureA = true;
    float Sensitivity = 0.5f;
    char UserAlias[64] = "ProGamer";
    int WindowMode = 0; // 0=Windowed, 1=Fullscreen, 2=Borderless
    
    void Load(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) return;

        std::string line;
        while (std::getline(file, line)) {
            size_t sep = line.find('=');
            if (sep != std::string::npos) {
                std::string key = line.substr(0, sep);
                std::string val = line.substr(sep + 1);
                
                if (key == "EnableFeatureA") EnableFeatureA = (val == "true");
                else if (key == "Sensitivity") Sensitivity = std::stof(val);
                else if (key == "WindowMode") WindowMode = std::stoi(val);
                else if (key == "UserAlias") {
                    strncpy(UserAlias, val.c_str(), sizeof(UserAlias)-1);
                    UserAlias[sizeof(UserAlias)-1] = '\0';
                }
            }
        }
    }
    
    void Save(const char* path) {
        std::ofstream file(path);
        file << "EnableFeatureA=" << (EnableFeatureA ? "true" : "false") << "\n";
        file << "Sensitivity=" << Sensitivity << "\n";
        file << "UserAlias=" << UserAlias << "\n";
        file << "WindowMode=" << WindowMode << "\n";
    }
};

static Config g_Config;

const char* Sample_GetName() {
    return "Sample Config Editor (Dropdown)";
}

void Sample_OnInit(ImGuiContext* context, void (*LogFunc)(GamePlugPluginInterface::PluginLogLevel level, const char* message, void* context), void* logContext) {
    ImGui::SetCurrentContext(context);
    
    // Set the logger for the plugin helper class with context
    GamePlug::Logger::set_log(LogFunc, logContext);
    
    g_Config.Load("sample.conf");
    
    GamePlug::Logger::info("Sample Plugin: Initialized with contextual logging");
}




void Sample_OnImGuiRender() {
    // Custom UI still possible, but manual save is no longer needed
    ImGui::TextDisabled("(Configuration is saved automatically)");
}

void Sample_OnFieldsChanged() {
    g_Config.Save("sample.conf");
}

static GamePlugPluginInterface::FieldDescriptor g_Fields[] = {
    { "Enable Feature A", "General", GamePlugPluginInterface::TYPE_BOOL, &g_Config.EnableFeatureA, 0, nullptr },
    { "Sensitivity", "General", GamePlugPluginInterface::TYPE_FLOAT, &g_Config.Sensitivity, 0, nullptr },
    { "Window Mode", "General", GamePlugPluginInterface::TYPE_ENUM, &g_Config.WindowMode, 0, "Windowed,Fullscreen,Borderless" },
    { "User Alias", "User Profile", GamePlugPluginInterface::TYPE_STRING, g_Config.UserAlias, sizeof(g_Config.UserAlias), nullptr }
};

int Sample_GetFields(GamePlugPluginInterface::FieldDescriptor** outFields) {
    if (outFields) {
        *outFields = g_Fields;
    }
    return sizeof(g_Fields) / sizeof(g_Fields[0]);
}

static GamePlugPluginInterface g_Interface = {
    8, // Version
    Sample_GetName,
    Sample_OnInit,
    Sample_OnImGuiRender,
    nullptr, // OnShutdown
    Sample_OnFieldsChanged,
    Sample_GetFields
};







extern "C" GamePlug_PLUGIN_API GamePlugPluginInterface* GamePlug_GetPluginInterface() {
    return &g_Interface;
}

