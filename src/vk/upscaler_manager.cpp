#include "upscaler_manager.h"
#ifdef GAMEPLUG_VULKAN
#include "dispatch.h"
#include "image_tracker.h"
#include "overlay.h"
#endif
#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "jitter_helper.h"
#include "logger.h"

namespace GamePlug {

typedef GamePlugUpscalerInterface* (*GamePlug_GetUpscalerInterfaceFn)();

void UpscalerLogBridge(GamePlugUpscalerInterface::LogLevel level, const char* message, void* context) {
    std::string formattedMsg = "[Upscaler] " + std::string(message);
    switch (level) {
    case GamePlugUpscalerInterface::LOG_INFO:
        Logger::info(formattedMsg);
        break;
    case GamePlugUpscalerInterface::LOG_WARN:
        Logger::warn(formattedMsg);
        break;
    case GamePlugUpscalerInterface::LOG_ERROR:
        Logger::error(formattedMsg);
        break;
    case GamePlugUpscalerInterface::LOG_DEBUG:
        Logger::debug(formattedMsg);
        break;
    }
}

UpscalerManager::~UpscalerManager() {
    UnloadUpscaler();
}

bool UpscalerManager::LoadUpscaler(uintptr_t instance, uintptr_t physDevice, uintptr_t device) {
#ifdef GAMEPLUG_VULKAN
    auto vkInstance = reinterpret_cast<VkInstance>(instance);
    auto vkPhysDevice = reinterpret_cast<VkPhysicalDevice>(physDevice);
    auto vkDevice = reinterpret_cast<VkDevice>(device);
#endif

    // Phase 1: DLL and Interface Loading (needs only Instance/PhysDevice)
    if (!m_handle) {
#ifdef GAMEPLUG_VULKAN
        m_instance = reinterpret_cast<VkInstance>(instance);
        m_physDevice = reinterpret_cast<VkPhysicalDevice>(physDevice);
#else
        m_instance = instance;
        m_physDevice = physDevice;
#endif

        char buf[MAX_PATH];
        GetModuleFileNameA(NULL, buf, MAX_PATH);
        std::filesystem::path gamePath = std::filesystem::path(buf).parent_path();
        std::filesystem::path upscalerPath = gamePath / "vkupscaler.dll";

        Logger::info("UpscalerManager: Phase 1 loading " + upscalerPath.string());

        if (!std::filesystem::exists(upscalerPath)) {
            Logger::warn("UpscalerManager: vkupscaler.dll NOT found at " + upscalerPath.string());
            return false;
        }

        m_handle = LoadLibraryA(upscalerPath.string().c_str());
        if (!m_handle) {
            Logger::error("UpscalerManager: Failed to load upscaler.dll");
            return false;
        }

        auto getInterface = (GamePlug_GetUpscalerInterfaceFn)GetProcAddress(m_handle, GamePlug_GET_UPSCALER_INTERFACE_NAME);
        if (!getInterface) {
            Logger::error("UpscalerManager: upscaler.dll does not export " + std::string(GamePlug_GET_UPSCALER_INTERFACE_NAME));
            FreeLibrary(m_handle);
            m_handle = nullptr;
            return false;
        }

        m_pInterface = getInterface();
        Logger::info("UpscalerManager: Phase 1 Complete (Interface found).");
    }

#ifdef GAMEPLUG_VULKAN
    // Phase 2: Device Initialization (needs VkDevice and VkQueue)
    if (vkDevice != VK_NULL_HANDLE && m_device == VK_NULL_HANDLE) {
#ifdef GAMEPLUG_VULKAN
        m_device = vkDevice;
#else
        m_device = device;
#endif

        if (m_pInterface && m_pInterface->OnInit) {
            void* imguiCtx = ImGui::GetCurrentContext();
            Logger::info("UpscalerManager: Phase 2 OnInit calling with ImGuiCtx=" + std::to_string((uintptr_t)imguiCtx));

            auto* memProps = new VkPhysicalDeviceMemoryProperties();
            memset(memProps, 0, sizeof(VkPhysicalDeviceMemoryProperties));

            auto* inst_dispatch = DispatchManager::Get().GetInstance(vkInstance);
            if (inst_dispatch && inst_dispatch->table.vkGetPhysicalDeviceMemoryProperties) {
                inst_dispatch->table.vkGetPhysicalDeviceMemoryProperties(vkPhysDevice, memProps);
            } else {
                vkGetPhysicalDeviceMemoryProperties(vkPhysDevice, memProps);
            }

            VkQueue queue = VK_NULL_HANDLE;
            uint32_t queueFamilyIndex = 0;
            auto* dev_entry = DispatchManager::Get().GetDevice(vkDevice);
            if (dev_entry && dev_entry->table.vkGetDeviceQueue) {
                dev_entry->table.vkGetDeviceQueue(vkDevice, queueFamilyIndex, 0, &queue);
            }

            m_pInterface->OnInit(ImGui::GetCurrentContext(), UpscalerLogBridge, nullptr, (uintptr_t)vkInstance, (uintptr_t)vkPhysDevice,
                (uintptr_t)vkDevice, (uintptr_t)queue, queueFamilyIndex, memProps);
            delete memProps;
            Logger::info("UpscalerManager: Phase 2 Complete (OnInit called).");
        }
    }
#else
    // TODO: DX Initialization if needed
#endif

    return m_handle != nullptr;
}

void UpscalerManager::UnloadUpscaler() {
    if (m_handle && !m_isShuttingDown) {
        m_isShuttingDown = true;
#ifdef GAMEPLUG_VULKAN
        if (m_depthDebugSet)
            OverlayRenderer::Get().UnregisterDebugImage(m_depthDebugSet);
        if (m_mvDebugSet)
            OverlayRenderer::Get().UnregisterDebugImage(m_mvDebugSet);
        m_depthDebugSet = VK_NULL_HANDLE;
        m_mvDebugSet = VK_NULL_HANDLE;
        m_lastDepthView = VK_NULL_HANDLE;
        m_lastMVView = VK_NULL_HANDLE;
#endif

        if (m_pInterface && m_pInterface->OnShutdown)
            m_pInterface->OnShutdown();

        FreeLibrary(m_handle);
        m_handle = nullptr;
        m_pInterface = nullptr;
    }
}

bool UpscalerManager::IsUpscalingEnabled() const {
    if (!m_pInterface || !m_pInterface->GetFields)
        return false;
    GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
    int count = m_pInterface->GetFields(&fields);
    for (int i = 0; i < count; i++) {
        if (fields[i].Name && std::string(fields[i].Name) == "Upscaler Type") {
            int type = *(int*)fields[i].Data;
            return type > 0; // 0 is None
        }
    }
    return false;
}

void UpscalerManager::RenderUI(float fps, uint32_t width, uint32_t height) {
    if (!m_pInterface)
        return;

    if (ImGui::CollapsingHeader(m_pInterface->GetName() ? m_pInterface->GetName() : "Upscaler", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_pInterface->GetFields) {
            GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
            int count = m_pInterface->GetFields(&fields);

            // Find Upscaler Type to handle conditional fields
            int upscalerType = 0; // None
            for (int i = 0; i < count; i++) {
                if (fields[i].Name && std::string(fields[i].Name) == "Upscaler Type") {
                    upscalerType = *(int*)fields[i].Data;
                    break;
                }
            }

            if (count > 0 && fields) {
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
                for (int i = 0; i < count; i++) {
                    GamePlugUpscalerInterface::FieldDescriptor& f = fields[i];

                    // Filter: HDR and Inverted Depth only for FSR2+
                    if (upscalerType == 1) { // FSR 1.0
                        if (f.Name && (std::string(f.Name) == "HDR" || std::string(f.Name) == "Inverted Depth")) {
                            continue;
                        }
                    }

                    ImGui::PushID(f.Name);
                    bool changed = false;

                    switch (f.Type) {
                    case 0: // BOOL
                        changed = ImGui::Checkbox(f.Name, (bool*)f.Data);
                        break;
                    case 1: // INT
                        changed = ImGui::DragInt(f.Name, (int*)f.Data);
                        break;
                    case 2: // FLOAT
                    {
                        float minVal = 0.0f;
                        float maxVal = 1.0f;
                        bool hasRange = false;
                        if (f.Options) {
                            float optMin = 0.0f, optMax = 0.0f;
                            if (sscanf(f.Options, "%f,%f", &optMin, &optMax) == 2) {
                                minVal = optMin;
                                maxVal = optMax;
                                hasRange = true;
                            }
                        }
                        if (hasRange) {
                            changed = ImGui::SliderFloat(f.Name, (float*)f.Data, minVal, maxVal);
                        } else {
                            changed = ImGui::DragFloat(f.Name, (float*)f.Data);
                        }
                    } break;
                    case 3: // STRING
                        changed = ImGui::InputText(f.Name, (char*)f.Data, (size_t)f.DataSize);
                        break;
                    case 4: // ENUM
                        if (f.Options) {
                            std::string options(f.Options);
                            std::vector<std::string> items;
                            size_t start = 0, end = options.find(',');
                            while (end != std::string::npos) {
                                items.push_back(options.substr(start, end - start));
                                start = end + 1;
                                end = options.find(',', start);
                            }
                            items.push_back(options.substr(start));
                            int* current = (int*)f.Data;
                            const char* preview = (*current >= 0 && *current < (int)items.size()) ? items[*current].c_str() : "Unknown";
                            if (ImGui::BeginCombo(f.Name, preview)) {
                                for (int n = 0; n < (int)items.size(); n++) {
                                    if (ImGui::Selectable(items[n].c_str(), *current == n)) {
                                        *current = n;
                                        changed = true;
                                    }
                                }
                                ImGui::EndCombo();
                            }
                        }
                        break;
                    }

                    if (changed && m_pInterface->OnFieldsChanged)
                        m_pInterface->OnFieldsChanged();
                    ImGui::PopID();
                }
                ImGui::PopItemWidth();
            }
        }

        if (m_pInterface->OnImGuiRender) {
            m_pInterface->OnImGuiRender();
        }

        // Resolution and Performance Info (Single separator)
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "[ SYSTEM ACTIVE ]");
        ImGui::SameLine();
        float availW = ImGui::GetContentRegionAvail().x;
        float textW = ImGui::CalcTextSize("120 FPS").x;
        if (availW > textW) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availW - textW);
        }
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "%.0f FPS", fps);

        ImGui::TextDisabled("PIPELINE RESOLUTION:");
        ImGui::Text("  Target: %d x %d", width, height);
        ImGui::Text("  Render: %d x %d", m_renderWidth, m_renderHeight);
        float scale = (float)width / (float)m_renderWidth;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%.2fx)", scale);
        ImGui::Spacing();
    }
}

