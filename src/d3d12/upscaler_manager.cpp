#include "upscaler_manager.h"
#include <filesystem>
#include <algorithm>
#include "logger.h"
#include "imgui.h"
#include "config.h"
#include <MinHook.h>
#include <sstream>
#include "hooks_common.h"
#include <iomanip>
#include <unordered_map>

namespace GamePlug {
extern ID3D12Resource* g_lastEngineRenderTarget;
extern std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> g_ResourceStates;
extern bool g_IsResizing;
typedef GamePlugUpscalerInterface* (*GamePlug_GetUpscalerInterfaceFn)();

void DXUpscalerLogBridge(GamePlugUpscalerInterface::LogLevel level, const char* message, void* context) {
    std::string formattedMsg = "[DXUpscaler] " + std::string(message);
    switch (level) {
        case GamePlugUpscalerInterface::LOG_INFO:  Logger::info(formattedMsg); break;
        case GamePlugUpscalerInterface::LOG_WARN:  Logger::warn(formattedMsg); break;
        case GamePlugUpscalerInterface::LOG_ERROR: Logger::error(formattedMsg); break;
        case GamePlugUpscalerInterface::LOG_DEBUG: Logger::debug(formattedMsg); break;
    }
}

DXUpscalerManager::~DXUpscalerManager() {
    UnloadUpscaler();
}

void DXUpscalerManager::LoadPlugin() {
    if (m_handle) return;

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
    if (!m_pInterface) return;

    m_pd3dDevice = device;
    m_pd3dDeviceContext = context;

    if (m_pInterface->OnInit) {
        m_pInterface->OnInit(
            ImGui::GetCurrentContext(),
            DXUpscalerLogBridge,
            nullptr,
            0, // instance (not used in DX)
            0, // physicalDevice (not used in DX)
            (uintptr_t)device,
            (uintptr_t)context,
            11, // queueFamilyIndex reused for API version (11 = DX11)
            nullptr // memoryProperties
        );
    }
}

void DXUpscalerManager::InitDX12(ID3D12Device* device, ID3D12CommandQueue* queue) {
    LoadPlugin();
    if (!m_pInterface) return;

    m_pd3d12Device = device;
    if (queue) {
        m_pd3d12Queue = queue;
    }

    if (m_pInterface->OnInit) {
        m_pInterface->OnInit(
            ImGui::GetCurrentContext(),
            DXUpscalerLogBridge,
            nullptr,
            0, // instance
            0, // physicalDevice
            (uintptr_t)device,
            (uintptr_t)queue,
            12, // queueFamilyIndex reused for API version (12 = DX12)
            nullptr // memoryProperties
        );
    }

    // Initialize FSR-Dedicated Command Infrastructure
    if (m_pd3d12Device) {
        m_pd3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_fsrAllocator));
        m_pd3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_fsrAllocator, nullptr, IID_PPV_ARGS(&m_fsrCommandList));
        m_fsrCommandList->Close();
        m_pd3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fsrFence));
        m_fsrFenceValue = 1;
        Logger::info("DXUpscalerManager: FSR dedicated command infrastructure initialized.");
    }

    // Initialize RE Engine Native Scaling if requested
    if (Config::Get().GetBool("ReGame", false)) {
        InitREEngineHooks();
    }
}

