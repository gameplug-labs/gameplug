# 📄 plan.md — GamePlug (Proton-style Vulkan Upscaler Framework for Windows)

---

# 🎯 Project Goal

Build a **Proton-inspired Vulkan interception framework for Windows** that:

* Hooks Vulkan directly (no DXVK modification)
* Uses a **plugin-based architecture**
* Provides **ImGui overlay UI**
* Supports **multi-upscaler priority (FSR → DLSS later)**
* Works with:

  * DXVK (D3D9 / D3D11)
  * Native Vulkan games

---

# 🧠 Core Philosophy

* DXVK = black box renderer (UNCHANGED)
* Vulkan Layer = interception point
* gameplug = core runtime (inside layer)
* Plugins = features (FSR, DLSS, debug)
* ImGui = control UI

---

# 🧱 High-Level Architecture

```
Game
 ↓
DXVK (d3d9.dll / dxgi.dll)
 ↓
Vulkan API
 ↓
🟢 Vulkan Layer (gameplug.dll)
     ├── Hook Vulkan functions
     ├── Manage swapchain
     ├── Run framework
     ↓
🟡 gameplug Core
     ├── Plugin Manager
     ├── UI System (ImGui)
     ├── Resource Manager
     ↓
🔌 Plugins
     ├── Upscaler (FSR first)
     ├── Debug
     ├── DLSS (future)
 ↓
Final Present
```

---

# 📁 Repository Structure

```
GamePlug/
│
├── external/
│   ├── imgui/                     # UI system
│   ├── volk/                      # Vulkan loader
│   ├── glm/                       # math library
│   └── VulkanMemoryAllocator/     # GPU memory (future use)
│
├── layer/                         # Vulkan layer (ENTRY POINT)
│   ├── layer.cpp
│   ├── dispatch.cpp
│   ├── hooks.cpp
│   ├── swapchain.cpp
│   ├── vk_layer_exports.cpp
│   └── CMakeLists.txt
│
├── src/                           # gameplug (INSIDE LAYER)
│   ├── vk_framework.cpp
│   ├── vk_context.cpp
│   ├── vk_plugin_manager.cpp
│   ├── vk_imgui.cpp
│   ├── vk_resources.cpp
│   └── CMakeLists.txt
│
├── plugins/
│   └── upscaler/                  # multi-upscaler system
│       ├── upscaler_plugin.cpp
│       ├── fsr/
│       │   ├── fsr_easu.comp
│       │   ├── fsr_rcas.comp
│       │   └── fsr_backend.cpp
│       │
│       ├── dlss/                  # future
│       └── CMakeLists.txt
│
├── include/
│   ├── ivk_plugin.h
│   ├── vk_types.h
│   └── common.h
│
├── loader/                        # Proton-like launcher
│   ├── main.cpp
│   └── CMakeLists.txt
│
├── layer_manifest/
│   └── VK_LAYER_UPSCALER.json
│
├── shaders/                       # compiled SPIR-V output
│   ├── fsr_easu.spv
│   └── fsr_rcas.spv
│
├── config/
│   ├── upscaler.ini
│   └── dxvk.conf
│
├── CMakeLists.txt
└── README.md
```

---

# 🔧 Submodules (Already Added)

```
external/imgui
external/volk
external/glm
external/VulkanMemoryAllocator
```

---

# 🧩 Vulkan Layer (gameplug.dll)

## Responsibilities

* Hook Vulkan functions:

  * vkGetInstanceProcAddr
  * vkGetDeviceProcAddr
  * vkCreateSwapchainKHR
  * vkAcquireNextImageKHR
  * vkQueuePresentKHR (MAIN)

* Track:

  * swapchain images
  * current frame index
  * device/context

* Call gameplug each frame

---

# 🧠 gameplug (Inside Layer)

## Responsibilities

### 1. Plugin System

```
init()
onFrameBegin()
onFrame(VkImage)
onUI()
shutdown()
```

---

### 2. Resource Management

* Vulkan device
* command buffers
* descriptor pools
* swapchain info

---

### 3. UI System (ImGui)

* Global overlay
* Plugin-configurable panels

---

# 🔌 Plugin System

## Location

```
plugins/upscaler/
```

---

## Plugin Interface