void UpscalerManager::UpdateDimensions(uint32_t width, uint32_t height) {
    GetTargetResolution(width, height, m_renderWidth, m_renderHeight);
}

void UpscalerManager::GetTargetResolution(uint32_t width, uint32_t height, uint32_t& outW, uint32_t& outH) {
    outW = width;
    outH = height;
    if (m_pInterface && m_pInterface->GetFields) {
        GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
        int count = m_pInterface->GetFields(&fields);

        // Check for Native Rendering first
        for (int i = 0; i < count; i++) {
            if (fields[i].Name && std::string(fields[i].Name) == "Native Rendering") {
                if (*(bool*)fields[i].Data) {
                    static bool last_native = false;
                    if (!last_native) {
                        Logger::info("GetTargetResolution: Native Rendering Active -> 1.0x override.");
                        last_native = true;
                    }
                    outW = width;
                    outH = height;
                    return;
                }
                break;
            }
        }

        for (int i = 0; i < count; i++) {
            if (fields[i].Name && std::string(fields[i].Name) == "Upscale Quality") {
                int quality = *(int*)fields[i].Data;
                float ratio = 1.3f;
                if (quality == 0)
                    ratio = 1.2f; // Ultra Ultra Quality
                else if (quality == 1)
                    ratio = 1.3f; // Ultra Quality
                else if (quality == 2)
                    ratio = 1.5f; // Quality
                else if (quality == 3)
                    ratio = 1.7f; // Balanced
                else if (quality == 4)
                    ratio = 2.0f; // Performance
                else if (quality == 5)
                    ratio = 3.0f; // Ultra Performance
                else
                    ratio = 1.3f;

                outW = (uint32_t)((float)width / ratio + 0.5f);
                outH = (uint32_t)((float)height / ratio + 0.5f);

                static uint32_t lastReportedW = 0, lastReportedH = 0;
                static int lastReportedQ = -1;
                if (outW != lastReportedW || outH != lastReportedH || quality != lastReportedQ) {
                    Logger::info("GetTargetResolution Updated: Q=" + std::to_string(quality) + " " + std::to_string(width) + "x" +
                                 std::to_string(height) + " -> " + std::to_string(outW) + "x" + std::to_string(outH));
                    lastReportedW = outW;
                    lastReportedH = outH;
                    lastReportedQ = quality;
                }
                return;
            }
        }
    }
}

