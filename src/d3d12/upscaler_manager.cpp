#include "upscaler_manager.h"
#include <filesystem>
#include <algorithm>
#include "logger.h"
#include "imgui.h"
#include "config.h"
#include <MinHook.h>
#include <sstream>

// REFramework SDK Headers
#include "sdk/RETypeDB.hpp"
#include "sdk/REManagedObject.hpp"
#include "sdk/RETypes.hpp"
#include "sdk/SceneManager.hpp"
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
    std::filesystem::path upscalerPath = gamePath / "dxupscaler.dll";

    Logger::info("DXUpscalerManager: Loading " + upscalerPath.string());

    if (!std::filesystem::exists(upscalerPath)) {
        Logger::warn("DXUpscalerManager: dxupscaler.dll NOT found at " + upscalerPath.string());
        return;
    }

    m_handle = LoadLibraryA(upscalerPath.string().c_str());
    if (!m_handle) {
        Logger::error("DXUpscalerManager: Failed to load dxupscaler.dll");
        return;
    }

    auto getInterface = (GamePlug_GetUpscalerInterfaceFn)GetProcAddress(m_handle, GamePlug_GET_UPSCALER_INTERFACE_NAME);
    if (!getInterface) {
        Logger::error("DXUpscalerManager: dxupscaler.dll does not export " + std::string(GamePlug_GET_UPSCALER_INTERFACE_NAME));
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
    m_pd3d12Queue = queue;

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
    if (m_fsrAllocator) { m_fsrAllocator->Release(); m_fsrAllocator = nullptr; }
    if (m_fsrCommandList) { m_fsrCommandList->Release(); m_fsrCommandList = nullptr; }
    if (m_fsrFence) { m_fsrFence->Release(); m_fsrFence = nullptr; }
    if (m_rtvHeap) { m_rtvHeap->Release(); m_rtvHeap = nullptr; }
    m_rtvCache.clear();
    m_rtvDescriptorSize = 0;
    m_nextRtvIndex = 0;
    m_finalOutput = nullptr;
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

void DXUpscalerManager::RenderUI(float fps, uint32_t width, uint32_t height) {
    if (!m_pInterface) {
        if (ImGui::CollapsingHeader("DX Upscaler", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("dxupscaler.dll not loaded.");
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
                        case 2: changed = ImGui::DragFloat(f.Name, (float*)f.Data); break;
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

        if (ImGui::TreeNode("RE Engine Specific")) {
            bool reGame = Config::Get().GetBool("ReGame", false);
            if (ImGui::Checkbox("Enable Native Scale Spoofing (ReGame)", &reGame)) {
                Config::Get().SetBool("ReGame", reGame);
                if (reGame && !m_reHooksInitialized) {
                    InitREEngineHooks();
                }
            }
            bool reHooksActive = m_reHooksInitialized;
            ImGui::Text("Status: %s", reHooksActive ? "Active" : "Ready");
            if (reHooksActive) {
                ImGui::BulletText("Hijacking via.SceneView::get_Size");
            }
            ImGui::TreePop();
        }
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
            if (fields[i].Name && std::string(fields[i].Name) == "Native Rendering") {
                if (*(bool*)fields[i].Data) {
                    return; // Return native resolution
                }
                break;
            }
        }

        // Apply quality scaling if not native
        for (int i = 0; i < count; i++) {
            if (fields[i].Name && std::string(fields[i].Name) == "Upscale Quality") {
                int quality = *(int*)fields[i].Data;
                float ratio = 1.0f;
                float ratios[] = { 1.2f, 1.3f, 1.5f, 1.7f, 2.0f, 3.0f };
                if (quality >= 0 && quality < 6) ratio = ratios[quality];
                else ratio = 1.3f;

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

    m_pInterface->OnRenderFrame(
        (uintptr_t)cmd,
        (uint64_t)source,
        (uint64_t)target,
        0, // targetFormat (not strictly used yet for DX)
        width,
        height,
        m_renderWidth,
        m_renderHeight,
        0, 0, // depth
        0, 0, // mv
        0.0f, 0.0f // jitter
    );
}

void DXUpscalerManager::RenderFrameDX11(ID3D11DeviceContext* context, ID3D11ShaderResourceView* sourceSRV, ID3D11RenderTargetView* targetRTV, uint32_t width, uint32_t height) {
    if (!m_pInterface || !m_pInterface->OnRenderFrame || m_frameUpscaled) return;
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

    m_pInterface->OnRenderFrame(
        (uintptr_t)context,
        (uint64_t)sourceSRV,
        (uint64_t)targetRTV,
        0,
        width,
        height,
        m_renderWidth,
        m_renderHeight,
        0, 0,
        0, 0,
        0.0f, 0.0f
    );
}

void DXUpscalerManager::ResetFrame()
{
    m_frameUpscaled = false;
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

// void DXUpscalerManager::RunFSRPass() {
//     if (!m_fsrReady && !m_frameUpscaled) return;
//     if (!m_hasValidRT) {
//         Logger::warn("FSR SKIP: No valid RT yet");
//         return;
//     }

//     Logger::warn("RunFSRPass CALLED");

//     if (!m_pd3d12Device || !m_pd3d12Queue || !m_fsrCommandList || !m_fsrAllocator) {
//         Logger::warn("FSR SKIP: Missing DX12 setup");
//         return;
//     }

//     if (m_frameUpscaled) {
//         Logger::warn("FSR SKIP: Already upscaled");
//         return;
//     }

//     ID3D12Resource* src = g_lastEngineRenderTarget;
//     if (!src) {
//         Logger::warn("FSR SKIP: No source RT");
//         return;
//     }

//     if (!m_currentSwapChain) {
//         Logger::warn("FSR SKIP: No swapchain");
//         return;
//     }

//     IDXGISwapChain3* sc3 = nullptr;
//     if (FAILED(m_currentSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3))) return;
//     UINT bbIdx = sc3->GetCurrentBackBufferIndex();
//     sc3->Release();

//     ID3D12Resource* dst = nullptr;
//     if (FAILED(m_currentSwapChain->GetBuffer(bbIdx, IID_PPV_ARGS(&dst)))) return;

//     uint32_t width = m_width;
//     uint32_t height = m_height;
//     if (width == 0 || height == 0) {
//         DXGI_SWAP_CHAIN_DESC sd;
//         m_currentSwapChain->GetDesc(&sd);
//         width = sd.BufferDesc.Width;
//         height = sd.BufferDesc.Height;
//     }

//     m_fsrAllocator->Reset();
//     m_fsrCommandList->Reset(m_fsrAllocator, nullptr);

//     // [BEFORE FSR] MANUAL TRANSITIONS
//     D3D12_RESOURCE_BARRIER barriers[2] = {};
//     int barrierCount = 0;

//     // Transition src: RENDER_TARGET -> NON_PIXEL_SHADER_RESOURCE
//     // We assume it's in RENDER_TARGET or whatever state it was last in.
//     D3D12_RESOURCE_STATES srcStateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
//     if (g_ResourceStates.count(src)) srcStateBefore = g_ResourceStates[src];

//     if (srcStateBefore != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
//         barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
//         barriers[barrierCount].Transition.pResource = src;
//         barriers[barrierCount].Transition.StateBefore = srcStateBefore;
//         barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
//         barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
//         barrierCount++;
//     }

//     // Transition dst: PRESENT -> UNORDERED_ACCESS
//     barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
//     barriers[barrierCount].Transition.pResource = dst;
//     barriers[barrierCount].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
//     barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
//     barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
//     barrierCount++;

//     if (barrierCount > 0) {
//         Logger::info("DX: RunFSRPass - Applying PRE-FSR barriers (Count=" + std::to_string(barrierCount) + ")");
//         for (int i = 0; i < barrierCount; i++) {
//             Logger::info("  - Resource: " + std::to_string((uintptr_t)barriers[i].Transition.pResource) + 
//                          " State: " + std::to_string(barriers[i].Transition.StateBefore) + " -> " + 
//                          std::to_string(barriers[i].Transition.StateAfter));
//         }
//         m_fsrCommandList->ResourceBarrier(barrierCount, barriers);
//     }
//     Logger::warn("FSR: RunFSRPass 11");
//     // RUN FSR
//     this->RenderFrame(m_fsrCommandList, src, dst, width, height);

//     Logger::warn("FSR: RunFSRPass 12");

//     // [AFTER FSR] RESTORE STATES
//     D3D12_RESOURCE_BARRIER postBarriers[2] = {};
//     int postCount = 0;

//     // Restore src to engine's original state
//     if (srcStateBefore != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
//         postBarriers[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
//         postBarriers[postCount].Transition.pResource = src;
//         postBarriers[postCount].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
//         postBarriers[postCount].Transition.StateAfter = srcStateBefore;
//         postBarriers[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
//         postCount++;
//     }

//     // Restore dst to PRESENT for the SwapChain
//     postBarriers[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
//     postBarriers[postCount].Transition.pResource = dst;
//     postBarriers[postCount].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
//     postBarriers[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
//     postBarriers[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
//     postCount++;

//     if (postCount > 0) {
//         Logger::info("DX: RunFSRPass - Applying POST-FSR barriers (Count=" + std::to_string(postCount) + ")");
//         for (int i = 0; i < postCount; i++) {
//             Logger::info("  - Resource: " + std::to_string((uintptr_t)postBarriers[i].Transition.pResource) + 
//                          " State: " + std::to_string(postBarriers[i].Transition.StateBefore) + " -> " + 
//                          std::to_string(postBarriers[i].Transition.StateAfter));
//         }
//         m_fsrCommandList->ResourceBarrier(postCount, postBarriers);
//     }

//     m_fsrCommandList->Close();

//     // EXECUTE & SYNC
//     ID3D12CommandList* lists[] = { m_fsrCommandList };
//     m_pd3d12Queue->ExecuteCommandLists(1, lists);

//     UINT64 fenceValue = m_fsrFenceValue++;
//     m_pd3d12Queue->Signal(m_fsrFence, fenceValue);
//     if (m_fsrFence->GetCompletedValue() < fenceValue) {
//         m_fsrFence->SetEventOnCompletion(fenceValue, nullptr);
//         // Wait on GPU is handled by Signal/Wait pattern or we can just hope for now
//         // A better sync might be needed if Present is called too fast
//     }

//     dst->Release();
//     m_fsrReady = false;
//     m_hasValidRT = false;
// }
void DXUpscalerManager::RunFSRPass() {
    if (!m_pd3d12Queue || !m_fsrCommandList) {
        static int throttle = 0;
        if (throttle++ % 100 == 0) {
            Logger::error("FSR: RunFSRPass skipped - No Queue or List! (Queue=" + std::to_string((uintptr_t)m_pd3d12Queue) + ")");
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

    ID3D12Resource* src = g_lastEngineRenderTarget;
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

    Logger::warn("FSR: RunFSRPass 11");

    // RUN FSR
    this->RenderFrame(m_fsrCommandList, src, dst, width, height);

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
    if (!IsUpscalingEnabled()) return;
    
    // Only override if ReGame is enabled in config
    if (!Config::Get().GetBool("ReGame", false)) return;

    uint32_t renderW = GetRenderWidth();
    uint32_t renderH = GetRenderHeight();
    
    if (renderW == 0 || renderH == 0) return;

    REManagedObject* main_view = sdk::get_main_view();
    if (main_view == nullptr) return;

    static auto scene_view_type = sdk::find_type_definition("via.SceneView");
    if (scene_view_type == nullptr) return;

    // Use set_Size if available (standard in some engine versions)
    static auto set_size_method = scene_view_type->get_method("set_Size");
    
    if (set_size_method != nullptr) {
        via_Size size{ (float)renderW, (float)renderH };
        // In RE Engine native calls, structs are often passed by pointer
        set_size_method->call(sdk::get_thread_context(), main_view, &size);
        
        static uint32_t s_logCount = 0;
        if (s_logCount++ % 120 == 0) {
            Logger::info("DXUpscalerManager: Successfully injected resolution via set_Size: " + std::to_string(renderW) + "x" + std::to_string(renderH));
        }
        return;
    }
}

void DXUpscalerManager::InitREEngineHooks() {
    // legacy
}

void DXUpscalerManager::UpdateREEngineHooks() {
    this->PerformPropertyInjection();
}

DXUpscalerManager::via_Size DXUpscalerManager::Hooked_get_Size_Native(REManagedObject* scene_view) {
    // Left as stub, functionality moved to PerformPropertyInjection
    return {0.0f, 0.0f};
}

} // namespace GamePlug
