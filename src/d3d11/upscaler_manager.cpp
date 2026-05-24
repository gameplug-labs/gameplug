#include "upscaler_manager.h"
#include "config.h"
#include "imgui.h"
#include "logger.h"
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <vector>

namespace GamePlug {
extern void PatchDeviceContextVTable(ID3D11DeviceContext* context);
typedef GamePlugUpscalerInterface* (*GamePlug_GetUpscalerInterfaceFn)();

void DXUpscalerLogBridge(GamePlugUpscalerInterface::LogLevel level, const char* message, void* context) {
    std::string formattedMsg = "[DXUpscaler] " + std::string(message);
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

DXUpscalerManager::~DXUpscalerManager() {
    UnloadUpscaler();
}

void DXUpscalerManager::LoadPlugin() {
    if (m_handle)
        return;

    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::filesystem::path gamePath = std::filesystem::path(buf).parent_path();
    std::filesystem::path upscalerPath = gamePath / "upscaler_plugin.dll";

    Logger::info("DXUpscalerManager: Loading " + upscalerPath.string());

    if (!std::filesystem::exists(upscalerPath)) {
        Logger::warn("DXUpscalerManager: upscaler_plugin.dll NOT found at " + upscalerPath.string());
        return;
    }

    m_handle = LoadLibraryA(upscalerPath.string().c_str());
    if (!m_handle) {
        Logger::error("DXUpscalerManager: Failed to load upscaler_plugin.dll");
        return;
    }

    auto getInterface = (GamePlug_GetUpscalerInterfaceFn)GetProcAddress(m_handle, GamePlug_GET_UPSCALER_INTERFACE_NAME);
    if (!getInterface) {
        Logger::error("DXUpscalerManager: upscaler_plugin.dll does not export " + std::string(GamePlug_GET_UPSCALER_INTERFACE_NAME));
        FreeLibrary(m_handle);
        m_handle = nullptr;
        return;
    }

    m_pInterface = getInterface();
    Logger::info("DXUpscalerManager: Plugin interface loaded.");
}

void DXUpscalerManager::InitDX11(ID3D11Device* device, ID3D11DeviceContext* context) {
    LoadPlugin();
    if (!m_pInterface)
        return;

    m_pd3dDevice = device;
    m_pd3dDeviceContext = context;

    PatchDeviceContextVTable(context);

    if (m_pInterface->OnInit) {
        m_pInterface->OnInit(ImGui::GetCurrentContext(), DXUpscalerLogBridge, nullptr,
            0, // instance (not used in DX)
            0, // physicalDevice (not used in DX)
            (uintptr_t)device, (uintptr_t)context,
            11,     // queueFamilyIndex reused for API version (11 = DX11)
            nullptr // memoryProperties
        );
    }
}

void DXUpscalerManager::UnloadUpscaler() {
    if (m_handle && !m_isShuttingDown) {
        m_isShuttingDown = true;
        CleanupPlugin();
        FreeLibrary(m_handle);
        m_handle = nullptr;
    }
}

void DXUpscalerManager::CleanupPlugin() {
    if (m_pInterface && m_pInterface->OnShutdown) {
        Logger::info("DXUpscalerManager: Calling OnShutdown (Reset)");
        m_pInterface->OnShutdown();
    }
    DestroyFakeBackBuffer();
    m_pd3dDevice = nullptr;
    m_pd3dDeviceContext = nullptr;
}

bool DXUpscalerManager::IsUpscalingEnabled() const {
    if (!m_pInterface || !m_pInterface->GetFields)
        return false;
    GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
    int count = m_pInterface->GetFields(&fields);
    for (int i = 0; i < count; i++) {
        if (fields[i].Name && std::string(fields[i].Name) == "Upscaler Type") {
            int type = *(int*)fields[i].Data;
            return type > 0;
        }
    }
    return false;
}

void DXUpscalerManager::RenderUI(float fps, uint32_t width, uint32_t height) {
    if (!m_pInterface) {
        if (ImGui::CollapsingHeader("DX Upscaler", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("upscaler_plugin.dll not loaded.");
            if (ImGui::Button("Retry Loading")) {
                LoadPlugin();
            }
        }
        return;
    }

    if (ImGui::CollapsingHeader(m_pInterface->GetName() ? m_pInterface->GetName() : "DX Upscaler", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_pInterface->GetFields) {
            GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
            int count = m_pInterface->GetFields(&fields);

            int upscalerType = 0;
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

                    if (upscalerType == 1) { // FSR 1.0 filters
                        if (f.Name && (std::string(f.Name) == "HDR" || std::string(f.Name) == "Inverted Depth")) {
                            continue;
                        }
                    }

                    ImGui::PushID(f.Name);
                    bool changed = false;
                    switch (f.Type) {
                    case 0:
                        changed = ImGui::Checkbox(f.Name, (bool*)f.Data);
                        break;
                    case 1:
                        changed = ImGui::DragInt(f.Name, (int*)f.Data);
                        break;
                    case 2:
                        changed = ImGui::DragFloat(f.Name, (float*)f.Data);
                        break;
                    case 3:
                        changed = ImGui::InputText(f.Name, (char*)f.Data, (size_t)f.DataSize);
                        break;
                    case 4:
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

        if (m_pInterface->OnImGuiRender)
            m_pInterface->OnImGuiRender();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "[ DX SYSTEM ACTIVE ]");
        // ImGui::SameLine();
        // float availW = ImGui::GetContentRegionAvail().x;
        // float textW = ImGui::CalcTextSize("120 FPS").x;
        // if (availW > textW)
        //     ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availW - textW);
        // ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "%.0f FPS", fps);

        // ImGui::TextDisabled("PIPELINE RESOLUTION:");
        // ImGui::Text("  Target: %d x %d", width, height);
        // ImGui::Text("  Render: %d x %d", m_renderWidth, m_renderHeight);
        // float scale = (float)width / (float)m_renderWidth;
        // ImGui::SameLine();
        // ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%.2fx)", scale);
    }

    // if (IsShowDebugImageEnabled()) {
    //     bool show = true;
    //     ImGui::Begin("Upscaler Debug View", &show);
    //     if (!show) {
    //         SetShowDebugImageEnabled(false);
    //     }

    //     uint32_t dw = m_renderWidth;
    //     uint32_t dh = m_renderHeight;
    //     ID3D11ShaderResourceView* debugSRV = m_fakeBackBufferSRV;

    //     if (debugSRV && dw > 0 && dh > 0) {
    //         ImGui::Text("Source Resource: %u x %u", dw, dh);

    //         float windowWidth = ImGui::GetContentRegionAvail().x;
    //         float windowHeight = ImGui::GetContentRegionAvail().y - 30.0f;
    //         float aspect = (float)dh / (float)dw;

    //         float imgWidth = windowWidth;
    //         float imgHeight = windowWidth * aspect;

    //         if (imgHeight > windowHeight) {
    //             imgHeight = windowHeight;
    //             imgWidth = windowHeight / aspect;
    //         }

    //         ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (windowWidth - imgWidth) * 0.5f);
    //         ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (windowHeight - imgHeight) * 0.5f);

    //         ImGui::Image((ImTextureID)debugSRV, ImVec2(imgWidth, imgHeight));
    //     } else {
    //         ImGui::Text("Debug image not available.");
    //     }

    //     ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 25.0f);
    //     if (ImGui::Button("Close")) {
    //         SetShowDebugImageEnabled(false);
    //     }
    //     ImGui::End();
    // }
}

void DXUpscalerManager::UpdateDimensions(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
    GetTargetResolution(width, height, m_renderWidth, m_renderHeight);
}

void DXUpscalerManager::GetTargetResolution(uint32_t width, uint32_t height, uint32_t& outW, uint32_t& outH) {
    outW = width;
    outH = height;
    if (m_pInterface && m_pInterface->GetFields) {
        GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
        int count = m_pInterface->GetFields(&fields);

        // Check Native Rendering first
        for (int i = 0; i < count; i++) {
            if (fields[i].Name && std::string(fields[i].Name) == "Native Rendering") {
                if (*(bool*)fields[i].Data) {
                    return;
                }
                break;
            }
        }

        // Apply quality scaling if not native
        for (int i = 0; i < count; i++) {
            if (fields[i].Name && std::string(fields[i].Name) == "Upscale Quality") {
                int quality = *(int*)fields[i].Data;
                float ratio = 1.0f;
                float ratios[] = {1.2f, 1.3f, 1.5f, 1.7f, 2.0f, 3.0f};
                if (quality >= 0 && quality < 6)
                    ratio = ratios[quality];
                else
                    ratio = 1.3f;

                outW = (uint32_t)((float)width / ratio + 0.5f);
                outH = (uint32_t)((float)height / ratio + 0.5f);
                return;
            }
        }
    }
}

float DXUpscalerManager::GetScaleFactor() const {
    if (m_width == 0 || m_renderWidth == 0)
        return 1.0f;
    return (float)m_renderWidth / (float)m_width;
}

void DXUpscalerManager::RenderFrameDX11(
    ID3D11DeviceContext* context, ID3D11ShaderResourceView* sourceSRV, ID3D11RenderTargetView* targetRTV, uint32_t width, uint32_t height) {
    if (!m_pInterface || !m_pInterface->OnRenderFrame || m_frameUpscaled)
        return;
    if (!sourceSRV || !targetRTV) {
        Logger::warn("DXUpscalerManager: RenderFrameDX11 called with NULL resources");
        return;
    }

    Logger::warn("FSR EXECUTED FRAME");
    Logger::info("DXUpscalerManager: Calling OnRenderFrame (DX11)");

    m_frameUpscaled = true;
    m_width = width;
    m_height = height;
    UpdateDimensions(width, height);

    m_pInterface->OnRenderFrame((uintptr_t)context, (uint64_t)sourceSRV, (uint64_t)targetRTV, 0, width, height, m_renderWidth,
        m_renderHeight, 0, 0, 0, 0, 0.0f, 0.0f);
}

void DXUpscalerManager::ResetFrame() {
    m_frameUpscaled = false;
}

void DXUpscalerManager::CreateFakeBackBuffer(IDXGISwapChain* swapChain) {
    if (!m_pd3dDevice) {
        Logger::error("DXUpscalerManager: CreateFakeBackBuffer failed - Device is NULL!");
        return;
    }

    DXGI_SWAP_CHAIN_DESC sd;
    if (FAILED(swapChain->GetDesc(&sd))) {
        Logger::error("DXUpscalerManager: CreateFakeBackBuffer failed - SwapChain GetDesc failed!");
        return;
    }

    UpdateDimensions(sd.BufferDesc.Width, sd.BufferDesc.Height);

    if (m_fakeBackBuffer) {
        D3D11_TEXTURE2D_DESC desc;
        m_fakeBackBuffer->GetDesc(&desc);
        if (desc.Width == m_renderWidth && desc.Height == m_renderHeight && desc.Format == sd.BufferDesc.Format) {
            Logger::info("DXUpscalerManager: Fake BackBuffer already exists with correct dimensions (" + std::to_string(m_renderWidth) +
                         "x" + std::to_string(m_renderHeight) + ") and format (" + std::to_string(sd.BufferDesc.Format) +
                         "). Skipping recreation.");
            return;
        }
    }

    DestroyFakeBackBuffer();

    Logger::info("DXUpscalerManager: Creating Fake BackBuffer at " + std::to_string(m_renderWidth) + "x" + std::to_string(m_renderHeight) +
                 " (Display: " + std::to_string(m_width) + "x" + std::to_string(m_height) + ")");

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = m_renderWidth;
    desc.Height = m_renderHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = sd.BufferDesc.Format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    HRESULT hr = m_pd3dDevice->CreateTexture2D(&desc, nullptr, &m_fakeBackBuffer);
    if (FAILED(hr)) {
        Logger::error("DXUpscalerManager: Failed to create Fake BackBuffer texture (HR=" + std::to_string(hr) + ")");
        return;
    }

    hr = m_pd3dDevice->CreateShaderResourceView(m_fakeBackBuffer, nullptr, &m_fakeBackBufferSRV);
    if (FAILED(hr)) {
        Logger::error("DXUpscalerManager: Failed to create SRV for Fake BackBuffer (HR=" + std::to_string(hr) + ")");
        m_fakeBackBuffer->Release();
        m_fakeBackBuffer = nullptr;
        return;
    }

    Logger::info("DXUpscalerManager: Fake BackBuffer successfully created.");
}

void DXUpscalerManager::DestroyFakeBackBuffer() {
    if (m_fakeBackBufferSRV) {
        m_fakeBackBufferSRV->Release();
        m_fakeBackBufferSRV = nullptr;
    }
    if (m_fakeBackBuffer) {
        m_fakeBackBuffer->Release();
        m_fakeBackBuffer = nullptr;
    }
    Logger::info("DXUpscalerManager: Fake BackBuffer destroyed.");
}

bool DXUpscalerManager::IsShowDebugImageEnabled() const {
    if (!m_pInterface || !m_pInterface->GetFields)
        return false;
    GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
    int count = m_pInterface->GetFields(&fields);
    for (int i = 0; i < count; i++) {
        if (fields[i].Name && std::string(fields[i].Name) == "Show Debug Image") {
            return *(bool*)fields[i].Data;
        }
    }
    return false;
}

void DXUpscalerManager::SetShowDebugImageEnabled(bool enabled) {
    if (!m_pInterface || !m_pInterface->GetFields)
        return;
    GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
    int count = m_pInterface->GetFields(&fields);
    for (int i = 0; i < count; i++) {
        if (fields[i].Name && std::string(fields[i].Name) == "Show Debug Image") {
            *(bool*)fields[i].Data = enabled;
            if (m_pInterface->OnFieldsChanged) {
                m_pInterface->OnFieldsChanged();
            }
            break;
        }
    }
}

} // namespace GamePlug
