#include "plugin_helper.h"

class ExtraPlugin : public GamePlug::Plugin {
public:
    const char* GetName() const override {
        return "Extra Counter Plugin";
    }

    void OnImGuiRender() override {
        ImGui::Text("Counter: %d", m_counter);
        if (ImGui::Button("Increment")) {
            m_counter++;
        }
    }

    int GetFields(GamePlugPluginInterface::FieldDescriptor** outFields) override {
        static GamePlugPluginInterface::FieldDescriptor fields[] = {
            { "Counter Value", "Statistics", GamePlugPluginInterface::TYPE_INT, &m_counter, 0, nullptr }
        };

        if (outFields) {
            *outFields = fields;
        }
        return sizeof(fields) / sizeof(fields[0]);
    }

private:
    int m_counter = 0;
};

REGISTER_GAMEPLUG_PLUGIN(ExtraPlugin)
