# GamePlug Plugin Development Guide

This guide explains how to create your own plugins for GamePlug. GamePlug plugins are DLLs that export a specific interface, allowing them to render ImGui overlays, handle configuration, and interact with the host game/application.

## 🚀 Getting Started

The easiest way to create a plugin is to use the **C++ Helper Library**. This handles all the boilerplate for you.

To start, include `plugin_helper.h` from the GamePlug include directory.

### Basic Requirements
- **C++ Compiler**: Support for C++17 or later.
- **Dear ImGui**: Your plugin should link against the same version of ImGui as the host.

## 🏗️ Creating a Plugin (C++ Class)

Inherit from the `GamePlug::Plugin` base class and override the methods you need.

### Example: Minimal Plugin

```cpp
#include "plugin_helper.h"

class MyPlugin : public GamePlug::Plugin {
public:
    // Returns the display name of the plugin
    const char* GetName() const override {
        return "My Awesome Plugin";
    }

    // Called every frame to render the UI
    void OnImGuiRender() override {
        if (ImGui::Begin("My Plugin Window")) {
            ImGui::Text("Hello from GamePlug!");
        }
        ImGui::End();
    }
};

// This macro handles the DLL export boilerplate
REGISTER_GAMEPLUG_PLUGIN(MyPlugin)
```

## 🔄 Lifecycle Hooks

| Method | Description |
| :--- | :--- |
| `GetName` | Returns the human-readable name of the plugin. |
| `OnInit` | Called once when the plugin is loaded. Use this for setup. |
| `OnImGuiRender` | Called every frame while the overlay is active. |
| `OnShutdown` | Called before the plugin is unloaded. Clean up resources here. |
| `OnFieldsChanged` | Called when a configuration field is modified via the UI. |
| `GetFields` | Returns an array of configuration fields for automatic UI generation. |

### Using `OnInit`
If you override `OnInit`, make sure to call the base class implementation so that ImGui and the Logger are set up correctly.

```cpp
void OnInit(ImGuiContext* context, void (*LogFunc)(...), void* logContext) override {
    GamePlug::Plugin::OnInit(context, LogFunc, logContext);
    GamePlug::Logger::info("Plugin starting up!");
}
```

## ⚙️ Configuration Fields System

GamePlug features an automatic UI generation system. By defining "Fields", you don't have to write manual ImGui code for every checkbox or slider.

### Defining Fields
Override `GetFields` and return an array of `FieldDescriptor` structs.

```cpp
private:
    bool m_Enabled = true;
    float m_Sensitivity = 1.0f;

public:
    int GetFields(GamePlugPluginInterface::FieldDescriptor** outFields) override {
        static GamePlugPluginInterface::FieldDescriptor fields[] = {
            { "Enable Feature", "General", GamePlugPluginInterface::TYPE_BOOL, &m_Enabled, 0, nullptr },
            { "Sensitivity", "General", GamePlugPluginInterface::TYPE_FLOAT, &m_Sensitivity, 0, nullptr }
        };

        if (outFields) *outFields = fields;
        return 2;
    }
```

## 📝 Logging

Use the `GamePlug::Logger` class to send messages back to the host.

```cpp
GamePlug::Logger::info("User changed a setting!");
GamePlug::Logger::error("Failed to load config!");
```

## 🛠️ Building Your Plugin

1. Create a C++ DLL project.
2. Add `include/` to your include paths.
3. Include `plugin_helper.h` in your source.
4. Use the `REGISTER_GAMEPLUG_PLUGIN(YourClass)` macro.
5. Compile as `x32` or `x64` depending on the target game.

For complete working examples, check [examples/minimal_plugin.cpp](../examples/minimal_plugin.cpp), [examples/extra/extra_plugin.cpp](../examples/extra/extra_plugin.cpp), or [examples/sample/sample_plugin.cpp](../examples/sample/sample_plugin.cpp).

---

### ⚠️ Low-Level C Interface (Advanced)

If you cannot use the C++ helper (e.g., you are writing a plugin in pure C or another language), you must manually include `plugin_interface.h` and export the `GamePlug_GetPluginInterface` function returning a `GamePlugPluginInterface` struct. See the header file for details.