void DXUpscalerManager::EarlyInit() {
    LoadPlugin();
    if (m_pInterface && m_pInterface->OnInit) {
        // Trigger OnInit with dummy handles to force g_Cfg.Load("upscaler.conf")
        // We use API version 12 as a hint for DX12 games (typical for FSR1 focus)
        m_pInterface->OnInit(nullptr, DXUpscalerLogBridge, nullptr, 0, 0, 0, 0, 12, nullptr);
    }
    
    // Initialize RE Engine Native Scaling if requested
    if (Config::Get().GetBool("ReGame", false)) {
        InitREEngineHooks();
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
    CleanupDX12Resources();
    m_pd3d12Device = nullptr;
    m_pd3d12Queue = nullptr;
    m_pd3dDevice = nullptr;
    m_pd3dDeviceContext = nullptr;
}

void DXUpscalerManager::CleanupDX12Resources() {
    DestroyFakeBackBuffer();
    if (m_fsrAllocator) { m_fsrAllocator->Release(); m_fsrAllocator = nullptr; }
    if (m_fsrCommandList) { m_fsrCommandList->Release(); m_fsrCommandList = nullptr; }
    if (m_fsrFence) { m_fsrFence->Release(); m_fsrFence = nullptr; }
    if (m_rtvHeap) { m_rtvHeap->Release(); m_rtvHeap = nullptr; }
    m_rtvCache.clear();
    m_rtvDescriptorSize = 0;
    m_nextRtvIndex = 0;
    m_finalOutput = nullptr;
}

void DXUpscalerManager::CreateFakeBackBuffer(IDXGISwapChain* swapChain) {
    if (!m_pd3d12Device) {
        Logger::error("DXUpscalerManager: CreateFakeBackBuffer failed - Device is NULL!");
        return;
    }
    
    DXGI_SWAP_CHAIN_DESC desc;
    if (FAILED(swapChain->GetDesc(&desc))) {
        Logger::error("DXUpscalerManager: CreateFakeBackBuffer failed - SwapChain GetDesc failed!");
        return;
    }

    UpdateDimensions(desc.BufferDesc.Width, desc.BufferDesc.Height);
    
    if (m_fakeBackBuffer) {
        D3D12_RESOURCE_DESC resDesc = m_fakeBackBuffer->GetDesc();
        if (resDesc.Width == m_renderWidth && resDesc.Height == m_renderHeight && resDesc.Format == desc.BufferDesc.Format) {
            Logger::info("DXUpscalerManager: Fake BackBuffer already exists with correct dimensions.");
            return;
        }
        DestroyFakeBackBuffer();
    }

    Logger::info("DXUpscalerManager: Creating Fake BackBuffer at " + std::to_string(m_renderWidth) + "x" + std::to_string(m_renderHeight) +
                 " (Display: " + std::to_string(m_width) + "x" + std::to_string(m_height) + ")");

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Alignment = 0;
    resDesc.Width = m_renderWidth;
    resDesc.Height = m_renderHeight;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = desc.BufferDesc.Format;
    resDesc.SampleDesc.Count = 1;
    resDesc.SampleDesc.Quality = 0;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = desc.BufferDesc.Format;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    HRESULT hr = m_pd3d12Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_PRESENT,
        &clearValue,
        IID_PPV_ARGS(&m_fakeBackBuffer)
    );

    if (FAILED(hr)) {
        Logger::error("DXUpscalerManager: Failed to create Fake BackBuffer (HR=" + std::to_string(hr) + ")");
        m_fakeBackBuffer = nullptr;
        return;
    }

    // Attach custom GUID to identify the fake back buffer in hooks
    static const GUID GUID_FakeBackBuffer = { 0xf192e666, 0xc0de, 0x4d12, { 0x87, 0x65, 0xfc, 0xeb, 0x5a, 0x4b, 0x2b, 0xa1 } };
    UINT tag = 1;
    m_fakeBackBuffer->SetPrivateData(GUID_FakeBackBuffer, sizeof(tag), &tag);

    Logger::info("DXUpscalerManager: Fake BackBuffer successfully created.");
}

void DXUpscalerManager::DestroyFakeBackBuffer() {
    if (m_fakeBackBuffer) {
        m_fakeBackBuffer->Release();
        m_fakeBackBuffer = nullptr;
        Logger::info("DXUpscalerManager: Fake BackBuffer destroyed.");
    }
}

