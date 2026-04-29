# 🚀 GamePlug: The Universal Plugin Layer

GamePlug is a powerful, multi-API interception framework designed for modders. It provides a unified plugin system to inject custom ImGui UIs, upscaling tech (FSR/DLSS), and game logic across **Vulkan**, **DirectX 9**, and **DirectX 12**.

## ✨ Key Features

*   **Multi-API Support**: One framework to rule them all. Works seamlessly with Vulkan, D3D9, and D3D12.
*   **Unified Plugin System**: Build plugins once using a clean C++ interface. No need to worry about the underlying rendering backend.
*   **ImGui Integration**: Full support for Dear ImGui overlays with shared context between the host and plugins.
*   **Performance Tech**: Built-in hooks for modern upscalers (FSR/DLSS/XeSS).
*   **Cross-Architecture**: Supports both x32 (Legacy/DXVK titles) and x64 (Modern titles).

## 🛠 Build Instructions

GamePlug supports both x32 (x86) and x64 builds. **x32 is often the primary target** for older games utilizing DXVK.

### Requirements
- **CMake** (v3.20+)
- **Visual Studio 2022** with C++ Desktop Development

### 1. Build x32 (Legacy Support)
```powershell
cmake -B build32 -A Win32
cmake --build build32 --config Release
```

### 2. Build x64 (Modern Titles)
```powershell
cmake -B build64 -A x64
cmake --build build64 --config Release
```

## 📦 Outputs

### 🌋 Vulkan (Layer Mode)
- `vklayer.dll`: The Vulkan interception layer.
- `VK_LAYER_GAMEPLUG.json`: The manifest file for Vulkan.
- `gameplug.exe`: The launcher that automatically sets up the Vulkan layer environment.
- `run_game.bat`: A helper script for launching Vulkan games with the layer.

### 🎮 DirectX 9 / 12 (Proxy Mode)
- `dinput8.dll`: The universal drop-in proxy for all DirectX games (DX9 & DX12).

## 🔧 Usage

### 🌋 Vulkan Integration
Vulkan uses a layer system. The easiest way to use it is via the launcher:
```powershell
.\gameplug.exe "C:\Path\To\Game.exe" [args]
```
**Manual Method:**
1. Place `vklayer.dll` and `VK_LAYER_GAMEPLUG.json` in a folder.
2. Set `VK_LAYER_PATH` to that folder and `VK_INSTANCE_LAYERS` to `VK_LAYER_GAMEPLUG`.

### 🎮 DirectX Integration (9 / 12)
DirectX integration is simpler and uses a single universal proxy:
1. Copy `dinput8.dll` into the game's executable directory.
2. Launch the game normally.




