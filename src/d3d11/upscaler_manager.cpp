#include "upscaler_manager.h"
#include "config.h"
#include "imgui.h"
#include "logger.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <vector>
#include <d3dcompiler.h>

namespace GamePlug {
extern void PatchDeviceContextVTable(ID3D11DeviceContext* context);
typedef GamePlugUpscalerInterface* (*GamePlug_GetUpscalerInterfaceFn)();

static std::string DXGIFormatToString(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_UNKNOWN:
        return "DXGI_FORMAT_UNKNOWN";
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return "DXGI_FORMAT_R32G32B32A32_TYPELESS";
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return "DXGI_FORMAT_R32G32B32A32_FLOAT";
    case DXGI_FORMAT_R32G32B32A32_UINT:
        return "DXGI_FORMAT_R32G32B32A32_UINT";
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return "DXGI_FORMAT_R32G32B32A32_SINT";
    case DXGI_FORMAT_R32G32B32_TYPELESS:
        return "DXGI_FORMAT_R32G32B32_TYPELESS";
    case DXGI_FORMAT_R32G32B32_FLOAT:
        return "DXGI_FORMAT_R32G32B32_FLOAT";
    case DXGI_FORMAT_R32G32B32_UINT:
        return "DXGI_FORMAT_R32G32B32_UINT";
    case DXGI_FORMAT_R32G32B32_SINT:
        return "DXGI_FORMAT_R32G32B32_SINT";
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return "DXGI_FORMAT_R16G16B16A16_TYPELESS";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return "DXGI_FORMAT_R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_UNORM:
        return "DXGI_FORMAT_R16G16B16A16_UNORM";
    case DXGI_FORMAT_R16G16B16A16_UINT:
        return "DXGI_FORMAT_R16G16B16A16_UINT";
    case DXGI_FORMAT_R16G16B16A16_SNORM:
        return "DXGI_FORMAT_R16G16B16A16_SNORM";
    case DXGI_FORMAT_R16G16B16A16_SINT:
        return "DXGI_FORMAT_R16G16B16A16_SINT";
    case DXGI_FORMAT_R32G32_TYPELESS:
        return "DXGI_FORMAT_R32G32_TYPELESS";
    case DXGI_FORMAT_R32G32_FLOAT:
        return "DXGI_FORMAT_R32G32_FLOAT";
    case DXGI_FORMAT_R32G32_UINT:
        return "DXGI_FORMAT_R32G32_UINT";
    case DXGI_FORMAT_R32G32_SINT:
        return "DXGI_FORMAT_R32G32_SINT";
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        return "DXGI_FORMAT_R32G8X24_TYPELESS";
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return "DXGI_FORMAT_D32_FLOAT_S8X24_UINT";
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        return "DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS";
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        return "DXGI_FORMAT_X32_TYPELESS_G8X24_UINT";
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return "DXGI_FORMAT_R10G10B10A2_TYPELESS";
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return "DXGI_FORMAT_R10G10B10A2_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return "DXGI_FORMAT_R10G10B10A2_UINT";
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return "DXGI_FORMAT_R11G11B10_FLOAT";
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return "DXGI_FORMAT_R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return "DXGI_FORMAT_R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_UINT:
        return "DXGI_FORMAT_R8G8B8A8_UINT";
    case DXGI_FORMAT_R8G8B8A8_SNORM:
        return "DXGI_FORMAT_R8G8B8A8_SNORM";
    case DXGI_FORMAT_R8G8B8A8_SINT:
        return "DXGI_FORMAT_R8G8B8A8_SINT";
    case DXGI_FORMAT_R16G16_TYPELESS:
        return "DXGI_FORMAT_R16G16_TYPELESS";
    case DXGI_FORMAT_R16G16_FLOAT:
        return "DXGI_FORMAT_R16G16_FLOAT";
    case DXGI_FORMAT_R16G16_UNORM:
        return "DXGI_FORMAT_R16G16_UNORM";
    case DXGI_FORMAT_R16G16_UINT:
        return "DXGI_FORMAT_R16G16_UINT";
    case DXGI_FORMAT_R16G16_SNORM:
        return "DXGI_FORMAT_R16G16_SNORM";
    case DXGI_FORMAT_R16G16_SINT:
        return "DXGI_FORMAT_R16G16_SINT";
    case DXGI_FORMAT_R32_TYPELESS:
        return "DXGI_FORMAT_R32_TYPELESS";
    case DXGI_FORMAT_D32_FLOAT:
        return "DXGI_FORMAT_D32_FLOAT";
    case DXGI_FORMAT_R32_FLOAT:
        return "DXGI_FORMAT_R32_FLOAT";
    case DXGI_FORMAT_R32_UINT:
        return "DXGI_FORMAT_R32_UINT";
    case DXGI_FORMAT_R32_SINT:
        return "DXGI_FORMAT_R32_SINT";
    case DXGI_FORMAT_R24G8_TYPELESS:
        return "DXGI_FORMAT_R24G8_TYPELESS";
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return "DXGI_FORMAT_D24_UNORM_S8_UINT";
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        return "DXGI_FORMAT_R24_UNORM_X8_TYPELESS";
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        return "DXGI_FORMAT_X24_TYPELESS_G8_UINT";
    case DXGI_FORMAT_R8G8_TYPELESS:
        return "DXGI_FORMAT_R8G8_TYPELESS";
    case DXGI_FORMAT_R8G8_UNORM:
        return "DXGI_FORMAT_R8G8_UNORM";
    case DXGI_FORMAT_R8G8_UINT:
        return "DXGI_FORMAT_R8G8_UINT";
    case DXGI_FORMAT_R8G8_SNORM:
        return "DXGI_FORMAT_R8G8_SNORM";
    case DXGI_FORMAT_R8G8_SINT:
        return "DXGI_FORMAT_R8G8_SINT";
    case DXGI_FORMAT_R16_TYPELESS:
        return "DXGI_FORMAT_R16_TYPELESS";
    case DXGI_FORMAT_R16_FLOAT:
        return "DXGI_FORMAT_R16_FLOAT";
    case DXGI_FORMAT_D16_UNORM:
        return "DXGI_FORMAT_D16_UNORM";
    case DXGI_FORMAT_R16_UNORM:
        return "DXGI_FORMAT_R16_UNORM";
    case DXGI_FORMAT_R16_UINT:
        return "DXGI_FORMAT_R16_UINT";
    case DXGI_FORMAT_R16_SNORM:
        return "DXGI_FORMAT_R16_SNORM";
    case DXGI_FORMAT_R16_SINT:
        return "DXGI_FORMAT_R16_SINT";
    case DXGI_FORMAT_R8_TYPELESS:
        return "DXGI_FORMAT_R8_TYPELESS";
    case DXGI_FORMAT_R8_UNORM:
        return "DXGI_FORMAT_R8_UNORM";
    case DXGI_FORMAT_R8_UINT:
        return "DXGI_FORMAT_R8_UINT";
    case DXGI_FORMAT_R8_SNORM:
        return "DXGI_FORMAT_R8_SNORM";
    case DXGI_FORMAT_R8_SINT:
        return "DXGI_FORMAT_R8_SINT";
    case DXGI_FORMAT_A8_UNORM:
        return "DXGI_FORMAT_A8_UNORM";
    case DXGI_FORMAT_R1_UNORM:
        return "DXGI_FORMAT_R1_UNORM";
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        return "DXGI_FORMAT_R9G9B9E5_SHAREDEXP";
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
        return "DXGI_FORMAT_R8G8_B8G8_UNORM";
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
        return "DXGI_FORMAT_G8R8_G8B8_UNORM";
    case DXGI_FORMAT_BC1_TYPELESS:
        return "DXGI_FORMAT_BC1_TYPELESS";
    case DXGI_FORMAT_BC1_UNORM:
        return "DXGI_FORMAT_BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return "DXGI_FORMAT_BC1_UNORM_SRGB";
    case DXGI_FORMAT_BC2_TYPELESS:
        return "DXGI_FORMAT_BC2_TYPELESS";
    case DXGI_FORMAT_BC2_UNORM:
        return "DXGI_FORMAT_BC2_UNORM";
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        return "DXGI_FORMAT_BC2_UNORM_SRGB";
    case DXGI_FORMAT_BC3_TYPELESS:
        return "DXGI_FORMAT_BC3_TYPELESS";
    case DXGI_FORMAT_BC3_UNORM:
        return "DXGI_FORMAT_BC3_UNORM";
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return "DXGI_FORMAT_BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_TYPELESS:
        return "DXGI_FORMAT_BC4_TYPELESS";
    case DXGI_FORMAT_BC4_UNORM:
        return "DXGI_FORMAT_BC4_UNORM";
    case DXGI_FORMAT_BC4_SNORM:
        return "DXGI_FORMAT_BC4_SNORM";
    case DXGI_FORMAT_BC5_TYPELESS:
        return "DXGI_FORMAT_BC5_TYPELESS";
    case DXGI_FORMAT_BC5_UNORM:
        return "DXGI_FORMAT_BC5_UNORM";
    case DXGI_FORMAT_BC5_SNORM:
        return "DXGI_FORMAT_BC5_SNORM";
    case DXGI_FORMAT_B5G6R5_UNORM:
        return "DXGI_FORMAT_B5G6R5_UNORM";
    case DXGI_FORMAT_B5G5R5A1_UNORM:
        return "DXGI_FORMAT_B5G5R5A1_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return "DXGI_FORMAT_B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return "DXGI_FORMAT_B8G8R8X8_UNORM";
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return "DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM";
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return "DXGI_FORMAT_B8G8R8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return "DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return "DXGI_FORMAT_B8G8R8X8_TYPELESS";
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return "DXGI_FORMAT_B8G8R8X8_UNORM_SRGB";
    case DXGI_FORMAT_BC6H_TYPELESS:
        return "DXGI_FORMAT_BC6H_TYPELESS";
    case DXGI_FORMAT_BC6H_UF16:
        return "DXGI_FORMAT_BC6H_UF16";
    case DXGI_FORMAT_BC6H_SF16:
        return "DXGI_FORMAT_BC6H_SF16";
    case DXGI_FORMAT_BC7_TYPELESS:
        return "DXGI_FORMAT_BC7_TYPELESS";
    case DXGI_FORMAT_BC7_UNORM:
        return "DXGI_FORMAT_BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return "DXGI_FORMAT_BC7_UNORM_SRGB";
    case DXGI_FORMAT_AYUV:
        return "DXGI_FORMAT_AYUV";
    case DXGI_FORMAT_Y410:
        return "DXGI_FORMAT_Y410";
    case DXGI_FORMAT_Y416:
        return "DXGI_FORMAT_Y416";
    case DXGI_FORMAT_NV12:
        return "DXGI_FORMAT_NV12";
    case DXGI_FORMAT_P010:
        return "DXGI_FORMAT_P010";
    case DXGI_FORMAT_P016:
        return "DXGI_FORMAT_P016";
    case DXGI_FORMAT_420_OPAQUE:
        return "DXGI_FORMAT_420_OPAQUE";
    case DXGI_FORMAT_YUY2:
        return "DXGI_FORMAT_YUY2";
    case DXGI_FORMAT_Y210:
        return "DXGI_FORMAT_Y210";
    case DXGI_FORMAT_Y216:
        return "DXGI_FORMAT_Y216";
    case DXGI_FORMAT_NV11:
        return "DXGI_FORMAT_NV11";
    case DXGI_FORMAT_AI44:
        return "DXGI_FORMAT_AI44";
    case DXGI_FORMAT_IA44:
        return "DXGI_FORMAT_IA44";
    case DXGI_FORMAT_P8:
        return "DXGI_FORMAT_P8";
    case DXGI_FORMAT_A8P8:
        return "DXGI_FORMAT_A8P8";
    case DXGI_FORMAT_B4G4R4A4_UNORM:
        return "DXGI_FORMAT_B4G4R4A4_UNORM";
    default:
        return "DXGI_FORMAT_UNKNOWN(" + std::to_string((int)format) + ")";
    }
}