bool DXUpscalerManager::IsUpscalingEnabled() const {
    if (!m_pInterface || !m_pInterface->GetFields) return false;
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

void DXUpscalerManager::RenderUI(float fps, uint32_t width, uint32_t height) {
    if (!m_pInterface) {
        if (ImGui::CollapsingHeader("DX Upscaler", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("upscaler_plugin.dll not loaded.");
            if (ImGui::Button("Retry Loading")) {
                LoadPlugin();
                // Potentially re-init if device is known
            }
        }
        return;
    }

    if (ImGui::CollapsingHeader(m_pInterface->GetName() ? m_pInterface->GetName() : "DX Upscaler", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_pInterface->GetFields) {
            GamePlugUpscalerInterface::FieldDescriptor* fields = nullptr;
            int count = m_pInterface->GetFields(&fields);
            
            int upscalerType = 0;
            for(int i=0; i<count; i++) {
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
                        case 0: changed = ImGui::Checkbox(f.Name, (bool*)f.Data); break;
                        case 1: changed = ImGui::DragInt(f.Name, (int*)f.Data); break;
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
                        case 3: changed = ImGui::InputText(f.Name, (char*)f.Data, (size_t)f.DataSize); break;
                        case 4: 
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
                    if (changed && m_pInterface->OnFieldsChanged) m_pInterface->OnFieldsChanged();
                    ImGui::PopID();
                }
                ImGui::PopItemWidth();
            }
        }
        
        if (m_pInterface->OnImGuiRender) m_pInterface->OnImGuiRender();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "[ DX SYSTEM ACTIVE ]");
        ImGui::SameLine();
        float availW = ImGui::GetContentRegionAvail().x;
        float textW = ImGui::CalcTextSize("120 FPS").x; 
        if (availW > textW) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availW - textW);
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "%.0f FPS", fps);
        
        ImGui::TextDisabled("PIPELINE RESOLUTION:");
        ImGui::Text("  Target: %d x %d", width, height);
        ImGui::Text("  Render: %d x %d", m_renderWidth, m_renderHeight);
        float scale = (float)width / (float)m_renderWidth;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%.2fx)", scale);
        ImGui::Spacing();

    }

    if (IsShowDebugImageEnabled()) {
        bool show = true;
        ImGui::Begin("Upscaler Debug View", &show);
        if (!show) {
            SetShowDebugImageEnabled(false);
        }

        const char* debugItems[] = {"Source (Fake Back Buffer)", "Depth Buffer", "Motion Vectors"};
        ImGui::Combo("Preview Target", &m_debugPreviewIndex, debugItems, IM_ARRAYSIZE(debugItems));

        uint32_t dw = 0;
        uint32_t dh = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE debugSRV = {0};
        std::string targetName = "";

        {
            std::lock_guard<std::mutex> lock(m_trackerMtx);
            if (m_debugPreviewIndex == 0) {
                dw = m_renderWidth;
                dh = m_renderHeight;
                if (m_fakeBackBuffer) {
                    debugSRV = GetImGuiSRVForResource(m_fakeBackBuffer);
                }
                targetName = "Fake Back Buffer";
            } else if (m_debugPreviewIndex == 1) {
                dw = m_depthWidth;
                dh = m_depthHeight;
                if (m_depthTexture) {
                    debugSRV = GetImGuiSRVForResource(m_depthTexture);
                }
                targetName = "Depth Buffer";
            } else if (m_debugPreviewIndex == 2) {
                dw = m_mvWidth;
                dh = m_mvHeight;
                if (m_mvTexture) {
                    debugSRV = GetImGuiSRVForResource(m_mvTexture);
                }
                targetName = "Motion Vectors";
            }
        }

        if (debugSRV.ptr != 0 && dw > 0 && dh > 0) {
            ImGui::Text("%s Resource: %u x %u", targetName.c_str(), dw, dh);

            float windowWidth = ImGui::GetContentRegionAvail().x;
            float windowHeight = ImGui::GetContentRegionAvail().y - 30.0f;
            float aspect = (float)dh / (float)dw;

            float imgWidth = windowWidth;
            float imgHeight = windowWidth * aspect;

            if (imgHeight > windowHeight) {
                imgHeight = windowHeight;
                imgWidth = windowHeight / aspect;
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (windowWidth - imgWidth) * 0.5f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (windowHeight - imgHeight) * 0.5f);

            ImGui::Image((ImTextureID)debugSRV.ptr, ImVec2(imgWidth, imgHeight));
        } else {
            ImGui::Text("%s is not available (or cannot be bound as SRV).", targetName.c_str());
        }

        ImGui::End();
    }
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
                        isBatmanCached = (exeName.find("batman") != std::string::npos);
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
    if (m_width == 0 || m_renderWidth == 0) return 1.0f;
    return (float)m_renderWidth / (float)m_width;
}

ID3D12Resource* DXUpscalerManager::GetFinalOutput()
{
    return m_finalOutput; // your FSR output texture
}

D3D12_CPU_DESCRIPTOR_HANDLE DXUpscalerManager::GetOrCreateRTV(ID3D12Resource* res)
{
    if (m_rtvCache.count(res))
        return m_rtvCache[res];

    if (!m_rtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 16; // Accommodate multiple backbuffers + extra
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        m_pd3d12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvHeap));
        m_rtvDescriptorSize = m_pd3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_nextRtvIndex = 0;
    }

    if (m_nextRtvIndex >= 16) {
        Logger::warn("DXUpscalerManager: RTV Heap exhausted, resetting indices (potential leak/churn)");
        m_nextRtvIndex = 0;
        m_rtvCache.clear();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (SIZE_T)m_nextRtvIndex * m_rtvDescriptorSize;
    m_nextRtvIndex++;

    m_pd3d12Device->CreateRenderTargetView(res, nullptr, handle);
    m_rtvCache[res] = handle;

    Logger::info("DXUpscalerManager: Created new RTV for resource " + std::to_string((uintptr_t)res) + " at slot " + std::to_string(m_nextRtvIndex-1));

    return handle;
}