```cpp
class IVkPlugin {
public:
    virtual void onInit(VkDevice device) = 0;
    virtual void onFrame(VkCommandBuffer cmd, VkImage image) = 0;
    virtual void onUI() = 0;
    virtual void onShutdown() = 0;
};
```

---

## Multi-Upscaler Logic

Priority:

```
DLSS (future)
↓
FSR1 (initial implementation)
↓
Fallback (bilinear / passthrough)
```

---

# 🎮 Frame Execution Flow

```
vkAcquireNextImageKHR
   ↓
Store image index

vkQueuePresentKHR (HOOK)
   ↓
gameplug::onFrameBegin()

→ Plugins process image
→ ImGui UI rendered

gameplug::onFrameEnd()
   ↓
Call real vkQueuePresentKHR
```

---

# 🖥 ImGui Integration (Phase 1 Priority)

## Initialization

* ImGui context
* Vulkan backend
* Win32 backend

---

## Frame Flow

```
ImGui_ImplVulkan_NewFrame()
ImGui_ImplWin32_NewFrame()
ImGui::NewFrame()

Draw UI

ImGui::Render()
ImGui_ImplVulkan_RenderDrawData()
```

---

## Example UI

```
[GamePlug Overlay]

Resolution: 1280x720 → 1920x1080

[✓] Enable Upscaler
Method: FSR1
Sharpness: 0.3
```

---

# 🔁 Rendering Strategy

## Phase 1

* Only overlay UI
* No image modification

---

## Phase 2

* Run FSR compute shader:

  * EASU (upscale)
  * RCAS (sharpen)

---

# 📉 Resolution Strategy (Proton GE Style)

* Game renders LOW resolution (e.g., 720p)
* Vulkan layer upscales to display resolution

---

## Methods

* User sets resolution manually OR
* Future: D3D9 wrapper to force resolution

---

# ⚙️ Build Outputs

```
build/
│
├── gameplug.dll              ← Vulkan Layer (MAIN OUTPUT)
├── VK_LAYER_UPSCALER.json
│
├── plugins/
│   └── upscaler/
│       └── upscaler_plugin.dll
│
├── shaders/
│   ├── fsr_easu.spv
│   └── fsr_rcas.spv
│
├── upscaler_loader.exe
```

---

# 📦 Final Release Layout (Game Folder)

```
/Game
│
├── d3d9.dll                      ← DXVK (bundled)
├── dxgi.dll (optional)
│
├── gameplug.dll               ← Vulkan layer
├── VK_LAYER_UPSCALER.json
│
├── plugins/
│   └── upscaler/
│       └── upscaler_plugin.dll
│
├── shaders/
│   ├── fsr_easu.spv
│   └── fsr_rcas.spv
│
├── config/
│   ├── upscaler.ini
│   └── dxvk.conf
│
├── upscaler_loader.exe
└── logs/
```

---

# 🚀 Loader (upscaler_loader.exe)

## Purpose

* Auto-inject Vulkan layer
* Provide Proton-like control

---

## Behavior

```cpp
SetEnvironmentVariable("VK_LAYER_PATH", ".");
SetEnvironmentVariable("VK_INSTANCE_LAYERS", "VK_LAYER_UPSCALER");

Launch game.exe
```

---

# ⚠️ Critical Systems

## 1. Swapchain Recreation

* Handle resolution change
* Recreate:

  * framebuffers
  * ImGui
  * FSR resources

---

## 2. Synchronization

* Proper image layout transitions:

  * PRESENT → GENERAL
  * GENERAL → PRESENT

---

## 3. Plugin Safety

* Versioned interface
* Fail-safe fallback

---

# 🧪 Development Phases

---

## Phase 1 (Foundation)

* Vulkan layer loads
* vkQueuePresentKHR hooked
* Logging works

---

## Phase 2 (UI)

* ImGui initialized
* Overlay renders

---

## Phase 3 (Plugins)

* Plugin system working
* Upscaler plugin loads

---

## Phase 4 (FSR1)

* EASU + RCAS implemented
* Working upscale

---

## Phase 5 (Advanced)

* DLSS plugin
* Motion vector reconstruction
* Dynamic scaling

---

# 🧠 Final Vision

GamePlug becomes:

> A Proton-style Vulkan overlay and upscaler framework for Windows

* Works with DXVK (D3D9 games)
* Works with Vulkan titles
* Extensible via plugins
* Fully self-contained release

---