static ID3D11ShaderResourceView* CreateSRVForTrackedTexture(ID3D11Device* device, ID3D11Texture2D* texture) {
    if (!device || !texture)
        return nullptr;

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    DXGI_FORMAT srvFormat = desc.Format;
    if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS) {
        srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    } else if (desc.Format == DXGI_FORMAT_R32G8X24_TYPELESS) {
        srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    } else if (desc.Format == DXGI_FORMAT_R32_TYPELESS) {
        srvFormat = DXGI_FORMAT_R32_FLOAT;
    } else if (desc.Format == DXGI_FORMAT_R16_TYPELESS) {
        srvFormat = DXGI_FORMAT_R16_UNORM;
    }

    srvDesc.Format = srvFormat;

    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = device->CreateShaderResourceView(texture, &srvDesc, &srv);
    if (SUCCEEDED(hr)) {
        return srv;
    }
    return nullptr;
}

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
    ResetTracker();
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
    ResetTracker();
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

                    if (f.Name && (std::string(f.Name) == "Camera Near" || std::string(f.Name) == "Camera Far" ||
                                      std::string(f.Name) == "Camera FOV" || std::string(f.Name) == "Meters Factor")) {
                        continue;
                    }

                    bool isBatman = false;
                    static bool checkedBatman = false;
                    static bool isBatmanCached = false;
                    if (!checkedBatman) {
                        char exePath[MAX_PATH];
                        GetModuleFileNameA(NULL, exePath, MAX_PATH);
                        std::string exeName = std::filesystem::path(exePath).filename().string();
                        std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
                        isBatmanCached = (exeName.find("batman") != std::string::npos || exeName.find("fallout4") != std::string::npos);
                        checkedBatman = true;
                    }
                    isBatman = isBatmanCached;

                    if (isBatman && f.Name && (std::string(f.Name) == "Upscale Quality" || std::string(f.Name) == "Quality Type" || std::string(f.Name) == "Quality")) {
                        continue;
                    }

                    ImGui::PushID(f.Name);
                    bool changed = false;
                    bool isNativeField = false;
                    if (isBatman && f.Name) {
                        std::string name(f.Name);
                        if (name == "Native AA" || name == "Native Rendering" || name == "DLAA") {
                            isNativeField = true;
                            *(bool*)f.Data = true;
                        }
                    }

                    if (isNativeField) {
                        ImGui::BeginDisabled();
                    }

                    switch (f.Type) {
                    case 0:
                        changed = ImGui::Checkbox(f.Name, (bool*)f.Data);
                        break;
                    case 1:
                        changed = ImGui::DragInt(f.Name, (int*)f.Data);
                        break;
                    case 2:
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
                            if (isBatman && f.Name && std::string(f.Name) == "Upscaler Type" && *current == 2) {
                                *current = 0; // reset from FSR 2 to None
                            }
                            const char* preview = (*current >= 0 && *current < (int)items.size()) ? items[*current].c_str() : "Unknown";
                            if (ImGui::BeginCombo(f.Name, preview)) {
                                for (int n = 0; n < (int)items.size(); n++) {
                                    if (isBatman && f.Name && std::string(f.Name) == "Upscaler Type" && items[n] == "FSR 2") {
                                        continue;
                                    }
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

                    if (isNativeField) {
                        *(bool*)f.Data = true;
                        ImGui::EndDisabled();
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

        // ImGui::Spacing();
        // ImGui::Separator();
        // ImGui::Spacing();

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

    //     const char* debugItems[] = {"Source (Fake Back Buffer)", "Depth Buffer", "Motion Vectors"};
    //     ImGui::Combo("Preview Target", &m_debugPreviewIndex, debugItems, IM_ARRAYSIZE(debugItems));

    //     uint32_t dw = 0;
    //     uint32_t dh = 0;
    //     ID3D11ShaderResourceView* debugSRV = nullptr;
    //     std::string targetName = "";

    //     {
    //         std::lock_guard<std::mutex> lock(m_trackerMtx);
    //         if (m_debugPreviewIndex == 0) {
    //             dw = m_renderWidth;
    //             dh = m_renderHeight;
    //             debugSRV = m_fakeBackBufferSRV;
    //             targetName = "Fake Back Buffer";
    //         } else if (m_debugPreviewIndex == 1) {
    //             if (m_downsampledDepthSRV && m_depthWidth != m_renderWidth) {
    //                 dw = m_renderWidth;
    //                 dh = m_renderHeight;
    //                 debugSRV = m_downsampledDepthSRV;
    //                 targetName = "Depth Buffer (Downsampled)";
    //             } else {
    //                 dw = m_depthWidth;
    //                 dh = m_depthHeight;
    //                 debugSRV = m_depthSRV;
    //                 targetName = "Depth Buffer";
    //             }
    //         } else if (m_debugPreviewIndex == 2) {
    //             if (m_downsampledMVSRV && m_mvWidth != m_renderWidth) {
    //                 dw = m_renderWidth;
    //                 dh = m_renderHeight;
    //                 debugSRV = m_downsampledMVSRV;
    //                 targetName = "Motion Vectors (Downsampled)";
    //             } else {
    //                 dw = m_mvWidth;
    //                 dh = m_mvHeight;
    //                 debugSRV = m_mvSRV;
    //                 targetName = "Motion Vectors";
    //             }
    //         }
    //     }

    //     if (debugSRV && dw > 0 && dh > 0) {
    //         ImGui::Text("%s Resource: %u x %u", targetName.c_str(), dw, dh);

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
    //         ImGui::Text("%s is not available (or cannot be bound as SRV).", targetName.c_str());
    //     }

    //     ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 25.0f);
    //     if (ImGui::Button("Close")) {
    //         SetShowDebugImageEnabled(false);
    //     }
    //     ImGui::End();
    // }
}

void DXUpscalerManager::UpdateDimensions(uint32_t width, uint32_t height) {
    if (width != m_width || height != m_height) {
        ResetTracker();
    }
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
            if (fields[i].Name) {
                std::string name(fields[i].Name);

                if (name == "Native AA" || name == "Native Rendering" || name == "DLAA") {
                    bool isBatman = false;
                    static bool checkedBatman = false;
                    static bool isBatmanCached = false;
                    if (!checkedBatman) {
                        char exePath[MAX_PATH];
                        GetModuleFileNameA(NULL, exePath, MAX_PATH);
                        std::string exeName = std::filesystem::path(exePath).filename().string();
                        std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
                        isBatmanCached = (exeName.find("batman") != std::string::npos || exeName.find("fallout4") != std::string::npos);
                        checkedBatman = true;
                    }
                    isBatman = isBatmanCached;

                    if (isBatman) {
                        *(bool*)fields[i].Data = true;
                    }

                    if (*(bool*)fields[i].Data) {
                        return;
                    }

                    break;
                }
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

    // HDR autodetection
    bool isHDR = false;
    if (sourceSRV) {
        ID3D11Resource* srcRes = nullptr;
        sourceSRV->GetResource(&srcRes);
        if (srcRes) {
            ID3D11Texture2D* srcTex = nullptr;
            if (SUCCEEDED(srcRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&srcTex))) {
                D3D11_TEXTURE2D_DESC srcDesc;
                srcTex->GetDesc(&srcDesc);
                if (srcDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || srcDesc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
                    srcDesc.Format == DXGI_FORMAT_R11G11B10_FLOAT || srcDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
                    isHDR = true;
                }
                srcTex->Release();
            }
            srcRes->Release();
        }
    }
    if (targetRTV && !isHDR) {
        ID3D11Resource* dstRes = nullptr;
        targetRTV->GetResource(&dstRes);
        if (dstRes) {
            ID3D11Texture2D* dstTex = nullptr;
            if (SUCCEEDED(dstRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&dstTex))) {
                D3D11_TEXTURE2D_DESC dstDesc;
                dstTex->GetDesc(&dstDesc);
                if (dstDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || dstDesc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
                    dstDesc.Format == DXGI_FORMAT_R11G11B10_FLOAT || dstDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
                    isHDR = true;
                    Logger::info("DXUpscalerManager: Check HDR detect " + std::string(isHDR ? "true" : "false"));
                }
                Logger::info("DXUpscalerManager: Check HDR detect " + std::string(isHDR ? "true" : "false"));
                dstTex->Release();
            }
            dstRes->Release();
        }
    }

    if (isHDR == m_detectedHDR) {
        if (m_hdrConfidence < 10) {
            m_hdrConfidence++;
            if (m_hdrConfidence == 10) {
                Logger::info("DXUpscalerManager: HDR state stabilized to: " + std::string(isHDR ? "true" : "false"));
            }
        }
    } else {
        m_detectedHDR = isHDR;
        m_hdrConfidence = 1;
    }

    Logger::warn("FSR EXECUTED FRAME");
    Logger::info("DXUpscalerManager: Calling OnRenderFrame (DX11)");

    m_frameUpscaled = true;
    UpdateDimensions(width, height);

    m_projScanCounter++;
    if (m_projScanCounter >= 120 || m_projScanCounter == 1) {
        m_projScanCounter = 1;
        ScanProjectionMatrix(context);
    }

    static uint32_t frameCount = 0;
    static uint32_t lastW = 0, lastH = 0;
    bool shouldLog = (frameCount % 300 == 0) || (width != lastW || height != lastH);
    frameCount++;

    // Halton sequence for jitter
    m_jitterIndex++;
    if (m_jitterIndex > 128)
        m_jitterIndex = 1;

    auto HaltonFn = [](uint32_t index, uint32_t base) -> float {
        float f = 1.0f;
        float r = 0.0f;
        while (index > 0) {
            f /= (float)base;
            r += f * (float)(index % base);
            index /= base;
        }
        return r;
    };

    float jitterX = HaltonFn(m_jitterIndex, 2) - 0.5f;
    float jitterY = HaltonFn(m_jitterIndex, 3) - 0.5f;

    if (shouldLog) {
        Logger::info("DXUpscalerManager::RenderFrameDX11 [Jitter] x=" + std::to_string(jitterX) + " y=" + std::to_string(jitterY));
    }

    // 1. Compile downsampling compute shader if not done
    if (!m_downsampleCS && m_pd3dDevice) {
        const char* shaderSrc = R"(
Texture2D<float4> InputTex : register(t0);
RWTexture2D<float4> OutputTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 dstSize;
    OutputTex.GetDimensions(dstSize.x, dstSize.y);
    if (dispatchThreadID.x >= dstSize.x || dispatchThreadID.y >= dstSize.y) return;
    
    uint2 srcSize;
    InputTex.GetDimensions(srcSize.x, srcSize.y);
    
    float2 uv = (float2(dispatchThreadID.xy) + 0.5f) / float2(dstSize);
    uint2 srcPos = uint2(uv * float2(srcSize));
    OutputTex[dispatchThreadID.xy] = InputTex[srcPos];
}
)";
        ID3DBlob* csBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        HRESULT hr = D3DCompile(shaderSrc, strlen(shaderSrc), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &csBlob, &errorBlob);
        if (SUCCEEDED(hr)) {
            m_pd3dDevice->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_downsampleCS);
            csBlob->Release();
        } else {
            if (errorBlob) {
                Logger::error("DXUpscalerManager: Failed to compile downsample CS: " + std::string((char*)errorBlob->GetBufferPointer()));
                errorBlob->Release();
            }
        }
    }

    // 2. Fetch tracked resources under lock
    ID3D11Texture2D* depthTexToDownsample = nullptr;
    ID3D11ShaderResourceView* depthSRVToDownsample = nullptr;
    uint32_t depthW = 0, depthH = 0;
    DXGI_FORMAT depthFmt = DXGI_FORMAT_UNKNOWN;

    ID3D11Texture2D* mvTexToDownsample = nullptr;
    ID3D11ShaderResourceView* mvSRVToDownsample = nullptr;
    uint32_t mvW = 0, mvH = 0;
    DXGI_FORMAT mvFmt = DXGI_FORMAT_UNKNOWN;

    {
        std::lock_guard<std::mutex> lock(m_trackerMtx);
        if (m_depthTexture) {
            depthTexToDownsample = m_depthTexture;
            depthTexToDownsample->AddRef();
            depthSRVToDownsample = m_depthSRV;
            if (depthSRVToDownsample) depthSRVToDownsample->AddRef();
            depthW = m_depthWidth;
            depthH = m_depthHeight;
            depthFmt = m_depthFormat;
        }
        if (m_mvTexture) {
            mvTexToDownsample = m_mvTexture;
            mvTexToDownsample->AddRef();
            mvSRVToDownsample = m_mvSRV;
            if (mvSRVToDownsample) mvSRVToDownsample->AddRef();
            mvW = m_mvWidth;
            mvH = m_mvHeight;
            mvFmt = m_mvFormat;
        }
    }

    // 3. Downsample Depth
    bool didDownsampleDepth = false;
    if (m_downsampleCS && depthTexToDownsample && depthSRVToDownsample && 
        (depthW != m_renderWidth || depthH != m_renderHeight) && m_renderWidth > 0 && m_renderHeight > 0) {
        
        // Recreate UAV/SRV if dimensions or format changed
        D3D11_TEXTURE2D_DESC desc = {};
        if (m_downsampledDepthTex) m_downsampledDepthTex->GetDesc(&desc);
        
        if (!m_downsampledDepthTex || desc.Width != m_renderWidth || desc.Height != m_renderHeight) {
            if (m_downsampledDepthTex) m_downsampledDepthTex->Release();
            if (m_downsampledDepthSRV) m_downsampledDepthSRV->Release();
            if (m_downsampledDepthUAV) m_downsampledDepthUAV->Release();
            m_downsampledDepthTex = nullptr;
            m_downsampledDepthSRV = nullptr;
            m_downsampledDepthUAV = nullptr;

            D3D11_TEXTURE2D_DESC newDesc = {};
            newDesc.Width = m_renderWidth;
            newDesc.Height = m_renderHeight;
            newDesc.MipLevels = 1;
            newDesc.ArraySize = 1;
            newDesc.Format = DXGI_FORMAT_R32_FLOAT;
            newDesc.SampleDesc.Count = 1;
            newDesc.Usage = D3D11_USAGE_DEFAULT;
            newDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

            HRESULT hr = m_pd3dDevice->CreateTexture2D(&newDesc, nullptr, &m_downsampledDepthTex);
            if (SUCCEEDED(hr)) {
                m_pd3dDevice->CreateShaderResourceView(m_downsampledDepthTex, nullptr, &m_downsampledDepthSRV);
                m_pd3dDevice->CreateUnorderedAccessView(m_downsampledDepthTex, nullptr, &m_downsampledDepthUAV);
            }
        }

        if (m_downsampledDepthUAV) {
            context->CSSetShader(m_downsampleCS, nullptr, 0);
            context->CSSetShaderResources(0, 1, &depthSRVToDownsample);
            context->CSSetUnorderedAccessViews(0, 1, &m_downsampledDepthUAV, nullptr);

            UINT groupX = (m_renderWidth + 7) / 8;
            UINT groupY = (m_renderHeight + 7) / 8;
            context->Dispatch(groupX, groupY, 1);

            ID3D11ShaderResourceView* nullSRV = nullptr;
            ID3D11UnorderedAccessView* nullUAV = nullptr;
            context->CSSetShaderResources(0, 1, &nullSRV);
            context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

            didDownsampleDepth = true;
        }
    }

    // 4. Downsample Motion Vectors
    bool didDownsampleMV = false;
    if (m_downsampleCS && mvTexToDownsample && mvSRVToDownsample && 
        (mvW != m_renderWidth || mvH != m_renderHeight) && m_renderWidth > 0 && m_renderHeight > 0) {
        
        // Recreate UAV/SRV if dimensions or format changed
        D3D11_TEXTURE2D_DESC desc = {};
        if (m_downsampledMVTex) m_downsampledMVTex->GetDesc(&desc);
        
        if (!m_downsampledMVTex || desc.Width != m_renderWidth || desc.Height != m_renderHeight) {
            if (m_downsampledMVTex) m_downsampledMVTex->Release();
            if (m_downsampledMVSRV) m_downsampledMVSRV->Release();
            if (m_downsampledMVUAV) m_downsampledMVUAV->Release();
            m_downsampledMVTex = nullptr;
            m_downsampledMVSRV = nullptr;
            m_downsampledMVUAV = nullptr;

            D3D11_TEXTURE2D_DESC newDesc = {};
            newDesc.Width = m_renderWidth;
            newDesc.Height = m_renderHeight;
            newDesc.MipLevels = 1;
            newDesc.ArraySize = 1;
            newDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
            newDesc.SampleDesc.Count = 1;
            newDesc.Usage = D3D11_USAGE_DEFAULT;
            newDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

            HRESULT hr = m_pd3dDevice->CreateTexture2D(&newDesc, nullptr, &m_downsampledMVTex);
            if (SUCCEEDED(hr)) {
                m_pd3dDevice->CreateShaderResourceView(m_downsampledMVTex, nullptr, &m_downsampledMVSRV);
                m_pd3dDevice->CreateUnorderedAccessView(m_downsampledMVTex, nullptr, &m_downsampledMVUAV);
            }
        }

        if (m_downsampledMVUAV) {
            context->CSSetShader(m_downsampleCS, nullptr, 0);
            context->CSSetShaderResources(0, 1, &mvSRVToDownsample);
            context->CSSetUnorderedAccessViews(0, 1, &m_downsampledMVUAV, nullptr);

            UINT groupX = (m_renderWidth + 7) / 8;
            UINT groupY = (m_renderHeight + 7) / 8;
            context->Dispatch(groupX, groupY, 1);

            ID3D11ShaderResourceView* nullSRV = nullptr;
            ID3D11UnorderedAccessView* nullUAV = nullptr;
            context->CSSetShaderResources(0, 1, &nullSRV);
            context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

            didDownsampleMV = true;
        }
    }

    uint64_t depthImage = 0;
    uint32_t depthFormatVal = 0;
    uint64_t mvImage = 0;
    uint32_t mvFormatVal = 0;

    if (didDownsampleDepth) {
        depthImage = (uint64_t)m_downsampledDepthTex;
        depthFormatVal = (uint32_t)DXGI_FORMAT_R32_FLOAT;
    } else if (depthTexToDownsample) {
        depthImage = (uint64_t)depthTexToDownsample;
        depthFormatVal = (uint32_t)depthFmt;
    }

    if (didDownsampleMV) {
        mvImage = (uint64_t)m_downsampledMVTex;
        mvFormatVal = (uint32_t)DXGI_FORMAT_R16G16_FLOAT;
    } else if (mvTexToDownsample) {
        mvImage = (uint64_t)mvTexToDownsample;
        mvFormatVal = (uint32_t)mvFmt;
    }

    if (shouldLog) {
        std::string depthName = didDownsampleDepth ? "DXGI_FORMAT_R32_FLOAT (Downsampled)" : (depthTexToDownsample ? DXGIFormatToString(depthFmt) : "DXGI_FORMAT_UNKNOWN");
        std::string mvName = didDownsampleMV ? "DXGI_FORMAT_R16G16_FLOAT (Downsampled)" : (mvTexToDownsample ? DXGIFormatToString(mvFmt) : "DXGI_FORMAT_UNKNOWN");

        Logger::info("DXUpscalerManager::RenderFrameDX11 [Buffers] depth=" + std::to_string(depthImage) + " (" +
                     std::to_string(didDownsampleDepth ? m_renderWidth : depthW) + "x" + std::to_string(didDownsampleDepth ? m_renderHeight : depthH) + ", fmt=" + depthName +
                     "), mv=" + std::to_string(mvImage) + " (fmt=" + mvName + ")");
    }

    m_pInterface->OnRenderFrame((uintptr_t)context, (uint64_t)sourceSRV, (uint64_t)targetRTV, 0, width, height, m_renderWidth,
        m_renderHeight, depthImage, depthFormatVal, mvImage, mvFormatVal, 0.0f, 0.0f, m_cameraNear, m_cameraFar, m_cameraFov,
        m_viewSpaceToMetersFactor, m_detectedInvertedDepth, m_detectedHDR);

    // 5. Clean up local references
    if (depthTexToDownsample) depthTexToDownsample->Release();
    if (depthSRVToDownsample) depthSRVToDownsample->Release();
    if (mvTexToDownsample) mvTexToDownsample->Release();
    if (mvSRVToDownsample) mvSRVToDownsample->Release();

    lastW = width;
    lastH = height;
}

