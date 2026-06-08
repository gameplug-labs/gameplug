#include "logger.h"
#include "upscaler_manager.h"
#include "config.h"
#include "imgui.h"
#include <filesystem>
#include <vector>

namespace GamePlug {

UpscalerManager& UpscalerManager::Get() {
    static UpscalerManager instance;
    return instance;
}

UpscalerManager::UpscalerManager() {}

UpscalerManager::~UpscalerManager() {
    UnloadUpscaler();
}

bool UpscalerManager::LoadUpscaler(const std::string& name) {
    if (m_handle) return true;

    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path gamePath = std::filesystem::path(buf).parent_path();
    std::filesystem::path upscalerPath = gamePath / name;

    Logger::info("UpscalerManager: Loading " + upscalerPath.string());

    if (!std::filesystem::exists(upscalerPath)) {
        Logger::warn("UpscalerManager: Plugin NOT found at " + upscalerPath.string());
        return false;
    }

    m_handle = LoadLibraryA(upscalerPath.string().c_str());
    if (!m_handle) {
        Logger::error("UpscalerManager: Failed to load plugin DLL");
        return false;
    }

    typedef GamePlugUpscalerInterface* (*GetInterfaceFn)();
    auto getInterface = (GetInterfaceFn)GetProcAddress(m_handle, GamePlug_GET_UPSCALER_INTERFACE_NAME);
    if (!getInterface) {
        Logger::error("UpscalerManager: Plugin does not export interface function");
        FreeLibrary(m_handle);
        m_handle = nullptr;
        return false;
    }

    m_pInterface = getInterface();
    Logger::info("UpscalerManager: Plugin loaded successfully.");
    return true;
}

void UpscalerManager::UnloadUpscaler() {
    if (m_handle && !m_isShuttingDown) {
        m_isShuttingDown = true;
        if (m_pInterface && m_pInterface->OnShutdown) m_pInterface->OnShutdown();
        FreeLibrary(m_handle);
        m_handle = nullptr;
        m_pInterface = nullptr;
    }
}

void UpscalerManager::InitUpscaler(void* device) {
    Logger::info("UpscalerManager::InitUpscaler: Entry (device={:p})", (void*)device);
    if (m_pInterface && m_pInterface->OnInit) {
        Logger::info("UpscalerManager::InitUpscaler: Calling OnInit...");
        m_pInterface->OnInit(
            ImGui::GetCurrentContext(),
            Logger::LogBridge,
            nullptr,
            0, 0, (uintptr_t)device, 0, 0, nullptr
        );
        Logger::info("UpscalerManager: OnInit returned.");
    } else {
        Logger::warn("UpscalerManager::InitUpscaler: Interface or OnInit missing!");
    }
}

void UpscalerManager::OnReset() {
    Logger::info("UpscalerManager::OnReset: Triggering plugin shutdown to release resources.");
    if (m_pInterface && m_pInterface->OnShutdown) {
        m_pInterface->OnShutdown();
    }
}

void UpscalerManager::RenderFrame(void* device, void* source, void* target, uint32_t w, uint32_t h, uint32_t rw, uint32_t rh) {
    if (!m_pInterface || !m_pInterface->OnRenderFrame) return;

    m_width = w;
    m_height = h;
    m_renderWidth = rw;
    m_renderHeight = rh;

    static uint32_t frameCount = 0;
    if (frameCount % 100 == 0) {
        Logger::info("UpscalerManager::RenderFrame: Frame {} Display {}x{}, Render {}x{}, device={:p}", frameCount, w, h, rw, rh, (void*)device);
    }
    frameCount++;

    m_pInterface->OnRenderFrame(
        (uintptr_t)device, (uint64_t)source, (uint64_t)target, 0,
        w, h, rw, rh, 0, 0, 0, 0, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, false, false
    );

}

bool UpscalerManager::IsUpscalingEnabled() const {
    if (!m_pInterface || !m_pInterface->GetFields) return false;
    GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
    int count = m_pInterface->GetFields(&fields);
    for (int i = 0; i < count; i++) {
        if (fields[i].Name && strcmp(fields[i].Name, "Upscaler Type") == 0) {
            int type = *(int*)fields[i].Data;
            return type > 0;
        }
    }
    return false;
}

bool UpscalerManager::IsNativeRenderingEnabled() const {
    if (!m_pInterface || !m_pInterface->GetFields) return false;
    GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
    int count = m_pInterface->GetFields(&fields);
    for (int i = 0; i < count; i++) {
        if (fields[i].Name) {
            std::string name(fields[i].Name);
            if (name == "Native AA" || name == "Native Rendering" || name == "DLAA") {
                bool native = *(bool*)fields[i].Data;
                return native;
            }
        }
    }
    return false;
}

int UpscalerManager::GetUpscaleQuality() const {
    if (!m_pInterface || !m_pInterface->GetFields) return 1; // Default to Quality
    GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
    int count = m_pInterface->GetFields(&fields);
    for (int i = 0; i < count; i++) {
        if (fields[i].Name && strcmp(fields[i].Name, "Upscale Quality") == 0) {
            int quality = *(int*)fields[i].Data;
            return quality;
        }
    }
    return 1;
}

void UpscalerManager::RenderUI(float fps, uint32_t width, uint32_t height) {
    if (!m_pInterface) return;

    static bool logged = false;
    if (!logged) {
        Logger::info("UpscalerManager::RenderUI: mgr={:p}", (void*)this);
        logged = true;
    }

    if (ImGui::CollapsingHeader(m_pInterface->GetName() ? m_pInterface->GetName() : "Upscaler", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_pInterface->GetFields) {
            GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
            int count = m_pInterface->GetFields(&fields);
            
            // Find Upscaler Type to handle conditional fields
            int upscalerType = 0; // None
            for(int i=0; i<count; i++) {
                if (fields[i].Name && strcmp(fields[i].Name, "Upscaler Type") == 0) {
                    upscalerType = *(int*)fields[i].Data;
                    break;
                }
            }
 
            if (count > 0 && fields) {
                ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
                for (int i = 0; i < count; i++) {
                    GamePlugUpscalerInterface::FieldDescriptor& f = fields[i];
                    
                    // Filter: HDR and Inverted Depth only for FSR2+ (if applicable in DX proxy)
                    if (upscalerType == 1) { // FSR 1.0
                        if (f.Name && (strcmp(f.Name, "HDR") == 0 || strcmp(f.Name, "Inverted Depth") == 0)) {
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
                            }
                            break;
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
                                    start = end + 1; end = options.find(',', start);
                                }
                                items.push_back(options.substr(start));
                                int* current = (int*)f.Data;
                                const char* preview = (*current >= 0 && *current < (int)items.size()) ? items[*current].c_str() : "Unknown";
                                if (ImGui::BeginCombo(f.Name, preview)) {
                                    for (int n = 0; n < (int)items.size(); n++) {
                                        if (ImGui::Selectable(items[n].c_str(), *current == n)) {
                                            *current = n; changed = true;
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                            break;
                    }
 
                    if (changed) {
                        Logger::info("UpscalerManager: UI Field '{}' changed. Notifying interface.", f.Name);
                        if (m_pInterface->OnFieldsChanged) m_pInterface->OnFieldsChanged();
                    }
                    ImGui::PopID();
                }
                ImGui::PopItemWidth();
            }
        }
        
        if (m_pInterface->OnImGuiRender) {
             m_pInterface->OnImGuiRender();
        }
 
        // Resolution and Performance Info (Single separator)
        // ImGui::Spacing();
        // ImGui::Separator();
        // ImGui::Spacing();
        
        // ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "[ SYSTEM ACTIVE ]");
        // ImGui::SameLine();
        // float availW = ImGui::GetContentRegionAvail().x;
        // float textW = ImGui::CalcTextSize("120 FPS").x; 
        // if (availW > textW) {
        //     ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availW - textW);
        // }
        // ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "%.0f FPS", fps);
        
        // ImGui::TextDisabled("PIPELINE RESOLUTION:");
        
        // ImGui::Text("  Target: %d x %d", width, height);
        
        // // In DX9 proxy, we often render at m_renderWidth x m_renderHeight
        // ImGui::Text("  Render: %d x %d", m_renderWidth, m_renderHeight);
        // float scale = (float)(width) / (float)m_renderWidth;
        // ImGui::SameLine();
        // ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%.2fx)", scale);
        // ImGui::Spacing();
    }
}

uint32_t UpscalerManager::GetVirtualWidth() const {
    uint32_t tw = Config::Get().GetTargetWidth();
    return tw > 0 ? tw : m_width;
}

uint32_t UpscalerManager::GetVirtualHeight() const {
    uint32_t th = Config::Get().GetTargetHeight();
    return th > 0 ? th : m_height;
}

} // namespace GamePlug
