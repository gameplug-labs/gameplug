#include "plugin_helper.h"

/**
 * @brief A minimal GamePlug plugin using the C++ helper.
 */
class MinimalPlugin : public GamePlug::Plugin {
public:
    const char* GetName() const override {
        return "Minimal C++ Plugin";
    }

    void OnImGuiRender() override {
        ImGui::Begin("Minimal Plugin");
        ImGui::Text("Hello from C++!");
        if (ImGui::Button("Log Something")) {
            GamePlug::Logger::info("Button clicked in Minimal Plugin!");
        }
        ImGui::End();
    }
};

REGISTER_GAMEPLUG_PLUGIN(MinimalPlugin)