void DXUpscalerManager::TrackTexture(ID3D11Texture2D* texture, const D3D11_TEXTURE2D_DESC* desc) {
    if (!texture || !desc)
        return;

    std::lock_guard<std::mutex> lock(m_trackerMtx);

    // Identify Depth Buffer candidate
    if (desc->BindFlags & D3D11_BIND_DEPTH_STENCIL) {
        float score = 0.0f;
        if (m_width > 0 && desc->Width == m_width && desc->Height == m_height)
            score += 1000000.0f;
        if (desc->Width != desc->Height)
            score += 500000.0f;
        if (desc->Width >= 640 && desc->Height >= 360)
            score += 100000.0f;

        // Shadow map penalty
        if (desc->Width == desc->Height && m_width != m_height)
            score -= 800000.0f;

        // Minimum size filter
        if (desc->Width < 320 || desc->Height < 200)
            score = -100.0f;

        if (score > m_bestDepthScore && score > 0) {
            if (m_depthTexture) {
                m_depthTexture->Release();
            }
            if (m_depthSRV) {
                m_depthSRV->Release();
                m_depthSRV = nullptr;
            }
            m_depthTexture = texture;
            m_depthTexture->AddRef();
            m_depthWidth = desc->Width;
            m_depthHeight = desc->Height;
            m_depthFormat = desc->Format;
            m_bestDepthScore = score;

            m_depthSRV = CreateSRVForTrackedTexture(m_pd3dDevice, m_depthTexture);

            Logger::info("DXUpscalerManager: Depth buffer candidate updated (Width=" + std::to_string(desc->Width) +
                         ", Height=" + std::to_string(desc->Height) + ", Format=" + DXGIFormatToString(desc->Format) +
                         ", Score=" + std::to_string(score) + ", HasSRV=" + std::to_string(m_depthSRV != nullptr) + ")");
        }
    }

    // Identify Motion Vector candidate
    if ((desc->BindFlags & D3D11_BIND_RENDER_TARGET) || (desc->BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
        float score = 0.0f;

        // Size match to current render resolution or display resolution
        if (m_renderWidth > 0 && desc->Width == m_renderWidth && desc->Height == m_renderHeight) {
            score += 500000.0f;
        } else if (m_width > 0 && desc->Width == m_width && desc->Height == m_height) {
            score += 300000.0f;
        }

        // Format scoring: R16G16_FLOAT / R32G32_FLOAT etc are very common motion vector formats
        if (desc->Format == DXGI_FORMAT_R16G16_FLOAT || desc->Format == DXGI_FORMAT_R32G32_FLOAT) {
            score += 200000.0f;
        } else if (desc->Format == DXGI_FORMAT_R16G16_SNORM || desc->Format == DXGI_FORMAT_R16G16_UNORM) {
            score += 150000.0f;
        } else if (desc->Format == DXGI_FORMAT_R8G8_UNORM || desc->Format == DXGI_FORMAT_R8G8_SNORM) {
            score += 50000.0f;
        }

        if (score > m_bestMVScore && score > 0) {
            if (m_mvTexture) {
                m_mvTexture->Release();
            }
            if (m_mvSRV) {
                m_mvSRV->Release();
                m_mvSRV = nullptr;
            }
            m_mvTexture = texture;
            m_mvTexture->AddRef();
            m_mvWidth = desc->Width;
            m_mvHeight = desc->Height;
            m_mvFormat = desc->Format;
            m_bestMVScore = score;

            m_mvSRV = CreateSRVForTrackedTexture(m_pd3dDevice, m_mvTexture);

            Logger::info("DXUpscalerManager: Motion Vector candidate updated (Width=" + std::to_string(desc->Width) +
                         ", Height=" + std::to_string(desc->Height) + ", Format=" + DXGIFormatToString(desc->Format) +
                         ", Score=" + std::to_string(score) + ", HasSRV=" + std::to_string(m_mvSRV != nullptr) + ")");
        }
    }
}

void DXUpscalerManager::ResetTracker() {
    std::lock_guard<std::mutex> lock(m_trackerMtx);
    if (m_depthTexture) {
        m_depthTexture->Release();
        m_depthTexture = nullptr;
    }
    if (m_mvTexture) {
        m_mvTexture->Release();
        m_mvTexture = nullptr;
    }
    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }
    if (m_mvSRV) {
        m_mvSRV->Release();
        m_mvSRV = nullptr;
    }

    if (m_downsampleCS) {
        m_downsampleCS->Release();
        m_downsampleCS = nullptr;
    }
    if (m_downsampledDepthTex) {
        m_downsampledDepthTex->Release();
        m_downsampledDepthTex = nullptr;
    }
    if (m_downsampledDepthSRV) {
        m_downsampledDepthSRV->Release();
        m_downsampledDepthSRV = nullptr;
    }
    if (m_downsampledDepthUAV) {
        m_downsampledDepthUAV->Release();
        m_downsampledDepthUAV = nullptr;
    }
    if (m_downsampledMVTex) {
        m_downsampledMVTex->Release();
        m_downsampledMVTex = nullptr;
    }
    if (m_downsampledMVSRV) {
        m_downsampledMVSRV->Release();
        m_downsampledMVSRV = nullptr;
    }
    if (m_downsampledMVUAV) {
        m_downsampledMVUAV->Release();
        m_downsampledMVUAV = nullptr;
    }

    m_bestDepthScore = -1.0f;
    m_bestMVScore = -1.0f;
    m_depthWidth = 0;
    m_depthHeight = 0;
    m_depthFormat = DXGI_FORMAT_UNKNOWN;
    m_mvWidth = 0;
    m_mvHeight = 0;
    m_mvFormat = DXGI_FORMAT_UNKNOWN;
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
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
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

void DXUpscalerManager::RecordDepthClearValue(bool isInverted) {
    if (isInverted == m_detectedInvertedDepth) {
        if (m_invertedDepthConfidence < 10) {
            m_invertedDepthConfidence++;
            if (m_invertedDepthConfidence == 10) {
                Logger::info("DXUpscalerManager: Inverted Depth state stabilized to: " + std::string(isInverted ? "true" : "false"));
            }
        }
    } else {
        m_detectedInvertedDepth = isInverted;
        m_invertedDepthConfidence = 1;
    }
}

bool DXUpscalerManager::SetPluginFieldBool(const std::string& name, bool value) {
    if (!m_pInterface || !m_pInterface->GetFields)
        return false;
    GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
    int count = m_pInterface->GetFields(&fields);
    for (int i = 0; i < count; i++) {
        if (fields[i].Name && std::string(fields[i].Name) == name) {
            bool* pBool = (bool*)fields[i].Data;
            if (*pBool != value) {
                *pBool = value;
                if (m_pInterface->OnFieldsChanged) {
                    m_pInterface->OnFieldsChanged();
                }
                return true;
            }
            break;
        }
    }
    return false;
}

bool DXUpscalerManager::SetPluginFieldFloat(const std::string& name, float value) {
    if (!m_pInterface || !m_pInterface->GetFields)
        return false;
    GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
    int count = m_pInterface->GetFields(&fields);
    for (int i = 0; i < count; i++) {
        if (fields[i].Name && std::string(fields[i].Name) == name) {
            float* pFloat = (float*)fields[i].Data;
            if (std::abs(*pFloat - value) > 0.001f) {
                *pFloat = value;
                if (m_pInterface->OnFieldsChanged) {
                    m_pInterface->OnFieldsChanged();
                }
                return true;
            }
            break;
        }
    }
    return false;
}

bool DXUpscalerManager::IsValidProjectionMatrix(const float* m, float& outFovY, float& outNear, float& outFar, bool& outInverted) {
    auto is_zero = [](float f) { return std::abs(f) < 1e-4f; };

    bool rowMajor = false;
    bool colMajor = false;

    if (std::abs(m[11] - 1.0f) < 1e-3f || std::abs(m[11] + 1.0f) < 1e-3f) {
        rowMajor = true;
    } else if (std::abs(m[14] - 1.0f) < 1e-3f || std::abs(m[14] + 1.0f) < 1e-3f) {
        colMajor = true;
    }

    if (!rowMajor && !colMajor)
        return false;

    float A, B, y, x;

    if (rowMajor) {
        // For row-major projection:
        // m[2] (_13) and m[6] (_23) are jitter / off-center offsets and can be non-zero (within limit, e.g. < 0.1f).
        // The other elements m[1], m[3], m[4], m[7], m[8], m[9], m[12], m[13], m[15] must be zero.
        if (!is_zero(m[1]) || !is_zero(m[3]) || !is_zero(m[4]) || !is_zero(m[7]) || !is_zero(m[8]) || !is_zero(m[9]) || !is_zero(m[12]) ||
            !is_zero(m[13]) || !is_zero(m[15])) {
            return false;
        }
        if (std::abs(m[2]) > 0.1f || std::abs(m[6]) > 0.1f)
            return false;

        x = m[0];
        y = m[5];
        A = m[10];
        B = m[14];
    } else {
        // For column-major projection:
        // m[2] (_31) and m[6] (_32) are jitter / off-center offsets and can be non-zero (within limit, e.g. < 0.1f).
        // The other elements m[1], m[4], m[3], m[7], m[8], m[9], m[12], m[13], m[15] must be zero.
        if (!is_zero(m[1]) || !is_zero(m[4]) || !is_zero(m[3]) || !is_zero(m[7]) || !is_zero(m[8]) || !is_zero(m[9]) || !is_zero(m[12]) ||
            !is_zero(m[13]) || !is_zero(m[15])) {
            return false;
        }
        if (std::abs(m[2]) > 0.1f || std::abs(m[6]) > 0.1f)
            return false;

        x = m[0];
        y = m[5];
        A = m[10];
        B = m[11];
    }

    if (x <= 0.05f || x > 20.0f || y <= 0.05f || y > 20.0f)
        return false;

    outFovY = 2.0f * std::atanf(1.0f / y);
    // Restrict vertical FOV to normal human perspective range [5 deg, 115 deg] -> [0.08 rad, 2.01 rad].
    // This avoids picking up shadow/reflection maps, while still allowing the main menu camera and sniper zoom.
    if (outFovY < 0.08f || outFovY > 2.01f)
        return false;

    // Filter out square (shadow/reflection) matrices where aspect ratio is exactly 1.0
    float aspect = y / x;
    if (std::abs(aspect - 1.0f) < 0.01f)
        return false;

    float near_std = -B / A;
    float far_std = A * near_std / (A - 1.0f);

    float near_inv = B / (A - 1.0f);
    float far_inv = B / A;

    if (near_std > 0.0f && far_std > near_std && std::abs(A - 1.0f) > 1e-4f) {
        outNear = near_std;
        outFar = far_std;
        outInverted = false;
        return true;
    }

    if (near_inv > 0.0f && far_inv > near_inv && std::abs(A) > 1e-4f) {
        outNear = near_inv;
        outFar = far_inv;
        outInverted = true;
        return true;
    }

    if (std::abs(A) < 1e-4f && B > 0.0f) {
        outNear = B;
        outFar = 100000.0f;
        outInverted = true;
        return true;
    }
    if (std::abs(A - 1.0f) < 1e-4f && B < 0.0f) {
        outNear = -B;
        outFar = 100000.0f;
        outInverted = false;
        return true;
    }

    return false;
}

struct CandidateMatrix {
    UINT slot;
    UINT offset;
    bool isRow;
    float m[16];
};

void DXUpscalerManager::ScanProjectionMatrix(ID3D11DeviceContext* context) {
    if (!m_pd3dDevice)
        return;

    // Autodetect viewSpaceToMetersFactor from executable name
    static bool metersFactorSet = false;
    if (!metersFactorSet) {
        float factor = 1.0f;
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string exeName = std::filesystem::path(exePath).filename().string();
        std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);

        if (exeName.find("skyrim") != std::string::npos || exeName.find("tesv") != std::string::npos ||
            exeName.find("fallout4") != std::string::npos) {
            factor = 1.0f / 70.0f;
            Logger::info("DXUpscalerManager: Detected Skyrim/Fallout engine. Setting Meters Factor to " + std::to_string(factor));
        } else {
            Logger::info("DXUpscalerManager: Defaulting Meters Factor to " + std::to_string(factor));
        }

        m_viewSpaceToMetersFactor = factor;
        metersFactorSet = true;
    }

    std::vector<CandidateMatrix> candidates;

    for (UINT slot = 0; slot < 14; ++slot) {
        ID3D11Buffer* cb = nullptr;
        context->VSGetConstantBuffers(slot, 1, &cb);
        if (!cb)
            continue;

        D3D11_BUFFER_DESC desc;
        cb->GetDesc(&desc);

        if (desc.ByteWidth >= 64 && desc.ByteWidth <= 4096) {
            ID3D11Buffer* staging = nullptr;
            D3D11_BUFFER_DESC stagingDesc = {};
            stagingDesc.ByteWidth = desc.ByteWidth;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDesc.MiscFlags = 0;
            stagingDesc.StructureByteStride = 0;

            if (SUCCEEDED(m_pd3dDevice->CreateBuffer(&stagingDesc, nullptr, &staging))) {
                context->CopyResource(staging, cb);
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                    float* data = (float*)mapped.pData;
                    UINT numFloats = desc.ByteWidth / 4;

                    for (UINT offset = 0; offset + 16 <= numFloats; offset += 4) {
                        float fovY = 0.0f, nearP = 0.0f, farP = 0.0f;
                        bool inverted = false;
                        if (IsValidProjectionMatrix(&data[offset], fovY, nearP, farP, inverted)) {
                            float fovDegrees = fovY * (180.0f / 3.14159265f);

                            std::string matrixStr = "";
                            const float* m = &data[offset];
                            for (int i = 0; i < 16; ++i) {
                                matrixStr += std::to_string(m[i]) + (i == 15 ? "" : ", ");
                            }

                            Logger::info("DXUpscalerManager: Detected Projection Matrix in Slot " + std::to_string(slot) + ": Near=" +
                                         std::to_string(nearP) + ", Far=" + std::to_string(farP) + ", FOV=" + std::to_string(fovDegrees) +
                                         ", Inverted=" + std::to_string(inverted) + ", Raw: [" + matrixStr + "]");

                            m_cameraNear = nearP;
                            m_cameraFar = farP;
                            m_cameraFov = fovDegrees;
                            RecordDepthClearValue(inverted);

                            context->Unmap(staging, 0);
                            staging->Release();
                            cb->Release();
                            return;
                        } else {
                            const float* m = &data[offset];
                            bool isRow = false, isCol = false;
                            if (std::abs(m[11] - 1.0f) < 1e-3f || std::abs(m[11] + 1.0f) < 1e-3f)
                                isRow = true;
                            else if (std::abs(m[14] - 1.0f) < 1e-3f || std::abs(m[14] + 1.0f) < 1e-3f)
                                isCol = true;

                            if ((isRow || isCol) && candidates.size() < 5) {
                                CandidateMatrix cand;
                                cand.slot = slot;
                                cand.offset = offset;
                                cand.isRow = isRow;
                                for (int i = 0; i < 16; ++i) {
                                    cand.m[i] = m[i];
                                }
                                candidates.push_back(cand);
                            }
                        }
                    }
                    context->Unmap(staging, 0);
                }
                staging->Release();
            }
        }
        cb->Release();
    }

    if (!candidates.empty()) {
        Logger::info(
            "DXUpscalerManager: Failed to detect projection matrix. Found " + std::to_string(candidates.size()) + " close candidates:");
        for (const auto& cand : candidates) {
            std::string matrixStr = "";
            for (int i = 0; i < 16; ++i) {
                matrixStr += std::to_string(cand.m[i]) + (i == 15 ? "" : ", ");
            }
            Logger::info("  Candidate (Slot=" + std::to_string(cand.slot) + ", Offset=" + std::to_string(cand.offset) +
                         ", RowMajor=" + std::to_string(cand.isRow) + "): [" + matrixStr + "]");
        }
    }
}

} // namespace GamePlug
