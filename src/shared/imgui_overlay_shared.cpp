#include "imgui_overlay_shared.h"
#include "config.h"
#include "imgui.h"
#include "plugin_manager.h"
#include <algorithm>
#include <cstring>
#include <functional>

namespace GamePlug {

void ImGuiOverlayShared::DrawUI(uint32_t width, uint32_t height, std::function<void()> apiSpecificUI) {
    ImGuiIO& io = ImGui::GetIO();
    float uiScale = (std::max)(1.0f, (float)height / 720.0f);
    io.FontGlobalScale = uiScale;

    // Premium Styling
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f * uiScale;
    style.FrameRounding = 4.0f * uiScale;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.75f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.29f, 0.48f, 0.90f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);

    ImGui::SetNextWindowPos(ImVec2(20.0f * uiScale, 20.0f * uiScale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f * uiScale, 150.0f * uiScale), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("GamePlug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // 1. Master Toggle (at the top)
        Config::Get().RenderUI();

        bool pluginsEnabled = Config::Get().GetBool("PluginEnabled", true);
        bool hasPlugins = pluginsEnabled && !PluginManager::Get().IsEmpty();

        // 2. Render Plugins UI
        if (hasPlugins) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            PluginManager::Get().RenderPlugins();
        }

        if (!hasPlugins && !apiSpecificUI) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("No modules or plugins active.");
        }

        if (apiSpecificUI) {
            apiSpecificUI();
        }
    }
    ImGui::End();
}

} // namespace GamePlug
