# GamePlug Plugin Usage Guide

This guide explains how to install, manage, and use plugins with GamePlug.

## 📂 Installation

Plugins for GamePlug are provided as `.dll` files. To install a plugin, you must place it in the correct directory.

### Plugin Directory Location
GamePlug searches for plugins in a folder named `GamePlug/plugins`. This folder should be located in one of the following places:

1. **Beside the Game Executable**:
   - `C:/Games/CoolGame/Game.exe`
   - `C:/Games/CoolGame/GamePlug/plugins/YourPlugin.dll`
2. **Beside the GamePlug DLL** (e.g., `dinput8.dll` or `vklayer.dll`):
   - If you are using Proxy Mode (DX9/DX12), place the folder where you put `dinput8.dll`.

> [!TIP]
> If the `GamePlug/plugins` folder does not exist, create it manually.

## 🛠️ Managing Plugins

Once a plugin is placed in the folder, GamePlug will attempt to load it automatically when the game starts.

### The GamePlug Overlay
1. Launch your game.
2. Open the GamePlug overlay (usually via the **Home** or **Insert** key, depending on your configuration).
3. Navigate to the **Plugins** tab.

### Enabling/Disabling Plugins
In the Plugins tab, you will see a list of discovered plugins. You can toggle them on or off individually. Some plugins may require a game restart to take effect, but most support "Hot Reloading."

## ⚙️ Configuration

Many plugins offer configurable settings. GamePlug automatically generates a user interface for these settings in the overlay.

- **Automatic UI**: Settings like checkboxes, sliders, and dropdowns will appear under the plugin's header in the overlay.
- **Saving Settings**: Most plugins are designed to save their configuration automatically when you change a setting. These settings are typically stored in a `.conf` or `.json` file in the same directory as the plugin DLL.

## 🔍 Troubleshooting

### Plugin Not Appearing
- **Architecture Mismatch**: Ensure the plugin DLL matches the game's architecture. If the game is 64-bit, the plugin must be 64-bit. If the game is 32-bit (x86), the plugin must be 32-bit.
- **Missing Dependencies**: Some plugins may require additional DLLs (like `msvcp140.dll`). Check the plugin's documentation.
- **Wrong Directory**: Double-check that the plugin is inside `GamePlug/plugins/` and NOT directly in the game folder.

### Game Crashes on Launch
- Remove all plugins from the `plugins/` folder and add them back one by one to identify the culprit.
- Check the `gameplug.log` file in the game directory for error messages related to `PluginManager`.