void DXUpscalerManager::RenderFrame(ID3D12GraphicsCommandList* cmd, ID3D12Resource* source, ID3D12Resource* target, uint32_t width, uint32_t height) {
    static uint64_t s_renderFrameCalls = 0;
    bool shouldLog = (s_renderFrameCalls < 100) || (s_renderFrameCalls % 600 == 0);
    s_renderFrameCalls++;

    if (shouldLog) {
        Logger::info("DXUpscalerManager: RenderFrame Entry [Call=" + std::to_string(s_renderFrameCalls) + 
                     "] InterfaceValid=" + std::to_string(m_pInterface != nullptr) + 
                     " AlreadyUpscaled=" + std::to_string(m_frameUpscaled));
    }

    if (!m_pInterface || !m_pInterface->OnRenderFrame || m_frameUpscaled) {
        if (shouldLog) {
            if (!m_pInterface) Logger::warn("DXUpscalerManager: Dropout - Interface NOT Loaded");
            else if (!m_pInterface->OnRenderFrame) Logger::error("DXUpscalerManager: Dropout - OnRenderFrame NULL");
            else if (m_frameUpscaled) Logger::warn("DXUpscalerManager: Dropout - Frame Already Upscaled");
        }
        return;
    }
    if (!source || !target) {
        Logger::warn("DXUpscalerManager: RenderFrame called with NULL resources");
        return;
    }

    Logger::warn("FSR EXECUTED FRAME");
    Logger::info("DXUpscalerManager: Calling OnRenderFrame (DX12)");

    m_frameUpscaled = true;
    m_finalOutput = target; // Cache for OMSetRenderTargets fix
    m_width = width;
    m_height = height;
    UpdateDimensions(width, height);

    bool isHDR = false;
    if (target) {
        D3D12_RESOURCE_DESC dstDesc = target->GetDesc();
        if (dstDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || dstDesc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
            dstDesc.Format == DXGI_FORMAT_R11G11B10_FLOAT || dstDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
            isHDR = true;
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

    // Halton sequence for fake jitter generation
    m_jitterIndex++;
    if (m_jitterIndex > 128)
        m_jitterIndex = 1;

    // Generate Halton sequence jitter
    float haltonX = 0.0f;
    float haltonY = 0.0f;
    int index = m_jitterIndex;
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= 2.0f;
        r += f * (index % 2);
        index /= 2;
    }
    haltonX = r;
    index = m_jitterIndex;
    f = 1.0f;
    r = 0.0f;
    while (index > 0) {
        f /= 3.0f;
        r += f * (index % 3);
        index /= 3;
    }
    haltonY = r;

    m_fakeJitterX = (haltonX - 0.5f) * 2.0f / (float)width;
    m_fakeJitterY = (haltonY - 0.5f) * 2.0f / (float)height;

    float jitterX = m_fakeJitterX;
    float jitterY = m_fakeJitterY;

    uint64_t depthImage = 0;
    uint32_t depthFormatVal = 0;
    uint64_t mvImage = 0;
    uint32_t mvFormatVal = 0;

    {
        std::lock_guard<std::mutex> lock(m_trackerMtx);
        if (m_depthTexture) {
            depthImage = (uint64_t)m_depthTexture;
            depthFormatVal = (uint32_t)m_depthFormat;
        }
        if (m_mvTexture) {
            mvImage = (uint64_t)m_mvTexture;
            mvFormatVal = (uint32_t)m_mvFormat;
        }
    }

    if (shouldLog) {
        Logger::info("DXUpscalerManager::RenderFrame [Buffers] depth=" + std::to_string(depthImage) + 
                     ", mv=" + std::to_string(mvImage));
    }

    m_pInterface->OnRenderFrame(
        (uintptr_t)cmd,
        (uint64_t)source,
        (uint64_t)target,
        0, // targetFormat (not strictly used yet for DX)
        width,
        height,
        m_renderWidth,
        m_renderHeight,
        depthImage, depthFormatVal,
        mvImage, mvFormatVal,
        jitterX, jitterY,
        m_cameraNear, m_cameraFar, m_cameraFov,
        m_viewSpaceToMetersFactor, m_detectedInvertedDepth, m_detectedHDR
    );
}


void DXUpscalerManager::TrackTexture(ID3D12Resource* resource) {
    if (!resource)
        return;

    std::lock_guard<std::mutex> lock(m_trackerMtx);
    D3D12_RESOURCE_DESC desc = resource->GetDesc();

    // Identify Depth Buffer candidate
    if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
        float score = 0.0f;
        if (m_width > 0 && desc.Width == m_width && desc.Height == m_height)
            score += 1000000.0f;
        if (desc.Width != desc.Height)
            score += 500000.0f;
        if (desc.Width >= 640 && desc.Height >= 360)
            score += 100000.0f;

        // Shadow map penalty
        if (desc.Width == desc.Height && m_width != m_height)
            score -= 800000.0f;

        // Minimum size filter
        if (desc.Width < 320 || desc.Height < 200)
            score = -100.0f;

        if (score > m_bestDepthScore && score > 0) {
            if (m_depthTexture) {
                m_depthTexture->Release();
            }
            m_depthTexture = resource;
            m_depthTexture->AddRef();
            m_depthWidth = (uint32_t)desc.Width;
            m_depthHeight = (uint32_t)desc.Height;
            m_depthFormat = desc.Format;
            m_bestDepthScore = score;

            Logger::info("DXUpscalerManager (DX12): Depth buffer candidate updated (Width=" + std::to_string(desc.Width) +
                         ", Height=" + std::to_string(desc.Height) + ", Format=" + std::to_string(desc.Format) +
                         ", Score=" + std::to_string(score) + ")");
        }
    }

    // Identify Motion Vector candidate
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) || !(desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
        float score = 0.0f;

        // Size match to current render resolution or display resolution
        if (m_renderWidth > 0 && desc.Width == m_renderWidth && desc.Height == m_renderHeight) {
            score += 500000.0f;
        } else if (m_width > 0 && desc.Width == m_width && desc.Height == m_height) {
            score += 300000.0f;
        }

        // Format scoring: R16G16_FLOAT / R32G32_FLOAT etc are very common motion vector formats
        if (desc.Format == DXGI_FORMAT_R16G16_FLOAT || desc.Format == DXGI_FORMAT_R32G32_FLOAT) {
            score += 200000.0f;
        } else if (desc.Format == DXGI_FORMAT_R16G16_SNORM || desc.Format == DXGI_FORMAT_R16G16_UNORM) {
            score += 150000.0f;
        } else if (desc.Format == DXGI_FORMAT_R8G8_UNORM || desc.Format == DXGI_FORMAT_R8G8_SNORM) {
            score += 50000.0f;
        }

        if (score > m_bestMVScore && score > 0) {
            if (m_mvTexture) {
                m_mvTexture->Release();
            }
            m_mvTexture = resource;
            m_mvTexture->AddRef();
            m_mvWidth = (uint32_t)desc.Width;
            m_mvHeight = (uint32_t)desc.Height;
            m_mvFormat = desc.Format;
            m_bestMVScore = score;

            Logger::info("DXUpscalerManager (DX12): Motion Vector candidate updated (Width=" + std::to_string(desc.Width) +
                         ", Height=" + std::to_string(desc.Height) + ", Format=" + std::to_string(desc.Format) +
                         ", Score=" + std::to_string(score) + ")");
        }
    }
}