void UpscalerManager::RenderFrame(uintptr_t cmd, uint64_t source, uint64_t target, uint32_t width, uint32_t height) {
    if (m_isShuttingDown)
        return;
    if (!m_pInterface || !m_pInterface->OnRenderFrame)
        return;
    if (m_frameUpscaled)
        return; // Already upscaled this frame
    if (source == 0 || target == 0)
        return;

    static uint32_t frameCount = 0;
    static uint32_t lastW = 0, lastH = 0;
    bool shouldLog = (frameCount % 300 == 0) || (width != lastW || height != lastH);

    if (shouldLog) {
        uint32_t rw, rh;
        GetTargetResolution(width, height, rw, rh);
        Logger::info("UpscalerManager::RenderFrame [Start] Frame=" + std::to_string(frameCount) + " cmd=" + std::to_string((uintptr_t)cmd) +
                     " Native=" + std::to_string(width) + "x" + std::to_string(height) + " -> Render=" + std::to_string(rw) + "x" +
                     std::to_string(rh));
    }

    m_frameUpscaled = true; // Mark as done for this frame
    m_width = width;
    m_height = height;
    frameCount++;

    // Update Jitter for this frame
    JitterHelper::Get().Update(width, height);

#ifdef GAMEPLUG_VULKAN
    ImageTracker::Get().SetScreenDimensions(width, height);
#endif

    float jitterX = JitterHelper::Get().GetJitterX();
    float jitterY = JitterHelper::Get().GetJitterY();

    // Calculate Render Resolution for Overlay
    GetTargetResolution(width, height, m_renderWidth, m_renderHeight);

    if (shouldLog) {
        Logger::info("UpscalerManager::RenderFrame [Jitter] x=" + std::to_string(jitterX) + " y=" + std::to_string(jitterY));
    }

    uint64_t depthImage = 0;
    uint32_t depthFormat = 0;
    uint64_t mvImage = 0;
    uint32_t mvFormat = 0;
    uint32_t swapchainFormat = 0;

#ifdef GAMEPLUG_VULKAN
    // Get suggested buffers from tracker
    auto depthInfo = ImageTracker::Get().GetCurrentDepthInfo(width, height);
    auto mvInfo = ImageTracker::Get().GetCurrentMVInfo(width, height);
    depthImage = (uint64_t)depthInfo.image;
    depthFormat = (uint32_t)depthInfo.format;
    mvImage = (uint64_t)mvInfo.image;
    mvFormat = (uint32_t)mvInfo.format;
    swapchainFormat = (uint32_t)ImageTracker::Get().GetSwapchainFormat();

    // Warning: mismatched resolution
    if (depthInfo.image != VK_NULL_HANDLE && (depthInfo.extent.width != width || depthInfo.extent.height != height)) {
        static uint32_t last_mismatch_w = 0;
        if (width != last_mismatch_w) {
            char buf[256];
            sprintf(buf,
                "UpscalerManager: WARNING! Depth buffer resolution mismatch: Buffer=%ux%u, Screen=%ux%u. Upscaler may produce incorrect "
                "output.",
                depthInfo.extent.width, depthInfo.extent.height, width, height);
            Logger::warn(buf);
            last_mismatch_w = width;
        }
    }

    if (shouldLog) {
        Logger::info("UpscalerManager::RenderFrame [Buffers] depth=" + std::to_string((uintptr_t)depthInfo.image) + " (" +
                     std::to_string(depthInfo.extent.width) + "x" + std::to_string(depthInfo.extent.height) +
                     ", fmt=" + std::to_string(depthInfo.format) + "), mv=" + std::to_string((uintptr_t)mvInfo.image) +
                     " (fmt=" + std::to_string(mvInfo.format) + ")");
    }
#endif

    bool invertedDepth = false;
    bool hdr = false;
    if (m_pInterface && m_pInterface->GetFields) {
        GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
        int count = m_pInterface->GetFields(&fields);
        for (int i = 0; i < count; i++) {
            if (fields[i].Name) {
                std::string name(fields[i].Name);
                if (name == "Inverted Depth") {
                    invertedDepth = *(bool*)fields[i].Data;
                } else if (name == "HDR") {
                    hdr = *(bool*)fields[i].Data;
                }
            }
        }
    }

    // Call actual upscaler plugin
    if (shouldLog)
        Logger::info("UpscalerManager::RenderFrame [CallPlugin] Calling OnRenderFrame...");

    m_pInterface->OnRenderFrame(cmd, source, target, swapchainFormat, (uint32_t)width, (uint32_t)height, m_renderWidth, m_renderHeight,
        depthImage, depthFormat, mvImage, mvFormat, (float)jitterX, (float)jitterY,
        0.1f,    // cameraNear
        1000.0f, // cameraFar
        1.0f,    // cameraFov
        1.0f,    // viewSpaceToMetersFactor
        invertedDepth, hdr);

    if (shouldLog)
        Logger::info("UpscalerManager::RenderFrame [End] OnRenderFrame returned.");

    lastW = width;
    lastH = height;
    m_frameUpscaled = true;
}

} // namespace GamePlug
