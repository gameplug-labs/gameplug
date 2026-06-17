#include "upscaler_manager.h"
#include "upscaler_interface.h"
#ifdef GAMEPLUG_VULKAN
#include "dispatch.h"
#include "image_tracker.h"
#include "overlay.h"
#include "downsample_comp.h"
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
        std::filesystem::path upscalerPath = gamePath / "upscaler_vk.dll";

        Logger::info("UpscalerManager: Phase 1 loading " + upscalerPath.string());

        if (!std::filesystem::exists(upscalerPath)) {
            Logger::warn("UpscalerManager: upscaler_vk.dll NOT found at " + upscalerPath.string());
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

            auto* inst_dispatch = DispatchManager::Get().GetInstance(vkInstance);
            if (inst_dispatch && inst_dispatch->table.vkGetPhysicalDeviceMemoryProperties) {
                inst_dispatch->table.vkGetPhysicalDeviceMemoryProperties(vkPhysDevice, &m_memProps);
            } else {
                vkGetPhysicalDeviceMemoryProperties(vkPhysDevice, &m_memProps);
            }

            VkQueue queue = VK_NULL_HANDLE;
            uint32_t queueFamilyIndex = 0;
            auto* dev_entry = DispatchManager::Get().GetDevice(vkDevice);
            if (dev_entry && dev_entry->table.vkGetDeviceQueue) {
                dev_entry->table.vkGetDeviceQueue(vkDevice, queueFamilyIndex, 0, &queue);
            }

            m_pInterface->OnInit(ImGui::GetCurrentContext(), UpscalerLogBridge, nullptr, (uintptr_t)vkInstance, (uintptr_t)vkPhysDevice,
                (uintptr_t)vkDevice, (uintptr_t)queue, queueFamilyIndex, &m_memProps);
            
            InitDownsampleResources();
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
        CleanupDownsampleResources();
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
        // ImGui::Text("  Render: %d x %d", m_renderWidth, m_renderHeight);
        // float scale = (float)width / (float)m_renderWidth;
        // ImGui::SameLine();
        // ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%.2fx)", scale);
        // ImGui::Spacing();
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
    // Get suggested buffers from tracker using native resolution
    auto depthInfo = ImageTracker::Get().GetCurrentDepthInfo(width, height);
    auto mvInfo = ImageTracker::Get().GetCurrentMVInfo(m_renderWidth, m_renderHeight);
    mvImage = (uint64_t)mvInfo.image;
    mvFormat = (uint32_t)mvInfo.format;
    swapchainFormat = (uint32_t)ImageTracker::Get().GetSwapchainFormat();

    bool didDownsampleDepth = false;
    if (depthInfo.image != VK_NULL_HANDLE &&
        (depthInfo.extent.width != m_renderWidth || depthInfo.extent.height != m_renderHeight) &&
        m_renderWidth > 0 && m_renderHeight > 0) {
        
        VkImageView srcView = ImageTracker::Get().GetMainView(depthInfo.image);
        if (srcView != VK_NULL_HANDLE) {
            PerformDepthDownsamplingVK(reinterpret_cast<VkCommandBuffer>(cmd),
                                       depthInfo.image, srcView, depthInfo.format,
                                       depthInfo.extent.width, depthInfo.extent.height);
            didDownsampleDepth = true;
        }
    }

    if (didDownsampleDepth) {
        depthImage = (uint64_t)m_downsampledDepthImage;
        depthFormat = (uint32_t)VK_FORMAT_R32_SFLOAT;
    } else {
        depthImage = (uint64_t)depthInfo.image;
        depthFormat = (uint32_t)depthInfo.format;
    }

    if (shouldLog) {
        Logger::info("UpscalerManager::RenderFrame [Buffers] depth=" + std::to_string((uintptr_t)depthImage) + " (" +
                     std::to_string(didDownsampleDepth ? m_renderWidth : depthInfo.extent.width) + "x" + 
                     std::to_string(didDownsampleDepth ? m_renderHeight : depthInfo.extent.height) +
                     ", fmt=" + std::to_string(depthFormat) + "), mv=" + std::to_string((uintptr_t)mvInfo.image) +
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

    float cameraNear = 0.1f;
    float cameraFar = 1000.0f;
    float cameraFov = 1.0f;
    float viewSpaceToMetersFactor = 1.0f / 70.0f;

    typedef GamePlugSharedFrameData* (*GetSharedFrameDataFn)();
    static GetSharedFrameDataFn pfnGetSharedFrameData = nullptr;
    static bool checkedD3D9 = false;
    if (!checkedD3D9) {
        HMODULE hMod = GetModuleHandleA("dinput8.dll");
        if (!hMod)
            hMod = GetModuleHandleA("version.dll");
        if (hMod) {
            pfnGetSharedFrameData = (GetSharedFrameDataFn)GetProcAddress(hMod, "GamePlug_GetSharedFrameData");
            if (pfnGetSharedFrameData) {
                Logger::info("UpscalerManager: Found GamePlug_GetSharedFrameData export in D3D9 layer.");
            }
        }
        checkedD3D9 = true;
    }

    if (pfnGetSharedFrameData) {
        GamePlugSharedFrameData* sharedData = pfnGetSharedFrameData();
        if (sharedData && sharedData->magic == 0x47505344) {
            jitterX = sharedData->jitterX;
            jitterY = sharedData->jitterY;
            cameraNear = sharedData->cameraNear;
            cameraFar = sharedData->cameraFar;
            cameraFov = sharedData->cameraFov;
            viewSpaceToMetersFactor = sharedData->viewSpaceToMetersFactor;
            invertedDepth = sharedData->invertedDepth;
            hdr = sharedData->hdr;

            if (shouldLog) {
                Logger::info("UpscalerManager::RenderFrame [SharedData] frameIndex=" + std::to_string(sharedData->frameIndex) +
                             ", jitter=(" + std::to_string(jitterX) + ", " + std::to_string(jitterY) + ")" +
                             ", near=" + std::to_string(cameraNear) + ", far=" + std::to_string(cameraFar) +
                             ", fov=" + std::to_string(cameraFov) + ", inverted=" + std::to_string(invertedDepth));
            }
        }
    }

    m_pInterface->OnRenderFrame(cmd, source, target, swapchainFormat, (uint32_t)width, (uint32_t)height, m_renderWidth, m_renderHeight,
        depthImage, depthFormat, mvImage, mvFormat, (float)jitterX, (float)jitterY, cameraNear, cameraFar, cameraFov,
        viewSpaceToMetersFactor, invertedDepth, hdr);

    if (shouldLog)
        Logger::info("UpscalerManager::RenderFrame [End] OnRenderFrame returned.");

    lastW = width;
    lastH = height;
    m_frameUpscaled = true;
}

#ifdef GAMEPLUG_VULKAN
static uint32_t FindMemoryType(VkPhysicalDeviceMemoryProperties* memProps, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < memProps->memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProps->memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

void UpscalerManager::InitDownsampleResources() {
    if (m_device == VK_NULL_HANDLE) return;
    auto* dev = DispatchManager::Get().GetDevice(m_device);
    if (!dev) return;

    // 1. Create Shader Module
    VkShaderModuleCreateInfo shaderCI = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    shaderCI.codeSize = sizeof(g_downsample_spirv);
    shaderCI.pCode = g_downsample_spirv;

    if (dev->table.vkCreateShaderModule(m_device, &shaderCI, nullptr, &m_downsampleShader) != VK_SUCCESS) {
        Logger::error("UpscalerManager: Failed to create downsample shader module");
        return;
    }

    // 2. Create Descriptor Set Layout
    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutCI.bindingCount = 2;
    layoutCI.pBindings = bindings;

    if (dev->table.vkCreateDescriptorSetLayout(m_device, &layoutCI, nullptr, &m_downsampleSetLayout) != VK_SUCCESS) {
        Logger::error("UpscalerManager: Failed to create downsample descriptor set layout");
        return;
    }

    // 3. Create Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutCI = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &m_downsampleSetLayout;

    if (dev->table.vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &m_downsamplePipelineLayout) != VK_SUCCESS) {
        Logger::error("UpscalerManager: Failed to create downsample pipeline layout");
        return;
    }

    // 4. Create Pipeline
    VkComputePipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pipelineCI.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineCI.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineCI.stage.module = m_downsampleShader;
    pipelineCI.stage.pName = "main";
    pipelineCI.layout = m_downsamplePipelineLayout;

    if (dev->table.vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_downsamplePipeline) != VK_SUCCESS) {
        Logger::error("UpscalerManager: Failed to create downsample compute pipeline");
        return;
    }

    // 5. Create Descriptor Pool
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 16;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 16;

    VkDescriptorPoolCreateInfo poolCI = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolCI.maxSets = 16;
    poolCI.poolSizeCount = 2;
    poolCI.pPoolSizes = poolSizes;
    poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (dev->table.vkCreateDescriptorPool(m_device, &poolCI, nullptr, &m_downsampleDescriptorPool) != VK_SUCCESS) {
        Logger::error("UpscalerManager: Failed to create downsample descriptor pool");
        return;
    }

    // 6. Create Sampler
    VkSamplerCreateInfo samplerCI = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCI.magFilter = VK_FILTER_NEAREST;
    samplerCI.minFilter = VK_FILTER_NEAREST;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = 1.0f;

    if (dev->table.vkCreateSampler(m_device, &samplerCI, nullptr, &m_downsampleSampler) != VK_SUCCESS) {
        Logger::error("UpscalerManager: Failed to create downsample sampler");
    }

    Logger::info("UpscalerManager: Downsample compute resources initialized successfully.");
}

void UpscalerManager::CleanupDownsampleResources() {
    if (m_device == VK_NULL_HANDLE) return;
    auto* dev = DispatchManager::Get().GetDevice(m_device);
    if (!dev) return;

    if (m_downsampleShader != VK_NULL_HANDLE) {
        dev->table.vkDestroyShaderModule(m_device, m_downsampleShader, nullptr);
        m_downsampleShader = VK_NULL_HANDLE;
    }
    if (m_downsampleSetLayout != VK_NULL_HANDLE) {
        dev->table.vkDestroyDescriptorSetLayout(m_device, m_downsampleSetLayout, nullptr);
        m_downsampleSetLayout = VK_NULL_HANDLE;
    }
    if (m_downsamplePipelineLayout != VK_NULL_HANDLE) {
        dev->table.vkDestroyPipelineLayout(m_device, m_downsamplePipelineLayout, nullptr);
        m_downsamplePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_downsamplePipeline != VK_NULL_HANDLE) {
        dev->table.vkDestroyPipeline(m_device, m_downsamplePipeline, nullptr);
        m_downsamplePipeline = VK_NULL_HANDLE;
    }
    if (m_downsampleDescriptorPool != VK_NULL_HANDLE) {
        dev->table.vkDestroyDescriptorPool(m_device, m_downsampleDescriptorPool, nullptr);
        m_downsampleDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_downsampleSampler != VK_NULL_HANDLE) {
        dev->table.vkDestroySampler(m_device, m_downsampleSampler, nullptr);
        m_downsampleSampler = VK_NULL_HANDLE;
    }
    if (m_downsampledDepthView != VK_NULL_HANDLE) {
        ImageTracker::Get().UntrackImageView(m_downsampledDepthView);
        dev->table.vkDestroyImageView(m_device, m_downsampledDepthView, nullptr);
        m_downsampledDepthView = VK_NULL_HANDLE;
    }
    if (m_downsampledDepthImage != VK_NULL_HANDLE) {
        ImageTracker::Get().UntrackImage(m_downsampledDepthImage);
        dev->table.vkDestroyImage(m_device, m_downsampledDepthImage, nullptr);
        m_downsampledDepthImage = VK_NULL_HANDLE;
    }
    if (m_downsampledDepthMemory != VK_NULL_HANDLE) {
        dev->table.vkFreeMemory(m_device, m_downsampledDepthMemory, nullptr);
        m_downsampledDepthMemory = VK_NULL_HANDLE;
    }
    m_downsampleDescriptorSet = VK_NULL_HANDLE;
    m_downsampleW = 0;
    m_downsampleH = 0;
}

bool UpscalerManager::CreateDownsampleTarget(uint32_t w, uint32_t h) {
    if (m_device == VK_NULL_HANDLE) return false;
    auto* dev = DispatchManager::Get().GetDevice(m_device);
    if (!dev) return false;

    if (m_downsampledDepthImage != VK_NULL_HANDLE && m_downsampleW == w && m_downsampleH == h) {
        return true;
    }

    if (m_downsampledDepthView != VK_NULL_HANDLE) {
        ImageTracker::Get().UntrackImageView(m_downsampledDepthView);
        dev->table.vkDestroyImageView(m_device, m_downsampledDepthView, nullptr);
        m_downsampledDepthView = VK_NULL_HANDLE;
    }
    if (m_downsampledDepthImage != VK_NULL_HANDLE) {
        ImageTracker::Get().UntrackImage(m_downsampledDepthImage);
        dev->table.vkDestroyImage(m_device, m_downsampledDepthImage, nullptr);
        m_downsampledDepthImage = VK_NULL_HANDLE;
    }
    if (m_downsampledDepthMemory != VK_NULL_HANDLE) {
        dev->table.vkFreeMemory(m_device, m_downsampledDepthMemory, nullptr);
        m_downsampledDepthMemory = VK_NULL_HANDLE;
    }

    m_downsampleW = w;
    m_downsampleH = h;

    VkImageCreateInfo imageCI = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.extent.width = w;
    imageCI.extent.height = h;
    imageCI.extent.depth = 1;
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.format = VK_FORMAT_R32_SFLOAT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCI.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (dev->table.vkCreateImage(m_device, &imageCI, nullptr, &m_downsampledDepthImage) != VK_SUCCESS) {
        Logger::error("UpscalerManager: Failed to create downsampled depth image");
        return false;
    }
    ImageTracker::Get().TrackImage(m_downsampledDepthImage, &imageCI);

    VkMemoryRequirements memReqs;
    dev->table.vkGetImageMemoryRequirements(m_device, m_downsampledDepthImage, &memReqs);

    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(&m_memProps, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (dev->table.vkAllocateMemory(m_device, &allocInfo, nullptr, &m_downsampledDepthMemory) != VK_SUCCESS) {
        Logger::error("UpscalerManager: Failed to allocate memory for downsampled depth image");
        return false;
    }

    dev->table.vkBindImageMemory(m_device, m_downsampledDepthImage, m_downsampledDepthMemory, 0);

    VkImageViewCreateInfo viewCI = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image = m_downsampledDepthImage;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = VK_FORMAT_R32_SFLOAT;
    viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewCI.subresourceRange.baseMipLevel = 0;
    viewCI.subresourceRange.levelCount = 1;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount = 1;

    if (dev->table.vkCreateImageView(m_device, &viewCI, nullptr, &m_downsampledDepthView) != VK_SUCCESS) {
        Logger::error("UpscalerManager: Failed to create view for downsampled depth image");
        return false;
    }
    ImageTracker::Get().TrackImageView(m_downsampledDepthView, m_downsampledDepthImage);

    if (m_downsampleDescriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfoDS = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfoDS.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfoDS.descriptorPool = m_downsampleDescriptorPool;
        allocInfoDS.descriptorSetCount = 1;
        allocInfoDS.pSetLayouts = &m_downsampleSetLayout;

        if (dev->table.vkAllocateDescriptorSets(m_device, &allocInfoDS, &m_downsampleDescriptorSet) != VK_SUCCESS) {
            Logger::error("UpscalerManager: Failed to allocate downsample descriptor set");
            return false;
        }
    }

    Logger::info("UpscalerManager: Created downsampled depth target image at " + std::to_string(w) + "x" + std::to_string(h));
    return true;
}

void UpscalerManager::PerformDepthDownsamplingVK(VkCommandBuffer cmd, VkImage srcDepth, VkImageView srcView, VkFormat srcFormat, uint32_t srcW, uint32_t srcH) {
    if (m_device == VK_NULL_HANDLE) return;
    auto* dev = DispatchManager::Get().GetDevice(m_device);
    if (!dev) return;

    if (!CreateDownsampleTarget(m_renderWidth, m_renderHeight)) {
        return;
    }

    auto transitionLayout = [&](VkImage img, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspect) {
        VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = img;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

        dev->table.vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    };

    transitionLayout(srcDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    transitionLayout(m_downsampledDepthImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

    VkDescriptorImageInfo inputInfo = {};
    inputInfo.sampler = m_downsampleSampler;
    inputInfo.imageView = srcView;
    inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo outputInfo = {};
    outputInfo.sampler = VK_NULL_HANDLE;
    outputInfo.imageView = m_downsampledDepthView;
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_downsampleDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &inputInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_downsampleDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &outputInfo;

    dev->table.vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);

    dev->table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_downsamplePipeline);
    dev->table.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_downsamplePipelineLayout, 0, 1, &m_downsampleDescriptorSet, 0, nullptr);

    uint32_t groupX = (m_renderWidth + 7) / 8;
    uint32_t groupY = (m_renderHeight + 7) / 8;
    dev->table.vkCmdDispatch(cmd, groupX, groupY, 1);

    transitionLayout(srcDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    transitionLayout(m_downsampledDepthImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
}
#endif

} // namespace GamePlug
