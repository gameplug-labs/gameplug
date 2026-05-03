#pragma once

#include "framework_export.h"

// Define IMGUI_API to use our shared framework's export/import macro.
// This ensures that ImGui's global state (e.g., GImGui) is shared across DLL boundaries.
#ifndef IMGUI_API
#define IMGUI_API FRAMEWORK_API
#endif

// We also want the internal backends to use the same API
#ifndef IMGUI_IMPL_API
#define IMGUI_IMPL_API FRAMEWORK_API
#endif