void DXUpscalerManager::ResetFrame()
{
    m_frameUpscaled = false;
    
    std::lock_guard<std::mutex> lock(m_trackerMtx);
    if (m_depthTexture) {
        m_depthTexture->Release();
        m_depthTexture = nullptr;
    }
    if (m_mvTexture) {
        m_mvTexture->Release();
        m_mvTexture = nullptr;
    }
    m_bestDepthScore = -1.0f;
    m_bestMVScore = -1.0f;
}

void DXUpscalerManager::MarkFSRReady() {
    m_fsrReady = true;
}

bool DXUpscalerManager::IsFSRReady() const {
    return m_fsrReady;
}

void DXUpscalerManager::SetHasValidRT(bool ready) {
    m_hasValidRT = ready;
}

bool DXUpscalerManager::HasValidRT() const {
    return m_hasValidRT;
}

void DXUpscalerManager::RunFSRPass() {
    if (!m_pd3d12Queue || !m_fsrCommandList) {
        static int throttle = 0;
        if (throttle++ % 100 == 0) {
            Logger::warn("FSR: RunFSRPass skipped - No Queue or List! (Queue=" + std::to_string((uintptr_t)m_pd3d12Queue) + ")");
        }
        return;
    }

    if (g_IsResizing) {
        return;
    }
    
    Logger::warn("FSR: RunFSRPass Entry [Queue=" + std::to_string((uintptr_t)m_pd3d12Queue) + "]");
    Logger::warn("FSR: RunFSRPass 1");
    
    if (!m_hasValidRT) {
        static uint64_t s_skipCount = 0;
        if (s_skipCount++ % 60 == 0) {
            Logger::warn("FSR SKIP: No valid RT yet (Skipped " + std::to_string(s_skipCount) + " times)");
        }
        return;
    }
    Logger::warn("FSR: RunFSRPass 2");

    if (!m_pd3d12Device || !m_pd3d12Queue || !m_fsrCommandList || !m_fsrAllocator) {
        Logger::error("FSR SKIP: Missing DX12 infrastructure (Device=" + std::to_string((uintptr_t)m_pd3d12Device) + 
                     " Queue=" + std::to_string((uintptr_t)m_pd3d12Queue) + ")");
        return;
    }

    Logger::warn("FSR: RunFSRPass 3");

    if (m_frameUpscaled) {
        Logger::warn("FSR SKIP: Already upscaled");
        return;
    }

    Logger::warn("FSR: RunFSRPass 4");

    ID3D12Resource* src = m_fakeBackBuffer ? m_fakeBackBuffer : g_lastEngineRenderTarget;
    if (!src) {
        Logger::warn("FSR SKIP: No source RT");
        return;
    }

    Logger::warn("FSR: RunFSRPass 5");

    if (!m_currentSwapChain) {
        Logger::warn("FSR SKIP: No swapchain");
        return;
    }

    Logger::warn("FSR: RunFSRPass 6");

    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(m_currentSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3))) return;
    UINT bbIdx = sc3->GetCurrentBackBufferIndex();
    sc3->Release();

    Logger::warn("FSR: RunFSRPass 7");

    ID3D12Resource* dst = nullptr;
    if (FAILED(m_currentSwapChain->GetBuffer(bbIdx, IID_PPV_ARGS(&dst)))) return;

    Logger::warn("FSR: RunFSRPass 8");

    uint32_t width = m_width;
    uint32_t height = m_height;
    if (width == 0 || height == 0) {
        DXGI_SWAP_CHAIN_DESC sd;
        m_currentSwapChain->GetDesc(&sd);
        width = sd.BufferDesc.Width;
        height = sd.BufferDesc.Height;
    }

    Logger::warn("FSR: RunFSRPass 9");

    m_fsrAllocator->Reset();
    m_fsrCommandList->Reset(m_fsrAllocator, nullptr);

    Logger::warn("FSR: RunFSRPass 10");

    // [BEFORE FSR] MANUAL TRANSITIONS
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    int barrierCount = 0;

    // Transition src: Tracked -> NON_PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_STATES srcStateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (g_ResourceStates.count(src)) srcStateBefore = g_ResourceStates[src];

    if (srcStateBefore != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = src;
        barriers[barrierCount].Transition.StateBefore = srcStateBefore;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
    }

    if (barrierCount > 0) {
        Logger::info("DX: RunFSRPass - Applying PRE-FSR barriers (Count=" + std::to_string(barrierCount) + ")");
        m_fsrCommandList->ResourceBarrier(barrierCount, barriers);
    }

    // [FIX] Clear Backbuffer to prevent Double Image
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // Standard black clear
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetOrCreateRTV(dst);

    // Transition dst: Tracked -> RENDER_TARGET
    D3D12_RESOURCE_STATES dstStateBeforeClearing = D3D12_RESOURCE_STATE_PRESENT;
    if (g_ResourceStates.count(dst)) dstStateBeforeClearing = g_ResourceStates[dst];

    D3D12_RESOURCE_BARRIER clearBarrier = {};
    clearBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    clearBarrier.Transition.pResource = dst;
    clearBarrier.Transition.StateBefore = dstStateBeforeClearing;
    clearBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    clearBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    if (dstStateBeforeClearing != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        Logger::info("DX: RunFSRPass - Transitioning dst to RENDER_TARGET for clear (Before=" + std::to_string(dstStateBeforeClearing) + ")");
        m_fsrCommandList->ResourceBarrier(1, &clearBarrier);
    }

    Logger::info("DX: RunFSRPass - Clearing Backbuffer");
    m_fsrCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // Transition dst: RENDER_TARGET -> COPY_DEST (for plugin copy)
    clearBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    clearBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    
    Logger::info("DX: RunFSRPass - Transitioning dst to COPY_DEST for FSR");
    m_fsrCommandList->ResourceBarrier(1, &clearBarrier);

    // RUN FSR OR COPY DIRECTLY
    if (IsUpscalingEnabled()) {
        this->RenderFrame(m_fsrCommandList, src, dst, width, height);
    } else {
        // Transition src to COPY_SOURCE
        D3D12_RESOURCE_BARRIER copySrcBarrier = {};
        copySrcBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        copySrcBarrier.Transition.pResource = src;
        copySrcBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        copySrcBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        copySrcBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        m_fsrCommandList->ResourceBarrier(1, &copySrcBarrier);

        // Copy
        m_fsrCommandList->CopyResource(dst, src);

        // Transition src back to D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
        copySrcBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        copySrcBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        m_fsrCommandList->ResourceBarrier(1, &copySrcBarrier);
    }

    Logger::warn("FSR: RunFSRPass 12");

    // [AFTER FSR] RESTORE STATES
    D3D12_RESOURCE_BARRIER postBarriers[2] = {};
    int postCount = 0;

    // Restore src to engine's original state
    if (srcStateBefore != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        postBarriers[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postBarriers[postCount].Transition.pResource = src;
        postBarriers[postCount].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        postBarriers[postCount].Transition.StateAfter = srcStateBefore;
        postBarriers[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        postCount++;
    }

    // Restore dst to PRESENT for the SwapChain
    postBarriers[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    postBarriers[postCount].Transition.pResource = dst;
    postBarriers[postCount].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    postBarriers[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    postBarriers[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    postCount++;

    if (postCount > 0) {
        Logger::info("DX: RunFSRPass - Applying POST-FSR barriers (Count=" + std::to_string(postCount) + ")");
        for (int i = 0; i < postCount; i++) {
            Logger::info("  - Resource: " + std::to_string((uintptr_t)postBarriers[i].Transition.pResource) + 
                         " State: " + std::to_string(postBarriers[i].Transition.StateBefore) + " -> " + 
                         std::to_string(postBarriers[i].Transition.StateAfter));
        }
        m_fsrCommandList->ResourceBarrier(postCount, postBarriers);
    }

    m_fsrCommandList->Close();

    // EXECUTE & SYNC
    ID3D12CommandList* lists[] = { m_fsrCommandList };
    m_pd3d12Queue->ExecuteCommandLists(1, lists);

    UINT64 fenceValue = m_fsrFenceValue++;
    m_pd3d12Queue->Signal(m_fsrFence, fenceValue);
    if (m_fsrFence->GetCompletedValue() < fenceValue) {
        m_fsrFence->SetEventOnCompletion(fenceValue, nullptr);
        // Wait on GPU is handled by Signal/Wait pattern or we can just hope for now
        // A better sync might be needed if Present is called too fast
    }

    dst->Release();
    g_ResourceStates[src] = srcStateBefore;
    g_ResourceStates[dst] = D3D12_RESOURCE_STATE_PRESENT;
    m_fsrReady = false;
    m_hasValidRT = false;
}

void DXUpscalerManager::PerformPropertyInjection() {
    // Stub
}

void DXUpscalerManager::InitREEngineHooks() {
    // Stub
}

void DXUpscalerManager::UpdateREEngineHooks() {
    this->PerformPropertyInjection();
}

DXUpscalerManager::via_Size DXUpscalerManager::Hooked_get_Size_Native(void* scene_view) {
    if (g_IsResizing) {
        return { (float)DXUpscalerManager::Get().m_width, (float)DXUpscalerManager::Get().m_height };
    }
    
    uint32_t renderW, renderH;
    DXUpscalerManager::Get().GetTargetResolution(DXUpscalerManager::Get().m_width, DXUpscalerManager::Get().m_height, renderW, renderH);
    return { (float)renderW, (float)renderH };
}

} // namespace GamePlug
