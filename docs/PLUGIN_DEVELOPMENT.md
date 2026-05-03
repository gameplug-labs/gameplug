# GamePlug Plugin Development Guide

This guide explains how to create your own plugins for GamePlug. GamePlug plugins are DLLs that export a specific interface, allowing them to render ImGui overlays, handle configuration, and interact with the host game/application.

## 🚀 Getting Started

To create a plugin, you need to include `plugin_interface.h` from the GamePlug include directory.

### Basic Requirements
- **C++ Compiler**: Support for C++17 or later.
- **Dear ImGui**: Your plugin should link against the same version of ImGui as the host (usually bundled or provided via headers).

## 🏗️ The Plugin Interface

Every plugin must define and return a `GamePlugPluginInterface` struct. This struct contains pointers to the functions the host will call.

### `GamePlugPluginInterface` Struct

| Field | Type | Description |
| :--- | :--- | :--- |
| `InterfaceVersion` | `int` | Must match the current host version (currently **8**). |
| `GetName` | `const char* (*)()` | Returns the display name of the plugin. |
| `OnInit` | `void (*)(ImGuiContext*, LogFunc, void*)` | Called when the plugin is loaded. |
| `OnImGuiRender` | `void (*)()` | Called every frame to render the UI. |
| `OnShutdown` | `void (*)()` | Called before the plugin is unloaded. |
| `OnFieldsChanged` | `void (*)()` | Called when a configuration field is modified via the UI. |
| `GetFields` | `int (*)(FieldDescriptor**)` | Returns an array of configuration fields for automatic UI generation. |

## 🔄 Lifecycle Hooks

### 1. `OnInit`
This is where you set up your plugin. The host passes the `ImGuiContext` so your plugin shares the same UI state as the host.

```cpp
void MyPlugin_OnInit(ImGuiContext* context, void (*LogFunc)(...), void* logContext) {
    ImGui::SetCurrentContext(context);
    // Initialize logging
    GamePlug::Logger::set_log(LogFunc, logContext);
    GamePlug::Logger::info("Plugin Initialized!");
}
```

### 2. `OnImGuiRender`
This function is called every frame. You can issue any ImGui commands here to draw your own windows or widgets.

```cpp
void MyPlugin_OnImGuiRender() {
    if (ImGui::Begin("My Plugin Window")) {
        ImGui::Text("Hello from GamePlug!");
    }
    ImGui::End();
}
```

### 3. `OnShutdown`
Cleanup any resources (memory, file handles, etc.) before the DLL is unloaded.

## ⚙️ Configuration Fields System

GamePlug features an automatic UI generation system for plugin settings. By defining "Fields", you don't have to write manual ImGui code for every checkbox or slider.

### Defining Fields
You define fields using the `FieldDescriptor` struct and return them in `GetFields`.

```cpp
static bool g_FeatureEnabled = true;
static float g_Sensitivity = 1.0f;

static GamePlugPluginInterface::FieldDescriptor g_Fields[] = {
    { "Enable Feature", "General", GamePlugPluginInterface::TYPE_BOOL, &g_FeatureEnabled, 0, nullptr },
    { "Sensitivity", "General", GamePlugPluginInterface::TYPE_FLOAT, &g_Sensitivity, 0, nullptr }
};

int MyPlugin_GetFields(GamePlugPluginInterface::FieldDescriptor** outFields) {
    if (outFields) *outFields = g_Fields;
    return 2; // Number of fields
}
```

### `OnFieldsChanged`
The host calls this function whenever a user modifies one of your fields in the GamePlug menu. This is the perfect place to save your configuration to a file.

## 📝 Logging
Use the `GamePlug::Logger` class to send messages back to the host's log.

```cpp
GamePlug::Logger::info("User changed a setting!");
GamePlug::Logger::error("Failed to load config!");
```

## 📤 Exporting the Interface
Finally, your DLL must export a single function named `GamePlug_GetPluginInterface`.

```cpp
extern "C" __declspec(dllexport) GamePlugPluginInterface* GamePlug_GetPluginInterface() {
    static GamePlugPluginInterface g_Interface = {
        8, // Version
        MyPlugin_GetName,
        MyPlugin_OnInit,
        MyPlugin_OnImGuiRender,
        nullptr, // OnShutdown
        MyPlugin_OnFieldsChanged,
        MyPlugin_GetFields
    };
    return &g_Interface;
}
```

## 🛠️ Building Your Plugin
1. Create a C++ DLL project.
2. Add `include/plugin_interface.h` to your include paths.
3. Link against ImGui (if not provided by the host environment).
4. Compile as `x32` or `x64` depending on the target game.

For a complete working example, check [examples/sample/sample_plugin.cpp](../examples/sample/sample_plugin.cpp).
