#include "plugin_helper.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

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

class SamplePlugin : public GamePlug::Plugin {
public:
    const char* GetName() const override {
        return "Sample Config Editor (C++ Base)";
    }

    void OnInit(ImGuiContext* context, void (*LogFunc)(GamePlugPluginInterface::PluginLogLevel, const char*, void*), void* logContext) override {
        // Call base to set up ImGui and Logger
        GamePlug::Plugin::OnInit(context, LogFunc, logContext);
        
        m_config.Load("sample.conf");
        GamePlug::Logger::info("Sample Plugin: Initialized using C++ Helper Class");
    }

    void OnImGuiRender() override {
        ImGui::TextDisabled("(Configuration is saved automatically)");
    }

    void OnFieldsChanged() override {
        m_config.Save("sample.conf");
    }

    int GetFields(GamePlugPluginInterface::FieldDescriptor** outFields) override {
        static GamePlugPluginInterface::FieldDescriptor fields[] = {
            { "Enable Feature A", "General", GamePlugPluginInterface::TYPE_BOOL, &m_config.EnableFeatureA, 0, nullptr },
            { "Sensitivity", "General", GamePlugPluginInterface::TYPE_FLOAT, &m_config.Sensitivity, 0, nullptr },
            { "Window Mode", "General", GamePlugPluginInterface::TYPE_ENUM, &m_config.WindowMode, 0, "Windowed,Fullscreen,Borderless" },
            { "User Alias", "User Profile", GamePlugPluginInterface::TYPE_STRING, m_config.UserAlias, sizeof(m_config.UserAlias), nullptr }
        };

        if (outFields) {
            *outFields = fields;
        }
        return sizeof(fields) / sizeof(fields[0]);
    }

private:
    Config m_config;
};

REGISTER_GAMEPLUG_PLUGIN(SamplePlugin)

