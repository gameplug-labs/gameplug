#pragma once
#include <cstdint>
#include <functional>
#include "framework_export.h"

namespace GamePlug {

class FRAMEWORK_API ImGuiOverlayShared {
public:
    /**
     * @brief Draws the common GamePlug ImGui UI.
     * @param width Viewport width.
     * @param height Viewport height (used for UI scaling).
     */
    static void DrawUI(uint32_t width, uint32_t height, std::function<void()> apiSpecificUI = nullptr);
};

} // namespace GamePlug
