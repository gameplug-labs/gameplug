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
    if (height > 1440) {
        height = 1440;
    }
    float uiScale = (std::max)(1.0f, (float)height / 720.0f);
    io.FontGlobalScale = uiScale;

    // Compact, translucent violet theme inspired by the reference overlay.
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(7.0f * uiScale, 7.0f * uiScale);
    style.FramePadding = ImVec2(5.0f * uiScale, 3.0f * uiScale);
    style.ItemSpacing = ImVec2(5.0f * uiScale, 4.0f * uiScale);
    style.ItemInnerSpacing = ImVec2(4.0f * uiScale, 3.0f * uiScale);
    style.WindowRounding = 4.0f * uiScale;
    style.ChildRounding = 1.0f * uiScale;
    style.FrameRounding = 1.0f * uiScale;
    style.PopupRounding = 1.0f * uiScale;
    style.ScrollbarRounding = 4.0f * uiScale;
    style.GrabRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_Text] = ImVec4(0.91f, 0.91f, 0.96f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.57f, 0.67f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.018f, 0.018f, 0.030f, 0.62f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.018f, 0.018f, 0.030f, 0.40f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.050f, 0.045f, 0.075f, 0.94f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.31f, 0.29f, 0.45f, 0.58f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.18f, 0.21f, 0.78f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.23f, 0.38f, 0.92f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.31f, 0.28f, 0.49f, 0.96f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.17f, 0.55f, 0.92f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.27f, 0.23f, 0.70f, 0.96f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.25f, 0.21f, 0.66f, 0.90f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.34f, 0.30f, 0.78f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.36f, 0.86f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.30f, 0.27f, 0.76f, 0.96f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.35f, 0.87f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.43f, 0.39f, 0.93f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.76f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.64f, 0.59f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.84f, 0.80f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.38f, 0.33f, 0.63f, 0.62f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.03f, 0.03f, 0.05f, 0.72f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.32f, 0.29f, 0.61f, 0.90f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.40f, 0.78f, 1.00f);

    ImGui::SetNextWindowPos(ImVec2(20.0f * uiScale, 20.0f * uiScale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f * uiScale, 150.0f * uiScale), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("GamePlug", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        const bool contentVisible = ImGui::TreeNodeEx("GamePlug", ImGuiTreeNodeFlags_DefaultOpen);

        if (contentVisible) {
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

            ImGui::TreePop();
        }
    }
    ImGui::End();
}

} // namespace GamePlug
